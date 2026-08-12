// Envelope dispatch: which subscriber gets called, with what data, and what
// gets recorded as the last-seen timestamp.

#include <string>
#include <vector>

#include "test_access.h"
#include "tiny_test.h"

namespace {

// Handlers are C function pointers, so captured state lives at file scope.
struct Capture {
    int         calls = 0;
    std::string channel;
    std::string dataJson;

    void reset() {
        calls = 0;
        channel.clear();
        dataJson.clear();
    }
};

Capture g_a;
Capture g_b;

void handlerA(const char* channel, JsonVariant data) {
    g_a.calls++;
    g_a.channel = channel ? channel : "";
    String out;
    serializeJson(data, out);
    g_a.dataJson = out.c_str();
}

void handlerB(const char* channel, JsonVariant data) {
    g_b.calls++;
    g_b.channel = channel ? channel : "";
    String out;
    serializeJson(data, out);
    g_b.dataJson = out.c_str();
}

struct BinaryCapture {
    int         calls = 0;
    std::string bytes;
    std::string channel;
    std::string contentType;
    std::string from;
    uint64_t    timestamp = 0;
    size_t      declaredBytes = 0;
    bool        raw = false;
    std::string metaJson;

    void reset() { *this = BinaryCapture(); }
};

BinaryCapture g_bin;

void binaryHandler(const uint8_t* data, size_t length, const BinaryMessageMeta& meta) {
    g_bin.calls++;
    g_bin.bytes.assign(reinterpret_cast<const char*>(data), length);
    g_bin.channel = meta.channel ? meta.channel : "";
    g_bin.contentType = meta.contentType ? meta.contentType : "";
    g_bin.from = meta.from ? meta.from : "";
    g_bin.timestamp = meta.timestamp;
    g_bin.declaredBytes = meta.bytes;
    g_bin.raw = meta.raw;
    String out;
    serializeJson(meta.metadata, out);
    g_bin.metaJson = out.c_str();
}

struct Sdk {
    DataNet dn{"ak_test", "http://gateway.local", "gateway.local", 80};

    Sdk() {
        datanetTestWire().reset();
        datanetTestSetMillis(50000);
        g_a.reset();
        g_b.reset();
        g_bin.reset();
    }
};

}  // namespace

TEST(protocol_dispatches_a_pub_to_the_matching_subscriber_only) {
    Sdk sdk;
    sdk.dn.subscribe("room.temp", handlerA);
    sdk.dn.subscribe("room.humidity", handlerB);

    DataNetTestAccess::handleMessage(
        sdk.dn, "{\"op\":\"pub\",\"ch\":\"room.temp\",\"d\":{\"c\":21.5}}");

    CHECK_EQ(g_a.calls, 1);
    CHECK_STR_EQ(g_a.channel, "room.temp");
    CHECK_STR_EQ(g_a.dataJson, "{\"c\":21.5}");
    CHECK_EQ(g_b.calls, 0);
}

TEST(protocol_ignores_pubs_for_unsubscribed_channels) {
    Sdk sdk;
    sdk.dn.subscribe("room.temp", handlerA);

    DataNetTestAccess::handleMessage(
        sdk.dn, "{\"op\":\"pub\",\"ch\":\"other.channel\",\"d\":1}");

    CHECK_EQ(g_a.calls, 0);
}

TEST(protocol_ignores_non_pub_frames) {
    Sdk sdk;
    sdk.dn.subscribe("room.temp", handlerA);

    DataNetTestAccess::handleMessage(sdk.dn, "{\"type\":\"connected\"}");
    DataNetTestAccess::handleMessage(sdk.dn, "{\"type\":\"hb_ack\"}");
    DataNetTestAccess::handleMessage(sdk.dn, "{\"op\":\"sub\",\"ch\":\"room.temp\"}");
    DataNetTestAccess::handleMessage(sdk.dn, "not json at all");

    CHECK_EQ(g_a.calls, 0);
}

TEST(protocol_records_a_millisecond_unix_timestamp) {
    // DataNet timestamps are Unix milliseconds, which passed 2^32 in 1970+49
    // days and are now ~1.7e12. Narrowing them to 32 bits loses the value
    // entirely, and getLastTimestamp() is documented for staleness checks.
    Sdk sdk;
    sdk.dn.subscribe("room.temp", handlerA);

    DataNetTestAccess::handleMessage(
        sdk.dn, "{\"op\":\"pub\",\"ch\":\"room.temp\",\"ts\":1755000000000,\"d\":{\"c\":21.5}}");

    CHECK_EQ(g_a.calls, 1);
    CHECK_EQ(sdk.dn.getLastTimestamp("room.temp"), static_cast<uint64_t>(1755000000000ULL));
}

