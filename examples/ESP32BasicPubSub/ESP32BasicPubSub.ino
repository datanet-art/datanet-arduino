/*
 * ESP32BasicPubSub.ino - Minimal DataNet ESP32 example
 *
 * Connects directly to the hosted DataNet HTTPS/WSS service, subscribes to a
 * JSON channel, and publishes a counter every three seconds.
 */

#include <Arduino.h>

#if !defined(ESP32)
  #error "ESP32BasicPubSub requires an ESP32 board."
#endif

#include <WiFi.h>
#include <DataNet.h>

#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define API_KEY       "ak_YOUR_API_KEY_HERE"
#define CHANNEL       "project.your-project-id.demo"

DataNet datanet(API_KEY);

void onMessage(const char* channel, JsonVariant data) {
  Serial.print(F("[DataNet] Message on "));
  Serial.print(channel);
  Serial.print(F(": "));
  serializeJson(data, Serial);
  Serial.println();
}

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
  datanet.subscribe(CHANNEL, onMessage);
  datanet.connect();
}

void loop() {
  datanet.loop();

  static uint32_t lastPublishMs = 0;
  static uint32_t count = 0;
  if (datanet.connected() && millis() - lastPublishMs >= 3000UL) {
    lastPublishMs = millis();
    StaticJsonDocument<96> data;
    data[F("source")] = F("esp32");
    data[F("count")] = count++;
    datanet.publish(CHANNEL, data.as<JsonVariant>());
  }
}
