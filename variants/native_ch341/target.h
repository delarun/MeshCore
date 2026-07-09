#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/portduino/PortduinoBoard.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/SensorManager.h>
#include <helpers/portduino/StdioSerial.h>

#include <unistd.h>

class PortduinoSensorManager : public SensorManager {
public:
  // called once per main-loop iteration -- sleep a little, otherwise the
  // process spins at 100% CPU (portduino's realHardware mode has no loop delay)
  void loop() override { ::usleep(2000); }
};

extern PortduinoBoard board;
extern WRAPPER_CLASS radio_driver;
extern PortduinoRTCClock rtc_clock;
extern PortduinoSensorManager sensors;
extern StdioSerial serial_console;

// route the examples' console I/O to stdin/stdout (portduino's own Serial
// object can't read); applies to every file that includes target.h
#define Serial serial_console

bool radio_init();
mesh::LocalIdentity radio_new_identity();