TEST(protocol_last_timestamp_is_zero_before_any_message) {
    Sdk sdk;
    sdk.dn.subscribe("room.temp", handlerA);
    CHECK_EQ(sdk.dn.getLastTimestamp("room.temp"), static_cast<uint64_t>(0));
    CHECK_EQ(sdk.dn.getLastTimestamp("never.subscribed"), static_cast<uint64_t>(0));
}

TEST(protocol_unsubscribe_stops_dispatch) {
    Sdk sdk;
    sdk.dn.subscribe("room.temp", handlerA);
    sdk.dn.unsubscribe("room.temp");

    DataNetTestAccess::handleMessage(
        sdk.dn, "{\"op\":\"pub\",\"ch\":\"room.temp\",\"d\":1}");

    CHECK_EQ(g_a.calls, 0);
}

TEST(protocol_resubscribing_replaces_the_handler_without_consuming_a_slot) {
    Sdk sdk;
    sdk.dn.subscribe("room.temp", handlerA);
    sdk.dn.subscribe("room.temp", handlerB);

    DataNetTestAccess::handleMessage(
        sdk.dn, "{\"op\":\"pub\",\"ch\":\"room.temp\",\"d\":1}");

    CHECK_EQ(g_a.calls, 0);
    CHECK_EQ(g_b.calls, 1);
}

TEST(protocol_rejects_channel_names_that_do_not_fit) {
    // Storing a truncated name means every inbound message for that channel is
    // dropped by the strcmp in _handleMessage. Failing loudly beats a
    // subscription that silently never fires.
    Sdk         sdk;
    std::string tooLong(120, 'c');

    datanetTestClearSerial();
    sdk.dn.subscribe(tooLong.c_str(), handlerA);

    std::string envelope = "{\"op\":\"pub\",\"ch\":\"" + tooLong + "\",\"d\":1}";
    DataNetTestAccess::handleMessage(sdk.dn, envelope.c_str());

    // Either the subscription works end to end, or it was refused with a log.
    bool dispatched = g_a.calls == 1;
    bool refused    = datanetTestSerialLog().find("channel name too long") != std::string::npos;
    CHECK(dispatched || refused);
}

TEST(protocol_decodes_a_binary_envelope_with_metadata) {
    Sdk sdk;
    sdk.dn.subscribeBinary("stage.dmx", binaryHandler, "binary/dmx");

    DataNetTestAccess::handleMessage(
        sdk.dn,
        "{\"op\":\"pub\",\"ch\":\"stage.dmx\",\"bin\":true,\"b64\":\"Zm9vYmFy\","
        "\"ct\":\"binary/dmx\",\"from\":\"console-1\",\"ts\":1755000000000,"
        "\"bytes\":6,\"meta\":{\"universe\":3}}");

    CHECK_EQ(g_bin.calls, 1);
    CHECK_STR_EQ(g_bin.bytes, "foobar");
    CHECK_STR_EQ(g_bin.channel, "stage.dmx");
    CHECK_STR_EQ(g_bin.contentType, "binary/dmx");
    CHECK_STR_EQ(g_bin.from, "console-1");
    CHECK_EQ(g_bin.declaredBytes, static_cast<size_t>(6));
    CHECK_EQ(g_bin.raw, false);
    CHECK_STR_EQ(g_bin.metaJson, "{\"universe\":3}");
    CHECK_EQ(g_bin.timestamp, static_cast<uint64_t>(1755000000000ULL));
}

TEST(protocol_binary_envelope_falls_back_to_the_subscription_content_type) {
    Sdk sdk;
    sdk.dn.subscribeBinary("stage.dmx", binaryHandler, "binary/dmx");

    DataNetTestAccess::handleMessage(
        sdk.dn,
        "{\"op\":\"pub\",\"ch\":\"stage.dmx\",\"bin\":true,\"b64\":\"Zm9v\"}");

    CHECK_EQ(g_bin.calls, 1);
    CHECK_STR_EQ(g_bin.contentType, "binary/dmx");
}

