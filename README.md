# DataNet Arduino SDK

Realtime pub/sub for Arduino-compatible devices including ESP32, ESP8266, and Teensy. Connect to the [DataNet](https://datanet.art) platform, subscribe to channels, and publish JSON or binary DMX/Art-Net messages — with automatic JWT authentication, heartbeating, and exponential-backoff reconnection.

---

## Installation

Install from the DataNet repository today:

1. Download this repo as a ZIP, or clone it locally.
2. Rename the folder to `DataNet` if needed.
3. In the Arduino IDE use **Sketch -> Include Library -> Add .ZIP Library...**, or place the `DataNet` folder inside your Arduino libraries directory manually.

Once the library is accepted into the Arduino Library Manager index, the
preferred install path becomes:

1. Open **Library Manager** in the Arduino IDE.
2. Search for `DataNet`.
3. Install the latest release.

Until then, the repo-first flow above is the canonical path.

<details>
<summary><strong>Submitting to the Arduino Library Manager index</strong></summary>

The index is a registry of git repositories, not uploaded files. Arduino scans
each registered repo for new tags, so a library is submitted once and every
later release is picked up automatically from its tags.

Prerequisites, all of which CI enforces:

- `library.properties` at the repo root with `name`, `version`, `author`,
  `maintainer`, `sentence`, `category`, `url`, and `architectures`.
- `arduino-lint --library-manager submit --compliance strict` passes clean.
- At least one git tag whose name is the exact version in `library.properties`
  (`0.2.0`, not `v0.2.0`).
- The `name` is unique in the index and not already claimed.

To submit:

1. Push the release tag.
2. Open an issue on
   [arduino/library-registry](https://github.com/arduino/library-registry)
   using the **Add library** template, with the repo clone URL.
3. A bot runs the same `arduino-lint` checks and merges automatically if they
   pass. Expect the library to appear in the IDE within a day.

To verify locally before submitting:

```bash
arduino-lint --library-manager submit --compliance strict
```

</details>

### Required dependencies

Install these via **Library Manager** before using DataNet:

| Library | Author | Version | Needed on |
|---|---|---|---|
| ArduinoJson | Benoit Blanchon | >= 7.0.0 | all boards |
| WebSockets | Markus Sattler (Links2004) | >= 2.4.0 | ESP32, ESP8266 |
| Ethernet | Arduino | >= 2.0.0 | Teensy, Arduino Ethernet |
| WiFiNINA | Arduino | >= 1.8.0 | Nano 33 IoT, MKR WiFi 1010 |

ArduinoJson **7** is required. The SDK uses the elastic `JsonDocument` type,
which does not exist in ArduinoJson 6.

`HTTPClient` is bundled with the ESP32 and ESP8266 Arduino board packages, so
no separate install is needed there.

ESP32, ESP8266, Nano 33 IoT, and MKR WiFi 1010 use HTTPS/WSS with the hosted
DataNet service. WiFiNINA boards must have current NINA firmware and SSL root
certificates for `api.datanet.art` and `ws.datanet.art` installed through the
Arduino IDE Firmware Updater. Teensy, Ethernet-style Arduino boards, and the
current Uno R4 WiFi transport use plain HTTP/WS and therefore require a
TLS-capable bridge before connecting to the hosted service.

### PlatformIO

This repository also includes `library.json` for PlatformIO, with dependencies
scoped per platform so an ESP build does not pull in `Ethernet` and `WiFiNINA`.

Install straight from the repository today:

```ini
lib_deps =
  https://github.com/datanet-art/datanet-arduino.git
```

After the package is published to the PlatformIO Registry (`pio pkg publish`):

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

For the hosted DataNet service, applications only provide their API key:

```cpp
DataNet datanet(API_KEY);
```

The SDK owns the hosted API URL, WebSocket hostname, path, and secure port.
Most applications should not declare or copy those values.

#### Custom endpoint override (advanced)

The full constructor is available for DataNet maintainers, staging, testing,
or explicitly configured self-hosted environments:

```cpp
DataNet datanet(
    const char* apiKey,
    const char* apiUrl = "https://api.datanet.art",
    const char* wsHost = "ws.datanet.art",
    int         wsPort = 443
);
```

When overriding the defaults, use this format:

```cpp
const char* API_URL = "https://api.datanet.art";
const char* WS_HOST = "ws.datanet.art";
const int WS_PORT = 443;

DataNet datanet(API_KEY, API_URL, WS_HOST, WS_PORT);
```

`apiUrl` is an HTTP origin and therefore includes `https://`. The SDK appends
REST paths such as `/auth/token` and `/presence`.

`wsHost` is a DNS hostname only. Do not include `wss://`, `https://`, a port,
or `/ws`; the SDK supplies the WebSocket path and uses `wsPort` to select the
transport. Port `443` selects secure WSS on ESP and WiFiNINA boards.

---

### Methods

| Method | Returns | Description |
|---|---|---|
| `connect()` | `bool` | Fetch JWT and open the WebSocket connection. Returns `false` if authentication fails, or if the transport cannot start the connection. ESP and WiFiNINA boards use HTTPS/WSS for the hosted service; Teensy/Ethernet currently uses HTTP/WS. Network must already be connected. |
| `loop()` | `void` | **Must be called every `loop()` iteration.** Drives WebSocket events and heartbeat. |
| `connected()` | `bool` | `true` if the WebSocket is currently open. |
| `getPresence(channel)` | `int` | Blocking HTTP lookup of authoritative occupancy. Returns `-1` on error; call selectively or on a throttled timer. |
| `subscribe(channel, handler)` | `void` | Subscribe to a channel. `handler` is called on each incoming message. Max `DATANET_MAX_SUBS` (default 8) channels. |
| `unsubscribe(channel)` | `void` | Remove a channel subscription and send an `unsub` envelope. |
| `subscribeBinary(channel, handler, contentType)` | `void` | Subscribe to binary envelopes on a channel. Handler receives bytes plus metadata. |
| `unsubscribeBinary(channel)` | `void` | Remove a binary subscription. |
| `getLastTimestamp(channel)` | `uint64_t` | Server-side Unix **millisecond** timestamp of the last message on the channel, or `0` if none has arrived. Useful for staleness checks. |
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
| `DATANET_MAX_CHANNEL_LEN` | `64` | Channel name buffer, including the null terminator. `subscribe()` refuses longer names rather than truncating them |
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

> Channel names longer than `DATANET_MAX_CHANNEL_LEN - 1` characters are
> **refused** by `subscribe()` with a message on `Serial`, because a truncated
> name could never match an inbound envelope.

### Quick start uses ArduinoJson 7

```cpp
JsonDocument data;               // not StaticJsonDocument<N>
data["source"] = "esp32";
data["count"]  = count++;
datanet.publish(CHANNEL, data.as<JsonVariant>());
```

---

## Examples

Choose the basic example that matches your board. ESP32 and ESP8266 use
different Arduino Wi-Fi libraries and board cores, so they are kept as two
separate sketches even though they demonstrate the same DataNet pub/sub flow.

### ESP32BasicPubSub

`File → Examples → DataNet → ESP32BasicPubSub`

Minimal hosted-cloud subscribe + publish loop for ESP32 boards. Messages carry
`source: "esp32"` so two-board tests are easy to read in Serial Monitor.

### ESP8266BasicPubSub

`File → Examples → DataNet → ESP8266BasicPubSub`

Minimal hosted-cloud subscribe + publish loop for ESP8266 boards.

### TemperatureSensor

`File → Examples → DataNet → TemperatureSensor`

Networked ESP32/ESP8266 example that simulates temperature and humidity,
publishes every 5 seconds, and subscribes to a commands channel. Unlike
`SerialSensor`, this sketch connects to DataNet directly over Wi-Fi.

### SerialSensor

`File → Examples → DataNet → SerialSensor`

Board-side sketch for an Uno, Mega, classic Nano, or other board without its
own network connection. It emits simulated sensor readings as newline-delimited
JSON over USB serial; a Node.js or Python bridge running on the computer then
publishes those readings to DataNet. The sketch itself does not use the DataNet
network client.

See the [complete serial bridge guide](https://github.com/datanet-art/datanet-examples/tree/main/arduino/serial-bridge)
for installation, port selection, and run instructions.

### ESP32Button

`File → Examples → DataNet → ESP32Button`

Connect a momentary button between GPIO 4 and GND. The example debounces the
input and publishes `pressed` plus a running `press_count` whenever it changes.

### ESP32Potentiometer

`File → Examples → DataNet → ESP32Potentiometer`

Connect a potentiometer between 3.3V and GND with its wiper on GPIO 34. The
example publishes the raw 12-bit reading and a normalized `0.0`–`1.0` value.

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

### Nano33IoTCloudPubSub

`File → Examples → DataNet → Nano33IoTCloudPubSub`

Hosted-cloud publish/subscribe for Nano 33 IoT **and MKR WiFi 1010** using
WiFiNINA HTTPS and WSS.
Before uploading, use the Arduino IDE Firmware Updater to install SSL root
certificates for `api.datanet.art` and `ws.datanet.art` on the NINA module.
Like the ESP examples, it uses the one-argument constructor and the SDK's
hosted-cloud defaults. Messages carry `source: "nano33iot"` for clear
cross-device testing.

### TeensyEthernetPubSub

`File → Examples → DataNet → TeensyEthernetPubSub`

Minimal Ethernet subscribe + publish loop for Teensy 4.1 or Arduino-compatible
Ethernet boards. This example uses a plain `http://` API URL and `ws://`
WebSocket port for local gateways/development servers.

### PresenceReactiveLighting

`File → Examples → DataNet → PresenceReactiveLighting`

ESP32 example that checks occupancy every 10 seconds and turns the built-in
LED into a repeating presence-count pulse pattern. It demonstrates that
`getPresence()` is a deliberate blocking lookup and must not run every frame.

---

## Protocol Notes

The SDK communicates using the DataNet WebSocket protocol:

- **Auth:** `POST https://api.datanet.art/auth/token` with `{"apiKey":"ak_..."}` → `{"token":"<jwt>"}`
- **WebSocket:** connect to `wss://ws.datanet.art/ws` with subprotocol header `Sec-WebSocket-Protocol: bearer, <jwt>`
- **Envelope format:** `{"op":"pub|sub|unsub|hb", "ch":"channel-name", "d":{...}}`
- **Binary envelope:** `{"op":"pub","ch":"channel-name","bin":true,"b64":"...","ct":"binary/dmx","meta":{...}}`
- **Heartbeat:** `{"op":"hb"}` sent every 30 seconds (configurable)
- **Presence:** authenticated `GET /presence?channel=...`; call occasionally, not in every `loop()` tick

---

## Memory Tips for Constrained Devices

**ESP8266 has ~80 KB of heap.** Keep these points in mind:

- Reduce `DATANET_MAX_SUBS` if you only use a few channels.
- Do not shrink `DATANET_JWT_BUF_SIZE` aggressively. Current platform JWTs can exceed 512 bytes once scopes and limits are embedded, so `2048` is the safe default for production examples.
- **In ArduinoJson 7 every `JsonDocument` is heap-backed and grows on demand.**
  `StaticJsonDocument` and `DynamicJsonDocument` are deprecated aliases for it,
  and their size parameter is ignored — the old "static is on the stack" advice
  no longer applies. Use plain `JsonDocument` and keep payloads small.
- Avoid subscribing to channels with very large payloads. Incoming envelopes
  larger than `DATANET_INCOMING_JSON_SIZE` are rejected before parsing, and
  decoded binary bytes are held in `DATANET_BINARY_BUF_SIZE`.
- A 512-byte DMX frame becomes about 684 base64 characters before JSON envelope overhead. The default binary scratch buffer is sized for full DMX and ArtDMX payloads.
- Call `WiFi.setOutputPower(10)` to reduce WiFi TX power if signal strength allows — this cuts current draw significantly on battery-powered nodes.
- The TLS/SSL handshake requires ~30 KB of heap momentarily. Ensure your sketch does not allocate large buffers before calling `connect()`.
- Classic Arduino Uno-class AVR boards are generally too small for the full SDK. Teensy 4.1 compiles cleanly with the Ethernet transport and is the recommended non-ESP Arduino-family target.

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

## Testing

### Native unit tests

The protocol and encoding logic is tested on the host, with no board attached.
`src/DataNet.cpp` is compiled for its generic-Ethernet transport against a
small Arduino shim (virtual clock, deterministic PRNG, scriptable socket), so
the suite runs in seconds and needs only a C++ compiler.

```bash
make -C extras/test/native test
```

Coverage: base64 codec, DMX and Art-Net packet construction, URL parsing and
percent-encoding, HTTP response reading (content-length, chunked, error
statuses), RFC 6455 client framing, envelope dispatch and timestamps, the
publish paths, and reconnect backoff including the `millis()` rollover.

Layout:

| Path | Purpose |
|---|---|
| `extras/test/native/shims/` | Minimal `Arduino.h`, `Client.h`, `Ethernet.h` for the host |
| `extras/test/native/test_*.cpp` | The test cases |
| `extras/test/native/tiny_test.h` | Dependency-free assertion framework |
| `extras/test/native/test_access.h` | Bridge to private helpers, gated on `DATANET_ENABLE_TEST_ACCESS` |

The suite lives under `extras/` on purpose. The Arduino library specification
reserves that folder for content the build system ignores completely, so
nothing here can reach a sketch or consume flash. It also keeps the host shim's
`Arduino.h` safely off the compiler's include path, where it would otherwise
shadow the real one. Please don't relocate it to a top-level `test/`.

### Compile checks

Point `arduino-cli` at the repo as a library:

```bash
arduino-cli compile --library . --fqbn esp32:esp32:esp32 examples/ESP32BasicPubSub
```

CI compiles every example on each board it targets — ESP32, ESP8266,
Nano 33 IoT, MKR WiFi 1010, Teensy 4.1, and Uno — and runs `arduino-lint` in
Library Manager submission mode.

### Releasing

Tags drive the Arduino Library Manager, so a release is:

1. Bump `version=` in `library.properties` and `"version"` in `library.json`.
2. Add a `## [x.y.z]` section to `CHANGELOG.md`.
3. Tag with the bare version (`0.2.0`, no `v` prefix) and push.

The release workflow refuses to publish if the tag, the two metadata files, and
the changelog disagree; it then attaches an IDE-installable ZIP to the release.
