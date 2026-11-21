// What MeshCore's companion_radio needs from the board (like a MeshCore variant's target.h).
#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include "TDeckRadio.h"
#include "SF32RTCClock.h"  // (src/platform: shared; its clock device is the ESP32's, TDeckDevices.cpp)
#include <helpers/SensorManager.h>
#include "TDeckBoard.h"

extern SPIClass tdeck_spi;  // the bus the screen, radio and microSD share
extern TDeckBoard board;
extern TDeckRadio radio_driver;
extern SF32RTCClock rtc_clock;  // the time survives a restart, and whether it's known (the UI)
extern SensorManager sensors;  // the original T-Deck has no GPS or environment sensors

#ifdef DISPLAY_CLASS
  #include "SF32Display.h"  // (src/platform: the display code is shared)
  #include <helpers/ui/MomentaryButton.h>
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  #ifdef PIN_USER_BTN_ANA
    #include "SF32TouchInput.h"
    extern MomentaryButton analog_btn;      // touchscreen taps
    #define rotary_input touch_input        // touchscreen swipes
  #endif
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
