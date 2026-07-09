#pragma once

#include <RadioLib.h>

#include <libpinedio-usb.h>

#include <sched.h>
#include <unistd.h>
#include <sys/time.h>
#include <cstring>
#include <cstdio>

// RadioLib HAL for a CH341A USB-SPI adapter, driven from userspace through
// libpinedio-usb (libusb, no kernel driver needed).
//
// RadioLib pin numbers refer to the CH341 GPIO lines D0..D7:
//   D0 = CS (NSS), D3 = SCK, D5 = MOSI, D7 = MISO are taken by the SPI bus,
//   leaving D1, D2, D4 and D6 free for RESET/BUSY/DIO1/RXEN etc.
// Interrupts (DIO1) are delivered by a background thread inside libpinedio
// that polls the pin state, so RadioLib's interrupt-driven RX works as-is.
class CH341Hal : public RadioLibHal {
public:
  explicit CH341Hal(const char* usb_serial = NULL, uint32_t vid = 0x1A86, uint32_t pid = 0x5512)
    : RadioLibHal(0 /*INPUT*/, 1 /*OUTPUT*/, 0 /*LOW*/, 1 /*HIGH*/,
                  PINEDIO_INT_MODE_RISING, PINEDIO_INT_MODE_FALLING) {
    memset(&pinedio, 0, sizeof(pinedio));
    if (usb_serial && usb_serial[0]) {
      strncpy(pinedio.serial_number, usb_serial, sizeof(pinedio.serial_number) - 1);
      pinedio_set_option(&pinedio, PINEDIO_OPTION_SEARCH_SERIAL, 1);
    }
    if (vid) {
      pinedio_set_option(&pinedio, PINEDIO_OPTION_VID, vid);
      pinedio_set_option(&pinedio, PINEDIO_OPTION_PID, pid);
    }
  }

  ~CH341Hal() {
    if (opened) pinedio_deinit(&pinedio);
  }

  // opens the USB device; safe to call more than once
  bool open() {
    if (opened) return true;
    if (pinedio_init(&pinedio, NULL) != 0) return false;
    pinedio_set_option(&pinedio, PINEDIO_OPTION_AUTO_CS, 0);  // CS is driven by RadioLib via digitalWrite()
    pinedio_set_pin_mode(&pinedio, 3, 1);  // SCK is an output
    pinedio_set_pin_mode(&pinedio, 5, 1);  // MOSI is an output
    opened = true;
    return true;
  }

  bool isConnected() const { return opened && !pinedio.in_error; }

  void getSerialString(char* dest, size_t len) {
    snprintf(dest, len, "%s", pinedio.serial_number);
  }
  void getProductString(char* dest, size_t len) {
    // pinedio stores the raw USB string descriptor: 2-byte header, then UTF-16LE
    const uint8_t* d = (const uint8_t*) pinedio.product_string;
    size_t n = d[0] >= 2 ? (d[0] - 2) / 2 : 0;
    if (n > len - 1) n = len - 1;
    for (size_t i = 0; i < n; i++) dest[i] = (char) d[2 + i*2];
    dest[n] = 0;
  }

  void init() override { }
  void term() override { }

  void pinMode(uint32_t pin, uint32_t mode) override {
    if (pin == RADIOLIB_NC || !opened) return;
    pinedio_set_pin_mode(&pinedio, pin, mode);
  }

  void digitalWrite(uint32_t pin, uint32_t value) override {
    if (pin == RADIOLIB_NC || !opened) return;
    pinedio_digital_write(&pinedio, pin, value);
  }

  uint32_t digitalRead(uint32_t pin) override {
    if (pin == RADIOLIB_NC || !opened) return 0;
    int32_t res = pinedio_digital_read(&pinedio, pin);  // returns a bitmask, not 0/1
    return res > 0 ? 1 : 0;
  }

  void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override {
    if (interruptNum == RADIOLIB_NC || !opened) return;
    pinedio_attach_interrupt(&pinedio, (pinedio_int_pin) interruptNum, (pinedio_int_mode) mode, interruptCb);
  }

  void detachInterrupt(uint32_t interruptNum) override {
    if (interruptNum == RADIOLIB_NC || !opened) return;
    pinedio_deattach_interrupt(&pinedio, (pinedio_int_pin) interruptNum);
  }

  void delay(unsigned long ms) override { delayMicroseconds(ms * 1000); }

  void delayMicroseconds(unsigned long us) override {
    if (us == 0) {
      sched_yield();
      return;
    }
    usleep(us);
  }

  void yield() override { sched_yield(); }

  unsigned long millis() override {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000UL) + (tv.tv_usec / 1000UL);
  }

  unsigned long micros() override {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000000UL) + tv.tv_usec;
  }

  long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override {
    return 0;  // not supported
  }

  void spiBegin() override { }
  void spiBeginTransaction() override { }

  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override {
    if (!opened) return;
    pinedio_transceive(&pinedio, out, in, len);
  }

  void spiEndTransaction() override { }
  void spiEnd() override { }

private:
  struct pinedio_inst pinedio;
  bool opened = false;
};
