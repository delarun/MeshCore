#include <Arduino.h>
#include "target.h"

#include <helpers/portduino/CH341Hal.h>
#include <cstdlib>

PortduinoBoard board;

#ifndef CH341_USB_VID
  #define CH341_USB_VID 0x1A86
#endif
#ifndef CH341_USB_PID
  #define CH341_USB_PID 0x5512
#endif

// when running multiple adapters, select one by its USB serial number:
//   MESHCORE_CH341_SERIAL=xxxxxxxx ./firmware
static CH341Hal hal(getenv("MESHCORE_CH341_SERIAL"), CH341_USB_VID, CH341_USB_PID);

RADIO_CLASS radio = new Module(&hal, P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);

WRAPPER_CLASS radio_driver(radio, board);

PortduinoRTCClock rtc_clock;
PortduinoSensorManager sensors;
StdioSerial serial_console;

void portduinoSetup() {
  // don't let portduino's main loop insert its 100ms simulation delay
  portduinoSetOptions({ .realHardware = true });
}

bool radio_init() {
  if (!hal.open()) {
    Serial.println("ERROR: CH341A USB adapter not found (check cabling, VID/PID and udev permissions)");
    return false;
  }

  char serial[16], product[100];
  hal.getSerialString(serial, sizeof(serial));
  hal.getProductString(product, sizeof(product));
  Serial.printf("CH341: %s (serial '%s')\n", product, serial);

  return radio.std_init();
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
