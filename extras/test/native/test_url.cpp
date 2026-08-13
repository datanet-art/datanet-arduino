// URL parsing and percent-encoding for the REST transport used by every
// non-ESP board.

#include "test_access.h"
#include "tiny_test.h"

namespace {

struct ParsedUrl {
    bool     ok = false;
    char     host[96] = {0};
    char     path[256] = {0};
    uint16_t port = 0;
    bool     secure = false;
};

ParsedUrl parse(const char* url) {
    ParsedUrl r;
    r.ok = DataNetTestAccess::parseHttpUrl(url, r.host, sizeof(r.host), &r.port,
                                           r.path, sizeof(r.path), &r.secure);
    return r;
}

}  // namespace

TEST(url_parses_https_with_default_port) {
    ParsedUrl r = parse("https://api.datanet.art/auth/token");
    CHECK(r.ok);
    CHECK_STR_EQ(r.host, "api.datanet.art");
    CHECK_STR_EQ(r.path, "/auth/token");
    CHECK_EQ(r.port, static_cast<uint16_t>(443));
    CHECK_EQ(r.secure, true);
}

TEST(url_parses_http_with_default_port) {
    ParsedUrl r = parse("http://192.168.1.50/auth/token");
    CHECK(r.ok);
    CHECK_STR_EQ(r.host, "192.168.1.50");
    CHECK_STR_EQ(r.path, "/auth/token");
    CHECK_EQ(r.port, static_cast<uint16_t>(80));
    CHECK_EQ(r.secure, false);
}

TEST(url_parses_explicit_port) {
    ParsedUrl r = parse("http://gateway.local:8080/presence?channel=a.b");
    CHECK(r.ok);
    CHECK_STR_EQ(r.host, "gateway.local");
    CHECK_STR_EQ(r.path, "/presence?channel=a.b");
    CHECK_EQ(r.port, static_cast<uint16_t>(8080));
}

TEST(url_defaults_bare_origin_to_root_path) {
    ParsedUrl r = parse("http://gateway.local");
    CHECK(r.ok);
    CHECK_STR_EQ(r.host, "gateway.local");
    CHECK_STR_EQ(r.path, "/");
    CHECK_EQ(r.port, static_cast<uint16_t>(80));
}

TEST(url_rejects_malformed_input) {
    CHECK(!parse("ws://ws.datanet.art/ws").ok);      // wrong scheme
    CHECK(!parse("api.datanet.art/auth").ok);        // no scheme
    CHECK(!parse("http:///auth/token").ok);          // empty host
    CHECK(!parse("http://host:99999/x").ok);         // port out of range
    CHECK(!parse("http://host:0/x").ok);             // port zero
    CHECK(!parse("").ok);
}

TEST(url_rejects_host_longer_than_the_destination_buffer) {
    // A silent truncation here would connect to the wrong host.
    std::string url = "http://";
    url.append(200, 'h');
    url += "/x";
    CHECK(!parse(url.c_str()).ok);
}

TEST(url_rejects_path_longer_than_the_destination_buffer) {
    ParsedUrl r;
    char      shortPath[8];
    CHECK(!DataNetTestAccess::parseHttpUrl("http://host/a/very/long/path", r.host,
                                           sizeof(r.host), &r.port, shortPath,
                                           sizeof(shortPath), &r.secure));
}

TEST(url_encode_leaves_unreserved_characters_alone) {
    // RFC 3986 unreserved set: ALPHA / DIGIT / "-" / "." / "_" / "~"
    CHECK_STR_EQ(DataNetTestAccess::urlEncode("abcXYZ019-._~").c_str(), "abcXYZ019-._~");
}

TEST(url_encode_percent_encodes_everything_else) {
    CHECK_STR_EQ(DataNetTestAccess::urlEncode("a b").c_str(), "a%20b");
    CHECK_STR_EQ(DataNetTestAccess::urlEncode("a/b?c=d&e").c_str(), "a%2Fb%3Fc%3Dd%26e");
    CHECK_STR_EQ(DataNetTestAccess::urlEncode("+").c_str(), "%2B");
    CHECK_STR_EQ(DataNetTestAccess::urlEncode("").c_str(), "");
}

TEST(url_encode_handles_non_ascii_bytes) {
    // UTF-8 "é" is 0xC3 0xA9 — must not sign-extend into a negative index.
    CHECK_STR_EQ(DataNetTestAccess::urlEncode("\xC3\xA9").c_str(), "%C3%A9");
}

TEST(url_encode_preserves_typical_channel_names) {
    CHECK_STR_EQ(DataNetTestAccess::urlEncode("project.lighting.dmx").c_str(),
                 "project.lighting.dmx");
}
