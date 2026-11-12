// mesh::MainBoard for the LilyGo T-Display-SF32.
#pragma once

#include <Mesh.h>
#include <helpers/KeyValueStore.h>

extern volatile unsigned long sf32_console_eol_ms;  // Arduino.cpp: a line just ended on the USB console
// A CLI command typed at the USB console (not from the phone app, the USB app or remote CLI,
// which also pass timestamp 0): the console's line ended a moment ago.
inline bool sf32IsLocalConsole(uint32_t sender_timestamp) {
  return sender_timestamp == 0 && sf32_console_eol_ms && millis() - sf32_console_eol_ms < 200;
}

class SF32RTCClock;
class DisplayDriver;

class SF32Board : public mesh::MainBoard {
public:
  void begin();
  void loop() override;  // watchdog + low-battery check
  // CPU speed: 240 MHz, or 96 MHz at the lower core voltage (DVFS S0) while the screen and
  // audio are off (saves power; nothing that needs speed runs then). See setCpuLow().
  void setCpuLow(bool low);
  bool cpuLow() const { return _cpu_low; }
  uint32_t cpuMHz() const;
  // the SF32 chip's own temperature (deg C; its die, not the room: a few degrees above the
  // air, more while charging or busy). Read at most every 30 s (it takes a few ms).
  int chipTempC();
  // MeshCore telemetry (a contact's "request telemetry"): the chip temperature as the MCU temperature
  float getMCUTemperature() override { int t = chipTempC(); return t ? (float)t : NAN; }

  // ---- sleep (RT-Thread / SiFli power manager, when built with it: companion) ----
  // The chip light-sleeps whenever it's idle unless something holds it awake. Reasons
  // (bits): the UI holds it while the screen or audio is on, at boot, and when the user
  // turned Power saving off. Stage 1: light sleep only (see SF32Board.cpp).
  enum AwakeReason : uint8_t { AWAKE_BOOT = 1, AWAKE_SCREEN = 2, AWAKE_AUDIO = 4, AWAKE_USER = 8, AWAKE_USB = 16 };
  void holdAwake(uint8_t reason, bool hold);
  bool sleepSupported() const;
  // test builds (SF32_PM_LOG): time asleep since the last call, ms, and how many sleeps
  uint32_t takeSleepStats(uint32_t* sleeps);
  void powerOff() override;

  uint16_t getBattMilliVolts() override;  // 0 when no battery is detected
  bool batteryPresent() const { return !_batt_absent; }
  bool setAdcMultiplier(float multiplier) override;
  float getAdcMultiplier() const override { return _adc_multiplier; }
  const char* getManufacturerName() const override { return "LilyGo T-Display-SF32"; }
  void reboot() override;
  uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }

  void attachDynamicPrefs(KeyValueStore*) {}  // no board-specific prefs (yet)
  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) override;

  // Charger (SGM41562B) state
  // USB power, from the charger (an I2C read): cached for a second, the UI loop asks several
  // times per pass (every 25 ms even with the screen off, waking the bus each time)
  bool isExternalPowered() override {
    unsigned long now = millis();
    if (!_ext_at || now - _ext_at >= 1000) {
      _ext = readCharger() && _chg_power_good;
      _ext_at = now | 1;
    }
    return _ext;
  }
  // charge limit: the charger's target voltage (4200 = full); set again on every plug-in
  void setChargeLimit(uint16_t mv) { _chg_limit_mv = mv; _chg_limit_ok = false; }
  uint8_t chargeState() const { return _chg_state; }  // last polled (1 s): 0 not charging, 1 pre, 2 fast, 3 done
  bool isChargerActive() override { return readCharger() && (_chg_state == 1 || _chg_state == 2); }
  bool hasUsbPowerDetect() const override { return true; }
  bool isUsbPowered() override { return isExternalPowered(); }
  void setRTC(SF32RTCClock* rtc) { _rtc = rtc; }
  // why the board stopped last time ("power off (menu)", "power lost or crash", ...)
  const char* lastStop() const { return _last_stop; }
  // recorded with the next powerOff() (default: a MeshCore command)
  void setPowerOffReason(const char* r) { _poweroff_reason = r; }
  // event log on flash (last 20): add one; read them newest first (times: 0 = unknown)
  // event log (Settings > Logs): each entry keeps a snapshot of the board (see SF32Board.cpp)
  static const int LOG_SNAP_LEN = 96;
  void logEvent(const char* text, const char* extra = NULL);
  int readLog(uint32_t* times, char (*texts)[48], int max, char (*snaps)[LOG_SNAP_LEN] = NULL);
  void setDisplay(DisplayDriver* d) { _display = d; }
  // Extra CLI commands that need other objects (radio, display); see target.cpp.
  typedef bool (*CommandHook)(const char* command, uint32_t sender_timestamp, char* reply);
  void setCommandHook(CommandHook hook) { _command_hook = hook; }

private:
  void checkBootLog();
  bool _boot_checked = false;
  char _last_stop[40] = "";
  const char* _poweroff_reason = "command";
  bool readCharger();
  uint16_t readBattRaw();  // one ADC conversion (driver averages internally), mV

  void checkBattery();
  void trackChargerState();
  uint16_t readBattAvg();  // rolling average, no ADC access
  void sampleBattery();    // one ADC reading (~25 ms) into the rolling average

  SF32RTCClock* _rtc = nullptr;
  DisplayDriver* _display = nullptr;
  CommandHook _command_hook = nullptr;
  unsigned long _next_wdt_feed = 0, _next_batt_check = 0;
  uint8_t _low_batt_count = 0;
  uint8_t _btn_level[3] = {0, 0, 0};
  uint16_t _btn_edges[3] = {0, 0, 0};
  uint16_t _chg_limit_mv = 4200;
  bool _chg_limit_ok = false, _chg_pg_before = false;
  uint8_t _chg_state = 0;  // 0 not charging, 1 pre-charge, 2 fast charge, 3 done
  bool _chg_power_good = false;
  bool _ext = false;
  unsigned long _ext_at = 0;
  uint8_t _chg_sys = 0, _chg_fault = 0;  // raw REG08 / REG09

  // No-battery detection: with nothing on BAT the charger cycles pre-charge / fast
  // charge / done within seconds. Count state changes per window.
  bool _batt_absent = false;

  // Rolling battery average, one sample every BATT_SAMPLE_MS (see .cpp)
  static const int BATT_AVG_N = 16;
  uint16_t _batt_samples[BATT_AVG_N] = {0};
  uint8_t _batt_idx = 0, _batt_count = 0;
  unsigned long _next_batt_sample = 0;
  uint8_t _last_chg_state = 0xFF;
  uint8_t _chg_changes = 0;
  unsigned long _next_chg_poll = 0, _chg_window_end = 0;
  float _adc_multiplier = 1.0f;  // user calibration ("set adc.multiplier")
  bool _cpu_low = false;
};

// Back to full speed before anything that needs it (the display turning on, audio).
extern "C" void sf32_cpu_full_speed();

// Battery stats (tools/pmstats.sh): events counted while on battery
#define SF32_PM_EV_RADIO_IRQ  0
#define SF32_PM_EV_TOUCH_IRQ  1
#define SF32_PM_EV_AUDIO_DOWN 2
#define SF32_PM_EV_AUDIO_UP   3
extern "C" void sf32_pm_event(int k);
