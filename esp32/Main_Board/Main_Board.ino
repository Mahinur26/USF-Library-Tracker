// Main_Board.ino
// Initiator node for one floor of the library occupancy tracker.
// - Reads two IR break-beam sensors for directional foot traffic detection
// - Receives entry/exit deltas from Support Boards via ESP-NOW
// - Aggregates all counts (local + received) into floor totals
// - Writes directly to Firebase Realtime Database every 60 seconds via REST

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Config.h"

// ---------------------------------------------------------------------------
// Pin assignments — adjust to match your wiring
// ---------------------------------------------------------------------------
const int SENSOR_A_PIN = 18; // Sensor closer to the corridor (outside)
const int SENSOR_B_PIN = 19; // Sensor closer to the floor interior (inside)

// ---------------------------------------------------------------------------
// Directional detection state
// A breaks first then B → entry
// B breaks first then A → exit
// ---------------------------------------------------------------------------
volatile bool sensorA_triggered = false;
volatile bool sensorB_triggered = false;
unsigned long sensorA_time      = 0;
unsigned long sensorB_time      = 0;
const unsigned long DEBOUNCE_WINDOW_MS = 500;

// ---------------------------------------------------------------------------
// Floor-level occupancy counters
// ---------------------------------------------------------------------------
int16_t entries_delta     = 0; // Delta since last Firebase write
int16_t exits_delta       = 0; // Delta since last Firebase write
int32_t total_entries     = 0; // Cumulative since boot (audit trail)
int32_t total_exits       = 0; // Cumulative since boot (audit trail)
int32_t current_occupancy = 0; // Running occupancy — clamped between 0 and MAX_CAPACITY

// ---------------------------------------------------------------------------
// Transmission timing
// ---------------------------------------------------------------------------
const unsigned long SEND_INTERVAL_MS = 60000; // 60 seconds
unsigned long last_send_time         = 0;

// ---------------------------------------------------------------------------
// ESP-NOW payload struct — must match Support_Board.ino exactly
// ---------------------------------------------------------------------------
typedef struct {
  int16_t entries_delta;
  int16_t exits_delta;
} OccupancyDelta;

// ---------------------------------------------------------------------------
// ESP-NOW receive callback
// ---------------------------------------------------------------------------
void onDataReceived(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(OccupancyDelta)) {
    Serial.println("[ESP-NOW] Unexpected packet size — ignored.");
    return;
  }

  OccupancyDelta received;
  memcpy(&received, data, sizeof(received));

  // Sanity check
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

// ---------------------------------------------------------------------------
// Interrupt handlers
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Directional logic
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// WiFi connection helper
// ---------------------------------------------------------------------------
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
    return true;
  }

  Serial.println("\n[WiFi] Connection failed. Deltas retained for next interval.");
  return false;
}

// ---------------------------------------------------------------------------
// Write floor data directly to Firebase Realtime Database via REST PATCH
// PATCH updates only the specified fields, leaving max_capacity untouched
// ---------------------------------------------------------------------------
void writeToFirebase() {
  if (!connectWiFi()) return;

  // Build the Firebase REST URL
  // PATCH to floors/floor_1.json updates only the fields in the payload
  String url = "https://" + String(FIREBASE_HOST)
             + "/floors/" + String(FLOOR_ID)
             + ".json?auth=" + String(FIREBASE_AUTH);

  // Build JSON payload
  JsonDocument doc;
  doc["current_occupancy"] = current_occupancy;
  doc["total_entries"]     = total_entries;
  doc["total_exits"]       = total_exits;
  doc["last_updated"]      = millis(); // ms since boot — replace with NTP time if needed

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // PATCH so we don't overwrite max_capacity or other fields set manually in Firebase
  int responseCode = http.PATCH(payload);

  if (responseCode == 200) {
    Serial.println("[Firebase] Write successful. Resetting deltas.");
    Serial.println("[Firebase] occupancy=" + String(current_occupancy)
                   + "  total_entries=" + String(total_entries)
                   + "  total_exits=" + String(total_exits));
    // Reset deltas only on confirmed success
    entries_delta = 0;
    exits_delta   = 0;
  } else if (responseCode > 0) {
    Serial.println("[Firebase] Write failed. HTTP " + String(responseCode) + ". Deltas retained.");
  } else {
    Serial.println("[Firebase] Error: " + http.errorToString(responseCode));
  }

  http.end();
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("[Boot] Main Board starting...");
  Serial.println("[Boot] Floor: " + String(FLOOR_ID));

  // Sensor pins
  pinMode(SENSOR_A_PIN, INPUT_PULLUP);
  pinMode(SENSOR_B_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SENSOR_A_PIN), onSensorA, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_B_PIN), onSensorB, FALLING);

  // WiFi station mode — needed for both WiFi and ESP-NOW
  WiFi.mode(WIFI_STA);
  connectWiFi();

  // Init ESP-NOW (after WiFi.mode)
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED. Halting.");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onDataReceived);
  Serial.println("[Boot] ESP-NOW ready. Listening for support boards.");

  // Set max_capacity in Firebase on boot if the record doesn't exist yet
  // This is a one-time write — Firebase won't overwrite it on subsequent boots
  // because we use PATCH (not PUT) in writeToFirebase()
  String url = "https://" + String(FIREBASE_HOST)
             + "/floors/" + String(FLOOR_ID)
             + "/max_capacity.json?auth=" + String(FIREBASE_AUTH);
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  // PUT to a single field — only writes if you want to force-set capacity on boot
  // Comment this out after first flash if you want to manage capacity from Firebase Console
  http.PUT(String(MAX_CAPACITY));
  http.end();

  Serial.println("[Boot] Main Board ready.\n");
  last_send_time = millis();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
  evaluatePassthrough();

  if (millis() - last_send_time >= SEND_INTERVAL_MS) {
    writeToFirebase();
    last_send_time = millis();
  }
}