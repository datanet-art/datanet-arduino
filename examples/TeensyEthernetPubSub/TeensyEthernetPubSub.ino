/*
 * TeensyEthernetPubSub.ino - DataNet over plain HTTP/WS Ethernet
 *
 * This example is intended for Teensy 4.1 or Arduino-compatible boards with
 * an EthernetClient-compatible network stack.
 *
 * Teensy/Ethernet transport in this SDK is plain HTTP/WS. Point API_URL and
 * WS_HOST/WS_PORT at a local DataNet gateway or development endpoint.
 */

#include <Arduino.h>
#include <Ethernet.h>
#include <DataNet.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
const char* API_KEY = "ak_your_key_here";

// Local/plain endpoint. Do not use https:// or port 443 on this transport.
const char* API_URL = "http://192.168.1.100:8080";
const char* WS_HOST = "192.168.1.100";
const int   WS_PORT = 8080;

const char* CHANNEL = "arduino.teensy.demo";

byte mac[] = {0x04, 0xE9, 0xE5, 0x00, 0x00, 0x01};

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

void connectEthernet() {
  Serial.println(F("[Ethernet] Starting DHCP"));

  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("[Ethernet] DHCP failed"));
    while (true) {
      delay(1000);
    }
  }

  Serial.print(F("[Ethernet] IP: "));
  Serial.println(Ethernet.localIP());
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }

  connectEthernet();

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
