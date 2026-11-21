// Based on MeshCore's variants/lilygo_tdeck/TDeckBoard.cpp (MIT, see TDeckBoard.h).
#include "TDeckBoard.h"
#include "TDeckContacts.h"
#include <SPIFFS.h>
#include "SF32RTCClock.h"
#include "TDeckDevices.h"
#include "TDeckKeys.h"

void TDeckBoard::begin() {
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

void TDeckBoard::loop() {
  if (!_boot_done) {  // the filesystem is mounted by now (MeshCore's setup())
    _boot_done = true;
    if (_rtc) _rtc->afterSetup();
  }
  tdeck_touch_poll();
#ifdef TDECK_SHOT
  tdeck_shot_poll();
#endif
}

// SF32Display calls this before using the panel (the SF32's display controller runs from the
// CPU clock). The T-Deck's doesn't: nothing to do.
extern "C" void sf32_cpu_full_speed() {}

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