TEST(protocol_binary_and_json_subscriptions_share_one_channel_slot) {
    Sdk sdk;
    sdk.dn.subscribe("stage.dmx", handlerA);
    sdk.dn.subscribeBinary("stage.dmx", binaryHandler, "binary/dmx");

    DataNetTestAccess::handleMessage(sdk.dn, "{\"op\":\"pub\",\"ch\":\"stage.dmx\",\"d\":1}");
    CHECK_EQ(g_a.calls, 1);

    DataNetTestAccess::handleMessage(
        sdk.dn, "{\"op\":\"pub\",\"ch\":\"stage.dmx\",\"bin\":true,\"b64\":\"Zm9v\"}");
    CHECK_EQ(g_bin.calls, 1);

    // Dropping only the binary side must leave the JSON side alive.
    sdk.dn.unsubscribeBinary("stage.dmx");
    DataNetTestAccess::handleMessage(sdk.dn, "{\"op\":\"pub\",\"ch\":\"stage.dmx\",\"d\":2}");
    CHECK_EQ(g_a.calls, 2);
}

TEST(protocol_rejects_a_binary_payload_larger_than_the_scratch_buffer) {
    Sdk sdk;
    sdk.dn.subscribeBinary("stage.dmx", binaryHandler, "binary/dmx");

    // DATANET_BINARY_BUF_SIZE+ bytes of base64 must not be handed to the
    // callback as a partial frame.
    std::string oversized(((DATANET_BINARY_BUF_SIZE + 64) / 3) * 4, 'A');
    std::string envelope =
        "{\"op\":\"pub\",\"ch\":\"stage.dmx\",\"bin\":true,\"b64\":\"" + oversized + "\"}";

    DataNetTestAccess::handleMessage(sdk.dn, envelope.c_str());

    CHECK_EQ(g_bin.calls, 0);
}

TEST(protocol_subscription_table_is_bounded) {
    Sdk sdk;
    datanetTestClearSerial();
    for (int i = 0; i < DATANET_MAX_SUBS + 2; i++) {
        std::string channel = "ch." + std::to_string(i);
        sdk.dn.subscribe(channel.c_str(), handlerA);
    }

    CHECK(datanetTestSerialLog().find("max subscriptions reached") != std::string::npos);

    // The first DATANET_MAX_SUBS still work.
    DataNetTestAccess::handleMessage(sdk.dn, "{\"op\":\"pub\",\"ch\":\"ch.0\",\"d\":1}");
    CHECK_EQ(g_a.calls, 1);
}

TEST(protocol_delivers_a_raw_binary_frame_to_the_single_binary_subscriber) {
    // Raw frames carry no channel, so they are only routable when exactly one
    // binary subscription exists.
    Sdk sdk;
    sdk.dn.subscribeBinary("stage.dmx", binaryHandler, "binary/dmx");
    DataNetTestAccess::setConnected(sdk.dn, true);
    datanetTestWire().connected = true;

    const uint8_t bytes[] = {1, 2, 3, 4};
    DataNetTestAccess::dispatchRawBinary(sdk.dn, bytes, sizeof(bytes));

    CHECK_EQ(g_bin.calls, 1);
    CHECK_STR_EQ(g_bin.channel, "stage.dmx");
    CHECK_STR_EQ(g_bin.contentType, "binary/dmx");
    CHECK_EQ(g_bin.raw, true);
    CHECK_EQ(g_bin.declaredBytes, static_cast<size_t>(4));
}

TEST(protocol_drops_ambiguous_raw_binary_frames) {
    Sdk sdk;
    sdk.dn.subscribeBinary("stage.a", binaryHandler, "binary/dmx");
    sdk.dn.subscribeBinary("stage.b", binaryHandler, "binary/dmx");

    datanetTestClearSerial();
    const uint8_t bytes[] = {1, 2, 3, 4};
    DataNetTestAccess::dispatchRawBinary(sdk.dn, bytes, sizeof(bytes));

    CHECK_EQ(g_bin.calls, 0);
    CHECK(datanetTestSerialLog().find("multiple binary subscriptions") != std::string::npos);
}

TEST(protocol_rejects_frames_larger_than_the_configured_json_size) {
    // ArduinoJson 7 documents are elastic, so the cap has to be enforced by
    // the SDK rather than by the document type.
    Sdk sdk;
    sdk.dn.subscribe("room.temp", handlerA);

    std::string filler(DATANET_INCOMING_JSON_SIZE, 'a');
    std::string envelope =
        "{\"op\":\"pub\",\"ch\":\"room.temp\",\"d\":{\"v\":\"" + filler + "\"}}";

    datanetTestClearSerial();
    DataNetTestAccess::handleMessage(sdk.dn, envelope.c_str());

    CHECK_EQ(g_a.calls, 0);
    CHECK(datanetTestSerialLog().find("DATANET_INCOMING_JSON_SIZE") != std::string::npos);
}
