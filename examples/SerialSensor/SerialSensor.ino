/*
 * SerialSensor.ino — DataNet Serial Bridge Example
 *
 * Outputs sensor readings as newline-delimited JSON over USB serial.
 * Works on any Arduino board — Uno, Mega, Nano, Leonardo, etc.
 * NO WiFi required. A separate bridge script (Node.js, Python, or browser
 * Web Serial API) reads this output and publishes it to DataNet.
 *
 * Output format — one JSON object per line at 115200 baud:
 *   {"sensor":"temperature","value":22.4,"unit":"C","n":1}
 *   {"sensor":"humidity","value":63.2,"unit":"%","n":1}
 *
 * Wiring:
 *   USB cable only — no additional components needed for simulation.
 *   Replace the stub functions at the bottom with real sensor libraries
 *   (DHT22, BME280, DS18B20, etc.) for production use.
 *
 * Bridge scripts (see demos/arduino-serial-bridge/):
 *   bridge-node/bridge.mjs          Node.js bridge
 *   bridge-python/bridge.py         Python bridge
 *   bridge-browser/index.html       p5.js + Web Serial API (Chrome/Edge)
 */

// ── Configuration ─────────────────────────────────────────────────────────
#define BAUD_RATE        115200UL
#define EMIT_INTERVAL_MS 2000UL    // how often to emit a reading (ms)

// ── State ─────────────────────────────────────────────────────────────────
static unsigned long lastEmitMs   = 0;
static unsigned int  readingCount = 0;

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(BAUD_RATE);

  // On boards with native USB (Leonardo, Micro, Pro Micro) the serial port
  // is emulated and needs time to enumerate. Wait up to 3 s then continue.
#if defined(USBCON)
  unsigned long t = millis();
  while (!Serial && (millis() - t) < 3000) { /* wait */ }
#endif

  // Emit a startup marker so the bridge knows the sketch is alive.
  Serial.println(F("{\"status\":\"ready\",\"sketch\":\"SerialSensor\",\"baud\":115200}"));
}

// ── Loop ───────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();
  if (now - lastEmitMs >= EMIT_INTERVAL_MS) {
    lastEmitMs = now;
    emitReadings();
  }
}

// ── Emit ───────────────────────────────────────────────────────────────────

void emitReadings() {
  readingCount++;

  float temp = readTemperature();
  float hum  = readHumidity();

  emitJSON("temperature", temp, "C");
  emitJSON("humidity",    hum,  "%");
}

/**
 * Emit a single sensor reading as a JSON line.
 * Format: {"sensor":"<name>","value":<v>,"unit":"<u>","n":<count>}
 *
 * Using Serial.print() calls instead of sprintf avoids pulling in the
 * heavyweight printf implementation on Uno (saves ~1.5 KB flash).
 */
void emitJSON(const char* sensor, float value, const char* unit) {
  Serial.print(F("{\"sensor\":\""));
  Serial.print(sensor);
  Serial.print(F("\",\"value\":"));
  Serial.print(value, 2);          // 2 decimal places
  Serial.print(F(",\"unit\":\""));
  Serial.print(unit);
  Serial.print(F("\",\"n\":"));
  Serial.print(readingCount);
  Serial.println(F("}"));          // newline terminates the JSON line
}

// ── Simulated sensor reads ─────────────────────────────────────────────────
// Replace these with real sensor library calls (DHT22, BME280, etc.)

float readTemperature() {
  // Sine wave 18–28 °C with small jitter
  float base  = 23.0 + 5.0 * sin((float)millis() / 10000.0);
  float jitter = ((float)(random(0, 21)) - 10.0) * 0.05;
  return base + jitter;
}

float readHumidity() {
  // Slow drift 45–75 %RH offset from temperature wave
  float base  = 60.0 + 15.0 * sin((float)millis() / 15000.0 + 1.0);
  float jitter = ((float)(random(0, 21)) - 10.0) * 0.1;
  return base + jitter;
}
