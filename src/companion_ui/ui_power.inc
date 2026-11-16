// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Battery warnings, burn-in care, motion, night dim, always-on display, back gesture.

// ---- SF32: battery warnings and burn-in care (called from loop()) ----

#define DIM_AFTER_MS       60000UL   // "always on while on USB": dim after 1 min idle
#define DIM_BRIGHTNESS     10
#define SHIFT_PERIOD_MS    180000UL  // move the layout every 3 min

// Display > Rotation: fixed (normal, turned right / left, upside down) or auto (the way it's
// held, from the accelerometer; lying flat keeps the last). The panel turns the picture
// and the touch coordinates turn with it.
void UITask::applyRotation() {
  static const int8_t FIXED[5] = {0, 2, -1, 3, 1};  // by stored value: quarter turns clockwise (Right = 3, Left = 1: checked on the board)
  SF32Display* d = (SF32Display*)_display;
  int rot = FIXED[_ui_prefs.rotation % 5];
  if (rot < 0) {  // auto
    int o = motion.orientation();
    rot = o >= 0 ? o : d->rotation();
  }
  if (rot == d->rotation()) return;
  d->setRotation(rot);
  touch_input.setRotation(rot);
  _next_refresh = 0;
}

void UITask::powerCare() {
  unsigned long now = millis();

  // motion: pick up to wake; laid face down turns the screen off
  motion.poll();
  glassLightLoop();
  {  // the pet: caught up when the screen turns on, saved when it goes dark (nothing in between)
    static bool pet_on = false;
    bool on = _display && _display->isOn();
    if (on != pet_on) { pet_on = on; pet.onScreen(on); }
  }
  if (_ui_prefs.rotation == 2 && _display && _display->isOn()) applyRotation();
  bool picked = motion.takePickup();
  if (picked && _aod && _ui_prefs.lift_wake) exitAOD();
  if (picked && _ui_prefs.lift_wake && _display && !_display->isOn() &&
      !(_ui_prefs.flip_quiet && motion.faceDown())) {
    resumeOrReset();
    _display->turnOn();
    _auto_off = now + autoOffMillis();
    _next_refresh = 0;
  }
  bool down = motion.faceDown();
  if (down && !_was_face_down && _ui_prefs.flip_quiet && _display && _display->isOn()) {
    if (_aod) {  // off, not the always-on clock
      _aod = false;
      setCurrScreen(_aod_return ? _aod_return : home);
    }
    _display->turnOff();
    _display_off_at = now;
  }
  _was_face_down = down;

  // battery warnings: once per discharge, re-armed when on USB power
  if ((long)(now - _next_batt_warn_check) >= 0) {
    _next_batt_warn_check = now + 10000;
    uint16_t mv = getBattMilliVolts();
    sf32ShownPercent(mv);  // (keeps the charging estimate moving while the screen is off)
    if (board.isExternalPowered() || mv == 0) {
      _batt_warn_level = 0;
      _lp_declined = false;  // re-armed for the next discharge
      if (_ui_prefs.low_power == 2 && board.isExternalPowered()) setLowPower(0);  // auto: off on USB
    } else {
      int pct = sf32ShownPercent(mv);
      // low power mode by itself at the chosen level (once per discharge; not if turned off by hand)
      int lp_at = LP_AUTO_LEVELS[_ui_prefs.lp_auto % NUM_LP_AUTO];
      if (lp_at && !lowPower() && !_lp_declined && pct <= lp_at) {
        setLowPower(2);
        _lp_note_pending = true;  // told next time the screen is on
      }
      int warn_pct = BATT_WARN_LEVELS[_ui_prefs.batt_warn % NUM_BATT_WARN];
      // the second alert (it wakes the screen) well before the power-off at 3550 mV (~9.5 %);
      // with the first set at 10 %, that one is the waking alert
      int crit_pct = warn_pct > 12 ? 12 : warn_pct;
      if (warn_pct == 0) {
        // alerts off (the board still powers itself off when the battery is empty)
      } else if (pct <= crit_pct && _batt_warn_level < 2) {
        _batt_warn_level = 2;
        if (_aod) exitAOD();  // important enough to wake the screen, fully
        if (_display && !_display->isOn()) {
          resumeOrReset();
          _display->turnOn();
          _auto_off = now + autoOffMillis();
        }
        char tmp[40];
        sprintf(tmp, "Battery %d%%: charge soon", pct);
        showAlert(tmp, 4000);
        board.logEvent(tmp);
        _next_refresh = 0;
      } else if (pct <= warn_pct && _batt_warn_level < 1) {
        _batt_warn_level = 1;
        _batt_warn_pending = true;  // shown next time the screen is on
        char tmp[40];
        sprintf(tmp, "Battery low: %d%%", pct);
        board.logEvent(tmp);
      }
    }
  }
  if (_lp_note_pending && _display && _display->isOn() && !_batt_warn_pending) {
    _lp_note_pending = false;
    showAlert("Low power mode on", 3000);
    _next_refresh = 0;
  }
  if (_batt_warn_pending && _display && _display->isOn()) {
    _batt_warn_pending = false;
    char tmp[40];
    sprintf(tmp, "Battery low: %d%%", sf32ShownPercent(getBattMilliVolts()));
    showAlert(tmp, 3000);
    _next_refresh = 0;
  }

  // night dim: re-check the hour every 10 s
  if ((long)(now - _next_night_check) >= 0) {
    _next_night_check = now + 10000;
    applyBrightness();
  }

  // burn-in: move the whole layout a little every few minutes
  static const int8_t SHIFTS[][2] = {{0, 0}, {3, 1}, {1, -3}, {-3, -1}, {-1, 3}, {2, -2}, {-2, 2}};
  int k = (now / SHIFT_PERIOD_MS) % (sizeof(SHIFTS) / sizeof(SHIFTS[0]));
  ((SF32Display*)_display)->setPixelShift(SHIFTS[k][0], SHIFTS[k][1]);

  // "never (USB)" and the cable comes out: the screen goes off a minute later (its deadline
  // was a day away while on USB)
  static bool was_usb = false;
  bool usb_now = board.isExternalPowered();
  if (was_usb && !usb_now && TIMEOUT_SECS[_ui_prefs.timeout_idx] == 0 &&
      (long)(_auto_off - (now + autoOffMillis())) > 0) {
    _auto_off = now + autoOffMillis();
  }
  // and plugged in with the screen on: it stays on from now (it was counting down a minute)
  if (!was_usb && usb_now && TIMEOUT_SECS[_ui_prefs.timeout_idx] == 0 && _display && _display->isOn()) {
    _auto_off = now + autoOffMillis();
  }
  was_usb = usb_now;

  // "always on while on USB": after a minute without input, dim and show the clock
  if (_display && _display->isOn()) {
    bool always_on = TIMEOUT_SECS[_ui_prefs.timeout_idx] == 0 && board.isExternalPowered();
    // every input pushes _auto_off forward: note when it last moved
    if (_auto_off != _seen_auto_off) {
      _seen_auto_off = _auto_off;
      _last_activity = now;
    }
    unsigned long idle = now - _last_activity;
    SF32Display* d = (SF32Display*)_display;
    if (always_on && idle > DIM_AFTER_MS && !_dimmed) {
      _dimmed = true;
      applyBrightness();
      if (curr == home || curr == menu || curr == about_screen || curr == theme_screen) {
        ((HomeScreen*)home)->showClock();
        _menu_return = NULL;
        setCurrScreen(home);
      }
    } else if (_dimmed && (!always_on || idle < DIM_AFTER_MS)) {
      _dimmed = false;
      applyBrightness();
    }
  }
}

