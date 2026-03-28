// Support_Board.ino
// Responder node for one floor of the library occupancy tracker.
// Reads two IR break-beam sensors to detect entry/exit direction,
// accumulates deltas locally, and transmits them to the Main Board
// every 60 seconds via ESP-NOW.
// No WiFi credentials needed — ESP-NOW only.

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"

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
unsigned long sensorA_time     = 0;
unsigned long sensorB_time     = 0;
const unsigned long DEBOUNCE_WINDOW_MS = 500; // Max ms between the two beams for a valid pass

// ---------------------------------------------------------------------------
// Occupancy counters (reset after each successful ESP-NOW send)
// ---------------------------------------------------------------------------
int16_t entries_delta = 0;
int16_t exits_delta   = 0;

// ---------------------------------------------------------------------------
// Transmission timing
// ---------------------------------------------------------------------------
const unsigned long SEND_INTERVAL_MS = 60000; // 60 seconds
unsigned long last_send_time         = 0;

// ---------------------------------------------------------------------------
// ESP-NOW peer (Main Board)
// ---------------------------------------------------------------------------
esp_now_peer_info_t peer;

// Payload struct — must match Main_Board.ino exactly
typedef struct {
  int16_t entries_delta;
  int16_t exits_delta;
} OccupancyDelta;

// ---------------------------------------------------------------------------
// ESP-NOW send callback — logs success/failure over Serial
// ---------------------------------------------------------------------------
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("[ESP-NOW] Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
}

// ---------------------------------------------------------------------------
// Interrupt handlers — record the time each sensor was first broken
// Only latch the first break within the debounce window
// ---------------------------------------------------------------------------
void IRAM_ATTR onSensorA() {
  // LOW = beam broken (INPUT_PULLUP logic)
  if (digitalRead(SENSOR_A_PIN) == LOW) {
    unsigned long now = millis();
    // Ignore if already triggered within the window
    if (!sensorA_triggered) {
      sensorA_triggered = true;
      sensorA_time = now;
    }
  }
}

void IRAM_ATTR onSensorB() {
  if (digitalRead(SENSOR_B_PIN) == LOW) {
    unsigned long now = millis();
    if (!sensorB_triggered) {
      sensorB_triggered = true;
      sensorB_time = now;
    }
  }
}

// ---------------------------------------------------------------------------
// Directional logic — call from loop() after either sensor triggers
// ---------------------------------------------------------------------------
void evaluatePassthrough() {
  unsigned long now = millis();

  // Wait until at least one sensor has triggered
  if (!sensorA_triggered && !sensorB_triggered) return;

  // Give the second sensor time to fire before evaluating
  bool windowExpired = false;
  if (sensorA_triggered) windowExpired = (now - sensorA_time >= DEBOUNCE_WINDOW_MS);
  if (sensorB_triggered) windowExpired = windowExpired || (now - sensorB_time >= DEBOUNCE_WINDOW_MS);

  if (!windowExpired) return; // Still within window, wait for second sensor

  // Both fired — determine direction from which came first
  if (sensorA_triggered && sensorB_triggered) {
    if (sensorA_time <= sensorB_time) {
      // A first → entry
      entries_delta++;
      Serial.println("[Sensor] Entry detected. entries_delta=" + String(entries_delta));
    } else {
      // B first → exit
      exits_delta++;
      Serial.println("[Sensor] Exit detected. exits_delta=" + String(exits_delta));
    }
  } else {
    // Only one sensor fired within the window — likely a partial pass or sensor noise, ignore
    Serial.println("[Sensor] Partial trigger ignored (only one beam broken in window).");
  }

  // Reset for next passthrough
  sensorA_triggered = false;
  sensorB_triggered = false;
  sensorA_time      = 0;
  sensorB_time      = 0;
}

// ---------------------------------------------------------------------------
// Send delta payload to Main Board via ESP-NOW then reset counters
// ---------------------------------------------------------------------------
void sendDelta() {
  OccupancyDelta payload = { entries_delta, exits_delta };

  esp_err_t result = esp_now_send(MAIN_BOARD_MAC, (uint8_t*)&payload, sizeof(payload));

  if (result == ESP_OK) {
    Serial.println("[ESP-NOW] Packet queued. entries=" + String(entries_delta)
                   + " exits=" + String(exits_delta));
    // Reset deltas only on successful queue — onDataSent confirms delivery
    entries_delta = 0;
    exits_delta   = 0;
  } else {
    Serial.println("[ESP-NOW] Send error: " + String(esp_err_to_name(result)));
    // Retain deltas so they are included in the next send attempt
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("[Boot] Support Board starting...");

  // Sensor pins with internal pull-up (LOW = beam broken, HIGH = beam clear)
  pinMode(SENSOR_A_PIN, INPUT_PULLUP);
  pinMode(SENSOR_B_PIN, INPUT_PULLUP);

  // Attach interrupts on FALLING edge (HIGH → LOW = beam just broken)
  attachInterrupt(digitalPinToInterrupt(SENSOR_A_PIN), onSensorA, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_B_PIN), onSensorB, FALLING);

  // Set ESP32 to Station mode — required for ESP-NOW even without joining a network
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // Ensure we don't accidentally connect to anything

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED. Halting.");
    while (true) delay(1000); // Halt — check wiring or flash
  }

  esp_now_register_send_cb(onDataSent);

  // Register Main Board as peer
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, MAIN_BOARD_MAC, 6);
  peer.channel = 0;    // 0 = use current channel
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] Failed to add Main Board peer. Check MAIN_BOARD_MAC in Config.h.");
    while (true) delay(1000);
  }

  Serial.println("[Boot] Support Board ready.");
  last_send_time = millis();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
  // Evaluate whether a full passthrough has occurred
  evaluatePassthrough();

  // Send delta to Main Board on the 60-second interval
  if (millis() - last_send_time >= SEND_INTERVAL_MS) {
    sendDelta();
    last_send_time = millis();
  }
}