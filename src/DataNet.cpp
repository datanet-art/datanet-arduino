/*
 * DataNet.cpp - DataNet Arduino SDK
 *
 * See DataNet.h for public API documentation.
 */

#include "DataNet.h"

// ---------------------------------------------------------------------------
// PROGMEM string constants (avoids placing literals in RAM on 8-bit targets;
// on ESP32/ESP8266 these end up in flash/IRAM regardless, but it is good
// practice and keeps the pattern consistent with Arduino conventions).
// ---------------------------------------------------------------------------
static const char DN_OP_SUB[]   PROGMEM = "sub";
static const char DN_OP_UNSUB[] PROGMEM = "unsub";
static const char DN_OP_PUB[]   PROGMEM = "pub";
static const char DN_OP_HB[]    PROGMEM = "hb";

static const char DN_EV_CONNECT[]    PROGMEM = "connect";
static const char DN_EV_DISCONNECT[] PROGMEM = "disconnect";
static const char DN_EV_ERROR[]      PROGMEM = "error";

// ---------------------------------------------------------------------------
// Static instance pointer — supports a single DataNet object per sketch
// (typical embedded usage pattern).
// ---------------------------------------------------------------------------
#if DATANET_USE_LINKS2004_WEBSOCKETS
DataNet* DataNet::_instance = nullptr;
#endif

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
DataNet::DataNet(
    const char* apiKey,
    const char* apiUrl,
    const char* wsHost,
    int         wsPort
)
    : _apiKey(apiKey)
    , _apiUrl(apiUrl)
    , _wsHost(wsHost)
    , _wsPort(wsPort)
    , _wsConnected(false)
    , _lastHeartbeatMs(0)
    , _reconnectAtMs(0)
    , _reconnectCount(0)
    , _reconnectPending(false)
{
    _jwt[0] = '\0';

    // Zero out subscriptions
    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        _subs[i].active               = false;
        _subs[i].handler              = nullptr;
        _subs[i].binaryHandler        = nullptr;
        _subs[i].channel[0]           = '\0';
        _subs[i].binaryContentType[0] = '\0';
        _subs[i].lastTs               = 0;
    }

    // Zero out event handlers
    for (int t = 0; t < static_cast<int>(EventType::COUNT); t++) {
        for (int h = 0; h < DATANET_MAX_EVENT_HANDLERS; h++) {
            _eventHandlers[t][h] = nullptr;
        }
    }

#if DATANET_USE_LINKS2004_WEBSOCKETS
    _instance = this;
#endif
}

// ---------------------------------------------------------------------------
// connect()
// ---------------------------------------------------------------------------
bool DataNet::connect() {
    // Guard: WiFi must already be connected by the caller
#if defined(ESP32) || defined(ESP8266)
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[DataNet] WiFi not connected — call WiFi.begin() first"));
        _dispatchEvent(EventType::Error, "WiFi not connected");
        return false;
    }
#elif DATANET_USE_GENERIC_WIFI
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[DataNet] WiFi not connected - call WiFi.begin() first"));
        _dispatchEvent(EventType::Error, "WiFi not connected");
        return false;
    }
#endif

    if (!_fetchJwt()) {
        return false;
    }

    _openWebSocket();
    return true;
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void DataNet::loop() {
    uint32_t now = millis();

    // Pause the underlying WebSocketsClient while our own reconnect backoff is
    // active. The Links2004 client keeps attempting low-level reconnects from
    // loop(), so letting it run here fights our explicit scheduler and can
    // trigger reconnect storms.
    if (!_reconnectPending || _wsConnected) {
        _networkLoop();
    }

    // Heartbeat
    if (_wsConnected && (now - _lastHeartbeatMs >= DATANET_HEARTBEAT_INTERVAL_MS)) {
        _lastHeartbeatMs = now;
        // Send bare {op:"hb"} — no channel, no data
        StaticJsonDocument<64> doc;
        doc[F("op")] = F("hb");
        char buf[64];
        serializeJson(doc, buf, sizeof(buf));
        _networkSendText(buf);
        Serial.println(F("[DataNet] Heartbeat sent"));
    }

    // Reconnect
    if (_reconnectPending && !_wsConnected && (now >= _reconnectAtMs)) {
        _reconnectPending = false;
        Serial.println(F("[DataNet] Attempting reconnect..."));

#if defined(ESP32) || defined(ESP8266) || DATANET_USE_GENERIC_WIFI
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("[DataNet] WiFi still not connected, deferring reconnect"));
            _scheduleReconnect();
            return;
        }
#endif

        // Re-fetch JWT after several failed reconnects
        if (_reconnectCount >= DATANET_JWT_REFRESH_AFTER_RECONNECTS) {
            Serial.println(F("[DataNet] Re-fetching JWT..."));
            if (!_fetchJwt()) {
                _scheduleReconnect();
                return;
            }
            _reconnectCount = 0;
        }

        _openWebSocket();
    }
}

// ---------------------------------------------------------------------------
// connected()
// ---------------------------------------------------------------------------
bool DataNet::connected() {
    return _wsConnected;
}

// ---------------------------------------------------------------------------
// subscribe()
// ---------------------------------------------------------------------------
void DataNet::subscribe(const char* channel, MessageHandler handler) {
    // Check for duplicate
    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (_subs[i].active && strncmp(_subs[i].channel, channel, sizeof(_subs[i].channel)) == 0) {
            // Update handler in place
            _subs[i].handler = handler;
            // If already connected, send a fresh sub
            if (_wsConnected) {
                StaticJsonDocument<128> doc;
                doc[F("op")] = F("sub");
                doc[F("ch")] = channel;
                char buf[128];
                serializeJson(doc, buf, sizeof(buf));
                _networkSendText(buf);
            }
            return;
        }
    }

    // Find a free slot
    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (!_subs[i].active) {
            _subs[i].active  = true;
            _subs[i].handler = handler;
            strncpy(_subs[i].channel, channel, sizeof(_subs[i].channel) - 1);
            _subs[i].channel[sizeof(_subs[i].channel) - 1] = '\0';

            // If connected, send sub immediately
            if (_wsConnected) {
                StaticJsonDocument<128> doc;
                doc[F("op")] = F("sub");
                doc[F("ch")] = channel;
                char buf[128];
                serializeJson(doc, buf, sizeof(buf));
                _networkSendText(buf);
            }
            return;
        }
    }

    Serial.println(F("[DataNet] subscribe: max subscriptions reached"));
}

