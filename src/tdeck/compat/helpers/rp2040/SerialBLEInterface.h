// T-Deck: the companion UI (UITask.cpp) includes this for the SF32's Bluetooth interface and
// calls a few things MeshCore's ESP32 one doesn't have (the pairing window, forgetting phones,
// slow advertising). Its `bluetooth_interface` is pointed at TDeckBLE, which adds them and
// works the real one (main.cpp's). Only UITask.cpp includes this.
#pragma once
#include "TDeckBLE.h"
#define SerialBLEInterface TDeckBLE
#define bluetooth_interface tdeck_ble