// ---- SF32: night dim ----

// inside the night window right now (local time; false while the time isn't known)
// night hours (Settings > Night mode: From / Until), whatever is switched on for them
bool UITask::nightHours() const {
  if (!rtc_clock.timeKnown()) return false;
  uint32_t utc = rtc_clock.getCurrentTime();
  if (utc < 1735689600UL) return false;
  time_t lt = (time_t)(utc + localOffsetSecs(utc));
  struct tm tm;
  gmtime_r(&lt, &tm);
  int h = tm.tm_hour, a = _ui_prefs.night_from, b = _ui_prefs.night_until;
  if (a == b) return false;
  return a < b ? (h >= a && h < b) : (h >= a || h < b);  // e.g. 22..7 wraps midnight
}
bool UITask::nightNow() const { return _ui_prefs.night_on && nightHours(); }  // night dim
bool UITask::dndNow() const { return _ui_prefs.night_dnd && nightHours(); }   // do not disturb

// the brightness to use now: the setting, lowered by night dim and the always-on dim
// ---- Low power mode ----
// Turns off what isn't needed when only the mesh matters (say, someone lost): tap and swipe
// sounds (message chimes stay), the auto-reply bot, the motion sensor, the always-on clock and
// the Glass style; Bluetooth advertises every 2 s while no phone is connected and asks a connected
// phone for a slow rhythm when it's quiet; the radio uses power-saving receive gain; the clock page is
// a minimal one. The radio keeps listening and Bluetooth stays available. On by hand (Power
// menu, Settings > Low power) or by itself at a chosen battery level, in which case USB power
// turns it back off. The saved settings are never changed.
// The pet's chirps: pitch from its look, only if Sounds is on (not at night with do not
// disturb, nor in low power mode).
void UITask::petSound(int kind) {
  if (!_ui_prefs.pet_sound || lowPower() || dndNow() || !_ui_prefs.sound) return;
  uint16_t base = 700 + (pet.seed() % 6) * 90;
  if (kind == 2) sound.tone(base + 200, 60, tapVolume(), 2);
  else sound.tone(base, 70, tapVolume());
}

