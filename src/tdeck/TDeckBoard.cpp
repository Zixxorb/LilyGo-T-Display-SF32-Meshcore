// Based on MeshCore's variants/lilygo_tdeck/TDeckBoard.cpp (MIT, see TDeckBoard.h).
#include "TDeckBoard.h"
#include "TDeckContacts.h"

void TDeckBoard::begin() {
  // The screen, LoRa radio and microSD share one SPI bus: every chip select high (idle)
  // before anything talks on it, or a floating CS lets two chips answer at once.
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  ESP32Board::begin();  // (I2C on 18/8: keyboard, touch)

  // Peripheral power: keyboard, backlight, microSD, amplifier, trackball
  pinMode(PIN_PERF_POWERON, OUTPUT);
  digitalWrite(PIN_PERF_POWERON, HIGH);

  pinMode(PIN_USER_BTN, INPUT);
  pinMode(P_LORA_MISO, INPUT_PULLUP);

  // (No deep sleep wake check: the radio's DIO1 is GPIO45, which can't wake the S3 from deep
  // sleep, so a received packet never starts it.)

  // Bring-up: give a USB serial monitor up to 2 s to connect, so the boot messages are seen.
  for (uint32_t t = millis(); !Serial && millis() - t < 2000; ) delay(10);

  tdeckTrimContacts();  // before MeshCore loads them (see TDeckContacts.h)
}

#ifdef TDECK_SHOT
void tdeck_shot_poll();  // TDeckShot.cpp (debug builds)
#endif

void TDeckBoard::loop() {
#ifdef TDECK_SHOT
  tdeck_shot_poll();
#endif
}

// SF32Display calls this before using the panel (the SF32's display controller runs from the
// CPU clock). The T-Deck's doesn't: nothing to do.
extern "C" void sf32_cpu_full_speed() {}
