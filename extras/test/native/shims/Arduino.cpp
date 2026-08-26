#include <Arduino.h>
#include <Ethernet.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>

// ---------------------------------------------------------------------------
// Virtual clock
// ---------------------------------------------------------------------------
static uint32_t g_millis = 0;

void datanetTestSetMillis(uint32_t value) { g_millis = value; }
void datanetTestAdvanceMillis(uint32_t delta) { g_millis += delta; }
uint32_t millis() { return g_millis; }

// delay() advances virtual time rather than sleeping, so the SDK's
// poll-with-timeout loops terminate instead of spinning forever.
void delay(uint32_t ms) { g_millis += ms; }

// ---------------------------------------------------------------------------
// Deterministic PRNG (xorshift32) — reproducible masks and nonces
// ---------------------------------------------------------------------------
static uint32_t g_rngState = 0x12345678u;

void randomSeed(unsigned long seed) {
    g_rngState = seed ? static_cast<uint32_t>(seed) : 0x12345678u;
}

static uint32_t nextRandom() {
    g_rngState ^= g_rngState << 13;
    g_rngState ^= g_rngState >> 17;
    g_rngState ^= g_rngState << 5;
    return g_rngState;
}

long random(long maxExclusive) {
    if (maxExclusive <= 0) return 0;
    return static_cast<long>(nextRandom() % static_cast<uint32_t>(maxExclusive));
}

long random(long minInclusive, long maxExclusive) {
    if (maxExclusive <= minInclusive) return minInclusive;
    return minInclusive + random(maxExclusive - minInclusive);
}

// ---------------------------------------------------------------------------
// String
// ---------------------------------------------------------------------------
void String::trim() {
    size_t begin = 0;
    size_t end   = _s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(_s[begin]))) begin++;
    while (end > begin && std::isspace(static_cast<unsigned char>(_s[end - 1]))) end--;
    _s = _s.substr(begin, end - begin);
}

void String::toLowerCase() {
    std::transform(_s.begin(), _s.end(), _s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
}

bool String::startsWith(const char* prefix) const {
    if (prefix == nullptr) return false;
    size_t n = std::strlen(prefix);
    return _s.size() >= n && _s.compare(0, n, prefix) == 0;
}

String String::substring(size_t from) const {
    if (from >= _s.size()) return String();
    return String(_s.substr(from));
}

String String::substring(size_t from, size_t to) const {
    if (from >= _s.size() || to <= from) return String();
    return String(_s.substr(from, to - from));
}

long String::toInt() const { return std::strtol(_s.c_str(), nullptr, 10); }

int String::indexOf(const char* needle) const {
    if (needle == nullptr) return -1;
    size_t pos = _s.find(needle);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

// ---------------------------------------------------------------------------
// Print / Stream
// ---------------------------------------------------------------------------
size_t Print::write(const uint8_t* buf, size_t size) {
    for (size_t i = 0; i < size; i++) write(buf[i]);
    return size;
}

size_t Print::print(const char* s) {
    if (s == nullptr) return 0;
    size_t n = std::strlen(s);
    write(reinterpret_cast<const uint8_t*>(s), n);
    return n;
}

size_t Print::print(const String& s) { return print(s.c_str()); }
size_t Print::print(char c) { return write(static_cast<uint8_t>(c)); }

static size_t printNumber(Print& p, const char* fmt, ...) {
    char buf[48];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return 0;
    return p.print(buf);
}

size_t Print::print(unsigned char v) { return printNumber(*this, "%u", static_cast<unsigned>(v)); }
size_t Print::print(int v) { return printNumber(*this, "%d", v); }
size_t Print::print(unsigned int v) { return printNumber(*this, "%u", v); }
size_t Print::print(long v) { return printNumber(*this, "%ld", v); }
size_t Print::print(unsigned long v) { return printNumber(*this, "%lu", v); }
size_t Print::print(double v) { return printNumber(*this, "%.2f", v); }

size_t Print::println() { return print("\r\n"); }

size_t Stream::readBytes(char* buffer, size_t length) {
    size_t n = 0;
    while (n < length && available()) {
        int c = read();
        if (c < 0) break;
        buffer[n++] = static_cast<char>(c);
    }
    return n;
}

String Stream::readStringUntil(char terminator) {
    String out;
    while (available()) {
        int c = read();
        if (c < 0) break;
        if (static_cast<char>(c) == terminator) break;
        out += static_cast<char>(c);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------
SerialShim Serial;

static std::string g_serialLog;
static bool        g_serialEcho = false;

size_t SerialShim::write(uint8_t b) {
    g_serialLog += static_cast<char>(b);
    if (g_serialEcho) std::fputc(static_cast<int>(b), stderr);
    return 1;
}

void        datanetTestClearSerial() { g_serialLog.clear(); }
std::string datanetTestSerialLog() { return g_serialLog; }
void        datanetTestSetSerialEcho(bool enabled) { g_serialEcho = enabled; }

// ---------------------------------------------------------------------------
// Fake wire + EthernetClient
// ---------------------------------------------------------------------------
static DataNetTestWire g_wire;

DataNetTestWire& datanetTestWire() { return g_wire; }

void DataNetTestWire::reset() {
    rx.clear();
    rxPos = 0;
    tx.clear();
    connected = false;
    connectShouldFail = false;
    connectLog.clear();
    connectCount = 0;
    stopCount = 0;
    writeByteCalls = 0;
    writeBufferCalls = 0;
}

void DataNetTestWire::feed(const std::string& bytes) { rx += bytes; }

int EthernetClient::connect(const char* host, uint16_t port) {
    g_wire.connectCount++;
    g_wire.connectLog.push_back(std::string(host ? host : "") + ":" + std::to_string(port));
    if (g_wire.connectShouldFail) {
        g_wire.connected = false;
        return 0;
    }
    g_wire.connected = true;
    return 1;
}

size_t EthernetClient::write(uint8_t b) {
    g_wire.writeByteCalls++;
    if (!g_wire.connected) return 0;
    g_wire.tx += static_cast<char>(b);
    return 1;
}

size_t EthernetClient::write(const uint8_t* buf, size_t size) {
    g_wire.writeBufferCalls++;
    if (!g_wire.connected) return 0;
    g_wire.tx.append(reinterpret_cast<const char*>(buf), size);
    return size;
}

int EthernetClient::available() { return static_cast<int>(g_wire.pending()); }

int EthernetClient::read() {
    if (g_wire.pending() == 0) return -1;
    return static_cast<uint8_t>(g_wire.rx[g_wire.rxPos++]);
}

int EthernetClient::read(uint8_t* buf, size_t size) {
    size_t n = std::min(size, g_wire.pending());
    for (size_t i = 0; i < n; i++) buf[i] = static_cast<uint8_t>(g_wire.rx[g_wire.rxPos++]);
    return static_cast<int>(n);
}

int EthernetClient::peek() {
    if (g_wire.pending() == 0) return -1;
    return static_cast<uint8_t>(g_wire.rx[g_wire.rxPos]);
}

void EthernetClient::stop() {
    g_wire.stopCount++;
    g_wire.connected = false;
}

// Mirrors Arduino semantics: a socket with buffered data still reads as
// connected even after the peer closes.
uint8_t EthernetClient::connected() {
    return (g_wire.connected || g_wire.pending() > 0) ? 1 : 0;
}
