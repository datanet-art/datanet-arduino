// Host-side stand-in for Arduino's Client.h.
//
// Only the members src/DataNet.cpp calls are declared. Notably absent are the
// IPAddress overloads, which the SDK never uses.

#pragma once

#include <Arduino.h>

class Client : public Stream {
public:
    virtual int     connect(const char* host, uint16_t port) = 0;
    virtual size_t  write(uint8_t b) override                = 0;
    virtual size_t  write(const uint8_t* buf, size_t size) override = 0;
    virtual int     available() override                     = 0;
    virtual int     read() override                          = 0;
    virtual int     read(uint8_t* buf, size_t size)          = 0;
    virtual int     peek() override                          = 0;
    virtual void    flush()                                  = 0;
    virtual void    stop()                                   = 0;
    virtual uint8_t connected()                              = 0;
};
