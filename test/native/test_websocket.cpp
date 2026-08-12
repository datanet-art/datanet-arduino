// RFC 6455 framing for the generic (non-ESP) WebSocket transport. The ESP
// boards delegate to the Links2004 library; everything else uses the framing
// implemented in DataNet.cpp, which is what these tests cover.

#include <string>
#include <vector>

#include "test_access.h"
#include "tiny_test.h"

namespace {

struct Frame {
    bool        fin = false;
    uint8_t     opcode = 0;
    bool        masked = false;
    uint64_t    length = 0;
    std::string payload;   // unmasked
    size_t      headerSize = 0;
};

// Decodes one client->server frame out of the fake wire's tx log.
bool decodeFrame(const std::string& raw, Frame& out) {
    if (raw.size() < 2) return false;
    out.fin    = (raw[0] & 0x80) != 0;
    out.opcode = raw[0] & 0x0F;
    out.masked = (raw[1] & 0x80) != 0;

    uint64_t len = static_cast<uint8_t>(raw[1]) & 0x7F;
    size_t   pos = 2;
    if (len == 126) {
        if (raw.size() < pos + 2) return false;
        len = (static_cast<uint8_t>(raw[pos]) << 8) | static_cast<uint8_t>(raw[pos + 1]);
        pos += 2;
    } else if (len == 127) {
        if (raw.size() < pos + 8) return false;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | static_cast<uint8_t>(raw[pos + i]);
        pos += 8;
    }
    out.length = len;

    uint8_t mask[4] = {0, 0, 0, 0};
    if (out.masked) {
        if (raw.size() < pos + 4) return false;
        for (int i = 0; i < 4; i++) mask[i] = static_cast<uint8_t>(raw[pos + i]);
        pos += 4;
    }
    out.headerSize = pos;

    if (raw.size() < pos + len) return false;
    out.payload.resize(len);
    for (uint64_t i = 0; i < len; i++) {
        out.payload[i] = static_cast<char>(static_cast<uint8_t>(raw[pos + i]) ^ mask[i % 4]);
    }
    return true;
}

// Builds a server->client frame (unmasked, as the RFC requires of servers).
std::string serverFrame(uint8_t opcode, const std::string& payload) {
    std::string f;
    f += static_cast<char>(0x80 | opcode);
    if (payload.size() < 126) {
        f += static_cast<char>(payload.size());
    } else {
        f += static_cast<char>(126);
        f += static_cast<char>((payload.size() >> 8) & 0xFF);
        f += static_cast<char>(payload.size() & 0xFF);
    }
    f += payload;
    return f;
}

// A DataNet instance wired to the fake transport and marked connected.
struct ConnectedSdk {
    DataNet dn{"ak_test", "http://gateway.local", "gateway.local", 80};

    ConnectedSdk() {
        datanetTestWire().reset();
        datanetTestWire().connected = true;
        datanetTestSetMillis(10000);
        randomSeed(1);
        DataNetTestAccess::setJwt(dn, "test.jwt.token");
        DataNetTestAccess::setConnected(dn, true);
    }
};

}  // namespace

TEST(ws_client_frames_are_masked_and_final) {
    ConnectedSdk sdk;
    const char*  text = "{\"op\":\"hb\"}";

    CHECK(DataNetTestAccess::sendPlainFrame(
        sdk.dn, 0x01, reinterpret_cast<const uint8_t*>(text), strlen(text)));

    Frame f;
    CHECK(decodeFrame(datanetTestWire().tx, f));
    CHECK_EQ(f.fin, true);
    CHECK_EQ(static_cast<int>(f.opcode), 0x01);
    CHECK_EQ(f.masked, true);   // RFC 6455 §5.1: client frames MUST be masked
    CHECK_STR_EQ(f.payload, text);
}

