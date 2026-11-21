// What MeshCore's companion_radio needs from the board (like a MeshCore variant's target.h).
#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include "TDeckBoard.h"

extern TDeckBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;  // the original T-Deck has no GPS or environment sensors

bool radio_init();
mesh::LocalIdentity radio_new_identity();
