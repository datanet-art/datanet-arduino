/*
 * BinaryDMXOutputBridge.ino - DataNet binary DMX/Art-Net to physical output
 *
 * What it does:
 *   - Connects to WiFi and DataNet
 *   - Subscribes to a binary lighting channel
 *   - Accepts incoming binary/dmx frames or binary/artnet ArtDMX packets
 *   - Forwards DMX to an Art-Net node/controller over UDP
 *   - Optionally mirrors DMX RGB values to WS2815/WS2812-style LED strips
 *
 * Default build has no optional hardware dependency. To enable LED output:
 *   1. Install FastLED through Arduino Library Manager.
 *   2. Set DATANET_ENABLE_FASTLED to 1 below.
 */

#include <Arduino.h>
#include <DataNet.h>

#ifdef ESP32
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

#include <WiFiUdp.h>

// Optional WS2815/WS2812 output. Keep disabled for Library Manager compile.
#define DATANET_ENABLE_FASTLED 0

#if DATANET_ENABLE_FASTLED
  #include <FastLED.h>
  #define LED_PIN 5
  #define LED_COUNT 60
  #define LED_COLOR_ORDER GRB
  // Many WS2815 strips work with WS2812B/WS2811 timing in FastLED.
  #ifndef DATANET_FASTLED_CHIPSET
    #define DATANET_FASTLED_CHIPSET WS2812B
  #endif
  CRGB leds[LED_COUNT];
#endif

// ---------------------------------------------------------------------------
// Configuration - edit before flashing
// ---------------------------------------------------------------------------
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define DATANET_API_KEY "ak_YOUR_API_KEY_HERE"

// Channel may carry either binary/dmx or binary/artnet.
#define CHANNEL_LIGHTING "your-project-id.lighting.dmx"

// Art-Net node/controller. Change to your Art-Net to DMX controller IP.
IPAddress ARTNET_NODE_IP(2, 0, 0, 10);
const uint16_t ARTNET_PORT = 6454;
const uint8_t ARTNET_UNIVERSE = 0;
const uint8_t ARTNET_SUBNET = 0;
const uint8_t ARTNET_NET = 0;

DataNet datanet(DATANET_API_KEY);
WiFiUDP udp;

uint8_t dmxFrame[512];
uint8_t artnetPacket[18 + 512];
uint8_t artnetSequence = 1;
bool platformConnected = false;
uint32_t framesReceived = 0;

// ---------------------------------------------------------------------------
// DataNet events
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
// Outputs
// ---------------------------------------------------------------------------
void sendArtNet(const uint8_t* dmx, size_t dmxLength) {
  size_t packetLength = DataNet::buildArtDmxPacket(
    artnetPacket,
    sizeof(artnetPacket),
    dmx,
    dmxLength,
    ARTNET_UNIVERSE,
    ARTNET_SUBNET,
    ARTNET_NET,
    artnetSequence++,
    0
  );
  if (packetLength == 0) {
    Serial.println(F("[ArtNet] Could not build ArtDMX packet"));
    return;
  }

  udp.beginPacket(ARTNET_NODE_IP, ARTNET_PORT);
  udp.write(artnetPacket, packetLength);
  udp.endPacket();
}

void mirrorToLeds(const uint8_t* dmx, size_t dmxLength) {
#if DATANET_ENABLE_FASTLED
  for (uint16_t pixel = 0; pixel < LED_COUNT; pixel++) {
    size_t base = (size_t)pixel * 3;
    uint8_t red = base < dmxLength ? dmx[base] : 0;
    uint8_t green = base + 1 < dmxLength ? dmx[base + 1] : 0;
    uint8_t blue = base + 2 < dmxLength ? dmx[base + 2] : 0;
    leds[pixel] = CRGB(red, green, blue);
  }
  FastLED.show();
#else
  (void)dmx;
  (void)dmxLength;
#endif
}

void applyLightingFrame(const uint8_t* dmx, size_t dmxLength) {
  size_t copyLength = dmxLength < sizeof(dmxFrame) ? dmxLength : sizeof(dmxFrame);
  memcpy(dmxFrame, dmx, copyLength);
  if (copyLength < sizeof(dmxFrame)) {
    memset(dmxFrame + copyLength, 0, sizeof(dmxFrame) - copyLength);
  }

  sendArtNet(dmxFrame, sizeof(dmxFrame));
  mirrorToLeds(dmxFrame, sizeof(dmxFrame));

  framesReceived++;
  Serial.print(F("[Lighting] Applied frame #"));
  Serial.print(framesReceived);
  Serial.print(F(" first channels: "));
  for (int i = 0; i < 6; i++) {
    Serial.print(dmxFrame[i]);
    Serial.print(i == 5 ? '\n' : ' ');
  }
}

// ---------------------------------------------------------------------------
// Binary subscriber
// ---------------------------------------------------------------------------
void onLightingBinary(const uint8_t* data, size_t length, const BinaryMessageMeta& meta) {
  const uint8_t* dmx = data;
  size_t dmxLength = length;

  if (strcmp(meta.contentType, "binary/artnet") == 0) {
    uint8_t universe = 0;
    uint8_t subnet = 0;
    uint8_t net = 0;
    if (!DataNet::extractArtDmx(data, length, &dmx, &dmxLength, &universe, &subnet, &net)) {
      Serial.println(F("[Lighting] Ignored non-ArtDMX Art-Net packet"));
      return;
    }
    Serial.print(F("[Lighting] Received ArtDMX universe="));
    Serial.print(universe);
    Serial.print(F(" subnet="));
    Serial.print(subnet);
    Serial.print(F(" net="));
    Serial.println(net);
  } else if (strcmp(meta.contentType, "binary/dmx") != 0 && strcmp(meta.contentType, "application/octet-stream") != 0) {
    Serial.print(F("[Lighting] Unknown content type: "));
    Serial.println(meta.contentType);
    return;
  }

  applyLightingFrame(dmx, dmxLength);
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
void connectWiFi() {
  Serial.print(F("[WiFi] Connecting to "));
  Serial.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
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
// setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(150);

  Serial.println(F("\n=== DataNet Binary DMX Output Bridge ==="));
  Serial.print(F("Channel: "));
  Serial.println(CHANNEL_LIGHTING);
  Serial.print(F("Art-Net node: "));
  Serial.println(ARTNET_NODE_IP);

  memset(dmxFrame, 0, sizeof(dmxFrame));
  connectWiFi();
  udp.begin(ARTNET_PORT);

#if DATANET_ENABLE_FASTLED
  FastLED.addLeds<DATANET_FASTLED_CHIPSET, LED_PIN, LED_COLOR_ORDER>(leds, LED_COUNT);
  FastLED.clear(true);
#endif

  datanet.on("connect", onConnect);
  datanet.on("disconnect", onDisconnect);
  datanet.on("error", onError);
  datanet.subscribeBinary(CHANNEL_LIGHTING, onLightingBinary, "binary/dmx");

  if (!datanet.connect()) {
    Serial.println(F("[DataNet] Initial connect failed"));
  }
}

void loop() {
  datanet.loop();

  if (WiFi.status() != WL_CONNECTED) {
    platformConnected = false;
    WiFi.reconnect();
    delay(250);
    return;
  }

  delay(5);
}