void UITask::applyMotion() { motion.setActive(motionWanted() || _glass_light_live); }

// Glass's light follows the board's tilt while the screen is on (Theme > Tilt light): the
// accelerometer runs only then (unless a motion setting needs it anyway); smoothed, and a
// redraw only when the glint has moved a few pixels (at most ~10 a second, the sensor's rate).
void UITask::glassLightLoop() {
  static float lx = 150, ly = 70, bx = 0, by = 0;
  static bool based = false;
  SF32Display* d = (SF32Display*)_display;
  bool want = _ui_prefs.glass_light && _ui_prefs.glass && !lowPower() && d && d->isOn() && !_aod;
  if (want != _glass_light_live) {
    _glass_light_live = want;
    based = false;  // a fresh baseline each time it starts
    lx = 150; ly = 70;
    applyMotion();
    if (!want) d->setGlassLight(false, 0, 0);
  }
  if (!want || !motion.ready()) return;
  static unsigned long last = 0;
  if (millis() - last < 100) return;
  last = millis();
  // The light moves with changes in tilt from how it's being held (a slow-following baseline),
  // so it responds the same held upright or lying flat, and drifts back to its resting place
  // (upper left, where the glass was always lit) when held still.
  if (!based) { bx = motion.ax; by = motion.ay; based = true; }
  bx += (motion.ax - bx) * 0.03f;  // ~3 s to settle
  by += (motion.ay - by) * 0.03f;
  float tx = 150 - (motion.ax - bx) * 550, ty = 70 - (motion.ay - by) * 550;  // (against the tilt: user's feel)
  if (tx < -60) tx = -60; if (tx > 540) tx = 540;
  if (ty < -60) ty = -60; if (ty > 540) ty = 540;
  lx += (tx - lx) * 0.5f;
  ly += (ty - ly) * 0.5f;
  static int shown_x = -1000, shown_y = -1000;
  int x = (int)lx, y = (int)ly;
  if (abs(x - shown_x) < 6 && abs(y - shown_y) < 6) return;
  shown_x = x;
  shown_y = y;
  d->setGlassLight(true, x, y);
  _next_refresh = 0;  // redraw with it
}

void UITask::setLowPower(uint8_t mode) {
  if (mode == _ui_prefs.low_power) return;
  bool was = lowPower();
  _ui_prefs.low_power = mode;
  saveUIPrefs();
  if (was == lowPower()) return;  // (by hand <-> auto: nothing else changes)
  applyMotion();
  bluetooth_interface.setSlowAdvertising(lowPower());
  bluetooth_interface.setSlowLink(lowPower());
  radio_driver.setLowPowerGain(lowPower());
  applyStyle();  // no Glass (it lights the whole panel)
  if (lowPower() && _aod) {  // the always-on clock: off now
    exitAOD();
    _display->turnOff();
    _display_off_at = millis();
  }
  board.logEvent(lowPower() ? (mode == 2 ? "Low power mode on (battery)" : "Low power mode on") : "Low power mode off");
  _next_refresh = 0;
}

