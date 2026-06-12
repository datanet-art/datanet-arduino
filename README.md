# DataNet Arduino SDK

Realtime pub/sub for ESP32 and ESP8266 devices. Connect to the [DataNet](https://datanet.art) platform, subscribe to channels, and publish JSON or binary DMX/Art-Net messages — with automatic JWT authentication, heartbeating, and exponential-backoff reconnection.

---

## Installation

Install from the DataNet repository today:

1. Download this repo as a ZIP, or clone it locally.
2. Rename the folder to `DataNet` if needed.
3. In the Arduino IDE use **Sketch -> Include Library -> Add .ZIP Library...**, or place the `DataNet` folder inside your Arduino libraries directory manually.

When the library is submitted to Arduino Library Manager, the preferred install
path will be:

1. Open **Library Manager** in the Arduino IDE.
2. Search for `DataNet`.
3. Install the latest release.

Until then, the repo-first flow above is the canonical path.

### Required dependencies

Install these via **Library Manager** before using DataNet:

| Library | Author | Version |
|---|---|---|
| ArduinoJson | Benoit Blanchon | 7.x |
| WebSockets | Markus Sattler (Links2004) | 2.4.x |

`HTTPClient` is bundled with the ESP32 and ESP8266 Arduino board packages, so
no separate install is needed there.

### PlatformIO

This repository also includes `library.json` for PlatformIO. After the package
is published to the PlatformIO Registry, use:

```ini
lib_deps =
  datanet-art/DataNet
```

---

## Quick Start

```cpp
#include <WiFi.h>      // or <ESP8266WiFi.h>
#include <DataNet.h>

DataNet datanet("ak_YOUR_API_KEY");

void onMessage(const char* channel, JsonVariant data) {
    Serial.println(data.as<String>());
}

void setup() {
    Serial.begin(115200);

    WiFi.begin("SSID", "PASSWORD");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    datanet.subscribe("my-project.my-channel", onMessage);
    datanet.connect();
}

void loop() {
    datanet.loop();                                          // required every iteration
    datanet.publishFloat("my-project.my-channel", "temp", 23.5f);
    delay(5000);
}
```

> **WiFi must be connected before calling `datanet.connect()`.**
> The SDK does not manage the WiFi connection for you.

---

## API Reference

### Constructor

```cpp
DataNet datanet(
    const char* apiKey,
    const char* apiUrl = "https://api.datanet.art",
    const char* wsHost = "ws.datanet.art",
    int         wsPort = 443
);
```

Override `apiUrl`, `wsHost`, and `wsPort` to point at a staging or local server.

---

### Methods

| Method | Returns | Description |
|---|---|---|
| `connect()` | `bool` | Fetch JWT via HTTPS, open WSS connection. Returns `true` on success. WiFi must already be connected. |
| `loop()` | `void` | **Must be called every `loop()` iteration.** Drives WebSocket events and heartbeat. |
| `connected()` | `bool` | `true` if the WebSocket is currently open. |
| `subscribe(channel, handler)` | `void` | Subscribe to a channel. `handler` is called on each incoming message. Max `DATANET_MAX_SUBS` (default 8) channels. |
| `unsubscribe(channel)` | `void` | Remove a channel subscription and send an `unsub` envelope. |
| `subscribeBinary(channel, handler, contentType)` | `void` | Subscribe to binary envelopes on a channel. Handler receives bytes plus metadata. |
| `unsubscribeBinary(channel)` | `void` | Remove a binary subscription. |
| `publish(channel, data)` | `bool` | Publish a `JsonVariant` as the `d` field. Returns `false` if not connected or serialization fails. |
| `publishFloat(channel, key, value)` | `bool` | Convenience: publish `{key: value}` as a float. |
| `publishString(channel, key, value)` | `bool` | Convenience: publish `{key: "value"}` as a string. |
| `publishBinary(channel, data, length, contentType, metadataJson)` | `bool` | Publish bytes as a DataNet binary envelope. |
| `publishDmx(channel, values, valueCount, frameLength)` | `bool` | Publish a 1-512 byte DMX frame as `binary/dmx`. |
| `publishArtNet(channel, dmx, dmxLength, universe, subnet, net, sequence, physical)` | `bool` | Build and publish an Art-Net ArtDMX packet as `binary/artnet`. |
| `on(event, handler)` | `void` | Register a lifecycle event handler. Supported events: `"connect"`, `"disconnect"`, `"error"`. |

---

### Handler signatures

```cpp
// Message handler
void myHandler(const char* channel, JsonVariant data);

// Binary message handler
void myBinaryHandler(const uint8_t* data, size_t length, const BinaryMessageMeta& meta);

// Event handler
void myEventHandler(const char* event, const char* info);
```

---

### Binary DMX / Art-Net

DataNet binary messages are still sent as WebSocket text frames, but the payload
bytes are base64 encoded inside a metadata-bearing envelope:

```json
{"op":"pub","ch":"project.lighting.dmx","bin":true,"b64":"AQID","ct":"binary/dmx","meta":{"universe":1}}
```

The Arduino SDK exposes those bytes directly in `subscribeBinary()` callbacks.
`meta.contentType` identifies the packet format. The supported helper formats
are:

| Content type | Helper |
|---|---|
| `binary/dmx` | `publishDmx`, `buildDmxFrame` |
| `binary/artnet` | `publishArtNet`, `buildArtDmxPacket`, `extractArtDmx` |
| `application/octet-stream` | `publishBinary` |

Example:

```cpp
uint8_t dmx[512];

void onDmx(const uint8_t* data, size_t length, const BinaryMessageMeta& meta) {
    Serial.print("binary content type: ");
    Serial.println(meta.contentType);
}

void loop() {
    datanet.loop();
    datanet.publishBinary("project.lighting.dmx", dmx, sizeof(dmx), "binary/dmx");
}
```

### Compile-time configuration

Override before `#include <DataNet.h>` or via `-D` compiler flags:

| Macro | Default | Description |
|---|---|---|
| `DATANET_MAX_SUBS` | `8` | Maximum simultaneous channel subscriptions |
| `DATANET_MAX_EVENT_HANDLERS` | `4` | Maximum handlers per event type |
| `DATANET_JWT_BUF_SIZE` | `2048` | JWT character buffer size (bytes) |
| `DATANET_HEARTBEAT_INTERVAL_MS` | `30000` | Heartbeat send interval (ms) |
| `DATANET_RECONNECT_BASE_MS` | `1000` | Base reconnect backoff (ms) |
| `DATANET_RECONNECT_MAX_MS` | `60000` | Maximum reconnect backoff cap (ms) |
| `DATANET_JWT_REFRESH_AFTER_RECONNECTS` | `3` | Re-fetch JWT after this many reconnects |
| `DATANET_INCOMING_JSON_SIZE` | `2048` | JSON document size for incoming protocol envelopes, including base64 binary payloads |
| `DATANET_BINARY_BUF_SIZE` | `768` | Scratch buffer for decoded incoming binary bytes |

Example:

```cpp
#define DATANET_MAX_SUBS 4        // save RAM on constrained devices
#define DATANET_JWT_BUF_SIZE 2048 // increase if your JWT is longer than the default
#include <DataNet.h>
```

---

## Examples

### BasicPubSub

`File → Examples → DataNet → BasicPubSub`

Minimal subscribe + publish loop. Good starting point.

### TemperatureSensor

`File → Examples → DataNet → TemperatureSensor`

Simulated temperature/humidity sensor that publishes every 5 seconds and subscribes to a commands channel. Demonstrates event handlers, multi-field payloads, and proper `setup()`/`loop()` patterns.

### BLEScanner

`File → Examples → DataNet → BLEScanner`

Compact BLE scan summary publisher for ESP32 boards. This example is the quickest way to verify BLE scan data is reaching a DataNet channel.

### BLETrackedScanner

`File → Examples → DataNet → BLETrackedScanner`

More production-shaped BLE scanner that tracks nearby devices over time and publishes larger batched payloads. Use an ESP32 board profile with a 3 MB app partition or larger.

### BinaryDMX

`File → Examples → DataNet → BinaryDMX`

Binary lighting round-trip sketch. It subscribes to a DataNet binary lighting
channel, accepts both `binary/dmx` and `binary/artnet`, and publishes a 512-byte
`binary/dmx` frame on an interval.

### BinaryDMXOutputBridge

`File → Examples → DataNet → BinaryDMXOutputBridge`

Hardware-facing bridge. It receives `binary/dmx` or `binary/artnet` from
DataNet, forwards frames to an Art-Net node/controller over UDP, and can
optionally mirror DMX RGB channels to WS2815/WS2812-style LEDs with FastLED.
FastLED is optional and disabled by default so the library still compiles
without extra dependencies.

---

## Protocol Notes

The SDK communicates using the DataNet WebSocket protocol:

- **Auth:** `POST https://api.datanet.art/auth/token` with `{"apiKey":"ak_..."}` → `{"token":"<jwt>"}`
- **WebSocket:** connect to `wss://ws.datanet.art/ws` with subprotocol header `Sec-WebSocket-Protocol: bearer, <jwt>`
- **Envelope format:** `{"op":"pub|sub|unsub|hb", "ch":"channel-name", "d":{...}}`
- **Binary envelope:** `{"op":"pub","ch":"channel-name","bin":true,"b64":"...","ct":"binary/dmx","meta":{...}}`
- **Heartbeat:** `{"op":"hb"}` sent every 30 seconds (configurable)

---

## Memory Tips for Constrained Devices

**ESP8266 has ~80 KB of heap.** Keep these points in mind:

- Reduce `DATANET_MAX_SUBS` if you only use a few channels.
- Do not shrink `DATANET_JWT_BUF_SIZE` aggressively. Current platform JWTs can exceed 512 bytes once scopes and limits are embedded, so `2048` is the safe default for production examples.
- Use `StaticJsonDocument` (stack-allocated) in your message handlers rather than `DynamicJsonDocument` (heap-allocated).
- Avoid subscribing to channels with very large payloads. Incoming protocol envelopes are sized by `DATANET_INCOMING_JSON_SIZE`, and decoded binary bytes are held in `DATANET_BINARY_BUF_SIZE`.
- A 512-byte DMX frame becomes about 684 base64 characters before JSON envelope overhead. The default binary scratch buffer is sized for full DMX and ArtDMX payloads.
- Call `WiFi.setOutputPower(10)` to reduce WiFi TX power if signal strength allows — this cuts current draw significantly on battery-powered nodes.
- The TLS/SSL handshake requires ~30 KB of heap momentarily. Ensure your sketch does not allocate large buffers before calling `connect()`.
- BLE examples on ESP32 often need a large app partition because WiFi + TLS + WebSockets + BLE is flash-heavy. On 4 MB boards, use `Huge APP (3MB No OTA/1MB SPIFFS)`. On ESP32-S3 boards, choose a board profile and partition layout that exposes at least a 3 MB app slot.

### SSL certificate verification

By default, the SDK calls `setInsecure()` on the TLS client, which skips server certificate verification. This is acceptable for prototyping but **should be replaced with certificate pinning in production**:

**ESP32:**
```cpp
// In DataNet.cpp, replace setInsecure() with:
secureClient.setCACert(rootCACertificate); // PEM string stored in PROGMEM
```

**ESP8266:**
```cpp
// In DataNet.cpp, replace setInsecure() with:
secureClient.setFingerprint("AA BB CC ..."); // SHA-1 fingerprint of the server cert
```

---

## About

DataNet is developed and supported by [Studio Jordan Shaw](https://www.jordanshaw.com), a creative technology studio building tools for realtime, networked, and physical-digital work.

- DataNet: [datanet.art](https://datanet.art)
- Studio: [jordanshaw.com](https://www.jordanshaw.com)
- Instagram: [@jshaw3](https://www.instagram.com/jshaw3)
- GitHub: [datanet-art](https://github.com/datanet-art)
- Source: [datanet-arduino](https://github.com/datanet-art/datanet-arduino)
- Examples: [datanet-examples](https://github.com/datanet-art/datanet-examples)

## License

MIT — see [LICENSE](LICENSE) for details.

## CI

For a quick local compile check with `arduino-cli`, point the compiler at the
repo as a library:

```bash
arduino-cli compile --library . --fqbn esp32:esp32:esp32 examples/BasicPubSub
arduino-cli compile --library . --fqbn esp32:esp32:esp32 examples/TemperatureSensor
arduino-cli compile --library . --fqbn esp32:esp32:esp32 examples/BinaryDMX
arduino-cli compile --library . --fqbn esp32:esp32:esp32 examples/BinaryDMXOutputBridge
```