TEST(ws_uses_7_bit_length_below_126_bytes) {
    ConnectedSdk sdk;
    std::string  payload(125, 'x');

    CHECK(DataNetTestAccess::sendPlainFrame(
        sdk.dn, 0x01, reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));

    Frame f;
    CHECK(decodeFrame(datanetTestWire().tx, f));
    CHECK_EQ(f.headerSize, static_cast<size_t>(2 + 4));   // 2 header + 4 mask
    CHECK_EQ(f.length, static_cast<uint64_t>(125));
    CHECK_STR_EQ(f.payload, payload);
}

TEST(ws_uses_16_bit_extended_length_at_and_above_126_bytes) {
    ConnectedSdk sdk;
    std::string  payload(700, 'y');   // a base64 DMX envelope lands here

    CHECK(DataNetTestAccess::sendPlainFrame(
        sdk.dn, 0x01, reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));

    Frame f;
    CHECK(decodeFrame(datanetTestWire().tx, f));
    CHECK_EQ(f.headerSize, static_cast<size_t>(4 + 4));   // 2 + 2 extended + 4 mask
    CHECK_EQ(f.length, static_cast<uint64_t>(700));
    CHECK_STR_EQ(f.payload, payload);
}

TEST(ws_writes_frames_without_a_syscall_per_byte) {
    // A 700-byte DMX envelope written one byte at a time is ~700 calls into
    // the network stack per frame. At 40 fps that is 28k calls/second and the
    // Ethernet/WiFiNINA transports cannot keep up.
    ConnectedSdk sdk;
    std::string  payload(700, 'z');

    DataNetTestAccess::sendPlainFrame(
        sdk.dn, 0x01, reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    int calls = datanetTestWire().writeByteCalls + datanetTestWire().writeBufferCalls;
    CHECK(calls <= 4);
}

TEST(ws_refuses_to_send_while_disconnected) {
    ConnectedSdk sdk;
    DataNetTestAccess::setConnected(sdk.dn, false);
    const char* text = "{\"op\":\"hb\"}";

    CHECK(!DataNetTestAccess::sendPlainFrame(
        sdk.dn, 0x01, reinterpret_cast<const uint8_t*>(text), strlen(text)));
    CHECK_EQ(datanetTestWire().tx.size(), static_cast<size_t>(0));
}

TEST(ws_replies_to_ping_with_a_pong_carrying_the_same_payload) {
    ConnectedSdk sdk;
    datanetTestWire().feed(serverFrame(0x09, "keepalive"));

    DataNetTestAccess::handlePlainWebSocket(sdk.dn);

    Frame f;
    CHECK(decodeFrame(datanetTestWire().tx, f));
    CHECK_EQ(static_cast<int>(f.opcode), 0x0A);   // pong
    CHECK_STR_EQ(f.payload, "keepalive");
}

TEST(ws_close_frame_marks_the_connection_down) {
    ConnectedSdk sdk;
    datanetTestWire().feed(serverFrame(0x08, ""));

    DataNetTestAccess::handlePlainWebSocket(sdk.dn);

    CHECK(!sdk.dn.connected());
    CHECK(DataNetTestAccess::reconnectPending(sdk.dn));
}

TEST(ws_oversized_frame_drops_the_connection_instead_of_overflowing) {
    ConnectedSdk sdk;
    std::string  huge(DATANET_INCOMING_JSON_SIZE + 64, 'q');
    datanetTestWire().feed(serverFrame(0x01, huge));

    DataNetTestAccess::handlePlainWebSocket(sdk.dn);

    CHECK(!sdk.dn.connected());
}

TEST(ws_handles_a_frame_exactly_at_the_size_limit) {
    ConnectedSdk sdk;
    // Valid JSON that fills the buffer exactly, so the boundary is exercised
    // with a payload the parser will actually accept.
    std::string filler(DATANET_INCOMING_JSON_SIZE - 34, 'a');
    std::string json = "{\"op\":\"pub\",\"ch\":\"c\",\"d\":{\"v\":\"" + filler + "\"}}";
    CHECK_EQ(json.size(), static_cast<size_t>(DATANET_INCOMING_JSON_SIZE));

    datanetTestWire().feed(serverFrame(0x01, json));
    DataNetTestAccess::handlePlainWebSocket(sdk.dn);

    CHECK(sdk.dn.connected());
}
