// Base64 codec — the encoder feeds every outbound binary envelope and the
// decoder handles every inbound one, so a defect here silently corrupts DMX.

#include <string>

#include "test_access.h"
#include "tiny_test.h"

static std::string enc(const std::string& raw) {
    return DataNetTestAccess::base64Encode(
               reinterpret_cast<const uint8_t*>(raw.data()), raw.size())
        .c_str();
}

TEST(base64_encode_matches_rfc4648_vectors) {
    CHECK_STR_EQ(enc(""), "");
    CHECK_STR_EQ(enc("f"), "Zg==");
    CHECK_STR_EQ(enc("fo"), "Zm8=");
    CHECK_STR_EQ(enc("foo"), "Zm9v");
    CHECK_STR_EQ(enc("foob"), "Zm9vYg==");
    CHECK_STR_EQ(enc("fooba"), "Zm9vYmE=");
    CHECK_STR_EQ(enc("foobar"), "Zm9vYmFy");
}

TEST(base64_encode_handles_high_bytes) {
    const uint8_t bytes[] = {0xFF, 0xFE, 0xFD, 0x00, 0x80};
    CHECK_STR_EQ(DataNetTestAccess::base64Encode(bytes, sizeof(bytes)).c_str(), "//79AIA=");
}

TEST(base64_decode_matches_rfc4648_vectors) {
    struct {
        const char* encoded;
        const char* expected;
    } cases[] = {
        {"Zg==", "f"},       {"Zm8=", "fo"},      {"Zm9v", "foo"},
        {"Zm9vYg==", "foob"}, {"Zm9vYmE=", "fooba"}, {"Zm9vYmFy", "foobar"},
    };

    for (auto& c : cases) {
        uint8_t out[32] = {0};
        size_t  n = DataNetTestAccess::base64Decode(c.encoded, out, sizeof(out));
        CHECK_STR_EQ(std::string(reinterpret_cast<char*>(out), n), c.expected);
    }
}

TEST(base64_roundtrips_a_full_dmx_frame) {
    uint8_t frame[512];
    for (size_t i = 0; i < sizeof(frame); i++) {
        frame[i] = static_cast<uint8_t>((i * 7) & 0xFF);
    }

    String encoded = DataNetTestAccess::base64Encode(frame, sizeof(frame));

    // 512 bytes -> 684 base64 chars (including padding). The README quotes this
    // number when sizing DATANET_BINARY_BUF_SIZE.
    CHECK_EQ(encoded.length(), static_cast<size_t>(684));

    uint8_t decoded[512] = {0};
    size_t  n = DataNetTestAccess::base64Decode(encoded.c_str(), decoded, sizeof(decoded));
    CHECK_EQ(n, sizeof(frame));
    CHECK_EQ(memcmp(frame, decoded, sizeof(frame)), 0);
}

TEST(base64_decode_rejects_output_overflow) {
    // Decoding 6 bytes into a 4-byte buffer must fail rather than scribble.
    uint8_t out[4] = {0};
    CHECK_EQ(DataNetTestAccess::base64Decode("Zm9vYmFy", out, sizeof(out)), static_cast<size_t>(0));
}

TEST(base64_decode_skips_whitespace_and_stops_at_padding) {
    uint8_t out[16] = {0};
    size_t  n = DataNetTestAccess::base64Decode("Zm9v\r\nYmFy", out, sizeof(out));
    CHECK_STR_EQ(std::string(reinterpret_cast<char*>(out), n), "foobar");
}

TEST(base64_value_maps_the_alphabet) {
    CHECK_EQ(static_cast<int>(DataNetTestAccess::base64Value('A')), 0);
    CHECK_EQ(static_cast<int>(DataNetTestAccess::base64Value('Z')), 25);
    CHECK_EQ(static_cast<int>(DataNetTestAccess::base64Value('a')), 26);
    CHECK_EQ(static_cast<int>(DataNetTestAccess::base64Value('z')), 51);
    CHECK_EQ(static_cast<int>(DataNetTestAccess::base64Value('0')), 52);
    CHECK_EQ(static_cast<int>(DataNetTestAccess::base64Value('9')), 61);
    CHECK_EQ(static_cast<int>(DataNetTestAccess::base64Value('+')), 62);
    CHECK_EQ(static_cast<int>(DataNetTestAccess::base64Value('/')), 63);
    CHECK_EQ(static_cast<int>(DataNetTestAccess::base64Value('!')), -1);
}
