// The LilyGo T-Deck board: power, battery, startup. Based on MeshCore's
// variants/lilygo_tdeck/TDeckBoard (MIT, Copyright (c) 2025 Scott Powell / rippleradios.com;
// see licenses/MeshCore-LICENSE.txt). It also has the SF32 board's interface (SF32Board.h),
// which the shared companion UI uses: the T-Deck's own answers where it has them (no charger
// chip, no power manager), so `SF32Board` names this class here.
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ESP32Board.h>

#define PIN_VBAT_READ 4  // battery through a 1:2 divider

// On battery below this the UI shows 0 % (SF32Board.h's name; the T-Deck doesn't power off on it yet)
#define SF32_BATT_EMPTY_MV 3400

class SF32RTCClock;
class DisplayDriver;

class TDeckBoard : public ESP32Board {
public:
  void begin();
  void loop() override;

  uint16_t getBattMilliVolts() override {
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_VBAT_READ);
    return (uint16_t)(mv * 2 / 8);
  }
  bool batteryPresent() const { return true; }

  const char* getManufacturerName() const override { return "LilyGo T-Deck"; }

  // ---- the SF32 board's interface (see SF32Board.h) ----
  void setCpuLow(bool low) { _cpu_low = low; }  // (power work: later)
  bool cpuLow() const { return _cpu_low; }
  uint32_t cpuMHz() const { return getCpuFrequencyMhz(); }
  int chipTempC();
  enum AwakeReason : uint8_t { AWAKE_BOOT = 1, AWAKE_SCREEN = 2, AWAKE_AUDIO = 4, AWAKE_USER = 8, AWAKE_USB = 16 };
  void holdAwake(uint8_t reason, bool hold) { _awake = hold ? (_awake | reason) : (_awake & ~reason); }
  bool sleepSupported() const { return false; }
  uint32_t takeSleepStats(uint32_t* sleeps) { if (sleeps) *sleeps = 0; return 0; }
  // USB: the T-Deck has no charger chip to ask, only whether a USB host is talking to it
  bool isExternalPowered() override { return HWCDC::isPlugged(); }
  void setChargeLimit(uint16_t) {}
  uint8_t chargeState() const { return 0; }
  bool isChargerActive() override { return false; }
  void setRTC(SF32RTCClock* rtc) { _rtc = rtc; }
  const char* lastStop() const { return ""; }
  void setPowerOffReason(const char* r) { _poweroff_reason = r; }
  static const int LOG_SNAP_LEN = 96;
  void logEvent(const char* text, const char* extra = NULL);
  int readLog(uint32_t* times, char (*texts)[48], int max, char (*snaps)[LOG_SNAP_LEN] = NULL);
  void setDisplay(DisplayDriver* d) { _display = d; }
  typedef bool (*CommandHook)(const char* command, uint32_t sender_timestamp, char* reply);
  void setCommandHook(CommandHook hook) { _command_hook = hook; }

private:
  bool _cpu_low = false, _boot_done = false;
  uint8_t _awake = 0;
  const char* _poweroff_reason = "command";
  SF32RTCClock* _rtc = nullptr;
  DisplayDriver* _display = nullptr;
  CommandHook _command_hook = nullptr;
};
using SF32Board = TDeckBoard;

extern "C" void sf32_cpu_full_speed();
extern "C" void sf32_pm_event(int k);
extern "C" void sf32_wake_loop(void);