// ---------------------------------------------------------------------------
// unsubscribe()
// ---------------------------------------------------------------------------
void DataNet::unsubscribe(const char* channel) {
    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (_subs[i].active && strncmp(_subs[i].channel, channel, sizeof(_subs[i].channel)) == 0) {
            _subs[i].handler = nullptr;

            if (_subs[i].binaryHandler == nullptr) {
                _subs[i].active = false;
                _subs[i].channel[0] = '\0';
                _subs[i].binaryContentType[0] = '\0';
            }

            if (_wsConnected && !_subs[i].active) {
                StaticJsonDocument<128> doc;
                doc[F("op")] = F("unsub");
                doc[F("ch")] = channel;
                char buf[128];
                serializeJson(doc, buf, sizeof(buf));
                _networkSendText(buf);
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// subscribeBinary()
// ---------------------------------------------------------------------------
void DataNet::subscribeBinary(const char* channel, BinaryMessageHandler handler, const char* contentType) {
    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (_subs[i].active && strncmp(_subs[i].channel, channel, sizeof(_subs[i].channel)) == 0) {
            _subs[i].binaryHandler = handler;
            if (contentType != nullptr) {
                strncpy(_subs[i].binaryContentType, contentType, sizeof(_subs[i].binaryContentType) - 1);
                _subs[i].binaryContentType[sizeof(_subs[i].binaryContentType) - 1] = '\0';
            }
            if (_wsConnected) {
                StaticJsonDocument<128> doc;
                doc[F("op")] = F("sub");
                doc[F("ch")] = channel;
                char buf[128];
                serializeJson(doc, buf, sizeof(buf));
                _networkSendText(buf);
            }
            return;
        }
    }

    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (!_subs[i].active) {
            _subs[i].active = true;
            _subs[i].handler = nullptr;
            _subs[i].binaryHandler = handler;
            _subs[i].lastTs = 0;
            strncpy(_subs[i].channel, channel, sizeof(_subs[i].channel) - 1);
            _subs[i].channel[sizeof(_subs[i].channel) - 1] = '\0';
            if (contentType != nullptr) {
                strncpy(_subs[i].binaryContentType, contentType, sizeof(_subs[i].binaryContentType) - 1);
                _subs[i].binaryContentType[sizeof(_subs[i].binaryContentType) - 1] = '\0';
            } else {
                _subs[i].binaryContentType[0] = '\0';
            }

            if (_wsConnected) {
                StaticJsonDocument<128> doc;
                doc[F("op")] = F("sub");
                doc[F("ch")] = channel;
                char buf[128];
                serializeJson(doc, buf, sizeof(buf));
                _networkSendText(buf);
            }
            return;
        }
    }

    Serial.println(F("[DataNet] subscribeBinary: max subscriptions reached"));
}

// ---------------------------------------------------------------------------
// unsubscribeBinary()
// ---------------------------------------------------------------------------
void DataNet::unsubscribeBinary(const char* channel) {
    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (_subs[i].active && strncmp(_subs[i].channel, channel, sizeof(_subs[i].channel)) == 0) {
            _subs[i].binaryHandler = nullptr;
            _subs[i].binaryContentType[0] = '\0';

            if (_subs[i].handler == nullptr) {
                _subs[i].active = false;
                _subs[i].channel[0] = '\0';
            }

            if (_wsConnected && !_subs[i].active) {
                StaticJsonDocument<128> doc;
                doc[F("op")] = F("unsub");
                doc[F("ch")] = channel;
                char buf[128];
                serializeJson(doc, buf, sizeof(buf));
                _networkSendText(buf);
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// publish()
// ---------------------------------------------------------------------------
bool DataNet::publish(const char* channel, JsonVariant data) {
    if (!_wsConnected) {
        Serial.println(F("[DataNet] publish: not connected"));
        return false;
    }

    // Build envelope: {op:"pub", ch:"...", d:<data>}
    // Use a moderately sized document; callers with large payloads should
    // use DynamicJsonDocument on the heap instead of this convenience method.
    StaticJsonDocument<512> doc;
    doc[F("op")] = F("pub");
    doc[F("ch")] = channel;
    doc[F("d")]  = data;

    char buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) {
        Serial.println(F("[DataNet] publish: serialization failed or payload too large"));
        return false;
    }

    return _networkSendText(buf);
}

// ---------------------------------------------------------------------------
// publishFloat()
// ---------------------------------------------------------------------------
bool DataNet::publishFloat(const char* channel, const char* key, float value) {
    StaticJsonDocument<128> dataDoc;
    dataDoc[key] = value;
    return publish(channel, dataDoc.as<JsonVariant>());
}

// ---------------------------------------------------------------------------
// publishString()
// ---------------------------------------------------------------------------
bool DataNet::publishString(const char* channel, const char* key, const char* value) {
    StaticJsonDocument<128> dataDoc;
    dataDoc[key] = value;
    return publish(channel, dataDoc.as<JsonVariant>());
}

// ---------------------------------------------------------------------------
// publishBinary()
// ---------------------------------------------------------------------------
bool DataNet::publishBinary(
    const char* channel,
    const uint8_t* data,
    size_t length,
    const char* contentType,
    const char* metadataJson
) {
    if (!_wsConnected) {
        Serial.println(F("[DataNet] publishBinary: not connected"));
        return false;
    }
    if (data == nullptr && length > 0) {
        Serial.println(F("[DataNet] publishBinary: null data"));
        return false;
    }

    String encoded = _base64Encode(data, length);
    String envelope;
    envelope.reserve(encoded.length() + strlen(channel) + strlen(contentType) + (metadataJson ? strlen(metadataJson) : 0) + 96);
    envelope += F("{\"op\":\"pub\",\"ch\":\"");
    envelope += channel;
    envelope += F("\",\"bin\":true,\"b64\":\"");
    envelope += encoded;
    envelope += F("\",\"ct\":\"");
    envelope += contentType;
    envelope += F("\"");
    if (metadataJson != nullptr && metadataJson[0] != '\0') {
        envelope += F(",\"meta\":");
        envelope += metadataJson;
    }
    envelope += F("}");

    return _networkSendText(envelope);
}

// ---------------------------------------------------------------------------
// publishDmx()
// ---------------------------------------------------------------------------
bool DataNet::publishDmx(const char* channel, const uint8_t* values, size_t valueCount, size_t frameLength) {
    uint8_t frame[512];
    size_t length = buildDmxFrame(frame, sizeof(frame), values, valueCount, frameLength);
    if (length == 0) {
        Serial.println(F("[DataNet] publishDmx: invalid frame length"));
        return false;
    }
    return publishBinary(channel, frame, length, "binary/dmx");
}

// ---------------------------------------------------------------------------
// publishArtNet()
// ---------------------------------------------------------------------------
bool DataNet::publishArtNet(
    const char* channel,
    const uint8_t* dmx,
    size_t dmxLength,
    uint8_t universe,
    uint8_t subnet,
    uint8_t net,
    uint8_t sequence,
    uint8_t physical
) {
    uint8_t packet[18 + 512];
    size_t length = buildArtDmxPacket(packet, sizeof(packet), dmx, dmxLength, universe, subnet, net, sequence, physical);
    if (length == 0) {
        Serial.println(F("[DataNet] publishArtNet: invalid packet"));
        return false;
    }
    return publishBinary(channel, packet, length, "binary/artnet");
}

// ---------------------------------------------------------------------------
// on()
// ---------------------------------------------------------------------------
void DataNet::on(const char* event, EventHandler handler) {
    EventType type = _parseEventType(event);
    if (type == EventType::COUNT) {
        Serial.println(F("[DataNet] on: unknown event type"));
        return;
    }

    int idx = static_cast<int>(type);
    for (int h = 0; h < DATANET_MAX_EVENT_HANDLERS; h++) {
        if (_eventHandlers[idx][h] == nullptr) {
            _eventHandlers[idx][h] = handler;
            return;
        }
    }
    Serial.println(F("[DataNet] on: max event handlers for this event reached"));
}

// ===========================================================================
// Private helpers
// ===========================================================================

// ---------------------------------------------------------------------------
// _fetchJwt()  — POST /auth/token and store the JWT
// ---------------------------------------------------------------------------
bool DataNet::_fetchJwt() {
    Serial.println(F("[DataNet] Fetching JWT..."));

    // Build the full URL
    String url = String(_apiUrl) + F("/auth/token");

    // Build request body
    StaticJsonDocument<128> reqDoc;
    reqDoc[F("apiKey")] = _apiKey;
    char reqBody[128];
    serializeJson(reqDoc, reqBody, sizeof(reqBody));
    String body;

#ifdef ESP32
    WiFiClientSecure secureClient;
    // NOTE: setInsecure() skips server certificate verification.
    // For production, replace with certificate pinning via setCACert().
    secureClient.setInsecure();

    HTTPClient http;
    http.begin(secureClient, url);
    http.addHeader(F("Content-Type"), F("application/json"));

    int httpCode = http.POST(reqBody);

    if (httpCode != 200) {
        Serial.printf("[DataNet] Auth failed, HTTP %d\n", httpCode);
        char info[32];
        snprintf(info, sizeof(info), "HTTP %d", httpCode);
        _dispatchEvent(EventType::Error, info);
        http.end();
        return false;
    }

    body = http.getString();
    http.end();

#elif defined(ESP8266)
    BearSSL::WiFiClientSecure secureClient;
    // NOTE: setInsecure() skips server certificate verification.
    // For production, use setFingerprint() or setTrustAnchors() instead.
    secureClient.setInsecure();

    HTTPClient http;
    http.begin(secureClient, url);
    http.addHeader(F("Content-Type"), F("application/json"));

    int httpCode = http.POST(reqBody);

    if (httpCode != 200) {
        Serial.printf("[DataNet] Auth failed, HTTP %d\n", httpCode);
        char info[32];
        snprintf(info, sizeof(info), "HTTP %d", httpCode);
        _dispatchEvent(EventType::Error, info);
        http.end();
        return false;
    }

    body = http.getString();
    http.end();
#else
    return _fetchJwtPlainHttp();
#endif

    // Parse {"token":"<jwt>"}
    StaticJsonDocument<2048> respDoc;
    DeserializationError err = deserializeJson(respDoc, body);
    if (err) {
        Serial.print(F("[DataNet] Auth response parse error: "));
        Serial.println(err.c_str());
        _dispatchEvent(EventType::Error, "JWT parse failed");
        return false;
    }

    const char* token = respDoc[F("token")] | "";
    if (token[0] == '\0') {
        Serial.println(F("[DataNet] Auth response missing token field"));
        _dispatchEvent(EventType::Error, "No token in response");
        return false;
    }

    strncpy(_jwt, token, DATANET_JWT_BUF_SIZE - 1);
    _jwt[DATANET_JWT_BUF_SIZE - 1] = '\0';
    Serial.print(F("[DataNet] JWT length: "));
    Serial.println(static_cast<unsigned>(strlen(_jwt)));
    Serial.println(F("[DataNet] JWT obtained"));
    return true;
}

// ---------------------------------------------------------------------------
// _fetchJwtPlainHttp() - POST /auth/token over a generic Arduino Client
// ---------------------------------------------------------------------------
bool DataNet::_fetchJwtPlainHttp() {
    char host[96];
    char path[96];
    uint16_t port = 80;
    bool secure = false;

    String url = String(_apiUrl) + F("/auth/token");
    if (!_parseHttpUrl(url.c_str(), host, sizeof(host), &port, path, sizeof(path), &secure)) {
        Serial.println(F("[DataNet] Auth URL parse failed"));
        _dispatchEvent(EventType::Error, "Auth URL parse failed");
        return false;
    }

    if (secure) {
        Serial.println(F("[DataNet] HTTPS auth is unavailable on this board transport; use http:// or an ESP board"));
        _dispatchEvent(EventType::Error, "HTTPS auth unavailable");
        return false;
    }

    StaticJsonDocument<128> reqDoc;
    reqDoc[F("apiKey")] = _apiKey;
    char reqBody[128];
    size_t reqLength = serializeJson(reqDoc, reqBody, sizeof(reqBody));
    if (reqLength == 0 || reqLength >= sizeof(reqBody)) {
        Serial.println(F("[DataNet] Auth request serialization failed"));
        _dispatchEvent(EventType::Error, "Auth request failed");
        return false;
    }

#if DATANET_USE_LINKS2004_WEBSOCKETS
    WEBSOCKETS_NETWORK_CLASS client;
#elif DATANET_USE_GENERIC_WIFI
    WiFiClient client;
#else
    EthernetClient client;
#endif
    if (!client.connect(host, port)) {
        Serial.println(F("[DataNet] Auth connection failed"));
        _dispatchEvent(EventType::Error, "Auth connection failed");
        return false;
    }

    client.print(F("POST "));
    client.print(path);
    client.println(F(" HTTP/1.1"));
    client.print(F("Host: "));
    client.println(host);
    client.println(F("User-Agent: DataNet-Arduino/0.1.0"));
    client.println(F("Content-Type: application/json"));
    client.print(F("Content-Length: "));
    client.println(reqLength);
    client.println(F("Connection: close"));
    client.println();
    client.write(reinterpret_cast<const uint8_t*>(reqBody), reqLength);

    String body;
    if (!_readHttpBody(client, body)) {
        Serial.println(F("[DataNet] Auth response read failed"));
        _dispatchEvent(EventType::Error, "Auth response read failed");
        client.stop();
        return false;
    }
    client.stop();

    StaticJsonDocument<2048> respDoc;
    DeserializationError err = deserializeJson(respDoc, body);
    if (err) {
        Serial.print(F("[DataNet] Auth response parse error: "));
        Serial.println(err.c_str());
        _dispatchEvent(EventType::Error, "JWT parse failed");
        return false;
    }

    const char* token = respDoc[F("token")] | "";
    if (token[0] == '\0') {
        Serial.println(F("[DataNet] Auth response missing token field"));
        _dispatchEvent(EventType::Error, "No token in response");
        return false;
    }

    strncpy(_jwt, token, DATANET_JWT_BUF_SIZE - 1);
    _jwt[DATANET_JWT_BUF_SIZE - 1] = '\0';
    Serial.print(F("[DataNet] JWT length: "));
    Serial.println(static_cast<unsigned>(strlen(_jwt)));
    Serial.println(F("[DataNet] JWT obtained"));
    return true;
}

// ---------------------------------------------------------------------------
// _parseHttpUrl()
// ---------------------------------------------------------------------------
bool DataNet::_parseHttpUrl(
    const char* url,
    char* host,
    size_t hostSize,
    uint16_t* port,
    char* path,
    size_t pathSize,
    bool* secure
) {
    if (url == nullptr || host == nullptr || port == nullptr || path == nullptr || secure == nullptr) {
        return false;
    }

    const char* cursor = url;
    if (strncmp(cursor, "http://", 7) == 0) {
        *secure = false;
        *port = 80;
        cursor += 7;
    } else if (strncmp(cursor, "https://", 8) == 0) {
        *secure = true;
        *port = 443;
        cursor += 8;
    } else {
        return false;
    }

    const char* hostStart = cursor;
    while (*cursor != '\0' && *cursor != ':' && *cursor != '/') {
        cursor++;
    }

    size_t hostLength = static_cast<size_t>(cursor - hostStart);
    if (hostLength == 0 || hostLength >= hostSize) {
        return false;
    }
    memcpy(host, hostStart, hostLength);
    host[hostLength] = '\0';

    if (*cursor == ':') {
        cursor++;
        uint32_t parsedPort = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            parsedPort = (parsedPort * 10) + static_cast<uint32_t>(*cursor - '0');
            if (parsedPort > 65535) {
                return false;
            }
            cursor++;
        }
        if (parsedPort == 0) {
            return false;
        }
        *port = static_cast<uint16_t>(parsedPort);
    }

    if (*cursor == '\0') {
        strncpy(path, "/", pathSize);
        path[pathSize - 1] = '\0';
        return true;
    }

    if (*cursor != '/') {
        return false;
    }

    size_t pathLength = strlen(cursor);
    if (pathLength == 0 || pathLength >= pathSize) {
        return false;
    }
    memcpy(path, cursor, pathLength + 1);
    return true;
}

// ---------------------------------------------------------------------------
// _readHttpBody()
// ---------------------------------------------------------------------------
bool DataNet::_readHttpBody(Client& client, String& body, uint32_t timeoutMs) {
    String statusLine;
    uint32_t start = millis();
    while (client.connected() && millis() - start < timeoutMs) {
        if (client.available()) {
            statusLine = client.readStringUntil('\n');
            statusLine.trim();
            break;
        }
        delay(1);
    }

    if (!statusLine.startsWith(F("HTTP/1.1 200")) && !statusLine.startsWith(F("HTTP/1.0 200"))) {
        Serial.print(F("[DataNet] Auth failed: "));
        Serial.println(statusLine);
        return false;
    }

    bool chunked = false;
    int contentLength = -1;
    while (client.connected() && millis() - start < timeoutMs) {
        if (!client.available()) {
            delay(1);
            continue;
        }

        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) {
            break;
        }

        String lower = header;
        lower.toLowerCase();
        if (lower.startsWith(F("content-length:"))) {
            contentLength = lower.substring(15).toInt();
        } else if (lower.startsWith(F("transfer-encoding:")) && lower.indexOf(F("chunked")) >= 0) {
            chunked = true;
        }
    }

    body = "";
    if (chunked) {
        while (client.connected() && millis() - start < timeoutMs) {
            while (!client.available() && client.connected() && millis() - start < timeoutMs) {
                delay(1);
            }
            if (!client.available()) {
                break;
            }

            String sizeLine = client.readStringUntil('\n');
            sizeLine.trim();
            unsigned long chunkSize = strtoul(sizeLine.c_str(), nullptr, 16);
            if (chunkSize == 0) {
                return true;
            }

            for (unsigned long i = 0; i < chunkSize && millis() - start < timeoutMs; i++) {
                while (!client.available() && client.connected() && millis() - start < timeoutMs) {
                    delay(1);
                }
                if (!client.available()) {
                    return false;
                }
                body += static_cast<char>(client.read());
            }

            if (client.available()) client.read();
            if (client.available()) client.read();
        }
        return false;
    }

    if (contentLength >= 0) {
        body.reserve(contentLength);
        while (static_cast<int>(body.length()) < contentLength && millis() - start < timeoutMs) {
            if (client.available()) {
                body += static_cast<char>(client.read());
            } else {
                delay(1);
            }
        }
        return static_cast<int>(body.length()) == contentLength;
    }

    while ((client.connected() || client.available()) && millis() - start < timeoutMs) {
        while (client.available()) {
            body += static_cast<char>(client.read());
        }
        delay(1);
    }
    return body.length() > 0;
}

// ---------------------------------------------------------------------------
// _openWebSocket()  — open WSS using the stored JWT as subprotocol header
// ---------------------------------------------------------------------------
void DataNet::_openWebSocket() {
    Serial.print(F("[DataNet] Opening WebSocket to "));
    Serial.println(_wsHost);

    // Clear any lingering TCP/TLS state before re-arming the client for a new
    // connection attempt.
    _networkDisconnect();

    // The DataNet server authenticates via the Sec-WebSocket-Protocol header:
    //   Sec-WebSocket-Protocol: bearer, <jwt>
    // Links2004's WebSocketsClient sends the protocol string as that header
    // when provided to beginSSL().
    String protocol = String(F("bearer, ")) + String(_jwt);

#if DATANET_USE_LINKS2004_WEBSOCKETS && defined(HAS_SSL)
    // beginSSL(host, port, path, fingerprint, protocol)
    // NOTE: certificate verification is disabled for simplicity on ESP.
    // For production, use setSSLClientCertKey() / beginSslWithCA().
    if (_wsPort == 443) {
        _ws.beginSSL(_wsHost, _wsPort, "/ws", nullptr, protocol.c_str());
    } else {
        _ws.begin(_wsHost, _wsPort, "/ws", protocol.c_str());
    }
#elif DATANET_USE_LINKS2004_WEBSOCKETS
    if (_wsPort == 443) {
        Serial.println(F("[DataNet] This board transport does not provide TLS; use a ws:// endpoint or local gateway"));
        _dispatchEvent(EventType::Error, "TLS transport unavailable");
        return;
    }
    _ws.begin(_wsHost, _wsPort, "/ws", protocol.c_str());
#else
    if (!_openPlainWebSocket(protocol.c_str())) {
        return;
    }
#endif

#if DATANET_USE_LINKS2004_WEBSOCKETS
    _ws.onEvent(_wsEventCallback);

    // Keep the library's own retry loop throttled for low-level connect
    // failures. Higher-level reconnect backoff is handled by DataNet once a
    // disconnect event is observed.
    _ws.setReconnectInterval(DATANET_RECONNECT_BASE_MS);
#endif
}

#if DATANET_USE_LINKS2004_WEBSOCKETS
void DataNet::_networkLoop() {
    _ws.loop();
}

void DataNet::_networkDisconnect() {
    _ws.disconnect();
}

bool DataNet::_networkSendText(const char* text) {
    return _ws.sendTXT(text);
}

bool DataNet::_networkSendText(const String& text) {
    return _ws.sendTXT(const_cast<String&>(text));
}

// ---------------------------------------------------------------------------
// _wsEventCallback()  — static trampoline dispatching to the instance
// ---------------------------------------------------------------------------
void DataNet::_wsEventCallback(WStype_t type, uint8_t* payload, size_t length) {
    if (_instance == nullptr) return;
    _instance->_handleWsEvent(type, payload, length);
}

// We define _handleWsEvent() as a non-static private method called from the
// static trampoline. Add the forward-declaration here (implementation below).
// (The method is not declared in the header to keep it an internal detail.)

void DataNet::_handleWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            if (!_wsConnected && _reconnectPending) {
                Serial.println(F("[DataNet] Duplicate disconnect event ignored"));
                break;
            }
            _wsConnected = false;
            Serial.println(F("[DataNet] WebSocket disconnected"));
            _dispatchEvent(EventType::Disconnect, "disconnected");
            _scheduleReconnect();
            break;

        case WStype_CONNECTED:
            _wsConnected = true;
            _reconnectPending = false;
            _reconnectCount = 0;
            _lastHeartbeatMs = millis();
            Serial.println(F("[DataNet] WebSocket connected"));
            _dispatchEvent(EventType::Connect, "connected");
            _resubscribeAll();
            break;

        case WStype_TEXT:
            if (payload != nullptr && length > 0) {
                _handleMessage(reinterpret_cast<const char*>(payload), length);
            }
            break;

        case WStype_BIN:
            if (payload != nullptr && length > 0) {
                int target = -1;
                for (int i = 0; i < DATANET_MAX_SUBS; i++) {
                    if (_subs[i].active && _subs[i].binaryHandler != nullptr) {
                        if (target >= 0) {
                            Serial.println(F("[DataNet] Raw binary frame ignored: multiple binary subscriptions"));
                            _dispatchEvent(EventType::Error, "Raw binary frame is ambiguous");
                            return;
                        }
                        target = i;
                    }
                }
                if (target >= 0) {
                    BinaryMessageMeta meta = {
                        _subs[target].channel,
                        "",
                        (uint64_t)millis(),
                        _subs[target].binaryContentType[0] != '\0' ? _subs[target].binaryContentType : "application/octet-stream",
                        length,
                        JsonVariant(),
                        true
                    };
                    _subs[target].binaryHandler(payload, length, meta);
                }
            }
            break;

        case WStype_ERROR:
            Serial.println(F("[DataNet] WebSocket error"));
            _dispatchEvent(EventType::Error, "WebSocket error");
            break;

        case WStype_PING:
        case WStype_PONG:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
        default:
            break;
    }
}
#else
void DataNet::_networkLoop() {
    _handlePlainWebSocket();
}

