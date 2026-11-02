// The companion's main.cpp is compiled with RP2040_PLATFORM (see lib/SConscript),
// so it includes <helpers/rp2040/SerialBLEInterface.h>. This directory is first on
// the companion's include path, so it gets the SF32 BLE interface instead.
#pragma once

#include "SF32SerialBLEInterface.h"

using SerialBLEInterface = SF32SerialBLEInterface;
