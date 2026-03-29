// Support_Board.ino
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "Config.h"

const int SENSOR_A_PIN = 18;
const int SENSOR_B_PIN = 19;

volatile bool sensorA_triggered = false;
volatile bool sensorB_triggered = false;
unsigned long sensorA_time      = 0;
unsigned long sensorB_time      = 0;
const unsigned long DEBOUNCE_WINDOW_MS = 500;

int16_t entries_delta = 0;
int16_t exits_delta   = 0;

const unsigned long SEND_INTERVAL_MS = 6000;
unsigned long last_send_time         = 0;

esp_now_peer_info_t peer;

typedef struct {
  int16_t entries_delta;
  int16_t exits_delta;
} OccupancyDelta;

void onDataSent(const wifi_tx_info_t* tx_info, esp_now_send_status_t status) {
  Serial.print("[ESP-NOW] Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
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
      Serial.println("[Sensor] Entry detected. entries_delta=" + String(entries_delta));
    } else {
      exits_delta++;
      Serial.println("[Sensor] Exit detected. exits_delta=" + String(exits_delta));
    }
  } else {
    Serial.println("[Sensor] Partial trigger ignored.");
  }

  sensorA_triggered = false;
  sensorB_triggered = false;
  sensorA_time      = 0;
  sensorB_time      = 0;
}

void sendDelta() {
  OccupancyDelta payload = { entries_delta, exits_delta };

  Serial.printf("[ESP-NOW] Sending to: %02X:%02X:%02X:%02X:%02X:%02X\n",
    MAIN_BOARD_MAC[0], MAIN_BOARD_MAC[1], MAIN_BOARD_MAC[2],
    MAIN_BOARD_MAC[3], MAIN_BOARD_MAC[4], MAIN_BOARD_MAC[5]);

  esp_err_t result = esp_now_send(MAIN_BOARD_MAC, (uint8_t*)&payload, sizeof(payload));

  if (result == ESP_OK) {
    // Always reset after successful queue — onDataSent FAILED is a false negative
    // caused by WiFi channel switching on the Main Board, but data arrives correctly
    Serial.println("[ESP-NOW] Packet queued. entries=" + String(entries_delta)
                   + " exits=" + String(exits_delta));
    entries_delta = 0;
    exits_delta   = 0;
  } else {
    // This is a real error (ESP-NOW not init'd, peer not registered, etc.)
    Serial.println("[ESP-NOW] Send error: " + String(esp_err_to_name(result)));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("[Boot] Support Board starting...");

  pinMode(SENSOR_A_PIN, INPUT_PULLUP);
  pinMode(SENSOR_B_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SENSOR_A_PIN), onSensorA, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_B_PIN), onSensorB, FALLING);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED. Halting.");
    while (true) delay(1000);
  }

  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Confirm channel actually set
  uint8_t primaryChan;
  wifi_second_chan_t secondChan;
  esp_wifi_get_channel(&primaryChan, &secondChan);
  Serial.println("[ESP-NOW] Channel set to: " + String(primaryChan));

  esp_now_register_send_cb(onDataSent);

  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, MAIN_BOARD_MAC, 6);
  peer.channel = 0;    // 0 = use current radio channel
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] Failed to add peer. Check MAIN_BOARD_MAC in Config.h.");
    while (true) delay(1000);
  }

  Serial.println("[Boot] Support Board ready. Channel=" + String(WIFI_CHANNEL));
  last_send_time = millis();
}

void loop() {
  evaluatePassthrough();

  if (millis() - last_send_time >= SEND_INTERVAL_MS) {
    sendDelta();
    last_send_time = millis();
  }
}