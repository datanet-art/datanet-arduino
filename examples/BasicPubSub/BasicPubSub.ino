/*
 * BasicPubSub.ino — Minimal DataNet SDK Example
 *
 * Quickstart: connect → subscribe → publish in as few lines as possible.
 *
 * Required libraries (install via Arduino Library Manager):
 *   - ArduinoJson by Benoit Blanchon, v7+
 *   - WebSockets by Markus Sattler (Links2004), v2.4+
 */

#include <Arduino.h>
#include <DataNet.h>

#ifdef ESP32
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

// ---------------------------------------------------------------------------
// Edit these three values
// ---------------------------------------------------------------------------
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define API_KEY       "ak_YOUR_API_KEY_HERE"

// ---------------------------------------------------------------------------
// DataNet client
// ---------------------------------------------------------------------------
DataNet datanet(API_KEY);

// ---------------------------------------------------------------------------
// Message handler
// ---------------------------------------------------------------------------
void onMessage(const char* channel, JsonVariant data) {
    Serial.print(F("Message on "));
    Serial.print(channel);
    Serial.print(F(": "));
    Serial.println(data.as<String>());
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print(F("Connecting to WiFi"));
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
    Serial.println(F(" OK"));

    // Subscribe before connect — SDK re-sends on every reconnect automatically
    datanet.subscribe("my-project-id.test.channel", onMessage);

    // Connect to DataNet (fetches JWT then opens WSS)
    datanet.connect();
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
void loop() {
    datanet.loop(); // must be called every iteration

    // Publish a counter value every 3 seconds
    static uint32_t lastMs = 0;
    static uint32_t count  = 0;

    if (millis() - lastMs > 3000) {
        lastMs = millis();
        datanet.publishFloat("my-project-id.test.channel", "count", (float)count++);
    }
}
