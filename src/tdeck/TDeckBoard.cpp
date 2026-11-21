// Based on MeshCore's variants/lilygo_tdeck/TDeckBoard.cpp (MIT, see TDeckBoard.h).
#include "TDeckBoard.h"
#include "TDeckContacts.h"
#include <SPIFFS.h>
#include "SF32RTCClock.h"
#include "TDeckDevices.h"
#include "TDeckKeys.h"
#include "target.h"
#include "SF32TouchInput.h"
#include "TDeckBLE.h"

#define ON_HOLD_MS 1000  // powered off: the trackball held this long turns it on

// Woken from "off" by the trackball: on only if it's held down a second (a knock in a pocket
// goes back to sleep at once)
static void checkPowerOnHold() {
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) return;
  rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);  // (held high while off: powerOff())
  rtc_gpio_deinit((gpio_num_t)PIN_USER_BTN);   // (the wake pin: a normal pin again)
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) return;
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  for (uint32_t t0 = millis(); millis() - t0 < ON_HOLD_MS; delay(10)) {
    if (digitalRead(PIN_USER_BTN) == HIGH) {  // let go too soon: back off
      rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
      rtc_gpio_pullup_en((gpio_num_t)PIN_USER_BTN);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BTN, 0);
      esp_deep_sleep_start();
    }
  }
}

void TDeckBoard::begin() {
  checkPowerOnHold();

  // The screen, LoRa radio and microSD share one SPI bus: every chip select high (idle)
  // before anything talks on it, or a floating CS lets two chips answer at once.
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  ESP32Board::begin();  // (I2C on 18/8: keyboard, touch)

  // Peripheral power: keyboard, backlight, microSD, amplifier, trackball
  pinMode(PIN_PERF_POWERON, OUTPUT);
  digitalWrite(PIN_PERF_POWERON, HIGH);

  pinMode(PIN_USER_BTN, INPUT);
  pinMode(P_LORA_MISO, INPUT_PULLUP);

  // (No deep sleep wake check: the radio's DIO1 is GPIO45, which can't wake the S3 from deep
  // sleep, so a received packet never starts it.)

  // Bring-up: give a USB serial monitor up to 2 s to connect, so the boot messages are seen.
  for (uint32_t t = millis(); !Serial && millis() - t < 2000; ) delay(10);

  tdeckTrimContacts();  // before MeshCore loads them (see TDeckContacts.h)
  tdeckKeysBegin();
}

#ifdef TDECK_SHOT
void tdeck_shot_poll();  // TDeckShot.cpp (debug builds)
#endif

#ifdef TDECK_POWER_AB
// Measuring build (env tdeck_power_ab): the power savings (pacing, 80 MHz) are on for an hour,
// off for an hour, and so on from boot; each change goes in the event log (Settings > Logs), so
// the drain of each can be read off the battery history.
static bool savingOff() { return (millis() / 3600000UL) % 2 == 1; }
#else
static inline bool savingOff() { return false; }
#endif

// The main loop's pacing, as the SF32's src/main.cpp does it (the companion UI sets these):
// it runs once per pass of MeshCore's loop, at the end (MeshCore's main.cpp calls board.loop()).
extern volatile bool sf32_ui_animating;
extern volatile uint8_t sf32_ui_wait_ms, sf32_loop_sleep_ms;
extern volatile uint16_t sf32_loop_event_ms;

// Screen off, on battery: wait up to ms for something to happen, in 10 ms naps (each lets the
// CPU idle): a packet coming in or going out (DIO1), the trackball click or a roll. With a phone
// connected only the normal nap: its commands would wait otherwise.
static void waitForEvents(uint32_t ms) {
  if (bluetooth_connected()) ms = sf32_loop_sleep_ms;
  for (uint32_t t0 = millis(); millis() - t0 < ms; ) {
    if (digitalRead(P_LORA_DIO_1) == HIGH || digitalRead(PIN_USER_BTN) == LOW || tdeckKeysPending()) return;
    delay(10);
  }
}

void TDeckBoard::loop() {
  if (!_boot_done) {  // the filesystem is mounted by now (MeshCore's setup())
    _boot_done = true;
    if (_rtc) _rtc->afterSetup();
    // why it started, when that wasn't a normal start: in the event log (Settings > Logs)
    switch (esp_reset_reason()) {
      case ESP_RST_PANIC: logEvent("restarted: crash"); break;
      case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: logEvent("restarted: watchdog"); break;
      case ESP_RST_BROWNOUT: logEvent("restarted: power dip"); break;
      default: break;
    }
  }
  // the touch chip and the keyboard are asked over I2C only while the screen is on (or taps
  // wake it): with it off, nothing they say is used
  bool screen = !_display || _display->isOn() || touch_input.tapWake();
  if (screen) tdeck_touch_poll();
  tdeckKeysActive(screen);
#ifdef TDECK_SHOT
  tdeck_shot_poll();
#endif
#ifdef TDECK_POWER_AB
  static int phase = -1;
  if (phase != (int)savingOff()) {
    phase = savingOff();
    logEvent(phase ? "A/B: savings off" : "A/B: savings on");
    if (phase) setCpuLow(false);
  }
  if (phase) return;  // (no pacing: the loop runs flat out, as before)
#endif
  if (sf32_ui_animating) delay(sf32_ui_wait_ms);  // mid-animation: back for its next frame
  else if (sf32_loop_event_ms) waitForEvents(sf32_loop_event_ms);
  else delay(sf32_loop_sleep_ms);  // 4 ms with the screen in use, else 25: the CPU idles meanwhile
}

