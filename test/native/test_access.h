// Bridge to DataNet's private static helpers for the native test suite.
//
// DataNet.h declares `friend struct DataNetTestAccess` only when
// DATANET_ENABLE_TEST_ACCESS is defined, which the test Makefile does and no
// sketch build ever does.

#pragma once

#include <DataNet.h>

struct DataNetTestAccess {
    static String base64Encode(const uint8_t* data, size_t length) {
        return DataNet::_base64Encode(data, length);
    }

    static size_t base64Decode(const char* input, uint8_t* out, size_t outSize) {
        return DataNet::_base64Decode(input, out, outSize);
    }

    static int8_t base64Value(char c) { return DataNet::_base64Value(c); }

    static String urlEncode(const char* value) { return DataNet::_urlEncode(value); }

    static bool parseHttpUrl(const char* url,
                             char*       host,
                             size_t      hostSize,
                             uint16_t*   port,
                             char*       path,
                             size_t      pathSize,
                             bool*       secure) {
        return DataNet::_parseHttpUrl(url, host, hostSize, port, path, pathSize, secure);
    }

    static bool readHttpBody(Client& client, String& body, uint32_t timeoutMs = 8000) {
        return DataNet::_readHttpBody(client, body, timeoutMs);
    }

    // --- instance internals -------------------------------------------------
    static void scheduleReconnect(DataNet& dn) { dn._scheduleReconnect(); }
    static uint32_t reconnectAtMs(const DataNet& dn) { return dn._reconnectAtMs; }
    static uint8_t  reconnectCount(const DataNet& dn) { return dn._reconnectCount; }
    static bool     reconnectPending(const DataNet& dn) { return dn._reconnectPending; }
    static void     clearReconnectPending(DataNet& dn) { dn._reconnectPending = false; }
    static void     setReconnectCount(DataNet& dn, uint8_t value) { dn._reconnectCount = value; }
    static void     setLastHeartbeatMs(DataNet& dn, uint32_t value) { dn._lastHeartbeatMs = value; }

    static void handleMessage(DataNet& dn, const char* json) {
        dn._handleMessage(json, strlen(json));
    }

    static void setConnected(DataNet& dn, bool value) { dn._wsConnected = value; }
    static void setJwt(DataNet& dn, const char* jwt) {
        strncpy(dn._jwt, jwt, DATANET_JWT_BUF_SIZE - 1);
        dn._jwt[DATANET_JWT_BUF_SIZE - 1] = '\0';
    }

    static bool sendPlainFrame(DataNet& dn, uint8_t opcode, const uint8_t* payload, size_t length) {
        return dn._sendPlainFrame(opcode, payload, length);
    }

    static void handlePlainWebSocket(DataNet& dn) { dn._handlePlainWebSocket(); }

    static void dispatchRawBinary(DataNet& dn, const uint8_t* payload, size_t length) {
        dn._dispatchRawBinary(payload, length);
    }
};
