// Main_Board.ino
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "Config.h"

const int SENSOR_A_PIN = 18;
const int SENSOR_B_PIN = 19;

volatile bool sensorA_triggered = false;
volatile bool sensorB_triggered = false;
unsigned long sensorA_time      = 0;
unsigned long sensorB_time      = 0;
const unsigned long DEBOUNCE_WINDOW_MS = 500;

int16_t entries_delta     = 0;
int16_t exits_delta       = 0;
int32_t total_entries     = 0;
int32_t total_exits       = 0;
int32_t current_occupancy = 0;

const unsigned long SEND_INTERVAL_MS = 20000;
unsigned long last_send_time         = 0;

typedef struct {
  int16_t entries_delta;
  int16_t exits_delta;
} OccupancyDelta;

void onDataReceived(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(OccupancyDelta)) {
    Serial.println("[ESP-NOW] Unexpected packet size — ignored.");
    return;
  }

  OccupancyDelta received;
  memcpy(&received, data, sizeof(received));

  if (received.entries_delta < 0 || received.exits_delta < 0 ||
      received.entries_delta > MAX_CAPACITY ||
      received.exits_delta   > MAX_CAPACITY) {
    Serial.println("[ESP-NOW] Out-of-range delta — ignored.");
    return;
  }

  entries_delta     += received.entries_delta;
  exits_delta       += received.exits_delta;
  total_entries     += received.entries_delta;
  total_exits       += received.exits_delta;
  current_occupancy  = min((int32_t)MAX_CAPACITY,
                        max((int32_t)0,
                          current_occupancy + received.entries_delta - received.exits_delta));

  Serial.print("[ESP-NOW] Received — entries: ");
  Serial.print(received.entries_delta);
  Serial.print("  exits: ");
  Serial.println(received.exits_delta);
}

void IRAM_ATTR onSensorA() {
  if (digitalRead(SENSOR_A_PIN) == LOW && !sensorA_triggered) {
    sensorA_triggered = true;
    sensorA_time = millis();
  }
}

void IRAM_ATTR onSensorB() {
  if (digitalRead(SENSOR_B_PIN) == LOW && !sensorB_triggered) {
    sensorB_triggered = true;
    sensorB_time = millis();
  }
}

void evaluatePassthrough() {
  if (!sensorA_triggered && !sensorB_triggered) return;

  unsigned long now = millis();
  bool windowExpired = false;
  if (sensorA_triggered) windowExpired = (now - sensorA_time >= DEBOUNCE_WINDOW_MS);
  if (sensorB_triggered) windowExpired = windowExpired || (now - sensorB_time >= DEBOUNCE_WINDOW_MS);

  if (!windowExpired) return;

  if (sensorA_triggered && sensorB_triggered) {
    if (sensorA_time <= sensorB_time) {
      entries_delta++;
      total_entries++;
      current_occupancy = min((int32_t)MAX_CAPACITY, current_occupancy + 1);
      Serial.println("[Sensor] Entry. occupancy=" + String(current_occupancy));
    } else {
      exits_delta++;
      total_exits++;
      current_occupancy = max((int32_t)0, current_occupancy - 1);
      Serial.println("[Sensor] Exit. occupancy=" + String(current_occupancy));
    }
  } else {
    Serial.println("[Sensor] Partial trigger ignored.");
  }

  sensorA_triggered = false;
  sensorB_triggered = false;
  sensorA_time      = 0;
  sensorB_time      = 0;
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected. IP: " + WiFi.localIP().toString());
    Serial.println("[WiFi] Channel: " + String(WiFi.channel()));
    return true;
  }

  Serial.println("\n[WiFi] Connection failed. Deltas retained for next interval.");
  return false;
}

void writeToFirebase() {
  if (!connectWiFi()) return;

  String url = "https://" + String(FIREBASE_HOST)
             + "/floors/" + String(FLOOR_ID)
             + ".json?auth=" + String(FIREBASE_AUTH);

  JsonDocument doc;
  doc["current_occupancy"] = current_occupancy;
  doc["total_entries"]     = total_entries;
  doc["total_exits"]       = total_exits;
  doc["last_updated"]      = millis();

  String payload;
  serializeJson(doc, payload);

  static WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  int responseCode = http.PATCH(payload);

  if (responseCode == 200) {
    Serial.println("[Firebase] Write successful.");
    Serial.println("[Firebase] occupancy=" + String(current_occupancy)
                   + "  total_entries=" + String(total_entries)
                   + "  total_exits=" + String(total_exits));
    entries_delta = 0;
    exits_delta   = 0;
  } else if (responseCode > 0) {
    Serial.println("[Firebase] Write failed. HTTP " + String(responseCode));
  } else {
    Serial.println("[Firebase] Error: " + http.errorToString(responseCode));
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  Serial.println("[Boot] Main Board starting...");
  Serial.println("[Boot] Floor: " + String(FLOOR_ID));

  pinMode(SENSOR_A_PIN, INPUT_PULLUP);
  pinMode(SENSOR_B_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SENSOR_A_PIN), onSensorA, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_B_PIN), onSensorB, FALLING);

  WiFi.mode(WIFI_STA);
  connectWiFi();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED. Halting.");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onDataReceived);

  uint8_t primaryChan;
  wifi_second_chan_t secondChan;
  esp_wifi_get_channel(&primaryChan, &secondChan);
  Serial.println("[ESP-NOW] Ready. Channel=" + String(primaryChan)
                 + " — set WIFI_CHANNEL=" + String(primaryChan) + " in Config.h for Support Boards");

  // Write max_capacity on first boot
  static WiFiClientSecure client;
  client.setInsecure();
  String url = "https://" + String(FIREBASE_HOST)
             + "/floors/" + String(FLOOR_ID)
             + "/max_capacity.json?auth=" + String(FIREBASE_AUTH);
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  int rc = http.PUT(String(MAX_CAPACITY));
  Serial.println("[Boot] max_capacity write HTTP " + String(rc));
  http.end();

  Serial.println("[Boot] Main Board ready.\n");
  last_send_time = millis();
}

void loop() {
  evaluatePassthrough();

  if (millis() - last_send_time >= SEND_INTERVAL_MS) {
    writeToFirebase();
    last_send_time = millis();
  }
}