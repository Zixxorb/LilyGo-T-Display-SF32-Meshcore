// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Battery history for the Battery page: one sample every 15 min, the last 48 h, kept in
// /sf32_batt (saved every hour and before powering off). With the clock known, the time
// spent powered off shows as a gap. Each sample: percent + USB, the raw battery voltage
// (finer than the percent, for power tests) and the chip temperature.

#define BATT_LOG_FILE     "/sf32_batt"
#define BATT_LOG_MAGIC_V1 0x53464231  // "SFB1": percent only
#define BATT_LOG_MAGIC    0x53464232  // "SFB2": + millivolts, chip temperature
#define BATT_MV_NONE      0           // mv[] code: unknown
#define BATT_TEMP_NONE    (-128)
#define BATT_LOG_N        192         // 48 h
#define BATT_LOG_PERIOD_S (15 * 60)
#define BATT_NO_DATA      0xFF        // a gap (powered off / no sample)
#define BATT_ON_USB       0x80        // bit 7: on USB power; bits 0..6: percent

class BatteryHistory {
  struct {
    uint32_t magic;
    uint32_t last_t;   // RTC time of the newest sample (0: unknown)
    uint16_t head;     // next slot
    uint16_t count;
    uint8_t s[BATT_LOG_N];
    uint8_t mv[BATT_LOG_N];   // (mV - 2900) / 6 + 1, 0: unknown
    int8_t temp[BATT_LOG_N];  // chip deg C, -128: unknown
  } _d;
  unsigned long _next = 60000;  // first sample a minute after boot (voltage settled)
  uint8_t _unsaved = 0;

  void push(uint8_t v, uint16_t mv = 0, int temp = BATT_TEMP_NONE) {
    _d.mv[_d.head] = mv >= 2900 && mv < 2900 + 6 * 254 ? (uint8_t)((mv - 2900) / 6 + 1) : BATT_MV_NONE;
    _d.temp[_d.head] = (int8_t)(temp < -127 ? -128 : (temp > 127 ? 127 : temp));
    _d.s[_d.head] = v;
    _d.head = (_d.head + 1) % BATT_LOG_N;
    if (_d.count < BATT_LOG_N) _d.count++;
  }
  static uint32_t clockNow() {
    uint32_t t = rtc_clock.getCurrentTime();
    return rtc_clock.timeKnown() && t >= 1735689600UL ? t : 0;
  }

public:
  void begin() {
    memset(&_d, 0, sizeof(_d));
    File f = InternalFS.open(BATT_LOG_FILE, "r");
    int n = f ? f.read((uint8_t*)&_d, sizeof(_d)) : 0;
    bool v1 = n >= (int)(12 + BATT_LOG_N) && _d.magic == BATT_LOG_MAGIC_V1;  // the older layout:
    bool ok = (n == (int)sizeof(_d) && _d.magic == BATT_LOG_MAGIC) || v1;    // keep its percents
    if (!ok || _d.head >= BATT_LOG_N || _d.count > BATT_LOG_N) {
      memset(&_d, 0, sizeof(_d));
    }
    if (v1 || !ok) {
      memset(_d.mv, BATT_MV_NONE, sizeof(_d.mv));
      memset(_d.temp, (uint8_t)BATT_TEMP_NONE, sizeof(_d.temp));
    }
    _d.magic = BATT_LOG_MAGIC;
    // time spent off: one gap slot per missed sample (at most the whole graph)
    uint32_t now = clockNow();
    if (now && _d.last_t && now > _d.last_t) {
      uint32_t missed = (now - _d.last_t) / BATT_LOG_PERIOD_S;
      for (uint32_t i = 1; i < missed && i <= BATT_LOG_N; i++) push(BATT_NO_DATA);
    } else if (_d.count) {
      push(BATT_NO_DATA);  // unknown how long: one gap marks the reboot
    }
  }

  bool due() const { return (long)(millis() - _next) >= 0; }
  void loop(uint16_t mv, bool usb) {  // when due()
    _next = millis() + BATT_LOG_PERIOD_S * 1000UL;
    if (mv == 0) return;
    push((uint8_t)sf32ShownPercent(mv) | (usb ? BATT_ON_USB : 0), mv, board.chipTempC());
    _d.last_t = clockNow();
    if (++_unsaved >= 4) save();
  }

