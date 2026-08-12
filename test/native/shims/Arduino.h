// Minimal host-side Arduino shim for native unit tests.
//
// This is NOT a general-purpose Arduino emulator. It implements only the
// surface that src/DataNet.cpp actually touches, so the SDK's protocol and
// encoding logic can be exercised with g++ on a development machine.
//
// Time is virtual: tests drive millis() via datanetTestSetMillis(). Random is
// deterministic and seedable so masked WebSocket frames are reproducible.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Flash-string macros collapse to plain RAM literals on the host.
// ---------------------------------------------------------------------------
#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)

// ---------------------------------------------------------------------------
// Virtual clock
// ---------------------------------------------------------------------------
void     datanetTestSetMillis(uint32_t value);
void     datanetTestAdvanceMillis(uint32_t delta);
uint32_t millis();
void     delay(uint32_t ms);

// ---------------------------------------------------------------------------
// Deterministic PRNG matching Arduino's random(min, max) signature
// ---------------------------------------------------------------------------
void randomSeed(unsigned long seed);
long random(long maxExclusive);
long random(long minInclusive, long maxExclusive);

// ---------------------------------------------------------------------------
// String
//
// ArduinoJson 7 adapts any type exposing c_str() + length(), so this class
// works directly with deserializeJson() without an explicit adapter.
// ---------------------------------------------------------------------------
class String {
public:
    String() {}
    String(const char* s) : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    explicit String(char c) : _s(1, c) {}
    explicit String(int v) : _s(std::to_string(v)) {}
    explicit String(unsigned int v) : _s(std::to_string(v)) {}
    explicit String(long v) : _s(std::to_string(v)) {}
    explicit String(unsigned long v) : _s(std::to_string(v)) {}

    const char* c_str() const { return _s.c_str(); }
    size_t      length() const { return _s.size(); }
    void        reserve(size_t n) { _s.reserve(n); }

    String& operator+=(const char* s) { if (s) _s += s; return *this; }
    String& operator+=(char c) { _s += c; return *this; }
    String& operator+=(const String& o) { _s += o._s; return *this; }

    // ArduinoJson's Writer<::String> builds output through concat().
    bool concat(const char* s) { if (s) _s += s; return true; }
    bool concat(char c) { _s += c; return true; }
    bool concat(const String& o) { _s += o._s; return true; }

    char operator[](size_t i) const { return _s[i]; }

    bool operator==(const char* s) const { return s && _s == s; }
    bool operator==(const String& o) const { return _s == o._s; }

    void trim();
    void toLowerCase();
    bool   startsWith(const char* prefix) const;
    String substring(size_t from) const;
    String substring(size_t from, size_t to) const;
    long   toInt() const;
    int    indexOf(const char* needle) const;

    const std::string& std_str() const { return _s; }

private:
    std::string _s;
};

inline String operator+(const String& a, const char* b) {
    String out(a);
    out += b;
    return out;
}

inline String operator+(const String& a, const String& b) {
    String out(a);
    out += b;
    return out;
}

// ---------------------------------------------------------------------------
// Print / Stream
// ---------------------------------------------------------------------------
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t* buf, size_t size);

    size_t print(const char* s);
    size_t print(const String& s);
    size_t print(char c);
    size_t print(unsigned char v);
    size_t print(int v);
    size_t print(unsigned int v);
    size_t print(long v);
    size_t print(unsigned long v);
    size_t print(double v);

    size_t println();
    template <typename T> size_t println(T v) { return print(v) + println(); }
};

// Unused by the SDK, but ArduinoJson's Arduino-Print integration references it.
class Printable {
public:
    virtual ~Printable() {}
    virtual size_t printTo(Print& p) const = 0;
};

class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read()      = 0;
    virtual int peek()      = 0;

    // Consumes through the terminator; the terminator is not returned.
    String readStringUntil(char terminator);
    size_t readBytes(char* buffer, size_t length);
};

// ---------------------------------------------------------------------------
// Serial — captured rather than printed so test output stays readable.
// ---------------------------------------------------------------------------
class SerialShim : public Stream {
public:
    size_t write(uint8_t b) override;
    int    available() override { return 0; }
    int    read() override { return -1; }
    int    peek() override { return -1; }

    void begin(unsigned long) {}
    explicit operator bool() const { return true; }
};

extern SerialShim Serial;

// Test hooks for the captured Serial log.
void        datanetTestClearSerial();
std::string datanetTestSerialLog();
void        datanetTestSetSerialEcho(bool enabled);
