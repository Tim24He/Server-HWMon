#include <Arduino.h>

static constexpr unsigned long HEARTBEAT_MS = 2000;
unsigned long last_heartbeat = 0;

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("ESP32 monitor receiver booted");
}

void loop() {
  while (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      Serial.print("RX: ");
      Serial.println(line);
    }
  }

  const unsigned long now = millis();
  if (now - last_heartbeat >= HEARTBEAT_MS) {
    last_heartbeat = now;
    Serial.println("ESP32 alive");
  }
}
