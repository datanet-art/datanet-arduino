/*
 * ESP32Potentiometer.ino - Publish an analog control to DataNet
 *
 * Wiring for a 10k potentiometer:
 *   - one outer leg to 3.3V
 *   - the other outer leg to GND
 *   - center/wiper leg to GPIO 34
 *
 * Never connect the ESP32 analog input to 5V. Change POT_PIN when using a
 * board that does not expose GPIO 34.
 */

#include <Arduino.h>

#if !defined(ESP32)
  #error "ESP32Potentiometer requires an ESP32 board."
#endif

#include <WiFi.h>
#include <DataNet.h>

#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define API_KEY       "ak_YOUR_API_KEY_HERE"
#define CHANNEL       "project.your-project-id.controls.potentiometer"

const uint8_t POT_PIN = 34;
const int ADC_MAX = 4095;
const int CHANGE_THRESHOLD = 16;
const uint32_t SAMPLE_INTERVAL_MS = 100;
const uint32_t KEEPALIVE_INTERVAL_MS = 1000;

DataNet datanet(API_KEY);
int lastPublishedValue = -1;
uint32_t lastSampleMs = 0;
uint32_t lastPublishMs = 0;

void onEvent(const char* event, const char* info) {
  Serial.print(F("[DataNet] "));
  Serial.print(event);
  if (info != nullptr && info[0] != '\0') {
    Serial.print(F(": "));
    Serial.print(info);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("[WiFi] Connecting"));
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.print(F("\n[WiFi] IP: "));
  Serial.println(WiFi.localIP());

  datanet.on("connect", onEvent);
  datanet.on("disconnect", onEvent);
  datanet.on("error", onEvent);
  datanet.connect();
}

void loop() {
  datanet.loop();
  const uint32_t now = millis();
  if (!datanet.connected() || now - lastSampleMs < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleMs = now;

  const int raw = analogRead(POT_PIN);
  const bool changed = lastPublishedValue < 0 || abs(raw - lastPublishedValue) >= CHANGE_THRESHOLD;
  const bool keepalive = now - lastPublishMs >= KEEPALIVE_INTERVAL_MS;
  if (!changed && !keepalive) {
    return;
  }

  JsonDocument data;
  data[F("raw")] = raw;
  data[F("normalized")] = static_cast<float>(raw) / static_cast<float>(ADC_MAX);

  if (datanet.publish(CHANNEL, data.as<JsonVariant>())) {
    lastPublishedValue = raw;
    lastPublishMs = now;
    Serial.print(F("[Potentiometer] raw="));
    Serial.print(raw);
    Serial.print(F(" normalized="));
    Serial.println(static_cast<float>(raw) / static_cast<float>(ADC_MAX), 3);
  }
}
