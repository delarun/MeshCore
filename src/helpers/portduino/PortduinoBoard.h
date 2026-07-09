#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#if defined(PORTDUINO)

#include <ctime>
#include <unistd.h>

class PortduinoBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin() {
    startup_reason = BD_STARTUP_NORMAL;
  }

  uint16_t getBattMilliVolts() override { return 0; }   // no battery on a host machine

  const char* getManufacturerName() const override {
    return "Linux Native";
  }

  void reboot() override {
    ::reboot();   // portduino: re-exec's this process
  }

  uint8_t getStartupReason() const override { return startup_reason; }
};

// RTC backed by the host's system clock (assumed correct, e.g. NTP-synced)
class PortduinoRTCClock : public mesh::RTCClock {
public:
  uint32_t getCurrentTime() override { return (uint32_t) time(NULL); }
  void setCurrentTime(uint32_t t) override { /* host clock is authoritative */ }
};

#endif
