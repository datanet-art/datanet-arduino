#pragma once

/*
 * DataNet.h - DataNet Arduino SDK
 *
 * Dependencies (install via Arduino Library Manager):
 *   - ArduinoJson by Benoit Blanchon, v7+
 *   - WebSockets by Markus Sattler (Links2004), v2.4+ on ESP32/ESP8266
 *   - Ethernet or board WiFi library on non-ESP Arduino-compatible boards
 *   - HTTPClient (built into ESP32/ESP8266 Arduino cores, when using HTTPS)
 *
 * Platform support: ESP32, ESP8266, Teensy/Ethernet, Arduino Ethernet, and
 * Arduino WiFi boards such as Uno R4 WiFi, MKR WiFi 1010, and Nano 33 IoT.
 * Non-ESP boards use plain HTTP/WS endpoints unless a future transport adds
 * TLS support.
 */

#include <Arduino.h>
#include <Client.h>
#include <ArduinoJson.h>

#if defined(ESP32) || defined(ESP8266)
  #define DATANET_USE_LINKS2004_WEBSOCKETS 1
  #define DATANET_USE_GENERIC_WIFI 0
  #define DATANET_USE_GENERIC_ETHERNET 0
  #include <WebSocketsClient.h>
#else
  #define DATANET_USE_LINKS2004_WEBSOCKETS 0
  #if defined(ARDUINO_UNOWIFIR4)
    #define DATANET_USE_GENERIC_WIFI 1
    #define DATANET_USE_GENERIC_ETHERNET 0
    #include <WiFiS3.h>
  #elif defined(ARDUINO_SAMD_MKRWIFI1010) || defined(ARDUINO_SAMD_NANO_33_IOT)
    #define DATANET_USE_GENERIC_WIFI 1
    #define DATANET_USE_GENERIC_ETHERNET 0
    #include <WiFiNINA.h>
  #else
    #define DATANET_USE_GENERIC_WIFI 0
    #define DATANET_USE_GENERIC_ETHERNET 1
    #include <Ethernet.h>
  #endif
#endif

#ifdef ESP32
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <WiFiClientSecureBearSSL.h>
#endif

// ---------------------------------------------------------------------------
// Compile-time configuration — override with -D flags or before including
// ---------------------------------------------------------------------------
#ifndef DATANET_MAX_SUBS
  #define DATANET_MAX_SUBS 8
#endif

#ifndef DATANET_MAX_EVENT_HANDLERS
  #define DATANET_MAX_EVENT_HANDLERS 4
#endif

#ifndef DATANET_JWT_BUF_SIZE
  #define DATANET_JWT_BUF_SIZE 2048
#endif

#ifndef DATANET_HEARTBEAT_INTERVAL_MS
  #define DATANET_HEARTBEAT_INTERVAL_MS 30000UL
#endif

#ifndef DATANET_RECONNECT_BASE_MS
  #define DATANET_RECONNECT_BASE_MS 1000UL
#endif

#ifndef DATANET_RECONNECT_MAX_MS
  #define DATANET_RECONNECT_MAX_MS 60000UL
#endif

// After this many reconnects, re-fetch a fresh JWT
#ifndef DATANET_JWT_REFRESH_AFTER_RECONNECTS
  #define DATANET_JWT_REFRESH_AFTER_RECONNECTS 3
#endif

#ifndef DATANET_INCOMING_JSON_SIZE
  #define DATANET_INCOMING_JSON_SIZE 2048
#endif

#ifndef DATANET_BINARY_BUF_SIZE
  #define DATANET_BINARY_BUF_SIZE 768
#endif

// ---------------------------------------------------------------------------
// Handler typedefs
// ---------------------------------------------------------------------------

// MessageHandler: called when a "pub" envelope arrives on a subscribed channel.
// channel — the channel name string
// data    — the "d" field from the envelope (may be any JSON type)
typedef void (*MessageHandler)(const char* channel, JsonVariant data);

// EventHandler: called for lifecycle events "connect", "disconnect", "error"
// event   — event name string
// info    — optional detail string (error message, disconnect reason, etc.)
typedef void (*EventHandler)(const char* event, const char* info);

struct BinaryMessageMeta {
    const char* channel;
    const char* from;
    uint64_t    timestamp;
    const char* contentType;
    size_t      bytes;
    JsonVariant metadata;
    bool        raw;
};

