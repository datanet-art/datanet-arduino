/*
 * ArduinoWiFiPubSub.ino - DataNet over plain HTTP/WS Arduino WiFi
 *
 * Supported targets:
 *   - Arduino Uno R4 WiFi (WiFiS3)
 *   - Arduino MKR WiFi 1010 / Nano 33 IoT (WiFiNINA)
 *
 * Non-ESP Arduino WiFi transport in this SDK is plain HTTP/WS. Point API_URL
 * and WS_HOST/WS_PORT at a local DataNet gateway or development endpoint.
 */

#include <Arduino.h>

#if defined(ARDUINO_UNOWIFIR4)
  #include <WiFiS3.h>
#elif defined(ARDUINO_SAMD_MKRWIFI1010) || defined(ARDUINO_SAMD_NANO_33_IOT)
  #include <WiFiNINA.h>
#else
  #error "ArduinoWiFiPubSub requires Uno R4 WiFi, MKR WiFi 1010, or Nano 33 IoT."
#endif

#include <DataNet.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const char* API_KEY = "ak_your_key_here";

// Local/plain endpoint. Do not use https:// or port 443 on this transport.
const char* API_URL = "http://192.168.1.100:8080";
const char* WS_HOST = "192.168.1.100";
const int   WS_PORT = 8080;

const char* CHANNEL = "arduino.wifi.demo";

DataNet datanet(API_KEY, API_URL, WS_HOST, WS_PORT);

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

void connectWiFi() {
  Serial.print(F("[WiFi] Connecting to "));
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }

  Serial.print(F("\n[WiFi] IP: "));
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }

  connectWiFi();

  datanet.on("connect", onEvent);
  datanet.on("disconnect", onEvent);
  datanet.on("error", onEvent);
  datanet.subscribe(CHANNEL, onMessage);

  if (!datanet.connect()) {
    Serial.println(F("[DataNet] Initial connection failed; loop() will retry after disconnect events"));
  }
}

void loop() {
  datanet.loop();

  if (datanet.connected() && millis() - lastPublishMs > 5000) {
    lastPublishMs = millis();
    datanet.publishFloat(CHANNEL, "uptime_ms", millis());
  }
}
