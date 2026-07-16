#include <Arduino.h>
#include "target.h"

ESP32Board board;

#if CONFIG_IDF_TARGET_ESP32C3
  // the C3 has a single usable SPI bus: SPIClass's default (HSPI) doesn't
  // exist there and silently yields a dead bus -- use FSPI explicitly
  static SPIClass spi(FSPI);
#else
  static SPIClass spi;
#endif
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);   // BOOT button, active low
#endif

#ifndef LORA_CR
  #define LORA_CR 5
#endif

// hardware bring-up diagnostics; enable with:
//   PLATFORMIO_BUILD_FLAGS="-D E80_BRINGUP_DIAG" pio run -e <env> -t upload
#ifdef E80_BRINGUP_DIAG
static uint8_t bb_xfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(P_LORA_MOSI, (out >> i) & 1);
    delayMicroseconds(2);
    digitalWrite(P_LORA_SCLK, HIGH);
    delayMicroseconds(2);
    in = (in << 1) | (digitalRead(P_LORA_MISO) ? 1 : 0);
    digitalWrite(P_LORA_SCLK, LOW);
    delayMicroseconds(2);
  }
  return in;
}

static void e80_diag() {
  for (int i = 5; i > 0; i--) {
    Serial.printf("E80 diag in %d...\n", i);
    delay(1000);
  }

  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
  pinMode(P_LORA_SCLK, OUTPUT);
  digitalWrite(P_LORA_SCLK, LOW);
  pinMode(P_LORA_MOSI, OUTPUT);
  pinMode(P_LORA_MISO, INPUT);
  pinMode(P_LORA_BUSY, INPUT);
  pinMode(P_LORA_RESET, OUTPUT);

  digitalWrite(P_LORA_RESET, LOW);
  delay(2);
  int in_reset = digitalRead(P_LORA_BUSY);
  digitalWrite(P_LORA_RESET, HIGH);
  delayMicroseconds(300);
  int boot = digitalRead(P_LORA_BUSY);
  delay(300);
  int idle = digitalRead(P_LORA_BUSY);
  Serial.printf("BUSY: in-reset=%d at-boot=%d idle=%d (want x/1/0; constant value = check BUSY or NRST wire)\n",
                in_reset, boot, idle);

  // LR11x0 GetVersion (0x0101): command transaction, BUSY handshake,
  // then the response comes in a second transaction
  digitalWrite(P_LORA_NSS, LOW);
  bb_xfer(0x01); bb_xfer(0x01);
  digitalWrite(P_LORA_NSS, HIGH);
  for (int i = 0; i < 1000 && digitalRead(P_LORA_BUSY); i++) delayMicroseconds(10);
  uint8_t r[5];
  digitalWrite(P_LORA_NSS, LOW);
  for (int i = 0; i < 5; i++) r[i] = bb_xfer(0x00);
  digitalWrite(P_LORA_NSS, HIGH);
  Serial.printf("GetVersion(bitbang): stat1=%02X hw=%02X device=%02X fw=%u.%u\n", r[0], r[1], r[2], r[3], r[4]);
  Serial.println("  (device 0x03 = LR1121 OK; all 00 = MISO stuck low/SPI dead; all FF or random = MISO floating)");
}
#endif

// E80-900M2213S RF switch: does NOT follow the Semtech reference design.
// DIO5 -> RFSW0_V1, DIO6 -> RFSW1_V2, DIO7 is not connected on the module
// (so no GNSS/WiFi scanning). See footnote #3 at
// https://www.cdebyte.com/products/E80-900M2213S/2#Pin
static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
  RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7, RADIOLIB_NC, RADIOLIB_NC
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                DIO5  DIO6  DIO7
  { LR11x0::MODE_STBY,  {LOW,  LOW,  LOW } },
  { LR11x0::MODE_RX,    {LOW,  HIGH, LOW } },
  { LR11x0::MODE_TX,    {HIGH, HIGH, LOW } },   // RFO_LP path
  { LR11x0::MODE_TX_HP, {HIGH, LOW,  LOW } },   // RFO_HP path (22 dBm)
  { LR11x0::MODE_TX_HF, {LOW,  LOW,  LOW } },   // 2.4GHz path
  { LR11x0::MODE_GNSS,  {LOW,  LOW,  HIGH} },
  { LR11x0::MODE_WIFI,  {LOW,  LOW,  LOW } },
  END_OF_MODE_TABLE,
};

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

#ifdef LR11X0_DIO3_TCXO_VOLTAGE
  float tcxo = LR11X0_DIO3_TCXO_VOLTAGE;
#else
  float tcxo = 1.8f;
#endif

#ifdef E80_BRINGUP_DIAG
  e80_diag();
#endif
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
  // as with CustomSX1262: -706/-707 during init can mean there is no TCXO
  // on this module population -- retry in crystal mode
  if (status == RADIOLIB_ERR_SPI_CMD_FAILED || status == RADIOLIB_ERR_SPI_CMD_INVALID) {
    tcxo = 0.0f;
    status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
  }
  if (status != RADIOLIB_ERR_NONE) {
    // don't return: on USB-CDC consoles (C3/S2) a one-shot message is lost
    // to port re-enumeration, so keep repeating it
    while (true) {
      Serial.print("ERROR: LR1121 radio init failed: ");
      Serial.println(status);
      delay(2000);
    }
  }
  Serial.println("LR1121 radio init OK");

  radio.setCRC(2);
  radio.explicitHeader();
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
#ifdef RX_BOOSTED_GAIN
  radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
#endif

  return true;  // success
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
