// SX1262-on-CH341A wiring diagnostic (MeshCore native_ch341 pinout):
//   D0=NSS  D1=RXEN  D2=NRST  D3=SCK  D4=BUSY  D5=MOSI  D6=DIO1  D7=MISO
//
// Build (libpinedio-usb sources are downloaded by any Native_CH341 pio build):
//   LIB="../../../.pio/libdeps/Native_CH341_repeater/Pine libch341-spi Userspace library"
//   gcc -O2 -D_GNU_SOURCE -I"$LIB" -o ch341diag ch341diag.c "$LIB/libpinedio-usb.c" -lusb-1.0 -lpthread
//
// Modes:
//   (none)     reset the radio, read status + version register (expect 'SX1261 V2D 2D02')
//   loopback   adapter self-test: disconnect module, jumper D5 (pin 20) to D7 (pin 22)
//   bitbang    same register read via slow software SPI (rules out edge/speed issues)
//   nsstest    verify the chip reacts to NSS (MISO must go Hi-Z when deselected)
//   watch      live view of input pins for 30s (short module pads to GND/3.3V to trace wires)
//   hold P V   drive CH341 pin P to level V until Enter (measure voltages with a multimeter)
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "libpinedio-usb.h"

#define PIN_NSS  0
#define PIN_RXEN 1
#define PIN_RST  2
#define PIN_BUSY 4
#define PIN_DIO1 6

static struct pinedio_inst io;

static void spi_xfer(uint8_t* out, uint8_t* in, int len) {
  pinedio_digital_write(&io, PIN_NSS, 0);
  pinedio_transceive(&io, out, in, len);
  pinedio_digital_write(&io, PIN_NSS, 1);
}

static int wait_busy_low(int ms) {
  for (int i = 0; i < ms; i++) {
    if (pinedio_digital_read(&io, PIN_BUSY) == 0) return 1;
    usleep(1000);
  }
  return 0;
}

static int rd(int pin) { return pinedio_digital_read(&io, pin) ? 1 : 0; }

// software SPI mode 0, one USB transaction per edge (~kHz instead of ~1.5MHz):
// slow enough that ringing/edge quality cannot matter
static uint8_t bb_byte(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    pinedio_digital_write(&io, 5, (out >> i) & 1);  // MOSI
    pinedio_digital_write(&io, 3, 1);               // SCK rising: chip samples MOSI
    in = (uint8_t)((in << 1) | rd(7));              // sample MISO
    pinedio_digital_write(&io, 3, 0);               // SCK falling: chip shifts next bit
  }
  return in;
}

