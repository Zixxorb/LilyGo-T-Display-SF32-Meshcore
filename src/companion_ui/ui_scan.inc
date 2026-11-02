// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Band scan (Radio page > swipe up > Band scan): the noise level across the US band,
// 902-928 MHz in 250 kHz steps, swept over and over. A few frequencies per frame, then
// the radio goes back to our channel and listens again, so MeshCore keeps receiving (and
// never sends off-channel); skipped while a packet is being received or sent.
// Narrow peaks: a device's carrier or a leaking clock; a broad hump: noisy electronics.

#define SCAN_FROM_KHZ 902000UL
#define SCAN_STEP_KHZ 250UL
#define SCAN_POINTS   105            // 902.00 .. 928.00 MHz
#define SCAN_PER_FRAME 4             // ~25 ms each
#define SCAN_LO_DBM   -120           // graph bottom
#define SCAN_HI_DBM   -60            // graph top

class ScanScreen : public UIScreen {
  UITask* _task;
  int8_t _live[SCAN_POINTS], _hold[SCAN_POINTS];
  int _next = 0, _sweeps = 0;

public:
  ScanScreen(UITask* task) : _task(task) { reset(); }

  void reset() {
    for (int i = 0; i < SCAN_POINTS; i++) { _live[i] = 0; _hold[i] = -128; }
    _next = 0;
    _sweeps = 0;
  }

  // a few more frequencies; false if the radio was busy (try next frame)
  bool step() {
    if (!radio_driver.isInRecvMode() || radio_driver.isReceiving()) return false;
    uint32_t home = radio_driver.frequencyHz();
    for (int k = 0; k < SCAN_PER_FRAME; k++) {
      radio_driver.setFrequencyHz((SCAN_FROM_KHZ + _next * SCAN_STEP_KHZ) * 1000UL);
      int v = radio_driver.sampleNoise(8);
      v = v < -127 ? -127 : (v > 0 ? 0 : v);
      _live[_next] = (int8_t)v;
      if (v > _hold[_next]) _hold[_next] = (int8_t)v;
      if (++_next >= SCAN_POINTS) { _next = 0; _sweeps++; }
    }
    radio_driver.setFrequencyHz(home);
    radio_driver.sampleNoise(1);  // back to listening on our channel
    return true;
  }

  static ColorVal levelColor(int dbm) {
    return dbm >= -85 ? THEME_RED : dbm >= -100 ? UIColor::warning_txt : THEME_GOOD;
  }

  int render(DisplayDriver& d) override {
    step();
    themeHeader(d, "Band scan", _task->getBattMilliVolts());
    const int gx = 2, gy = 30, gw = SCAN_POINTS, gh = 60;  // one logical px per 250 kHz
    // grid: -60 / -90 / -120 dBm
    d.setColor(THEME_DIM);
    d.fillRect(gx, gy, gw, 1);
    d.fillRect(gx, gy + gh / 2, gw, 1);
    d.fillRect(gx, gy + gh, gw, 1);
    for (int i = 0; i < SCAN_POINTS; i++) {
      if (_hold[i] > -128) {  // max hold: a faint tick
        int hh = (_hold[i] - SCAN_LO_DBM) * gh / (SCAN_HI_DBM - SCAN_LO_DBM);
        if (hh > 0 && hh <= gh) { d.setColor(THEME_DIM); d.fillRect(gx + i, gy + gh - hh, 1, 1); }
      }
      if (_live[i] == 0) continue;  // not measured yet
      int h = (_live[i] - SCAN_LO_DBM) * gh / (SCAN_HI_DBM - SCAN_LO_DBM);
      if (h < 1) h = 1;
      if (h > gh) h = gh;
      d.setColor(levelColor(_live[i]));
      d.fillRect(gx + i, gy + gh - h, 1, h);
    }
    // our channel
    uint32_t ch_khz = radio_driver.frequencyHz() / 1000;
    int chx = -1;
    if (ch_khz >= SCAN_FROM_KHZ && ch_khz < SCAN_FROM_KHZ + SCAN_POINTS * SCAN_STEP_KHZ) {
      chx = (int)((ch_khz - SCAN_FROM_KHZ) / SCAN_STEP_KHZ);
      d.setColor(THEME_ACCENT);
      d.fillRect(gx + chx, gy - 3, 1, 2);
      d.fillRect(gx + chx, gy + gh + 1, 1, 2);
    }
    // scale and readouts
    char tmp[40];
    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    d.setCursor(gx + gw + 2, gy - 3); d.print("-60");
    d.setCursor(gx + gw + 2, gy + gh - 5); d.print("-120");
    d.setCursor(gx, gy + gh + 4); d.print("902");
    d.drawTextCentered(gx + gw / 2, gy + gh + 4, "915");
    d.drawTextRightAlign(gx + gw, gy + gh + 4, "928 MHz");
    int peak = 0;
    for (int i = 1; i < SCAN_POINTS; i++) if (_live[i] != 0 && (_live[peak] == 0 || _live[i] > _live[peak])) peak = i;
    if (_live[peak] != 0) {
      unsigned long khz = SCAN_FROM_KHZ + peak * SCAN_STEP_KHZ;
      sprintf(tmp, "peak %lu.%02lu MHz", khz / 1000, (khz % 1000) / 10);
      d.setColor(UIColor::secondary_txt);
      d.setCursor(4, 104); d.print(tmp);
      sprintf(tmp, "%d dBm", _live[peak]);
      d.setColor(levelColor(_live[peak]));
      d.drawTextRightAlign(122, 104, tmp);
    }
    if (chx >= 0 && _live[chx] != 0) {
      sprintf(tmp, "our channel");
      d.setColor(THEME_ACCENT);
      d.setCursor(4, 114); d.print(tmp);
      sprintf(tmp, "%d dBm", _live[chx]);
      d.setColor(levelColor(_live[chx]));
      d.drawTextRightAlign(122, 114, tmp);
    }
    return 30;  // keep sweeping
  }

  bool handleInput(char c) override {  // button A: back
    if (c == 0) return false;
    _task->closeScan();
    return true;
  }
};

void UITask::openScan() {
  if (!scan_screen) scan_screen = new ScanScreen(this);
  ((ScanScreen*)scan_screen)->reset();
  _scan_return = _menu_return ? _menu_return : home;
  _menu_return = NULL;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(scan_screen);
}

void UITask::closeScan() {
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  setCurrScreen(_scan_return ? _scan_return : home);
  _scan_return = NULL;
}
