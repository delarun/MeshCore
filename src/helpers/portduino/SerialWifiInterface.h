#pragma once

#include "../BaseSerialInterface.h"
#include <WiFi.h>

// TCP server transport for the companion protocol on portduino (Linux).
// Same wire format as helpers/esp32/SerialWifiInterface, but built for the
// portduino WiFi shim: WiFiServer takes the port in its constructor, and
// WiFiClient::available() only ever reports 0..1 bytes (single lookahead),
// so frames are accumulated byte-by-byte into a local buffer instead.
class SerialWifiInterface : public BaseSerialInterface {
  bool deviceConnected;
  bool _isEnabled;

  WiFiServer* server;
  WiFiClient client;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  4
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  // receive accumulator: 3-byte header ('<', len LSB, len MSB) + payload
  uint8_t rx_buf[3 + MAX_FRAME_SIZE];
  size_t rx_len;

  void clearBuffers() { send_queue_len = 0; rx_len = 0; }

public:
  SerialWifiInterface() : server(NULL), client() {
    deviceConnected = false;
    _isEnabled = false;
    send_queue_len = 0;
    rx_len = 0;
  }

  void begin(int port);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override { return deviceConnected; }
  bool isWriteBusy() const override { return false; }

  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

#if WIFI_DEBUG_LOGGING
  #include <Arduino.h>
  #define WIFI_DEBUG_PRINTLN(F, ...) Serial.printf("WiFi: " F "\n", ##__VA_ARGS__)
#else
  #define WIFI_DEBUG_PRINTLN(...) {}
#endif
