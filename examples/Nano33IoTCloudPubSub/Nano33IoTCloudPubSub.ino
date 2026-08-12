/*
 * Nano33IoTCloudPubSub.ino - DataNet hosted cloud on Arduino Nano 33 IoT
 *
 * Before uploading:
 *   1. Install WiFiNINA through Arduino Library Manager.
 *   2. Update the NINA firmware with the Arduino IDE Firmware Updater.
 *   3. Install SSL root certificates for api.datanet.art and ws.datanet.art.
 *   4. Replace the WiFi, API key, and channel placeholders below.
 */

#include <Arduino.h>

#if !defined(ARDUINO_SAMD_NANO_33_IOT)
  #error "Nano33IoTCloudPubSub requires an Arduino Nano 33 IoT."
#endif

#include <WiFiNINA.h>
#include <DataNet.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* API_KEY = "ak_YOUR_API_KEY_HERE";
const char* CHANNEL = "project.your-project-id.demo";

// The one-argument constructor uses the hosted DataNet HTTPS/WSS defaults.
DataNet datanet(API_KEY);
unsigned long lastPublishMs = 0;

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

bool connectWiFi() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("[WiFi] NINA module not detected"));
    return false;
  }

  Serial.print(F("[WiFi] NINA firmware: "));
  Serial.println(WiFi.firmwareVersion());
  Serial.print(F("[WiFi] Connecting to "));
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 30000UL) {
    delay(500);
    Serial.print('.');
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("\n[WiFi] Connection failed, status="));
    Serial.println(WiFi.status());
    return false;
  }

  Serial.print(F("\n[WiFi] IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000UL) {
    delay(10);
  }

  if (!connectWiFi()) {
    return;
  }

  datanet.on("connect", onEvent);
  datanet.on("disconnect", onEvent);
  datanet.on("error", onEvent);
  datanet.subscribe(CHANNEL, onMessage);
  datanet.connect();
}

void loop() {
  datanet.loop();

  if (datanet.connected() && millis() - lastPublishMs >= 5000UL) {
    lastPublishMs = millis();
    JsonDocument data;
    data[F("source")] = F("nano33iot");
    data[F("uptime_ms")] = millis();
    datanet.publish(CHANNEL, data.as<JsonVariant>());
  }
}
