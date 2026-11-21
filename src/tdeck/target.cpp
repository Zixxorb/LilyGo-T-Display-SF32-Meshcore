// Based on MeshCore's variants/lilygo_tdeck/target.cpp (MIT, see TDeckBoard.h).
#include <Arduino.h>
#include "target.h"

TDeckBoard board;

SPIClass tdeck_spi;  // the shared bus (screen, radio, microSD): one SPIClass for all of them
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, tdeck_spi);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

void tdeckTestPattern();  // (bring-up: TDeckTestPattern.cpp)

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  bool ok = radio.std_init(&tdeck_spi);  // (starts the shared bus: SCK 40, MISO 38, MOSI 41)
  tdeckTestPattern();
  return ok;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // a new random identity
}