// BinaryMessageHandler: called when a binary envelope arrives on a subscribed
// channel. data points to an SDK-owned scratch buffer and is only valid during
// the callback.
typedef void (*BinaryMessageHandler)(const uint8_t* data, size_t length, const BinaryMessageMeta& meta);

// ---------------------------------------------------------------------------
// DataNet
// ---------------------------------------------------------------------------
class DataNet {
public:
    // -----------------------------------------------------------------------
    // Constructor
    // -----------------------------------------------------------------------
    DataNet(
        const char* apiKey,
        const char* apiUrl  = "https://api.datanet.art",
        const char* wsHost  = "ws.datanet.art",
        int         wsPort  = 443
    );

    // -----------------------------------------------------------------------
    // Connection
    // -----------------------------------------------------------------------

    // Fetches JWT via HTTPS POST then opens WSS connection.
    // WiFi must be connected before calling.
    // Returns true on success, false on failure.
    bool connect();

    // Must be called in Arduino loop() — drives WebSocket callbacks and heartbeat.
    void loop();

    // Returns true if the WebSocket is currently connected.
    bool connected();

    // -----------------------------------------------------------------------
    // Pub / Sub
    // -----------------------------------------------------------------------

    // Subscribe to a channel. handler is called when a "pub" arrives.
    // Up to DATANET_MAX_SUBS subscriptions allowed.
    void subscribe(const char* channel, MessageHandler handler);

    // Remove a subscription by channel name.
    void unsubscribe(const char* channel);

    // Subscribe to metadata-bearing binary envelopes on a channel.
    void subscribeBinary(const char* channel, BinaryMessageHandler handler, const char* contentType = nullptr);

    // Remove a binary subscription by channel name.
    void unsubscribeBinary(const char* channel);

    // Returns the server-side timestamp (Unix ms) of the last message received
    // on the given channel, or 0 if no message has been received yet.
    // Useful for detecting stale readings or measuring latency.
    uint64_t getLastTimestamp(const char* channel);

    // Publish a JsonVariant as the "d" field of a pub envelope.
    // Returns true if the message was sent.
    bool publish(const char* channel, JsonVariant data);

    // Convenience: publish {"<key>": <value>} as a float
    bool publishFloat(const char* channel, const char* key, float value);

    // Convenience: publish {"<key>": "<value>"} as a string
    bool publishString(const char* channel, const char* key, const char* value);

    // Publish bytes using the DataNet binary envelope:
    // {"op":"pub","ch":"...","bin":true,"b64":"...","ct":"...","meta":{...}}
    bool publishBinary(
        const char* channel,
        const uint8_t* data,
        size_t length,
        const char* contentType = "application/octet-stream",
        const char* metadataJson = nullptr
    );

    // Convenience: publish a DMX frame as binary/dmx. Values are copied into a
    // 1-512 byte DMX frame and missing channels are zero-filled.
    bool publishDmx(
        const char* channel,
        const uint8_t* values,
        size_t valueCount,
        size_t frameLength = 512
    );

    // Convenience: build an Art-Net ArtDMX packet and publish as binary/artnet.
    bool publishArtNet(
        const char* channel,
        const uint8_t* dmx,
        size_t dmxLength,
        uint8_t universe = 0,
        uint8_t subnet = 0,
        uint8_t net = 0,
        uint8_t sequence = 0,
        uint8_t physical = 0
    );

    // Helper builders for examples and bridges.
    static size_t buildDmxFrame(
        uint8_t* out,
        size_t outSize,
        const uint8_t* values,
        size_t valueCount,
        size_t frameLength = 512
    );

    static size_t buildArtDmxPacket(
        uint8_t* out,
        size_t outSize,
        const uint8_t* dmx,
        size_t dmxLength,
        uint8_t universe = 0,
        uint8_t subnet = 0,
        uint8_t net = 0,
        uint8_t sequence = 0,
        uint8_t physical = 0
    );

    static bool extractArtDmx(
        const uint8_t* packet,
        size_t packetLength,
        const uint8_t** dmx,
        size_t* dmxLength,
        uint8_t* universe = nullptr,
        uint8_t* subnet = nullptr,
        uint8_t* net = nullptr
    );

    // -----------------------------------------------------------------------
    // Event handlers
    // -----------------------------------------------------------------------

