/*
 * ESP32Button.ino - Publish a physical button to DataNet
 *
 * Wiring:
 *   - one button leg to GPIO 4
 *   - the opposite button leg to GND
 *
 * INPUT_PULLUP keeps the input stable without an external resistor. Pressed
 * therefore reads LOW. Each debounced state change is published as JSON.
 */

#include <Arduino.h>

#if !defined(ESP32)
  #error "ESP32Button requires an ESP32 board."
#endif

#include <WiFi.h>
#include <DataNet.h>

#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define API_KEY       "ak_YOUR_API_KEY_HERE"
#define CHANNEL       "project.your-project-id.controls.button"

const uint8_t BUTTON_PIN = 4;
const uint32_t DEBOUNCE_MS = 30;

DataNet datanet(API_KEY);
bool rawPressed = false;
bool stablePressed = false;
bool initialStatePublished = false;
uint32_t changedAtMs = 0;
uint32_t pressCount = 0;

void onEvent(const char* event, const char* info) {
  Serial.print(F("[DataNet] "));
  Serial.print(event);
  if (info != nullptr && info[0] != '\0') {
    Serial.print(F(": "));
    Serial.print(info);
  }
  Serial.println();
}

void publishButton() {
  JsonDocument data;
  data[F("pressed")] = stablePressed;
  data[F("press_count")] = pressCount;

  if (datanet.publish(CHANNEL, data.as<JsonVariant>())) {
    Serial.print(F("[Button] pressed="));
    Serial.print(stablePressed ? F("true") : F("false"));
    Serial.print(F(" count="));
    Serial.println(pressCount);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("[WiFi] Connecting"));
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.print(F("\n[WiFi] IP: "));
  Serial.println(WiFi.localIP());

  rawPressed = digitalRead(BUTTON_PIN) == LOW;
  stablePressed = rawPressed;

  datanet.on("connect", onEvent);
  datanet.on("disconnect", onEvent);
  datanet.on("error", onEvent);
  datanet.connect();
}

void loop() {
  datanet.loop();
  const uint32_t now = millis();
  const bool reading = digitalRead(BUTTON_PIN) == LOW;

  if (reading != rawPressed) {
    rawPressed = reading;
    changedAtMs = now;
  }

  if (rawPressed != stablePressed && now - changedAtMs >= DEBOUNCE_MS) {
    stablePressed = rawPressed;
    if (stablePressed) {
      pressCount++;
    }
    if (datanet.connected()) {
      publishButton();
    }
  }

  if (datanet.connected() && !initialStatePublished) {
    initialStatePublished = true;
    publishButton();
  }

  if (!datanet.connected()) {
    initialStatePublished = false;
  }
}
