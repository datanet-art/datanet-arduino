/*
 * PresenceReactiveLighting.ino
 *
 * Turns authoritative channel occupancy into a repeating LED pulse pattern.
 * Presence is polled only every 10 seconds because getPresence() performs a
 * blocking HTTPS round-trip. DataNet::loop() continues to run every frame.
 */

#if !defined(ESP32)
  #error "PresenceReactiveLighting currently targets ESP32 boards."
#endif

#include <WiFi.h>
#include <DataNet.h>

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const char* API_KEY = "ak_your_key_here";
const char* CHANNEL = "project.your_project_id.room";

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

DataNet datanet(API_KEY);

unsigned long lastPresenceMs = 0;
unsigned long lastPulseMs = 0;
int occupancy = 0;
int pulsesRemaining = 0;
bool ledOn = false;

void onEvent(const char* event, const char* info) {
  Serial.printf("[DataNet] %s%s%s\n", event, info[0] ? ": " : "", info);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  datanet.on("connect", onEvent);
  datanet.on("disconnect", onEvent);
  datanet.on("error", onEvent);
  datanet.subscribe(CHANNEL, [](const char*, JsonVariant) {});
  datanet.connect();
}

void loop() {
  datanet.loop();
  unsigned long now = millis();

  if (datanet.connected() && (lastPresenceMs == 0 || now - lastPresenceMs >= 10000UL)) {
    lastPresenceMs = now;
    int count = datanet.getPresence(CHANNEL);
    if (count >= 0) {
      occupancy = count;
      pulsesRemaining = min(occupancy, 12);
      Serial.printf("[Presence] %d connected device(s)\n", occupancy);
    }
  }

  if (now - lastPulseMs >= (ledOn ? 120UL : 260UL)) {
    lastPulseMs = now;
    if (ledOn) {
      digitalWrite(LED_BUILTIN, LOW);
      ledOn = false;
    } else if (pulsesRemaining > 0) {
      digitalWrite(LED_BUILTIN, HIGH);
      ledOn = true;
      pulsesRemaining--;
    } else if (now - lastPresenceMs > 1500UL) {
      pulsesRemaining = min(occupancy, 12);
    }
  }
}