void UITask::applyBrightness() {
  int level = BRIGHTNESS_STEPS[_ui_prefs.brightness_idx];
  if (nightNow() && NIGHT_LEVELS[_ui_prefs.night_level % NUM_NIGHT_LEVELS] < level)
    level = NIGHT_LEVELS[_ui_prefs.night_level % NUM_NIGHT_LEVELS];
  if (_dimmed && DIM_BRIGHTNESS < level) level = DIM_BRIGHTNESS;
  if (_aod && AOD_BRIGHTNESS < level) level = AOD_BRIGHTNESS;
  if (level != _applied_brightness) {
    _applied_brightness = level;
    ((SF32Display*)_display)->setBrightness(level);
  }
}

void UITask::nightChanged() {
  saveUIPrefs();
  applyBrightness();
}
void UITask::openHour(bool until) {
  ((HourScreen*)hour_screen)->open(until ? &_ui_prefs.night_until : &_ui_prefs.night_from, until ? "Until" : "From");
  _hour_menu_return = _menu_return;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(hour_screen);
}
void UITask::closeHour() {  // back to the Night mode menu, rebuilt
  setCurrScreen(home);
  openMenu(MENU_NIGHT);
  _menu_return = _hour_menu_return;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
}

void UITask::enterAOD() {
  _aod = true;
  _aod_return = curr;
  _display_off_at = millis();  // the 10 s "carry on where it was" rule counts from here
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_NONE);
  setCurrScreen(aod_screen);
  applyBrightness();
}

void UITask::exitAOD() {
  if (!_aod) return;
  _aod = false;
  curr = _aod_return ? _aod_return : home;
  resumeOrReset();  // within 10 s: where it was; otherwise the clock page
  setCurrScreen(curr);
  applyBrightness();
  _auto_off = millis() + autoOffMillis();
  _next_refresh = 0;
}

// Called by the board just before it powers off on an empty battery.
extern "C" void sf32_before_poweroff(int reason) {
  if (reason != 1) return;
  batt_history.save();
  pet.save();
  display.turnOn();
  display.startFrame();
  display.setSquareLayout(true);
  display.setTextSize(2);
  display.setColor(UIColor::warning_txt);
  display.drawTextCentered(64, 44, "Battery empty");
  display.setTextSize(1);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(64, 66, "Powering off to protect it.");
  display.drawTextCentered(64, 78, "Charge, then press D to start.");
  display.endFrame();
  delay(4000);
}

// SF32: back one level (swipe right)
void UITask::goBack() {
  SF32Display* d = (SF32Display*)_display;
  if (curr == menu) {
    MenuKind parent = parentOf(_menu_kind);
    // up a level; from the top of a stack (a page menu, Settings) close it. Battery opened
    // from the Battery page's menu goes back to that menu.
    bool to_page_menu = parent == MENU_PAGE && (menuParent(_menu_kind) == MENU_SETTINGS || _menu_kind == MENU_POWER_OFF);
    if (parent != MENU_PAGE || to_page_menu) {
      openMenu(parent);
      d->setTransition(SF32Display::TRANSITION_RIGHT);
    }
    else { closeMenu(); d->setTransition(SF32Display::TRANSITION_RIGHT); }
  } else if (curr == sos_screen) return;  // only its button leaves it
  else if (curr == msg_preview) { gotoHomeScreen(); d->setTransition(SF32Display::TRANSITION_RIGHT); }  // messages stay unread
  else if (curr == qr_screen) closeQr();
  else if (curr == scan_screen) closeScan();
  else if (curr == about_screen) closeAbout();
  else if (curr == logs_screen) closeLogs();
  else if (curr == theme_screen) closeTheme();
  else if (curr == tz_screen) ((TimezoneScreen*)tz_screen)->done();
  else if (curr == radio_field) closeRadioField();
  else if (curr == radio_review) closeRadioReview();
  else if (curr == radio_edit) leaveRadioEditor();
  _auto_off = millis() + autoOffMillis();
  _next_refresh = 0;
}
