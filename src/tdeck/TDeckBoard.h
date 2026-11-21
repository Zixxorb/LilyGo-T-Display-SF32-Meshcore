// The LilyGo T-Deck board: power, battery, startup. Based on MeshCore's
// variants/lilygo_tdeck/TDeckBoard (MIT, Copyright (c) 2025 Scott Powell / rippleradios.com;
// see licenses/MeshCore-LICENSE.txt).
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ESP32Board.h>

#define PIN_VBAT_READ 4  // battery through a 1:2 divider

class TDeckBoard : public ESP32Board {
public:
  void begin();

  uint16_t getBattMilliVolts() override {
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_VBAT_READ);
    return (uint16_t)(mv * 2 / 8);
  }

  const char* getManufacturerName() const override { return "LilyGo T-Deck"; }
};
