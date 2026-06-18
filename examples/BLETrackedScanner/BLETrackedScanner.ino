/*
 * BLETrackedScanner.ino — ESP32 BLE tracker -> DataNet example
 *
 * This example is intended as the practical "real step" for taking an
 * existing BLE scanner node and publishing its results into the DataNet
 * platform instead of a custom WebSocket or HTTP ingest service.
 *
 * Target boards:
 *   - Freenove ESP32-WROOM / ESP32 Dev Module
 *   - Freenove ESP32-S3-WROOM / ESP32S3 Dev Module
 *
 * Required board settings:
 *   - Board: match your actual hardware (ESP32 Dev Module or ESP32S3 Dev Module)
 *   - Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS) or larger
 *   - Flash Size: 4MB+ (8MB on many ESP32-S3 boards)
 *   - PSRAM: use the board default; enable it on boards that provide it
 *
 * Required libraries:
 *   - ArduinoJson by Benoit Blanchon, v7+
 *   - WebSockets by Markus Sattler (Links2004), v2.4+
 */

#include <Arduino.h>
#include <DataNet.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#ifndef ESP32
  #error "BLETrackedScanner.ino requires an ESP32 board."
#endif

// ---------------------------------------------------------------------------
// Configuration — edit before flashing
// ---------------------------------------------------------------------------
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* API_KEY       = "ak_YOUR_API_KEY_HERE";

// Use a real channel from your DataNet project.
const char* CHANNEL_BLE_SCAN = "your-project-id.ble.scan";

// Scanner identity metadata
const char* DEVICE_ID     = "esp32-studio-01";
const char* LOCATION_NAME = "Studio";
const float LATITUDE      = 43.6532f;
const float LONGITUDE     = -79.3832f;

// BLE scan behavior
const uint32_t SCAN_INTERVAL_MS = 1500UL;
const uint8_t SCAN_SECONDS      = 1;
const int RSSI_THRESHOLD        = -100;
const uint32_t DEVICE_TIMEOUT_MS = 15000UL;

// Payload / tracking limits
const uint8_t MAX_TRACKED_DEVICES = 30;
const uint8_t MAX_DEVICES_PER_PUSH = 20;

struct TrackedDevice {
  bool active;
  char address[18];
  char name[32];
  int rssi;
  uint16_t manufacturerId;
  char manufacturerData[41];
  uint32_t lastSeenMs;
};

DataNet datanet(API_KEY);
BLEScan* bleScan = nullptr;
TrackedDevice trackedDevices[MAX_TRACKED_DEVICES];

bool platformConnected = false;
uint32_t lastScanMs = 0;
uint32_t lastPublishMs = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void resetTrackedDevices() {
  for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
    trackedDevices[i].active = false;
    trackedDevices[i].address[0] = '\0';
    trackedDevices[i].name[0] = '\0';
    trackedDevices[i].rssi = 0;
    trackedDevices[i].manufacturerId = 0;
    trackedDevices[i].manufacturerData[0] = '\0';
    trackedDevices[i].lastSeenMs = 0;
  }
}

int findDeviceSlot(const char* address) {
  for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
    if (trackedDevices[i].active && strncmp(trackedDevices[i].address, address, sizeof(trackedDevices[i].address)) == 0) {
      return i;
    }
  }
  return -1;
}

int findFreeSlot() {
  for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
    if (!trackedDevices[i].active) return i;
  }
  return -1;
}

int activeDeviceCount() {
  int count = 0;
  for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
    if (trackedDevices[i].active) count++;
  }
  return count;
}

void cleanupExpiredDevices() {
  const uint32_t now = millis();
  for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
    if (trackedDevices[i].active && now - trackedDevices[i].lastSeenMs > DEVICE_TIMEOUT_MS) {
      trackedDevices[i].active = false;
    }
  }
}

