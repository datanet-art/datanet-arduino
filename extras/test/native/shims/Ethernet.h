// Host-side stand-in for EthernetClient, backed by a scriptable byte "wire".
//
// The SDK constructs EthernetClient instances internally (a member for the
// WebSocket, locals for REST calls), so the fake keeps its state in a single
// shared wire object. That matches the SDK's actual behaviour of holding one
// live connection at a time, and lets a test feed bytes in and read bytes out
// without touching a real socket.

#pragma once

#include <Arduino.h>
#include <Client.h>

#include <string>
#include <vector>

struct DataNetTestWire {
    std::string              rx;              // bytes the peer will deliver
    size_t                   rxPos = 0;       // read cursor into rx
    std::string              tx;               // bytes the SDK has written
    bool                     connected = false;
    bool                     connectShouldFail = false;
    std::vector<std::string> connectLog;       // "host:port" per connect() call
    int                      connectCount = 0;
    int                      stopCount = 0;

    // Call counts, not byte counts: a transport that writes one byte per
    // syscall is functionally correct but unusably slow on real hardware.
    int writeByteCalls = 0;
    int writeBufferCalls = 0;

    void reset();
    void feed(const std::string& bytes);       // queue inbound bytes
    size_t pending() const { return rx.size() - rxPos; }
};

DataNetTestWire& datanetTestWire();

class EthernetClient : public Client {
public:
    int     connect(const char* host, uint16_t port) override;
    size_t  write(uint8_t b) override;
    size_t  write(const uint8_t* buf, size_t size) override;
    int     available() override;
    int     read() override;
    int     read(uint8_t* buf, size_t size) override;
    int     peek() override;
    void    flush() override {}
    void    stop() override;
    uint8_t connected() override;
};
