// Outbound envelope construction. A publish that silently truncates is worse
// than one that fails: the server rejects unparseable JSON and the sketch has
// no idea its data never landed.

#include <string>

#include "test_access.h"
#include "tiny_test.h"

namespace {

struct ConnectedSdk {
    DataNet dn{"ak_test", "http://gateway.local", "gateway.local", 80};

    ConnectedSdk() {
        datanetTestWire().reset();
        datanetTestWire().connected = true;
        datanetTestSetMillis(10000);
        randomSeed(7);
        DataNetTestAccess::setJwt(dn, "test.jwt.token");
        DataNetTestAccess::setConnected(dn, true);
        // connect() stamps this; tests that bypass connect() must too, or the
        // first loop() sees a 10-second-old heartbeat clock.
        DataNetTestAccess::setLastHeartbeatMs(dn, millis());
    }
};

// Unmasks the payload of the single frame sitting in the tx log.
std::string sentPayload() {
    const std::string& raw = datanetTestWire().tx;
    if (raw.size() < 2) return "";

    uint64_t len = static_cast<uint8_t>(raw[1]) & 0x7F;
    size_t   pos = 2;
    if (len == 126) {
        len = (static_cast<uint8_t>(raw[2]) << 8) | static_cast<uint8_t>(raw[3]);
        pos = 4;
    }

    uint8_t mask[4] = {0, 0, 0, 0};
    if (static_cast<uint8_t>(raw[1]) & 0x80) {
        for (int i = 0; i < 4; i++) mask[i] = static_cast<uint8_t>(raw[pos + i]);
        pos += 4;
    }

    std::string out;
    for (uint64_t i = 0; i < len && pos + i < raw.size(); i++) {
        out += static_cast<char>(static_cast<uint8_t>(raw[pos + i]) ^ mask[i % 4]);
    }
    return out;
}

bool isParseableJson(const std::string& s) {
    JsonDocument doc;
    return deserializeJson(doc, s.c_str(), s.size()) == DeserializationError::Ok;
}

}  // namespace

TEST(publish_float_builds_the_documented_envelope) {
    ConnectedSdk sdk;
    CHECK(sdk.dn.publishFloat("room.temp", "c", 21.5f));
    CHECK_STR_EQ(sentPayload(), "{\"op\":\"pub\",\"ch\":\"room.temp\",\"d\":{\"c\":21.5}}");
}

TEST(publish_string_builds_the_documented_envelope) {
    ConnectedSdk sdk;
    CHECK(sdk.dn.publishString("room.state", "mode", "idle"));
    CHECK_STR_EQ(sentPayload(), "{\"op\":\"pub\",\"ch\":\"room.state\",\"d\":{\"mode\":\"idle\"}}");
}

TEST(publish_fails_cleanly_when_disconnected) {
    ConnectedSdk sdk;
    DataNetTestAccess::setConnected(sdk.dn, false);
    CHECK(!sdk.dn.publishFloat("room.temp", "c", 21.5f));
    CHECK_EQ(datanetTestWire().tx.size(), static_cast<size_t>(0));
}

TEST(publish_reports_failure_rather_than_truncating_a_large_payload) {
    // The convenience publish() path serialises into a fixed buffer. If the
    // envelope does not fit, it must return false — not emit a half-written
    // object the server will reject.
    ConnectedSdk sdk;

    JsonDocument data;
    JsonArray    values = data["values"].to<JsonArray>();
    for (int i = 0; i < 400; i++) values.add(i * 1000 + 7);

    bool        ok      = sdk.dn.publish("room.bulk", data.as<JsonVariant>());
    std::string payload = sentPayload();

    // Whatever it decides, it must not put malformed JSON on the wire.
    CHECK(!ok);
    CHECK_EQ(payload.size(), static_cast<size_t>(0));
    if (!payload.empty()) {
        CHECK(isParseableJson(payload));
    }
}