void DataNet::_networkDisconnect() {
    _tcp.stop();
}

bool DataNet::_networkSendText(const char* text) {
    if (text == nullptr) return false;
    return _sendPlainFrame(0x01, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

bool DataNet::_networkSendText(const String& text) {
    return _sendPlainFrame(0x01, reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
}

bool DataNet::_openPlainWebSocket(const char* protocol) {
    if (_wsPort == 443) {
        Serial.println(F("[DataNet] TLS is unavailable on this board transport; use ws:// or a local gateway"));
        _dispatchEvent(EventType::Error, "TLS transport unavailable");
        return false;
    }

    if (!_tcp.connect(_wsHost, _wsPort)) {
        Serial.println(F("[DataNet] WebSocket TCP connection failed"));
        _dispatchEvent(EventType::Error, "WebSocket TCP failed");
        _scheduleReconnect();
        return false;
    }

    uint8_t nonce[16];
    for (size_t i = 0; i < sizeof(nonce); i++) {
        nonce[i] = static_cast<uint8_t>(random(0, 256));
    }
    String key = _base64Encode(nonce, sizeof(nonce));

    _tcp.println(F("GET /ws HTTP/1.1"));
    _tcp.print(F("Host: "));
    _tcp.println(_wsHost);
    _tcp.println(F("Upgrade: websocket"));
    _tcp.println(F("Connection: Upgrade"));
    _tcp.println(F("Sec-WebSocket-Version: 13"));
    _tcp.print(F("Sec-WebSocket-Key: "));
    _tcp.println(key);
    _tcp.print(F("Sec-WebSocket-Protocol: "));
    _tcp.println(protocol);
    _tcp.println();

    uint32_t start = millis();
    String statusLine;
    while (_tcp.connected() && millis() - start < 5000) {
        if (_tcp.available()) {
            statusLine = _tcp.readStringUntil('\n');
            statusLine.trim();
            break;
        }
        delay(1);
    }

    if (!statusLine.startsWith(F("HTTP/1.1 101")) && !statusLine.startsWith(F("HTTP/1.0 101"))) {
        Serial.print(F("[DataNet] WebSocket handshake failed: "));
        Serial.println(statusLine);
        _dispatchEvent(EventType::Error, "WebSocket handshake failed");
        _tcp.stop();
        _scheduleReconnect();
        return false;
    }

    while (_tcp.connected() && millis() - start < 5000) {
        if (!_tcp.available()) {
            delay(1);
            continue;
        }
        String header = _tcp.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) {
            break;
        }
    }

    _wsConnected = true;
    _reconnectPending = false;
    _reconnectCount = 0;
    _lastHeartbeatMs = millis();
    Serial.println(F("[DataNet] WebSocket connected"));
    _dispatchEvent(EventType::Connect, "connected");
    _resubscribeAll();
    return true;
}

void DataNet::_handlePlainWebSocket() {
    if (!_tcp.connected()) {
        if (_wsConnected) {
            _wsConnected = false;
            Serial.println(F("[DataNet] WebSocket disconnected"));
            _dispatchEvent(EventType::Disconnect, "disconnected");
            _scheduleReconnect();
        }
        return;
    }

    while (_tcp.available() >= 2) {
        uint8_t header[2];
        if (!_readPlainBytes(header, sizeof(header), 100)) return;

        uint8_t opcode = header[0] & 0x0F;
        bool masked = (header[1] & 0x80) != 0;
        uint64_t payloadLength = header[1] & 0x7F;

        if (payloadLength == 126) {
            uint8_t ext[2];
            if (!_readPlainBytes(ext, sizeof(ext), 1000)) return;
            payloadLength = (static_cast<uint16_t>(ext[0]) << 8) | ext[1];
        } else if (payloadLength == 127) {
            uint8_t ext[8];
            if (!_readPlainBytes(ext, sizeof(ext), 1000)) return;
            payloadLength = 0;
            for (size_t i = 0; i < sizeof(ext); i++) {
                payloadLength = (payloadLength << 8) | ext[i];
            }
        }

        uint8_t mask[4] = {0, 0, 0, 0};
        if (masked && !_readPlainBytes(mask, sizeof(mask), 1000)) {
            return;
        }

        if (payloadLength > DATANET_INCOMING_JSON_SIZE) {
            Serial.println(F("[DataNet] WebSocket frame too large"));
            _dispatchEvent(EventType::Error, "WebSocket frame too large");
            _tcp.stop();
            _wsConnected = false;
            _scheduleReconnect();
            return;
        }

        uint8_t payload[DATANET_INCOMING_JSON_SIZE + 1];
        if (!_readPlainBytes(payload, static_cast<size_t>(payloadLength), 1000)) {
            return;
        }
        for (size_t i = 0; i < static_cast<size_t>(payloadLength); i++) {
            payload[i] ^= mask[i % 4];
        }
        payload[payloadLength] = '\0';

        if (opcode == 0x01) {
            _handleMessage(reinterpret_cast<const char*>(payload), static_cast<size_t>(payloadLength));
        } else if (opcode == 0x08) {
            _tcp.stop();
            _wsConnected = false;
            _dispatchEvent(EventType::Disconnect, "closed");
            _scheduleReconnect();
            return;
        } else if (opcode == 0x09) {
            _sendPlainFrame(0x0A, payload, static_cast<size_t>(payloadLength));
        }
    }
}

bool DataNet::_sendPlainFrame(uint8_t opcode, const uint8_t* payload, size_t length) {
    if (!_wsConnected || !_tcp.connected()) {
        return false;
    }
    if (payload == nullptr && length > 0) {
        return false;
    }
    if (length > 65535) {
        Serial.println(F("[DataNet] WebSocket payload too large"));
        return false;
    }

    uint8_t header[8];
    size_t headerLength = 0;
    header[headerLength++] = 0x80 | (opcode & 0x0F);
    if (length < 126) {
        header[headerLength++] = 0x80 | static_cast<uint8_t>(length);
    } else {
        header[headerLength++] = 0x80 | 126;
        header[headerLength++] = static_cast<uint8_t>((length >> 8) & 0xFF);
        header[headerLength++] = static_cast<uint8_t>(length & 0xFF);
    }

    uint8_t mask[4];
    for (size_t i = 0; i < sizeof(mask); i++) {
        mask[i] = static_cast<uint8_t>(random(0, 256));
        header[headerLength++] = mask[i];
    }

    if (_tcp.write(header, headerLength) != headerLength) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        uint8_t b = payload[i] ^ mask[i % 4];
        if (_tcp.write(&b, 1) != 1) {
            return false;
        }
    }
    return true;
}

bool DataNet::_readPlainBytes(uint8_t* out, size_t length, uint32_t timeoutMs) {
    uint32_t start = millis();
    size_t offset = 0;
    while (offset < length && millis() - start < timeoutMs) {
        if (_tcp.available()) {
            int value = _tcp.read();
            if (value < 0) {
                return false;
            }
            out[offset++] = static_cast<uint8_t>(value);
        } else {
            delay(1);
        }
    }
    return offset == length;
}
#endif

// ---------------------------------------------------------------------------
// _handleMessage()  — parse incoming JSON envelope and dispatch
// ---------------------------------------------------------------------------
void DataNet::_handleMessage(const char* json, size_t length) {
    // Use a reasonably sized static document; increase if large payloads expected.
    StaticJsonDocument<DATANET_INCOMING_JSON_SIZE> doc;
    DeserializationError err = deserializeJson(doc, json, length);
    if (err) {
        Serial.print(F("[DataNet] Message parse error: "));
        Serial.println(err.c_str());
        return;
    }

    const char* op = doc[F("op")] | "";
    const char* type = doc[F("type")] | "";

    if (strcmp(type, "connected") == 0) {
        Serial.println(F("[DataNet] Server acknowledged connection"));
        return;
    }

    if (strcmp(type, "hb_ack") == 0 || strcmp(op, "hb") == 0) {
        Serial.println(F("[DataNet] Heartbeat acknowledged"));
        return;
    }

    if (strcmp(op, "pub") != 0) {
        // Ignore non-pub server frames after surfacing useful connection/heartbeat acks.
        return;
    }

    const char* ch = doc[F("ch")] | "";
    if (doc[F("bin")] == true && doc[F("b64")].is<const char*>()) {
        _handleBinaryEnvelope(doc);
        return;
    }

    JsonVariant  d  = doc[F("d")];
    uint64_t     ts = 0;
    if (doc[F("ts")].is<uint32_t>()) {
        ts = doc[F("ts")].as<uint32_t>();
    }

    // Dispatch to all matching subscribers
    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (_subs[i].active && strcmp(_subs[i].channel, ch) == 0) {
            _subs[i].lastTs = ts;
            if (_subs[i].handler != nullptr) {
                _subs[i].handler(ch, d);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// _handleBinaryEnvelope()  — decode b64 and dispatch bytes + metadata
// ---------------------------------------------------------------------------
void DataNet::_handleBinaryEnvelope(JsonDocument& doc) {
    const char* ch = doc[F("ch")] | "";
    const char* b64 = doc[F("b64")] | "";
    const char* from = doc[F("from")] | "";
    const char* contentType = doc[F("ct")] | "";
    uint64_t ts = 0;
    if (doc[F("ts")].is<uint32_t>()) {
        ts = doc[F("ts")].as<uint32_t>();
    }
    size_t declaredBytes = doc[F("bytes")] | (size_t)0;

    if (ch[0] == '\0' || b64[0] == '\0') {
        return;
    }

    uint8_t bytes[DATANET_BINARY_BUF_SIZE];
    size_t decodedLength = _base64Decode(b64, bytes, sizeof(bytes));
    if (decodedLength == 0 && b64[0] != '\0') {
        Serial.println(F("[DataNet] Binary decode failed or buffer too small"));
        _dispatchEvent(EventType::Error, "Binary decode failed");
        return;
    }
    if (declaredBytes == 0) {
        declaredBytes = decodedLength;
    }

    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (_subs[i].active && _subs[i].binaryHandler != nullptr && strcmp(_subs[i].channel, ch) == 0) {
            _subs[i].lastTs = ts;
            const char* effectiveContentType = contentType[0] != '\0'
                ? contentType
                : (_subs[i].binaryContentType[0] != '\0' ? _subs[i].binaryContentType : "application/octet-stream");
            JsonVariant metaVariant = doc[F("meta")];
            BinaryMessageMeta meta = {
                ch,
                from,
                ts,
                effectiveContentType,
                declaredBytes,
                metaVariant,
                false
            };
            _subs[i].binaryHandler(bytes, decodedLength, meta);
        }
    }
}

// ---------------------------------------------------------------------------
// getLastTimestamp()  — server-side Unix ms timestamp of last pub on channel
// ---------------------------------------------------------------------------
uint64_t DataNet::getLastTimestamp(const char* channel) {
    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (_subs[i].active && strcmp(_subs[i].channel, channel) == 0) {
            return _subs[i].lastTs;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// _resubscribeAll()  — re-send sub envelopes after reconnect
// ---------------------------------------------------------------------------
void DataNet::_resubscribeAll() {
    for (int i = 0; i < DATANET_MAX_SUBS; i++) {
        if (_subs[i].active) {
            StaticJsonDocument<128> doc;
            doc[F("op")] = F("sub");
            doc[F("ch")] = _subs[i].channel;
            char buf[128];
            serializeJson(doc, buf, sizeof(buf));
            _networkSendText(buf);
        }
    }
}

// ---------------------------------------------------------------------------
// _scheduleReconnect()  — exponential backoff without blocking delay()
// ---------------------------------------------------------------------------
void DataNet::_scheduleReconnect() {
    if (_reconnectPending) {
        return;
    }

    _reconnectCount++;
    uint32_t backoff = DATANET_RECONNECT_BASE_MS;
    for (uint8_t i = 1; i < _reconnectCount && backoff < DATANET_RECONNECT_MAX_MS; i++) {
        backoff *= 2;
    }
    if (backoff > DATANET_RECONNECT_MAX_MS) {
        backoff = DATANET_RECONNECT_MAX_MS;
    }

    // Add ±20% jitter to avoid thundering herd
    uint32_t jitter = (backoff / 5) * (random(0, 100) / 100);  // 0–20%
    backoff += jitter;

    _reconnectAtMs   = millis() + backoff;
    _reconnectPending = true;

    Serial.print(F("[DataNet] Reconnect #"));
    Serial.print(_reconnectCount);
    Serial.print(F(" in "));
    Serial.print(backoff);
    Serial.println(F(" ms"));
}

// ---------------------------------------------------------------------------
// _dispatchEvent()
// ---------------------------------------------------------------------------
void DataNet::_dispatchEvent(EventType type, const char* info) {
    int idx = static_cast<int>(type);
    for (int h = 0; h < DATANET_MAX_EVENT_HANDLERS; h++) {
        if (_eventHandlers[idx][h] != nullptr) {
            const char* evName = "";
            switch (type) {
                case EventType::Connect:    evName = "connect";    break;
                case EventType::Disconnect: evName = "disconnect"; break;
                case EventType::Error:      evName = "error";      break;
                default: break;
            }
            _eventHandlers[idx][h](evName, info);
        }
    }
}

// ---------------------------------------------------------------------------
// _parseEventType()
// ---------------------------------------------------------------------------
DataNet::EventType DataNet::_parseEventType(const char* name) {
    if (strcmp(name, "connect")    == 0) return EventType::Connect;
    if (strcmp(name, "disconnect") == 0) return EventType::Disconnect;
    if (strcmp(name, "error")      == 0) return EventType::Error;
    return EventType::COUNT;
}

// ---------------------------------------------------------------------------
// buildDmxFrame()
// ---------------------------------------------------------------------------
size_t DataNet::buildDmxFrame(uint8_t* out, size_t outSize, const uint8_t* values, size_t valueCount, size_t frameLength) {
    if (out == nullptr || outSize == 0) return 0;
    if (frameLength < 1) frameLength = 1;
    if (frameLength > 512) frameLength = 512;
    if (frameLength > outSize) return 0;

    memset(out, 0, frameLength);
    if (values != nullptr) {
        size_t count = valueCount < frameLength ? valueCount : frameLength;
        memcpy(out, values, count);
    }
    return frameLength;
}

// ---------------------------------------------------------------------------
// buildArtDmxPacket()
// ---------------------------------------------------------------------------
size_t DataNet::buildArtDmxPacket(
    uint8_t* out,
    size_t outSize,
    const uint8_t* dmx,
    size_t dmxLength,
    uint8_t universe,
    uint8_t subnet,
    uint8_t net,
    uint8_t sequence,
    uint8_t physical
) {
    if (out == nullptr || outSize < 20) return 0;
    if (dmxLength < 2) dmxLength = 2;
    if (dmxLength > 512) dmxLength = 512;
    if (outSize < 18 + dmxLength) return 0;

    memset(out, 0, 18 + dmxLength);
    memcpy(out, "Art-Net", 7);
    out[7] = 0x00;
    out[8] = 0x00;
    out[9] = 0x50;  // OpOutput / ArtDMX, little-endian
    out[10] = 0x00;
    out[11] = 14;   // protocol version
    out[12] = sequence;
    out[13] = physical;
    out[14] = ((subnet & 0x0F) << 4) | (universe & 0x0F);
    out[15] = net & 0x7F;
    out[16] = (dmxLength >> 8) & 0xFF;
    out[17] = dmxLength & 0xFF;
    if (dmx != nullptr) {
        memcpy(out + 18, dmx, dmxLength);
    }
    return 18 + dmxLength;
}

// ---------------------------------------------------------------------------
// extractArtDmx()
// ---------------------------------------------------------------------------
bool DataNet::extractArtDmx(
    const uint8_t* packet,
    size_t packetLength,
    const uint8_t** dmx,
    size_t* dmxLength,
    uint8_t* universe,
    uint8_t* subnet,
    uint8_t* net
) {
    if (packet == nullptr || dmx == nullptr || dmxLength == nullptr || packetLength < 20) return false;
    if (memcmp(packet, "Art-Net", 7) != 0 || packet[7] != 0x00) return false;
    if (packet[8] != 0x00 || packet[9] != 0x50) return false;

    size_t length = ((size_t)packet[16] << 8) | packet[17];
    if (length < 2 || length > 512 || packetLength < 18 + length) return false;

    *dmx = packet + 18;
    *dmxLength = length;
    if (universe != nullptr) *universe = packet[14] & 0x0F;
    if (subnet != nullptr) *subnet = (packet[14] >> 4) & 0x0F;
    if (net != nullptr) *net = packet[15] & 0x7F;
    return true;
}

// ---------------------------------------------------------------------------
// _base64Encode()
// ---------------------------------------------------------------------------
String DataNet::_base64Encode(const uint8_t* data, size_t length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String out;
    out.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        uint32_t triple = ((uint32_t)data[i]) << 16;
        if (i + 1 < length) triple |= ((uint32_t)data[i + 1]) << 8;
        if (i + 2 < length) triple |= data[i + 2];

        out += alphabet[(triple >> 18) & 0x3F];
        out += alphabet[(triple >> 12) & 0x3F];
        out += (i + 1 < length) ? alphabet[(triple >> 6) & 0x3F] : '=';
        out += (i + 2 < length) ? alphabet[triple & 0x3F] : '=';
    }

    return out;
}

// ---------------------------------------------------------------------------
// _base64Decode()
// ---------------------------------------------------------------------------
size_t DataNet::_base64Decode(const char* input, uint8_t* out, size_t outSize) {
    if (input == nullptr || out == nullptr) return 0;

    size_t outLen = 0;
    uint32_t buffer = 0;
    uint8_t bits = 0;

    for (const char* p = input; *p; p++) {
        char c = *p;
        if (c == '=') break;
        int8_t value = _base64Value(c);
        if (value < 0) continue;

        buffer = (buffer << 6) | (uint8_t)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (outLen >= outSize) return 0;
            out[outLen++] = (buffer >> bits) & 0xFF;
        }
    }

    return outLen;
}

// ---------------------------------------------------------------------------
// _base64Value()
// ---------------------------------------------------------------------------
int8_t DataNet::_base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// ---------------------------------------------------------------------------
// _handleWsEvent — forward declaration glue
// (Method body already implemented above; this comment marks the seam.)
// ---------------------------------------------------------------------------
// The WebSocketsClient library requires a static callback.  We register
// DataNet::_wsEventCallback and it calls _instance->_handleWsEvent().
// Because _handleWsEvent is only called internally we declare it in the .cpp
// to avoid polluting the public/protected header surface.
// Add the method prototype at file scope so the compiler sees it before use:
// (already satisfied by the inline definition order above)
