// HTTP response reading for the generic (non-ESP) transport. This code path
// parses the auth token and the presence count, so it is the difference
// between a Teensy connecting and a Teensy silently failing.

#include <string>

#include "test_access.h"
#include "tiny_test.h"

namespace {

// Runs _readHttpBody against a canned response and returns whether it
// succeeded, with the parsed body in `body`.
bool readResponse(const std::string& raw, String& body) {
    DataNetTestWire& wire = datanetTestWire();
    wire.reset();
    wire.connected = true;
    wire.feed(raw);
    datanetTestSetMillis(1000);

    EthernetClient client;
    return DataNetTestAccess::readHttpBody(client, body);
}

}  // namespace

TEST(http_reads_a_content_length_body) {
    String body;
    bool   ok = readResponse(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 27\r\n"
        "\r\n"
        "{\"token\":\"header.body.sig\"}",
        body);

    CHECK(ok);
    CHECK_STR_EQ(body.c_str(), "{\"token\":\"header.body.sig\"}");
}

TEST(http_reads_a_chunked_body) {
    String body;
    bool   ok = readResponse(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "9\r\n"
        "{\"token\":\r\n"
        "6\r\n"
        "\"abc\"}\r\n"
        "0\r\n"
        "\r\n",
        body);

    CHECK(ok);
    CHECK_STR_EQ(body.c_str(), "{\"token\":\"abc\"}");
}

TEST(http_is_case_insensitive_about_header_names) {
    String body;
    bool   ok = readResponse(
        "HTTP/1.1 200 OK\r\n"
        "CONTENT-LENGTH: 7\r\n"
        "\r\n"
        "{\"a\":1}",
        body);

    CHECK(ok);
    CHECK_STR_EQ(body.c_str(), "{\"a\":1}");
}

TEST(http_rejects_non_200_responses) {
    String body;
    CHECK(!readResponse("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n", body));
    CHECK(!readResponse("HTTP/1.1 500 Server Error\r\nContent-Length: 2\r\n\r\n{}", body));
    CHECK(!readResponse("HTTP/1.1 302 Found\r\nLocation: /x\r\n\r\n", body));
}

TEST(http_accepts_http_1_0_responses) {
    String body;
    bool   ok = readResponse("HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n{}", body);
    CHECK(ok);
    CHECK_STR_EQ(body.c_str(), "{}");
}

TEST(http_reads_a_body_with_no_length_header_until_close) {
    String body;
    bool   ok = readResponse(
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{\"occupancy\":4}",
        body);

    CHECK(ok);
    CHECK_STR_EQ(body.c_str(), "{\"occupancy\":4}");
}

TEST(http_reports_failure_when_the_body_is_short) {
    // Declared 40 bytes, delivered 7. Returning a partial body here would feed
    // truncated JSON to the parser.
    String body;
    CHECK(!readResponse(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 40\r\n"
        "\r\n"
        "{\"a\":1}",
        body));
}

TEST(http_failure_message_does_not_hardcode_the_auth_endpoint) {
    // _readHttpBody serves both /auth/token and /presence. Logging "Auth
    // failed" for a presence lookup sends people debugging the wrong request.
    datanetTestClearSerial();
    String body;
    readResponse("HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n", body);

    std::string log = datanetTestSerialLog();
    CHECK(log.find("Auth failed") == std::string::npos);
    CHECK(log.find("503") != std::string::npos);
}
