// Based on MeshCore's variants/lilygo_tdeck/target.cpp (MIT, see TDeckBoard.h).
#include <Arduino.h>
#include "target.h"

TDeckBoard board;

SPIClass tdeck_spi;  // the shared bus (screen, radio, microSD): one SPIClass for all of them
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, tdeck_spi);
TDeckRadio radio_driver(radio, board);
SF32RTCClock rtc_clock;
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);  // the trackball's click (active low)
  #ifdef PIN_USER_BTN_ANA
    MomentaryButton analog_btn(PIN_USER_BTN_ANA, 1000, 2000);  // "pressed" when analogRead < 2000
  #endif
#endif

bool radio_init() {
  rtc_clock.begin();
  board.setRTC(&rtc_clock);
#ifdef DISPLAY_CLASS
  board.setDisplay(&display);
#endif
  return radio.std_init(&tdeck_spi);  // (the screen has started the shared bus already)
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // a new random identity
}
