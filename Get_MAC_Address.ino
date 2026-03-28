#include <WiFi.h>
// Will return a MAC address that will be used to connect the ESP32's to each other with ESP-NOW
void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_MODE_STA);
  delay(1000);

  Serial.println("ESP32 MAC Address:");
  Serial.println(WiFi.macAddress());
}

void loop() {
}