TEST(publish_binary_builds_a_base64_envelope) {
    ConnectedSdk  sdk;
    const uint8_t bytes[] = {'f', 'o', 'o', 'b', 'a', 'r'};

    CHECK(sdk.dn.publishBinary("stage.dmx", bytes, sizeof(bytes), "binary/dmx"));

    std::string payload = sentPayload();
    CHECK(isParseableJson(payload));
    CHECK(payload.find("\"bin\":true") != std::string::npos);
    CHECK(payload.find("\"b64\":\"Zm9vYmFy\"") != std::string::npos);
    CHECK(payload.find("\"ct\":\"binary/dmx\"") != std::string::npos);
    CHECK(payload.find("\"ch\":\"stage.dmx\"") != std::string::npos);
}

TEST(publish_binary_embeds_metadata_json) {
    ConnectedSdk  sdk;
    const uint8_t bytes[] = {1, 2};

    CHECK(sdk.dn.publishBinary("stage.dmx", bytes, sizeof(bytes), "binary/dmx",
                               "{\"universe\":3}"));

    std::string payload = sentPayload();
    CHECK(isParseableJson(payload));
    CHECK(payload.find("\"meta\":{\"universe\":3}") != std::string::npos);
}

TEST(publish_dmx_sends_a_full_zero_filled_frame) {
    ConnectedSdk  sdk;
    const uint8_t values[] = {255, 128, 64};

    CHECK(sdk.dn.publishDmx("stage.dmx", values, sizeof(values)));

    std::string payload = sentPayload();
    CHECK(isParseableJson(payload));
    CHECK(payload.find("\"ct\":\"binary/dmx\"") != std::string::npos);

    JsonDocument doc;
    deserializeJson(doc, payload.c_str(), payload.size());
    const char* b64 = doc["b64"];
    CHECK(b64 != nullptr);

    uint8_t decoded[512] = {0};
    size_t  n = DataNetTestAccess::base64Decode(b64, decoded, sizeof(decoded));
    CHECK_EQ(n, static_cast<size_t>(512));
    CHECK_EQ(static_cast<int>(decoded[0]), 255);
    CHECK_EQ(static_cast<int>(decoded[2]), 64);
    CHECK_EQ(static_cast<int>(decoded[3]), 0);
    CHECK_EQ(static_cast<int>(decoded[511]), 0);
}

TEST(publish_artnet_sends_a_wrapped_artdmx_packet) {
    ConnectedSdk  sdk;
    const uint8_t dmx[] = {10, 20, 30, 40};

    CHECK(sdk.dn.publishArtNet("stage.artnet", dmx, sizeof(dmx), /*universe*/ 2));

    std::string payload = sentPayload();
    CHECK(isParseableJson(payload));
    CHECK(payload.find("\"ct\":\"binary/artnet\"") != std::string::npos);

    JsonDocument doc;
    deserializeJson(doc, payload.c_str(), payload.size());
    const char* b64 = doc["b64"];
    CHECK(b64 != nullptr);

    uint8_t packet[600] = {0};
    size_t  n = DataNetTestAccess::base64Decode(b64, packet, sizeof(packet));

    const uint8_t* outDmx = nullptr;
    size_t         outLen = 0;
    uint8_t        universe = 0;
    CHECK(DataNet::extractArtDmx(packet, n, &outDmx, &outLen, &universe));
    CHECK_EQ(static_cast<int>(universe), 2);
    CHECK_EQ(std::memcmp(outDmx, dmx, sizeof(dmx)), 0);
}

TEST(publish_binary_rejects_null_data) {
    ConnectedSdk sdk;
    CHECK(!sdk.dn.publishBinary("stage.dmx", nullptr, 4, "binary/dmx"));
}

TEST(heartbeat_is_sent_on_the_configured_interval) {
    ConnectedSdk sdk;

    // Just short of the interval: nothing sent.
    datanetTestAdvanceMillis(DATANET_HEARTBEAT_INTERVAL_MS - 1);
    sdk.dn.loop();
    CHECK_EQ(datanetTestWire().tx.size(), static_cast<size_t>(0));

    datanetTestAdvanceMillis(2);
    sdk.dn.loop();
    CHECK_STR_EQ(sentPayload(), "{\"op\":\"hb\"}");
}
