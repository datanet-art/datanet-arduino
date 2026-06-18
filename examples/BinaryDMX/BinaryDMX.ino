/*
 * BinaryDMX.ino - DataNet binary DMX send/receive example
 *
 * What it does:
 *   - Connects to WiFi
 *   - Connects to DataNet
 *   - Subscribes to a binary DMX channel
 *   - Publishes a 512-byte DMX frame every 2 seconds
 *   - Accepts incoming binary/dmx and binary/artnet messages
 *
 * Required libraries:
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
// Configuration - edit before flashing
// ---------------------------------------------------------------------------
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define DATANET_API_KEY "ak_YOUR_API_KEY_HERE"

// Use a binary-capable channel from your DataNet project.
#define CHANNEL_DMX     "your-project-id.lighting.dmx"

DataNet datanet(DATANET_API_KEY);

uint8_t dmxFrame[512];
uint32_t lastPublishMs = 0;
uint8_t chase = 0;
bool platformConnected = false;

// ---------------------------------------------------------------------------
// DataNet event handlers
// ---------------------------------------------------------------------------
void onConnect(const char* event, const char* info) {
  (void)event;
  (void)info;
  platformConnected = true;
  Serial.println(F("[DataNet] Connected"));
}

void onDisconnect(const char* event, const char* info) {
  (void)event;
  platformConnected = false;
  Serial.print(F("[DataNet] Disconnected: "));
  Serial.println(info);
}

void onError(const char* event, const char* info) {
  (void)event;
  Serial.print(F("[DataNet] Error: "));
  Serial.println(info);
}

// ---------------------------------------------------------------------------
// Binary message handler
// ---------------------------------------------------------------------------
void onBinaryDmx(const uint8_t* data, size_t length, const BinaryMessageMeta& meta) {
  Serial.print(F("[DataNet] Binary message ct="));
  Serial.print(meta.contentType);
  Serial.print(F(" bytes="));
  Serial.print(length);
  Serial.print(F(" channel="));
  Serial.println(meta.channel);

  const uint8_t* dmx = data;
  size_t dmxLength = length;

  if (strcmp(meta.contentType, "binary/artnet") == 0) {
    uint8_t universe = 0;
    uint8_t subnet = 0;
    uint8_t net = 0;
    if (!DataNet::extractArtDmx(data, length, &dmx, &dmxLength, &universe, &subnet, &net)) {
      Serial.println(F("[DMX] Incoming Art-Net packet was not ArtDMX"));
      return;
    }
    Serial.print(F("[DMX] Extracted ArtDMX universe="));
    Serial.print(universe);
    Serial.print(F(" subnet="));
    Serial.print(subnet);
    Serial.print(F(" net="));
    Serial.println(net);
  }

  size_t copyLength = dmxLength < sizeof(dmxFrame) ? dmxLength : sizeof(dmxFrame);
  memcpy(dmxFrame, dmx, copyLength);
  if (copyLength < sizeof(dmxFrame)) {
    memset(dmxFrame + copyLength, 0, sizeof(dmxFrame) - copyLength);
  }

  Serial.print(F("[DMX] First 8 channels: "));
  for (int i = 0; i < 8; i++) {
    Serial.print(dmxFrame[i]);
    Serial.print(i == 7 ? '\n' : ' ');
  }
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
void connectWiFi() {
  Serial.print(F("[WiFi] Connecting to "));
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
    if (millis() - startedAt > 30000UL) {
      Serial.println(F("\n[WiFi] Timeout. Restarting."));
      ESP.restart();
    }
  }

  Serial.println();
  Serial.print(F("[WiFi] Connected. IP: "));
  Serial.println(WiFi.localIP());
}

// ---------------------------------------------------------------------------
// Publish a small moving DMX pattern
// ---------------------------------------------------------------------------
void publishDmxFrame() {
  memset(dmxFrame, 0, sizeof(dmxFrame));

  // A tiny RGB chase: channels 1-3 plus one moving pixel-ish slot.
  dmxFrame[0] = 255;        // dimmer
  dmxFrame[1] = chase;      // red
  dmxFrame[2] = 255 - chase;// green
  dmxFrame[3] = 64;         // blue

  uint16_t slot = 4 + ((chase / 16) * 3);
  if (slot + 2 < sizeof(dmxFrame)) {
    dmxFrame[slot] = 255;
    dmxFrame[slot + 1] = chase;
    dmxFrame[slot + 2] = 255 - chase;
  }

  const char* metadata = "{\"universe\":1,\"format\":\"dmx512\",\"source\":\"arduino\"}";
  if (datanet.publishBinary(CHANNEL_DMX, dmxFrame, sizeof(dmxFrame), "binary/dmx", metadata)) {
    Serial.println(F("[DMX] Published binary/dmx frame"));
  } else {
    Serial.println(F("[DMX] Publish failed"));
  }

  chase += 16;
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(150);

  Serial.println(F("\n=== DataNet Binary DMX Example ==="));
  connectWiFi();

  datanet.on("connect", onConnect);
  datanet.on("disconnect", onDisconnect);
  datanet.on("error", onError);
  datanet.subscribeBinary(CHANNEL_DMX, onBinaryDmx, "binary/dmx");

  if (!datanet.connect()) {
    Serial.println(F("[DataNet] Initial connect failed"));
  }
}

void loop() {
  datanet.loop();

  if (platformConnected && millis() - lastPublishMs >= 2000UL) {
    lastPublishMs = millis();
    publishDmxFrame();
  }

  delay(5);
}
