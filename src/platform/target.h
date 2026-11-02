// Board definition for MeshCore: the globals and functions every MeshCore
// variant provides (cf. MeshCore-main/variants/*/target.h).
#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include "SF32Board.h"
#include "SF32SX1262Radio.h"
#include "SF32RTCClock.h"

extern SF32Board board;
extern SF32SX1262Radio radio_driver;
extern SF32RTCClock rtc_clock;
extern SensorManager sensors;  // no onboard environment sensors yet

#ifdef DISPLAY_CLASS
  #include "SF32Display.h"
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
