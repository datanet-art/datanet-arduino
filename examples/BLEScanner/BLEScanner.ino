/*
 * BLEScanner.ino — ESP32 BLE scan -> DataNet example
 *
 * Target boards:
 *   - ESP32 Dev Module / Freenove ESP32-WROOM
 *   - ESP32S3 Dev Module / Freenove ESP32-S3-WROOM
 *
 * Recommended board settings for BLE sketches:
 *   - Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS) or larger
 *   - On ESP32-S3 boards, use the matching S3 board profile
 *
 * What it does:
 *   - Connects to WiFi
 *   - Connects to DataNet
 *   - Scans for nearby BLE advertisements on an interval
 *   - Publishes a compact scan summary to a DataNet channel
 *
 * Required libraries:
 *   - ArduinoJson by Benoit Blanchon, v7+
 *   - WebSockets by Markus Sattler (Links2004), v2.4+
 *
 * ESP32 board package:
 *   - esp32 by Espressif
 */

#include <Arduino.h>
#include <DataNet.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#ifndef ESP32
  #error "BLEScanner.ino requires an ESP32 board."
#endif

// ---------------------------------------------------------------------------
// Configuration — edit before flashing
// ---------------------------------------------------------------------------
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* API_KEY       = "ak_YOUR_API_KEY_HERE";

// Use a real channel from your project/dashboard.
const char* CHANNEL_BLE_SCAN = "your-project-id.ble.scan";

// Optional label so multiple boards are easy to distinguish in the dashboard.
const char* DEVICE_ID = "freenove-esp32-wroom-01";

// Scan timing
const uint32_t SCAN_INTERVAL_MS = 5000UL;
const uint32_t SCAN_WINDOW_MS   = 1000UL;
const uint8_t  MAX_DEVICES_SENT = 5;

DataNet datanet(API_KEY);
BLEScan* bleScan = nullptr;

bool platformConnected = false;
uint32_t lastScanMs = 0;

// ---------------------------------------------------------------------------
// DataNet events
// ---------------------------------------------------------------------------
void onConnect(const char* event, const char* info) {
  (void)event;
  (void)info;
  Serial.println(F("[DataNet] Connected"));
  platformConnected = true;
}

void onDisconnect(const char* event, const char* info) {
  (void)event;
  Serial.print(F("[DataNet] Disconnected: "));
  Serial.println(info);
  platformConnected = false;
}

void onError(const char* event, const char* info) {
  (void)event;
  Serial.print(F("[DataNet] Error: "));
  Serial.println(info);
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
void connectWiFi() {
  Serial.print(F("[WiFi] Connecting to "));
  Serial.print(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
    if (millis() - startMs > 30000UL) {
      Serial.println(F("\n[WiFi] Timeout. Restarting."));
      ESP.restart();
    }
  }

  Serial.println();
  Serial.print(F("[WiFi] Connected. IP: "));
  Serial.println(WiFi.localIP());
}

// ---------------------------------------------------------------------------
// BLE scan + publish
// ---------------------------------------------------------------------------
void publishBleScan() {
  if (!bleScan) return;

  // Service the socket before/after scan work so reconnects and heartbeats
  // are not starved by BLE activity on slower boards.
  datanet.loop();

  const uint32_t scanSeconds = SCAN_WINDOW_MS / 1000UL;
  BLEScanResults* results = bleScan->start(scanSeconds > 0 ? scanSeconds : 1, false);
  datanet.loop();

  if (!results) {
    Serial.println(F("[BLE] Scan returned no results object"));
    return;
  }

  const int totalFound = results->getCount();
  Serial.printf("[BLE] Found %d device(s)\n", totalFound);

  StaticJsonDocument<448> doc;
  doc["device_id"] = DEVICE_ID;
  doc["kind"] = "ble_scan";
  doc["count"] = totalFound;
  doc["scan_window_ms"] = SCAN_WINDOW_MS;
  doc["timestamp_ms"] = millis();

  JsonArray devices = doc.createNestedArray("devices");

  int strongestRssi = -999;
  String strongestMac = "";

  const int limit = totalFound < MAX_DEVICES_SENT ? totalFound : MAX_DEVICES_SENT;
  for (int i = 0; i < limit; i++) {
    BLEAdvertisedDevice device = results->getDevice(i);
    const int rssi = device.getRSSI();
    String mac = device.getAddress().toString().c_str();
    String name = device.haveName() ? device.getName().c_str() : "";

    if (rssi > strongestRssi) {
      strongestRssi = rssi;
      strongestMac = mac;
    }

    JsonObject row = devices.createNestedObject();
    row["mac"] = mac;
    row["rssi"] = rssi;
    if (name.length() > 0) {
      row["name"] = name;
    }
  }

  if (strongestMac.length() > 0) {
    doc["strongest_mac"] = strongestMac;
    doc["strongest_rssi"] = strongestRssi;
  }

  if (datanet.publish(CHANNEL_BLE_SCAN, doc.as<JsonVariant>())) {
    Serial.printf("[DataNet] Published BLE scan to %s\n", CHANNEL_BLE_SCAN);
  } else {
    Serial.println(F("[DataNet] Publish failed"));
  }

  bleScan->clearResults();
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(150);

  Serial.println(F("\n=== DataNet ESP32 BLE Scanner ==="));
  Serial.print(F("Device ID: "));
  Serial.println(DEVICE_ID);
  Serial.print(F("Channel: "));
  Serial.println(CHANNEL_BLE_SCAN);

  connectWiFi();

  datanet.on("connect", onConnect);
  datanet.on("disconnect", onDisconnect);
  datanet.on("error", onError);

  Serial.println(F("[DataNet] Connecting..."));
  if (!datanet.connect()) {
    Serial.println(F("[DataNet] Initial connect failed; reconnect logic will retry"));
  }

  BLEDevice::init("");
  bleScan = BLEDevice::getScan();
  bleScan->setActiveScan(true);
  bleScan->setInterval(160);
  bleScan->setWindow(120);

  lastScanMs = millis() - SCAN_INTERVAL_MS;
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
void loop() {
  datanet.loop();

  if (!platformConnected) {
    delay(20);
    return;
  }

  const uint32_t now = millis();
  if (now - lastScanMs >= SCAN_INTERVAL_MS) {
    lastScanMs = now;
    publishBleScan();
  }

  delay(20);
}