void TDeckBoard::powerOff() {
#ifdef DISPLAY_CLASS
  display.turnOff();
#endif
  radio_driver.powerOff();
  digitalWrite(P_LORA_NSS, HIGH);
  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);  // (the radio stays deselected, asleep)
  // the peripherals' power (keyboard, touch, backlight, trackball sensors, microSD, amplifier)
  // off: their pins first to inputs, so nothing feeds them through a pin meanwhile
  for (int pin : {PIN_TFT_CS, PIN_SD_CS, 11 /* TFT DC */, 42 /* backlight */}) pinMode(pin, INPUT);
  digitalWrite(PIN_PERF_POWERON, LOW);
  Serial.flush();
  delay(100);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  rtc_gpio_pullup_en((gpio_num_t)PIN_USER_BTN);  // the trackball click: pressed = low
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BTN, 0);
  esp_deep_sleep_start();
}

void TDeckBoard::setCpuLow(bool low) {
  if (savingOff()) low = false;
  if (low == _cpu_low) return;
  _cpu_low = low;
  setCpuFrequencyMhz(low ? 80 : 240);  // (80: the lowest that keeps Bluetooth and the APB clock)
}

// SF32Display calls this before using the panel: back to full speed to draw
extern "C" void sf32_cpu_full_speed() { board.setCpuLow(false); }

extern "C" void sf32_pm_event(int) {}  // (battery stats: the SF32's power manager)
extern "C" void sf32_wake_loop(void) {}  // (the loop doesn't wait for events here yet)

int TDeckBoard::chipTempC() {
  static int t = 0;
  static unsigned long at = 0;
  if (!at || millis() - at > 30000) {
    t = (int)lroundf(temperatureRead());
    at = millis() | 1;
  }
  return t;
}

// ---- event log (Settings > Logs): the SF32's format and file, last 20 ----
#define LOG_FILE "/sf32_log"
#define LOG_MAX  20
#define LOG_LINE 160
#define LOG_SEP  '\x1f'

void TDeckBoard::logEvent(const char* text, const char* extra) {
  static char buf[LOG_MAX * LOG_LINE];
  int n = 0;
  if (File f = SPIFFS.open(LOG_FILE, "r")) {
    n = f.read((uint8_t*)buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
  }
  buf[n] = 0;
  int lines = 0;
  for (int i = 0; i < n; i++) if (buf[i] == '\n') lines++;
  char* start = buf;
  while (lines >= LOG_MAX) {  // keep LOG_MAX - 1, then add one
    char* nl = strchr(start, '\n');
    if (!nl) break;
    start = nl + 1;
    lines--;
  }
  uint32_t t = (_rtc && _rtc->timeKnown()) ? _rtc->getCurrentTime() : 0;
  char clean[48];
  snprintf(clean, sizeof(clean), "%s", text);
  for (char* p = clean; *p; p++) if (*p == '\n' || *p == '\r' || *p == LOG_SEP) *p = ' ';
  char line[LOG_LINE];  // the snapshot: uptime s, battery mV, -1, -1 (no charger), chip deg C, free heap, min free heap, MHz
  snprintf(line, sizeof(line), "%lu|%s%c%lu,%u,-1,-1,%d,%lu,%lu,%lu%s%s\n", (unsigned long)t, clean, LOG_SEP,
           (unsigned long)(millis() / 1000), (unsigned)getBattMilliVolts(), chipTempC(),
           (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(), (unsigned long)cpuMHz(),
           extra ? "," : "", extra ? extra : "");
  if (File f = SPIFFS.open(LOG_FILE, "w")) {
    f.write((const uint8_t*)start, strlen(start));
    f.write((const uint8_t*)line, strlen(line));
  }
}

int TDeckBoard::readLog(uint32_t* times, char (*texts)[48], int max, char (*snaps)[LOG_SNAP_LEN]) {
  static char buf[LOG_MAX * LOG_LINE];
  int n = 0;
  if (File f = SPIFFS.open(LOG_FILE, "r")) {
    n = f.read((uint8_t*)buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
  }
  buf[n] = 0;
  int count = 0;  // newest first: walk the lines backwards
  char* end = buf + n;
  while (end > buf && count < max) {
    if (end[-1] == '\n') end--;
    char* line = end;
    while (line > buf && line[-1] != '\n') line--;
    if (end > line) {
      char tmp[LOG_LINE];
      int len = end - line < LOG_LINE - 1 ? end - line : LOG_LINE - 1;
      memcpy(tmp, line, len);
      tmp[len] = 0;
      char* sep = strchr(tmp, LOG_SEP);
      if (sep) *sep = 0;
      char* bar = strchr(tmp, '|');
      times[count] = bar ? strtoul(tmp, NULL, 10) : 0;
      snprintf(texts[count], 48, "%s", bar ? bar + 1 : tmp);
      if (snaps) snprintf(snaps[count], LOG_SNAP_LEN, "%s", sep ? sep + 1 : "");
      count++;
    }
    end = line;
  }
  return count;
}
