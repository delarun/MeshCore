#pragma once

#include <Arduino.h>

#if defined(PORTDUINO)

#include <unistd.h>
#include <sys/select.h>
#include <cstdio>

// portduino's built-in Serial (SimSerial) is write-only: available()/read()
// are stubs, so the interactive CLI of the examples would never receive
// input. This is a console Serial working over the process's stdin/stdout.
// Variants alias it via '#define Serial' in their target.h.
class StdioSerial : public Stream {
public:
  void begin(unsigned long baudrate) { /* no-op on a host */ }

  int available() override {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = { 0, 0 };
    return select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0 ? 1 : 0;
  }

  int read() override {
    if (!available()) return -1;
    char c;
    if (::read(STDIN_FILENO, &c, 1) != 1) return -1;
    return c == '\n' ? '\r' : c;   // terminal Enter -> CR, as the CLI expects
  }

  int peek() override { return -1; }  // not supported

  size_t write(uint8_t c) override {
    fputc(c, stdout);
    if (c == '\n') fflush(stdout);  // stay responsive when piped/logged
    return 1;
  }
  using Print::write;

  operator bool() { return true; }
};

#endif
