/*
 * TemperatureSensor.ino — DataNet SDK Example
 *
 * Simulates a temperature/humidity sensor that:
 *   - Connects to WiFi
 *   - Authenticates with the DataNet platform
 *   - Publishes temperature + humidity readings every 5 seconds
 *   - Subscribes to a "commands" channel and prints received commands
 *
 * Required libraries (install via Arduino Library Manager):
 *   - ArduinoJson by Benoit Blanchon, v7+
 *   - WebSockets by Markus Sattler (Links2004), v2.4+
 *
 * Board support:
 *   - ESP32: use "ESP32 Arduino" board package
 *   - ESP8266: use "ESP8266 Arduino" board package
 */

#include <Arduino.h>
#include <DataNet.h>

#ifdef ESP32
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

// ---------------------------------------------------------------------------
// Configuration — edit these before flashing
// ---------------------------------------------------------------------------
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define DATANET_API_KEY "ak_YOUR_API_KEY_HERE"

// Channels — replace "your-project-id" with your real project ID from the dashboard
#define CHANNEL_TELEMETRY  "your-project-id.sensors.temperature"
#define CHANNEL_COMMANDS   "your-project-id.sensors.commands"

// How often to publish telemetry (milliseconds)
#define PUBLISH_INTERVAL_MS 5000UL

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
DataNet datanet(DATANET_API_KEY);

uint32_t lastPublishMs = 0;
bool     platformConnected = false;

// ---------------------------------------------------------------------------
// Message handler — called when a message arrives on CHANNEL_COMMANDS
// ---------------------------------------------------------------------------
void onCommand(const char* channel, JsonVariant data) {
    Serial.print(F("[App] Command received on "));
    Serial.println(channel);

    // Show how old this message is using the server-side timestamp
    uint64_t ts  = datanet.getLastTimestamp(channel);
    uint64_t now = (uint64_t)millis(); // local uptime, not wall clock
    // ts is a Unix ms timestamp from the server; useful for latency or stale-data checks
    Serial.print(F("  server ts (ms): "));
    Serial.println((uint32_t)(ts & 0xFFFFFFFF));  // print lower 32 bits (fits Serial)

    // data is the "d" field of the incoming envelope — inspect it:
    if (data.is<JsonObject>()) {
        JsonObject obj = data.as<JsonObject>();
        for (JsonPair kv : obj) {
            Serial.print(F("  "));
            Serial.print(kv.key().c_str());
            Serial.print(F(": "));
            Serial.println(kv.value().as<String>());
        }
    } else {
        Serial.println(data.as<String>());
    }
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------
void onConnect(const char* event, const char* info) {
    Serial.println(F("[App] Connected to DataNet!"));
    platformConnected = true;
}

void onDisconnect(const char* event, const char* info) {
    Serial.print(F("[App] Disconnected: "));
    Serial.println(info);
    platformConnected = false;
}

void onError(const char* event, const char* info) {
    Serial.print(F("[App] Error: "));
    Serial.println(info);
}

// ---------------------------------------------------------------------------
// Simulate sensor readings
// ---------------------------------------------------------------------------
float readTemperature() {
    // Simulate 18–30 °C with one decimal place
    return 18.0f + static_cast<float>(random(0, 120)) / 10.0f;
}

float readHumidity() {
    // Simulate 40–80 % RH
    return 40.0f + static_cast<float>(random(0, 400)) / 10.0f;
}

// ---------------------------------------------------------------------------
// Publish a telemetry reading
// ---------------------------------------------------------------------------
void publishTelemetry() {
    float temp = readTemperature();
    float hum  = readHumidity();

    // Build the data object manually for a multi-field payload
    StaticJsonDocument<128> dataDoc;
    dataDoc[F("temperature")] = temp;
    dataDoc[F("humidity")]    = hum;
    dataDoc[F("unit")]        = F("celsius");

    if (datanet.publish(CHANNEL_TELEMETRY, dataDoc.as<JsonVariant>())) {
        Serial.printf("[App] Published: temp=%.1f°C  hum=%.1f%%\n", temp, hum);
    } else {
        Serial.println(F("[App] Publish failed (not connected?)"));
    }
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println(F("\n=== DataNet Temperature Sensor Example ==="));

    // Seed random number generator from an unconnected ADC pin (GPIO 0 / A0)
    randomSeed(analogRead(0));

    // --- Connect to WiFi ---
    Serial.print(F("[WiFi] Connecting to "));
    Serial.print(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Block until WiFi connects — in a production sketch you may want a
    // non-blocking approach with a timeout and deep-sleep fallback.
    uint32_t wifiStartMs = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(F("."));
        if (millis() - wifiStartMs > 30000UL) {
            Serial.println(F("\n[WiFi] Timeout — restarting"));
            ESP.restart();
        }
    }

    Serial.println();
    Serial.print(F("[WiFi] Connected, IP: "));
    Serial.println(WiFi.localIP());

    // --- Register DataNet event handlers ---
    datanet.on("connect",    onConnect);
    datanet.on("disconnect", onDisconnect);
    datanet.on("error",      onError);

    // --- Subscribe to commands channel ---
    // Registering before connect() is fine — the SDK re-sends sub envelopes
    // automatically on every (re)connect.
    datanet.subscribe(CHANNEL_COMMANDS, onCommand);

    // --- Connect to DataNet ---
    Serial.println(F("[DataNet] Connecting..."));
    if (!datanet.connect()) {
        Serial.println(F("[DataNet] Initial connect failed — will retry via reconnect logic"));
    }

    lastPublishMs = millis();
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop() {
    // MUST call this every iteration — drives WebSocket events and heartbeat
    datanet.loop();

    // Publish telemetry at the configured interval (non-blocking)
    uint32_t now = millis();
    if (platformConnected && (now - lastPublishMs >= PUBLISH_INTERVAL_MS)) {
        lastPublishMs = now;
        publishTelemetry();
    }
}
