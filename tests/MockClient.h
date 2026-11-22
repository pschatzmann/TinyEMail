// In-memory Arduino client for the tests: records what is connected and
// written, and returns canned server replies.
#pragma once

#include <cstdint>
#include <string>

#include <Arduino.h>

class MockClient : public Client {
 public:
  bool connectResult = true;
  std::string connectedHost;
  uint16_t connectedPort = 0;
  int connectCalls = 0;
  int stopCalls = 0;
  std::string written;
  std::string incoming;
  size_t readPos = 0;
  bool open = false;

  int connect(IPAddress, uint16_t) override { return 0; }

  int connect(const char* host, uint16_t port) override {
    ++connectCalls;
    connectedHost = host;
    connectedPort = port;
    open = connectResult;
    return connectResult ? 1 : 0;
  }

  size_t write(uint8_t b) override {
    if (!open) return 0;
    written.push_back(static_cast<char>(b));
    return 1;
  }

  size_t write(const uint8_t* buf, size_t size) override {
    if (!open) return 0;
    written.append(reinterpret_cast<const char*>(buf), size);
    return size;
  }

  int available() override {
    return static_cast<int>(incoming.size() - readPos);
  }

  int read() override {
    if (readPos >= incoming.size()) return -1;
    return static_cast<uint8_t>(incoming[readPos++]);
  }

  int read(uint8_t* buf, size_t size) override {
    int n = 0;
    while (n < static_cast<int>(size) && readPos < incoming.size()) {
      buf[n++] = static_cast<uint8_t>(incoming[readPos++]);
    }
    return n;
  }

  int peek() override {
    if (readPos >= incoming.size()) return -1;
    return static_cast<uint8_t>(incoming[readPos]);
  }

  void flush() override {}

  void stop() override {
    ++stopCalls;
    open = false;
  }

  uint8_t connected() override { return open ? 1 : 0; }

  operator bool() override { return open; }
};
