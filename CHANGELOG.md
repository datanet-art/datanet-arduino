# Changelog

All notable changes to the DataNet Arduino SDK are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-08-13

### Fixed

- **`getLastTimestamp()` always returned 0.** The `ts` envelope field was read
  through `is<uint32_t>()`, but DataNet timestamps are Unix milliseconds
  (~1.7 × 10¹²) and never fit in 32 bits, so the guard never passed. The same
  defect zeroed `BinaryMessageMeta::timestamp` on every binary message.
- **Over-long channel names produced dead subscriptions.** Names longer than
  the subscription buffer were silently truncated, after which dispatch could
  never match them. `subscribe()` and `subscribeBinary()` now refuse them and
  log. See `DATANET_MAX_CHANNEL_LEN` below.
- **Reconnect jitter was always zero.** `random(0, 100) / 100` is integer
  division, so the documented ±20% spread did not exist and a fleet that lost
  the network together reconnected in lockstep.
- **Reconnect backoff collapsed on `millis()` rollover.** The deadline used a
  bare `>=`, which fires immediately for one tick every ~49.7 days. It now uses
  a rollover-safe signed comparison, matching the heartbeat check.
- **`connect()` reported success after a failed handshake** on the generic
  (non-ESP) transport.
- **`publish()` could truncate a large payload into malformed JSON.** The
  envelope size is now measured before serialising, and oversized payloads are
  refused with an `error` event.
- **Odd Art-Net DMX lengths are rounded up to even,** as Art-Net 4 requires.
  Some commercial nodes reject odd-length frames outright.
- **Raw binary WebSocket frames were dropped on the generic transport.** ESP
  and generic transports now share one dispatch path.
- HTTP failures during a presence lookup no longer log `Auth failed`.

### Changed

- **Migrated off `StaticJsonDocument`,** which is deprecated in ArduinoJson 7
  and is now a heap-backed `JsonDocument` whose size parameter is ignored. As a
  result `DATANET_INCOMING_JSON_SIZE` no longer bounded anything; the cap is now
  enforced explicitly in the message handler on every transport.
- `_sendPlainFrame()` masks and writes in 64-byte blocks instead of one
  `write()` call per byte. A 700-byte DMX envelope previously cost ~700 calls
  into the network stack per frame.
- The `Nano33IoTCloudPubSub` example now also builds for the MKR WiFi 1010,
  which shares the same WiFiNINA transport.
- Dependencies in `library.properties` carry minimum versions. ArduinoJson 6
  will no longer be silently installed against a v7-only codebase.
- PlatformIO dependencies are scoped per platform, so an ESP build no longer
  pulls in `Ethernet` and `WiFiNINA`.
- Removed seven unreferenced `PROGMEM` string constants.
- **Moved `test/` and `demos/` under `extras/`,** the folder the Arduino library
  specification reserves for content the build system ignores. Neither was ever
  compiled into a sketch, but `extras/` states that contractually rather than
  relying on the reader knowing the `src/`-only rule. Run the suite with
  `make -C extras/test/native test`.

### Added

- **Native host test suite** (`extras/test/native`). 76 tests covering base64,
  DMX/Art-Net packet construction, URL parsing, HTTP response reading, RFC 6455
  framing, envelope dispatch, publish paths, and reconnect backoff. Runs with
  `make -C extras/test/native test` and needs only a C++ compiler.
- `DATANET_MAX_CHANNEL_LEN` (default `64`) to size the channel name buffer.
- `license` field in `library.properties`.
- CI jobs for `arduino-lint` and the native test suite.
- **`keywords.txt`**, so the SDK's classes, methods, and configuration macros
  get syntax highlighting in the Arduino IDE. `loop` and `on` are deliberately
  excluded: both collide with a sketch's own `loop()` and with ordinary
  variables such as `bool on`.

## [0.1.1]

- Presence lookup (`getPresence()`).
- Nano 33 IoT cloud TLS transport.
- Board-specific Arduino examples.

## [0.1.0]

- Initial release: JSON and binary DMX/Art-Net pub/sub over WebSocket with JWT
  auth, heartbeating, and exponential-backoff reconnection.

[0.2.0]: https://github.com/datanet-art/datanet-arduino/releases/tag/0.2.0
