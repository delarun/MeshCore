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