void connectWiFi() {
  Serial.print(F("[WiFi] Connecting to "));
  Serial.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
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
// BLE callback
// ---------------------------------------------------------------------------
class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    const int rssi = advertisedDevice.getRSSI();
    if (rssi < RSSI_THRESHOLD) return;

    String address = advertisedDevice.getAddress().toString().c_str();
    int slot = findDeviceSlot(address.c_str());
    if (slot < 0) slot = findFreeSlot();
    if (slot < 0) return;

    TrackedDevice& device = trackedDevices[slot];
    device.active = true;
    strncpy(device.address, address.c_str(), sizeof(device.address) - 1);
    device.address[sizeof(device.address) - 1] = '\0';

    if (advertisedDevice.haveName()) {
      String name = advertisedDevice.getName().c_str();
      strncpy(device.name, name.c_str(), sizeof(device.name) - 1);
      device.name[sizeof(device.name) - 1] = '\0';
    } else {
      device.name[0] = '\0';
    }

    device.rssi = rssi;
    device.lastSeenMs = millis();
    device.manufacturerId = 0;
    device.manufacturerData[0] = '\0';

    if (advertisedDevice.haveManufacturerData()) {
      String data = advertisedDevice.getManufacturerData();
      if (data.length() >= 2) {
        device.manufacturerId = ((uint8_t)data[1] << 8) | (uint8_t)data[0];
        const int limit = data.length() < 20 ? data.length() : 20;
        int out = 0;
        for (int i = 0; i < limit && out + 2 < (int)sizeof(device.manufacturerData); i++) {
          sprintf(&device.manufacturerData[out], "%02X", (uint8_t)data[i]);
          out += 2;
        }
        device.manufacturerData[out] = '\0';
      }
    }

    Serial.printf("[BLE] %s RSSI=%d %s\n",
      device.address,
      device.rssi,
      device.name[0] ? device.name : "(unnamed)");
  }
};

// ---------------------------------------------------------------------------
// Publish
// ---------------------------------------------------------------------------
void publishTrackedDevices() {
  if (!platformConnected) return;

  StaticJsonDocument<2048> doc;
  doc["device_id"] = DEVICE_ID;
  doc["location"] = LOCATION_NAME;
  doc["lat"] = LATITUDE;
  doc["lng"] = LONGITUDE;
  doc["kind"] = "ble_scan";
  doc["timestamp_ms"] = millis();
  doc["scan_interval_ms"] = SCAN_INTERVAL_MS;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["scan_count"] = activeDeviceCount();

  JsonArray devices = doc.createNestedArray("devices");

  int emitted = 0;
  for (int i = 0; i < MAX_TRACKED_DEVICES && emitted < MAX_DEVICES_PER_PUSH; i++) {
    if (!trackedDevices[i].active) continue;

    JsonObject row = devices.createNestedObject();
    row["address"] = trackedDevices[i].address;
    row["rssi"] = trackedDevices[i].rssi;

    if (trackedDevices[i].name[0]) {
      row["localName"] = trackedDevices[i].name;
    }
    if (trackedDevices[i].manufacturerId > 0) {
      row["manufacturerId"] = trackedDevices[i].manufacturerId;
    }
    if (trackedDevices[i].manufacturerData[0]) {
      row["manufacturerData"] = trackedDevices[i].manufacturerData;
    }

    emitted++;
  }

  if (devices.size() == 0) return;

  if (datanet.publish(CHANNEL_BLE_SCAN, doc.as<JsonVariant>())) {
    Serial.printf("[DataNet] Published %d tracked device(s) to %s\n", (int)devices.size(), CHANNEL_BLE_SCAN);
  } else {
    Serial.println(F("[DataNet] Publish failed"));
  }
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(150);

  Serial.println(F("\n=== DataNet BLE Tracked Scanner ==="));
  Serial.print(F("Device ID: "));
  Serial.println(DEVICE_ID);
  Serial.print(F("Channel: "));
  Serial.println(CHANNEL_BLE_SCAN);

  resetTrackedDevices();
  connectWiFi();

  datanet.on("connect", onConnect);
  datanet.on("disconnect", onDisconnect);
  datanet.on("error", onError);

  Serial.println(F("[DataNet] Connecting..."));
  if (!datanet.connect()) {
    Serial.println(F("[DataNet] Initial connect failed; reconnect logic will retry"));
  }

  BLEDevice::init("ESP32-BLE-Scanner");
  bleScan = BLEDevice::getScan();
  bleScan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  bleScan->setActiveScan(true);
  bleScan->setInterval(80);
  bleScan->setWindow(80);
}

void loop() {
  datanet.loop();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(250);
    return;
  }

  const uint32_t now = millis();
  if (now - lastScanMs >= SCAN_INTERVAL_MS) {
    lastScanMs = now;

    Serial.println(F("[BLE] Scanning..."));
    bleScan->start(SCAN_SECONDS, false);
    cleanupExpiredDevices();
    publishTrackedDevices();
    bleScan->clearResults();

    lastPublishMs = now;
    Serial.printf("[BLE] Active tracked devices: %d\n", activeDeviceCount());
  }

  delay(10);
}