int main(int argc, char** argv) {
  if (pinedio_init(&io, NULL) != 0) {
    printf("FAIL: CH341A not found (permissions? lsusb should show 1a86:5512)\n");
    return 1;
  }

  if (argc > 3 && strcmp(argv[1], "hold") == 0) {
    // hold a D-pin at a level so its voltage can be measured with a multimeter:
    //   ./ch341diag hold 5 1   -> drive D5/MOSI high until Enter is pressed
    int pin = atoi(argv[2]), val = atoi(argv[3]) ? 1 : 0;
    pinedio_set_pin_mode(&io, pin, 1);
    pinedio_digital_write(&io, pin, val);
    printf("holding D%d = %d; measure the voltage at the module pin, then press Enter\n", pin, val);
    getchar();
    pinedio_deinit(&io);
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "watch") == 0) {
    // live view of the input pins for 30s; short the module's MISO pad (or
    // the wire) to GND / 3.3V and check that D7 follows
    printf("watching D0..D7 for 30s (D7=MISO, D6=DIO1, D4=BUSY)...\n");
    int last = -1;
    for (int i = 0; i < 300; i++) {
      int v = 0;
      for (int p = 7; p >= 0; p--) v = (v << 1) | rd(p);
      if (v != last) {
        printf("  D7..D0 = ");
        for (int p = 7; p >= 0; p--) putchar('0' + ((v >> p) & 1));
        printf("\n");
        last = v;
      }
      usleep(100000);
    }
    pinedio_deinit(&io);
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "nsstest") == 0) {
    // SX1262 drives MISO only while its NSS is low. Clock bits with our NSS
    // low, then with our NSS high: if the status pattern appears in BOTH
    // cases, the chip never sees our NSS line (it is permanently selected).
    pinedio_set_option(&io, PINEDIO_OPTION_AUTO_CS, 0);
    pinedio_set_pin_mode(&io, 3, 1);
    pinedio_set_pin_mode(&io, 5, 1);
    pinedio_set_pin_mode(&io, PIN_NSS, 1);
    pinedio_set_pin_mode(&io, PIN_RST, 1);
    pinedio_digital_write(&io, PIN_NSS, 1);
    pinedio_digital_write(&io, 3, 0);
    pinedio_digital_write(&io, PIN_RST, 0);
    usleep(5000);
    pinedio_digital_write(&io, PIN_RST, 1);
    usleep(50000);

    uint8_t sel[8], desel[8];
    pinedio_digital_write(&io, PIN_NSS, 0);
    for (int i = 0; i < 8; i++) sel[i] = bb_byte(0x00);
    pinedio_digital_write(&io, PIN_NSS, 1);
    usleep(10000);
    for (int i = 0; i < 8; i++) desel[i] = bb_byte(0x00);

    printf("NSS=0 (selected):   ");
    for (int i = 0; i < 8; i++) printf("%02X ", sel[i]);
    printf("\nNSS=1 (deselected): ");
    for (int i = 0; i < 8; i++) printf("%02X ", desel[i]);
    printf("\n");

    int flat = 1;
    for (int i = 1; i < 8; i++)
      if (desel[i] != desel[0]) flat = 0;
    if (flat && (desel[0] == 0x00 || desel[0] == 0xFF))
      printf("VERDICT: NSS works (MISO went Hi-Z when deselected) - NSS wire is fine\n");
    else
      printf("VERDICT: chip still drives MISO with NSS high -> our NSS never reaches\n"
             "the chip: broken wire or wrong pad. Fix the NSS connection.\n");
    pinedio_deinit(&io);
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "bitbang") == 0) {
    pinedio_set_option(&io, PINEDIO_OPTION_AUTO_CS, 0);
    pinedio_set_pin_mode(&io, 3, 1);
    pinedio_set_pin_mode(&io, 5, 1);
    pinedio_set_pin_mode(&io, PIN_NSS, 1);
    pinedio_set_pin_mode(&io, PIN_RXEN, 1);
    pinedio_set_pin_mode(&io, PIN_RST, 1);
    pinedio_digital_write(&io, PIN_NSS, 1);   // applies directions too
    pinedio_digital_write(&io, PIN_RXEN, 0);
    pinedio_digital_write(&io, 3, 0);         // SCK idle low

    pinedio_digital_write(&io, PIN_RST, 0);
    usleep(5000);
    pinedio_digital_write(&io, PIN_RST, 1);
    usleep(50000);
    if (!wait_busy_low(100)) printf("warning: BUSY stuck high after reset\n");

    // ReadRegister 0x0320 (version string), software SPI
    pinedio_digital_write(&io, PIN_NSS, 0);
    uint8_t hdr[4] = { 0x1D, 0x03, 0x20, 0x00 };
    for (int i = 0; i < 4; i++) bb_byte(hdr[i]);
    uint8_t in[16];
    for (int i = 0; i < 16; i++) in[i] = bb_byte(0x00);
    pinedio_digital_write(&io, PIN_NSS, 1);

    printf("bitbang version reg: hex=");
    for (int i = 0; i < 16; i++) printf("%02X ", in[i]);
    printf("\n                     ascii='");
    for (int i = 0; i < 16; i++) putchar(in[i] >= 32 && in[i] < 127 ? in[i] : '.');
    printf("'\n");
    if (memmem(in, 16, "SX126", 5))
      printf("BITBANG OK: chip and wiring are fine at low speed -> the problem is\n"
             "signal integrity at full SPI speed (add 47-100 Ohm series resistors\n"
             "in SCK and MOSI at the adapter side, shorten wires, add GND return)\n");
    else
      printf("BITBANG FAILED TOO: not an edge-speed problem - recheck NSS/SCK/MOSI/MISO\n"
             "wiring pin by pin with a continuity tester, module powered at 3.3V\n");
    pinedio_deinit(&io);
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "loopback") == 0) {
    // adapter self-test: DISCONNECT the module, jumper D5/MOSI (pin 20)
    // directly to D7/MISO (pin 22), then run './ch341diag loopback'
    pinedio_set_option(&io, PINEDIO_OPTION_AUTO_CS, 0);
    pinedio_set_pin_mode(&io, 3, 1);
    pinedio_set_pin_mode(&io, 5, 1);
    pinedio_set_pin_mode(&io, PIN_NSS, 1);
    pinedio_digital_write(&io, PIN_NSS, 1);  // also applies pin directions
    uint8_t pat[8] = { 0x00, 0xFF, 0x55, 0xAA, 0xA5, 0x5A, 0x01, 0x80 };
    int pass = 1;
    for (int run = 0; run < 5; run++) {
      uint8_t in[8] = { 0 };
      pinedio_transceive(&io, pat, in, 8);
      printf("loopback run %d: ", run);
      for (int i = 0; i < 8; i++) printf("%02X ", in[i]);
      if (memcmp(pat, in, 8) != 0) { pass = 0; printf(" <- MISMATCH"); }
      printf("\n");
    }
    printf(pass ? "LOOPBACK OK: adapter, USB and driver are fine - problem is between adapter and module\n"
                : "LOOPBACK FAILED: adapter/driver problem (bad clone? shorted lines?)\n");
    pinedio_deinit(&io);
    return pass ? 0 : 1;
  }
  pinedio_set_option(&io, PINEDIO_OPTION_AUTO_CS, 0);
  pinedio_set_pin_mode(&io, 3, 1);        // SCK
  pinedio_set_pin_mode(&io, 5, 1);        // MOSI
  pinedio_set_pin_mode(&io, PIN_NSS, 1);
  pinedio_digital_write(&io, PIN_NSS, 1);
  pinedio_set_pin_mode(&io, PIN_RXEN, 1);
  pinedio_digital_write(&io, PIN_RXEN, 0);
  pinedio_set_pin_mode(&io, PIN_RST, 1);
  pinedio_set_pin_mode(&io, PIN_BUSY, 0);
  pinedio_set_pin_mode(&io, PIN_DIO1, 0);

  printf("before reset: BUSY=%d DIO1=%d\n", rd(PIN_BUSY), rd(PIN_DIO1));

  // hardware reset pulse
  pinedio_digital_write(&io, PIN_RST, 0);
  usleep(5000);
  pinedio_digital_write(&io, PIN_RST, 1);
  usleep(50000);

  int busy_ok = wait_busy_low(100);
  printf("after reset:  BUSY=%d (%s)\n", rd(PIN_BUSY),
         busy_ok ? "OK, went low" : "STUCK HIGH - check BUSY wire / module power");

  // GetStatus (0xC0)
  uint8_t out1[2] = { 0xC0, 0x00 }, in1[2] = { 0, 0 };
  spi_xfer(out1, in1, 2);
  int mode = (in1[1] >> 4) & 0x7;
  printf("GetStatus: 0x%02X (chip mode %d, expect 2=STDBY_RC)\n", in1[1], mode);

  // ReadRegister 0x0320: version string, 16 bytes ASCII; read repeatedly --
  // run-to-run variation means signal integrity, stable garbage means systematic
  uint8_t out2[4 + 16], in2[4 + 16];
  for (int run = 0; run < 5; run++) {
    memset(out2, 0, sizeof(out2));
    memset(in2, 0, sizeof(in2));
    out2[0] = 0x1D; out2[1] = 0x03; out2[2] = 0x20; out2[3] = 0x00;
    spi_xfer(out2, in2, sizeof(out2));
    printf("version reg [run %d]: hex=", run);
    for (int i = 4; i < 20; i++) printf("%02X ", in2[i]);
    printf(" ascii='");
    for (int i = 4; i < 20; i++) putchar(in2[i] >= 32 && in2[i] < 127 ? in2[i] : '.');
    printf("'\n");
  }

  int all_same = 1, all_dead = (in2[4] == 0x00 || in2[4] == 0xFF);
  for (int i = 5; i < 20; i++) {
    if (in2[i] != in2[4]) all_same = 0;
    if (in2[i] != 0x00 && in2[i] != 0xFF) all_dead = 0;
  }

  if (memmem(in2 + 4, 16, "SX126", 5))
    printf("RESULT: OK - SX126x is alive, SPI wiring is good\n");
  else if (all_dead)
    printf("RESULT: no response - check MISO/MOSI/SCK/NSS wires, module 3.3V power and GND\n");
  else if (all_same)
    // the chip clocks out its status byte on every transfer, but never the
    // register data: it is powered and talking, yet does not decode commands
    printf("RESULT: chip alive but commands not understood - MOSI path is bad:\n"
           "  * MOSI must go from CH341 D5/DOUT (chip pin 20) to module MOSI\n"
           "  * check logic levels: many CH341A boards drive 5V signals even\n"
           "    with the 3.3V jumper - SX1262 needs 3.3V (do the 3.3V mod)\n"
           "  * keep wires short (<10cm), route GND next to SCK/MOSI\n");
  else
    printf("RESULT: garbage - likely loose wire, MISO/MOSI swapped or 5V logic levels\n");

  pinedio_deinit(&io);
  return 0;
}