    // Register a lifecycle event handler.
    // Supported events: "connect", "disconnect", "error"
    // Up to DATANET_MAX_EVENT_HANDLERS handlers per event type.
    void on(const char* event, EventHandler handler);

private:
    // -----------------------------------------------------------------------
    // Internal types
    // -----------------------------------------------------------------------
    struct Subscription {
        char                 channel[64];
        MessageHandler       handler;
        BinaryMessageHandler binaryHandler;
        char                 binaryContentType[32];
        bool                 active;
        uint64_t             lastTs;   // server-side Unix ms timestamp of last pub
    };

    enum class EventType : uint8_t {
        Connect    = 0,
        Disconnect = 1,
        Error      = 2,
        COUNT      = 3
    };

    // -----------------------------------------------------------------------
    // Configuration (stored as pointers to caller-owned strings)
    // -----------------------------------------------------------------------
    const char* _apiKey;
    const char* _apiUrl;
    const char* _wsHost;
    int         _wsPort;

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    char        _jwt[DATANET_JWT_BUF_SIZE];
    bool        _wsConnected;
    uint32_t    _lastHeartbeatMs;
    uint32_t    _reconnectAtMs;
    uint8_t     _reconnectCount;
    bool        _reconnectPending;

    // -----------------------------------------------------------------------
    // Subscriptions
    // -----------------------------------------------------------------------
    Subscription _subs[DATANET_MAX_SUBS];

    // -----------------------------------------------------------------------
    // Event handlers storage
    // -----------------------------------------------------------------------
    EventHandler _eventHandlers[static_cast<uint8_t>(EventType::COUNT)][DATANET_MAX_EVENT_HANDLERS];

    // -----------------------------------------------------------------------
    // WebSocket client (Links2004 library)
    // -----------------------------------------------------------------------
#if DATANET_USE_LINKS2004_WEBSOCKETS
    WebSocketsClient _ws;
#elif DATANET_USE_GENERIC_WIFI
    WiFiClient _tcp;
#else
    EthernetClient _tcp;
#endif

#if DATANET_USE_LINKS2004_WEBSOCKETS
    // Static trampoline so we can pass a C-style function pointer to the
    // WebSocketsClient library while still dispatching to the instance.
    // Only one DataNet instance is expected per sketch (typical for embedded).
    static DataNet* _instance;
    static void _wsEventCallback(WStype_t type, uint8_t* payload, size_t length);

    // Instance-level WebSocket event handler called from the static trampoline.
    void _handleWsEvent(WStype_t type, uint8_t* payload, size_t length);
#endif

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    // Fetch a fresh JWT from the REST API. Returns true on success.
    bool _fetchJwt();

    // Open WSS connection using the stored JWT.
    void _openWebSocket();
    void _networkLoop();
    void _networkDisconnect();
    bool _networkSendText(const char* text);
    bool _networkSendText(const String& text);
#if !DATANET_USE_LINKS2004_WEBSOCKETS
    bool _openPlainWebSocket(const char* protocol);
    void _handlePlainWebSocket();
    bool _sendPlainFrame(uint8_t opcode, const uint8_t* payload, size_t length);
    bool _readPlainBytes(uint8_t* out, size_t length, uint32_t timeoutMs = 1000);
#endif

    // Handle an incoming WebSocket text frame (raw JSON string).
    void _handleMessage(const char* json, size_t length);

    // Handle an incoming binary envelope after the JSON frame is parsed.
    void _handleBinaryEnvelope(JsonDocument& doc);

    // Send a raw JSON string over the WebSocket.
    bool _sendJson(const char* json);

    // Send an already-serialised envelope string.
    bool _sendEnvelope(const char* op, const char* channel = nullptr, JsonVariant data = JsonVariant());

    // Re-send "sub" envelopes for all active subscriptions (called on reconnect).
    void _resubscribeAll();

    // Schedule the next reconnect attempt with exponential backoff.
    void _scheduleReconnect();

    // Dispatch a lifecycle event to all registered handlers.
    void _dispatchEvent(EventType type, const char* info = "");

    // Map a string event name to EventType. Returns COUNT on unknown name.
    static EventType _parseEventType(const char* name);

    static String _base64Encode(const uint8_t* data, size_t length);
    static size_t _base64Decode(const char* input, uint8_t* out, size_t outSize);
    static int8_t _base64Value(char c);
    bool _fetchJwtPlainHttp();
    static bool _parseHttpUrl(
        const char* url,
        char* host,
        size_t hostSize,
        uint16_t* port,
        char* path,
        size_t pathSize,
        bool* secure
    );
    static bool _readHttpBody(Client& client, String& body, uint32_t timeoutMs = 8000);
};