  void save() {
    _unsaved = 0;
    File f = InternalFS.open(BATT_LOG_FILE, "w", true);
    if (f) f.write((const uint8_t*)&_d, sizeof(_d));
  }

  int count() const { return _d.count; }
  // i = 0: oldest kept sample, count()-1: newest
  uint8_t at(int i) const { return _d.s[(_d.head + BATT_LOG_N - _d.count + i) % BATT_LOG_N]; }
  uint16_t mvAt(int i) const {  // 0: unknown
    uint8_t c = _d.mv[(_d.head + BATT_LOG_N - _d.count + i) % BATT_LOG_N];
    return c == BATT_MV_NONE ? 0 : 2900 + (c - 1) * 6 + 3;
  }
  int tempAt(int i) const { return _d.temp[(_d.head + BATT_LOG_N - _d.count + i) % BATT_LOG_N]; }

  // % per hour now, from the newest unplugged stretch (up to 12 h): a straight line fitted
  // through the charge worked out from each sample's voltage (finer than whole percents).
  // The first half hour after unplugging is left out (the voltage sags off the charger
  // faster than the battery empties). Needs an hour of samples; 0 = not known yet.
  float drainPerHour() const {
    const int max_n = 12 * 3600 / BATT_LOG_PERIOD_S + 1, skip = 1800 / BATT_LOG_PERIOD_S;
    int end = _d.count;  // samples [start, end) on battery, no gap
    int start = end;
    while (start > 0 && end - start < max_n + skip) {
      uint8_t v = at(start - 1);
      if (v == BATT_NO_DATA || (v & BATT_ON_USB) || mvAt(start - 1) == 0) break;
      start--;
    }
    bool from_usb = start > 0 && at(start - 1) != BATT_NO_DATA && (at(start - 1) & BATT_ON_USB);
    if (from_usb) start += skip;  // just off the charger
    int n = end - start;
    if (n > max_n) { start = end - max_n; n = max_n; }
    if (n < 5) return 0;  // an hour
    // least squares: charge (%) against time (h)
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int k = 0; k < n; k++) {
      float x = k * BATT_LOG_PERIOD_S / 3600.0f, y = sf32BatteryPercentF(mvAt(start + k));
      sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    float den = n * sxx - sx * sx;
    if (den <= 0) return 0;
    float rate = -(n * sxy - sx * sy) / den;
    return rate > 0.1f ? rate : 0;
  }

};

static BatteryHistory batt_history;

// The graph: 48 h across, newest at the right; plugged-in stretches in the "good" colour.
static void drawBatteryGraph(DisplayDriver& d, int x, int y, int w, int h) {
  d.setColor(THEME_DIM);
  d.fillRect(x, y, w, 1);              // 100 %
  d.fillRect(x, y + h / 2, w, 1);      // 50 %
  d.fillRect(x, y + h, w, 1);          // 0 %
  for (int k = 1; k < 4; k++) d.fillRect(x + w * k / 4, y + h + 1, 1, 2);  // 12 h ticks
  int n = batt_history.count();
  for (int c = 0; c < w; c++) {
    // the slot this column shows (slots fill from the right)
    int slot = (c * BATT_LOG_N + BATT_LOG_N / 2) / w;
    int i = slot - (BATT_LOG_N - n);
    if (i < 0) continue;
    uint8_t v = batt_history.at(i);
    if (v == BATT_NO_DATA) continue;
    int pct = v & 0x7F;
    int bh = (pct * h + 50) / 100;
    if (bh < 1) bh = 1;
    d.setColor((v & BATT_ON_USB) ? THEME_GOOD : (pct <= 15 ? UIColor::warning_txt : THEME_ACCENT));
    d.fillRect(x + c, y + h - bh, 1, bh);
  }
  d.setTextSize(1);
  d.setColor(UIColor::secondary_txt);
  d.setCursor(x, y + h + 4);
  d.print("48h");
  d.drawTextCentered(x + w / 2, y + h + 4, "24h");
  d.drawTextRightAlign(x + w, y + h + 4, "now");
}
