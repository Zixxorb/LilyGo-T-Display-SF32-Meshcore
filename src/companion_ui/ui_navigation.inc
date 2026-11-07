// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Opening/closing menus, menu actions, touch routing (taps, swipes, drags).

// ---- SF32: menus ----

void UITask::openMenu(MenuKind kind) {
  MenuScreen* m = (MenuScreen*)menu;
  ((SF32Display*)_display)->cancelListFrame();  // the next frame is a whole new one
  bool same = curr == menu && kind == _menu_kind;
  int keep_sel = same ? m->selected() : 0;
  if (!same) m->resetScroll();
  SF32Display* d = (SF32Display*)_display;
  if (curr != menu) {
    _menu_return = curr;
    // swipe down: the power menu comes down from the top; swipe up: page menu from below
    _menu_open_dir = kind == MENU_POWER ? SF32Display::TRANSITION_DOWN : SF32Display::TRANSITION_UP;
    // the power menu slides down from the top; the others' rows glide in, one after another
    if (kind == MENU_POWER) d->setTransition((SF32Display::Transition)_menu_open_dir);
    else m->startEnter();
  } else if (kind != _menu_kind) {
    m->startEnter();
  }
  _menu_kind = kind;
  m->setHandle(kind == MENU_POWER);  // came down from the top: swipe up to close
  char tmp[16];
  if (kind == MENU_POWER) {
    m->begin("Power");
    m->add("Flashlight", ACT_FLASHLIGHT);
    m->add("Screen off", ACT_SCREEN_OFF);
    m->add("Reboot", ACT_REBOOT);
    m->add("Power off", ACT_POWER_OFF);
    m->add("Send SOS", ACT_SOS);
  } else if (kind == MENU_SETTINGS) {
    // what you see (screen, pages, clock), what you hear and when (sound, night), sensors,
    // the radio side (Bluetooth, SOS), then the system (power use, logs, power off)
    m->begin("Settings");
    m->add("Theme", ACT_THEME_MENU);
    m->add("Display", ACT_DISPLAY_MENU);
    m->add("Pages", ACT_PAGES_MENU);  // which pages to show
    m->add("Clock", ACT_TIME_MENU);
    m->add("Sound", ACT_SOUND_MENU);
    m->add("Night mode", ACT_NIGHT_MENU);  // hours, dim, do not disturb
    m->add("Motion", ACT_MOTION_MENU);
    m->add("Bluetooth", ACT_SET_BT, isBluetoothEnabled() ? "On" : "Off");
    m->add("USB app", ACT_USB_APP, _ui_prefs.usb_app ? "On" : "Off");  // the app protocol over USB
    m->add("Auto advert", ACT_ADVERTS_MENU);  // scheduled adverts
    {
      ChannelDetails ch;
      m->add("SOS to", ACT_SOS_CHAN, sosChannel(_ui_prefs.sos_chan, ch) >= 0 ? ch.name : "none");
    }
    if (board.sleepSupported()) m->add("Low power", ACT_POWER_SAVE, _ui_prefs.power_save ? "On" : "Off");
    m->add("Logs", ACT_LOGS);
    m->add("Power", ACT_POWER_MENU);  // screen off / reboot / power off
  } else if (kind == MENU_THEME) {
    m->begin("Theme");
    m->add("Style", ACT_THEME_STYLE, _ui_prefs.glass ? "Glass" : "Normal");
    m->add("Color", ACT_THEME, THEMES[themeIndex()].name);  // each style keeps its own
    if (_ui_prefs.glass) {
      static const char* const BG_NAMES[] = {"Aurora", "Dunes", "Blend", "None"};
      m->add("Backdrop", ACT_THEME_BG, BG_NAMES[_ui_prefs.glass_bg % 4]);
    }
    m->add("Back", ACT_SETTINGS);
  } else if (kind == MENU_DISPLAY) {
    m->begin("Display");
    // how it looks, then how the screen behaves
    m->add("Clock face", ACT_CLOCK_FACE, CLOCK_FACE_NAMES[_ui_prefs.clock_face % NUM_CLOCK_FACES]);
    sprintf(tmp, "%d%%", BRIGHTNESS_STEPS[_ui_prefs.brightness_idx]);
    m->add("Brightness", ACT_SET_BRIGHTNESS, tmp);
    timeoutLabel(tmp, _ui_prefs.timeout_idx);
    m->add("Timeout", ACT_SET_TIMEOUT, tmp);
    m->add("Always-on", ACT_SET_AOD, _ui_prefs.aod ? "On" : "Off");
    static const char* const ROT_NAMES[] = {"Normal", "Upside down", "Auto", "Right", "Left"};  // by stored value
    m->add("Rotation", ACT_ROTATION, ROT_NAMES[_ui_prefs.rotation % 5]);
    m->add("Back", ACT_SETTINGS);
    m->setSelected(keep_sel);
    setCurrScreen(menu);
    return;
  } else if (kind == MENU_PAGES) {
    m->begin("Pages");  // swipe through only the shown ones (the clock always is)
    for (int p = 1; p < HomeScreen::pageCount(); p++)
      m->add(HomeScreen::pageName(p), ACT_PAGE_TOGGLE + p, (_ui_prefs.hidden_pages & (1u << p)) ? "Hide" : "Show");
    m->add("Back", ACT_SETTINGS);
    m->setSelected(keep_sel);
    setCurrScreen(menu);
    return;
  } else if (kind == MENU_ADVERTS) {
    m->begin("Auto advert");  // like repeaters: nearby (zero hop) and to the whole mesh (flood)
    advHoursLabel(tmp, ADV_NEARBY_HOURS[_ui_prefs.adv_nearby % NUM_ADV_NEARBY]);
    m->add("Nearby", ACT_ADV_NEARBY, tmp);
    advHoursLabel(tmp, ADV_MESH_HOURS[_ui_prefs.adv_mesh % NUM_ADV_MESH]);
    m->add("Mesh", ACT_ADV_MESH, tmp);
    m->add("Back", ACT_SETTINGS);
    m->setSelected(keep_sel);
    setCurrScreen(menu);
    return;
  } else if (kind == MENU_MOTION) {
    m->begin("Motion");
    m->add("Lift to wake", ACT_SET_LIFT, _ui_prefs.lift_wake ? "On" : "Off");
    m->add("Flip to quiet", ACT_SET_FLIP, _ui_prefs.flip_quiet ? "On" : "Off");
    m->add("Back", ACT_SETTINGS);
    m->setSelected(keep_sel);
    setCurrScreen(menu);
    return;
  } else if (kind == MENU_NIGHT) {
    m->begin("Night mode");  // the hours, then what happens in them: dim (and how much), do not disturb
    fmtHour(tmp, _ui_prefs.night_from, _ui_prefs.clock24);
    m->add("From", ACT_NIGHT_FROM, tmp);
    fmtHour(tmp, _ui_prefs.night_until, _ui_prefs.clock24);
    m->add("Until", ACT_NIGHT_UNTIL, tmp);
    m->add("Dim", ACT_NIGHT_TOGGLE, _ui_prefs.night_on ? "On" : "Off");
    sprintf(tmp, "%d%%", NIGHT_LEVELS[_ui_prefs.night_level % NUM_NIGHT_LEVELS]);
    m->add("Dim to", ACT_NIGHT_LEVEL, tmp);
    m->add("DND", ACT_NIGHT_DND, _ui_prefs.night_dnd ? "On" : "Off");  // do not disturb
    m->add("Back", ACT_SETTINGS);
    m->setSelected(keep_sel);
    setCurrScreen(menu);
    return;
  } else if (kind == MENU_SOUND) {
    static const char* const SOUND_NAMES[] = {"Off", "Low", "High", "Medium"};  // by stored value
    static const char* const TAP_NAMES[] = {"Off", "Click", "Pop", "Tick"};
    static const char* const ALERT_NAMES[] = {"All", "Direct", "Off"};
    m->begin("Sound");
    m->add("Volume", ACT_SET_SOUND, SOUND_NAMES[_ui_prefs.sound % 4]);
    m->add("Alerts", ACT_SET_ALERTS, ALERT_NAMES[_ui_prefs.alerts % 3]);  // which messages chime
    m->add("Tap sound", ACT_SET_TOUCH_SND, TAP_NAMES[_ui_prefs.touch_snd % 4]);
    m->add("Back", ACT_SETTINGS);
    m->setSelected(keep_sel);
    setCurrScreen(menu);
    return;
  } else if (kind == MENU_TIME) {
    m->begin("Clock");
    int tz = _node_prefs->tz_offset;
    if (tz == 0) strcpy(tmp, "UTC"); else sprintf(tmp, "UTC%+d", tz);
    m->add("Time zone", ACT_SET_TZ, tmp);
    m->add("DST", ACT_SET_DST, DST_NAMES[_ui_prefs.dst_rule]);  // daylight saving
    m->add("Format", ACT_SET_CLOCK, _ui_prefs.clock24 ? "24 hour" : "12 hour");
    m->add("Mesh time", ACT_SET_MESH_TIME, _ui_prefs.mesh_time ? "On" : "Off");  // ui_meshtime.inc
    m->add("Reset clock", ACT_CLOCK_RESET);  // a wrong time ahead can't be set back otherwise
    m->add("Back", ACT_SETTINGS);
    m->setSelected(keep_sel);
    setCurrScreen(menu);
    return;
  } else {  // MENU_PAGE
    if (_menu_return == msg_preview) {
      m->begin("Message");
      m->add("Mark all read", ACT_MSG_CLEAR);
    } else {
      ((HomeScreen*)home)->buildContextMenu(*m);
    }
    m->add("Settings", ACT_SETTINGS);
    m->add("About", ACT_ABOUT);
  }
  if (kind != MENU_POWER) m->add("Close", ACT_CLOSE);  // the power menu: swipe up closes it
  m->setSelected(keep_sel);
  setCurrScreen(menu);
}

void UITask::closeMenu() {
  // leave the way it came in
  bool from_top = _menu_kind == MENU_POWER || _menu_open_dir == SF32Display::TRANSITION_DOWN;
  ((SF32Display*)_display)->setTransition(from_top ? SF32Display::TRANSITION_UP : SF32Display::TRANSITION_DOWN);
  UIScreen* back = _menu_return ? _menu_return : home;
  if (back == msg_preview && getUnreadCount() == 0) back = home;
  _menu_return = NULL;
  setCurrScreen(back);
}

void UITask::screenOff() {
  if (_display) _display->turnOff();
  ((HomeScreen*)home)->showClock();  // wake up to the clock
  _menu_return = NULL;
  setCurrScreen(home);
}

void UITask::menuAction(uint8_t action) {
  HomeScreen* h = (HomeScreen*)home;
  if (action >= ACT_PAGE_TOGGLE && action < ACT_PAGE_TOGGLE + HomeScreen::pageCount()) {  // Settings > Pages
    _ui_prefs.hidden_pages ^= (uint16_t)(1u << (action - ACT_PAGE_TOGGLE));
    saveUIPrefs();
    openMenu(MENU_PAGES);
    return;
  }
  switch (action) {
    case ACT_CLOSE: closeMenu(); break;
    case ACT_SCREEN_OFF: screenOff(); break;
#ifdef SF32_UI_TEST  // scripted test builds (SF32_UI_DEMO): never power off / reboot by a stray tap
    case ACT_REBOOT: case ACT_POWER_OFF: showAlert("(test build: skipped)", 1500); break;
#else
    case ACT_REBOOT: shutdown(true); break;
    case ACT_POWER_OFF: board.setPowerOffReason("menu"); shutdown(false); break;
#endif
    case ACT_SETTINGS: openMenu(MENU_SETTINGS); break;
    case ACT_READ_MSGS: _menu_return = NULL; showMessages(); break;
    case ACT_GOTO_TZ: openTimezone(); break;
    case ACT_PAGE_PRIMARY: closeMenu(); h->pagePrimary(); break;
    case ACT_ADVERT:
      closeMenu();
      notify(UIEventType::ack);
      showAlert(sendAdvert(false) ? "Advert sent nearby" : "Advert failed..", 1000);
      break;
    case ACT_ADVERT_MESH:
      closeMenu();
      notify(UIEventType::ack);
      showAlert(sendAdvert(true) ? "Advert sent to mesh" : "Advert failed..", 1000);
      break;
    case ACT_ADVERTS_MENU: openMenu(MENU_ADVERTS); break;
    case ACT_ROTATION: {  // Normal -> Auto -> Right -> Upside down -> Left -> Normal
      static const uint8_t NEXT[5] = {2, 4, 3, 1, 0};  // by stored value
      _ui_prefs.rotation = NEXT[_ui_prefs.rotation % 5];
      saveUIPrefs();
      motion.setActive(_ui_prefs.lift_wake || _ui_prefs.flip_quiet || _ui_prefs.rotation == 2);
      applyRotation();
      openMenu(_menu_kind);
      break;
    }
    case ACT_USB_APP:
      _ui_prefs.usb_app = !_ui_prefs.usb_app;
      saveUIPrefs();
      if (_ui_prefs.usb_app) usb_app_interface.enable(); else usb_app_interface.disable();
      openMenu(_menu_kind);
      break;
    case ACT_ADV_PAGE_MODE:  // the Advert page's long press: nearby / whole mesh
      _ui_prefs.adv_page_mesh = !_ui_prefs.adv_page_mesh;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_ADV_NEARBY:
      _ui_prefs.adv_nearby = (_ui_prefs.adv_nearby + 1) % NUM_ADV_NEARBY;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_ADV_MESH:
      _ui_prefs.adv_mesh = (_ui_prefs.adv_mesh + 1) % NUM_ADV_MESH;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_BT_FORGET:
      bluetooth_interface.forgetBonds();
      closeMenu();
      showAlert("Paired phones forgotten", 1500);
      break;
    case ACT_DISC_NAMES: h->toggleDiscoveryNames(); closeMenu(); break;
    case ACT_MSG_CLEAR:
      ((MsgPreviewScreen*)msg_preview)->clearUnread();
      _menu_return = NULL;
      gotoHomeScreen();
      break;
    case ACT_SET_BRIGHTNESS:
      _ui_prefs.brightness_idx = (_ui_prefs.brightness_idx + 1) % sizeof(BRIGHTNESS_STEPS);
      applyBrightness();
      saveUIPrefs();
      openMenu(_menu_kind);
      if (nightNow() && NIGHT_LEVELS[_ui_prefs.night_level % NUM_NIGHT_LEVELS] < BRIGHTNESS_STEPS[_ui_prefs.brightness_idx]) {
        // the setting changed, but Night mode > Dim holds the screen lower until morning: say so
        char t[8], msg[40];
        fmtHour(t, _ui_prefs.night_until, _ui_prefs.clock24);
        snprintf(msg, sizeof(msg), "Night dim: %d%% until %s", NIGHT_LEVELS[_ui_prefs.night_level % NUM_NIGHT_LEVELS], t);
        showAlert(msg, 2500);
      }
      break;
    case ACT_SET_TIMEOUT:
      _ui_prefs.timeout_idx = (_ui_prefs.timeout_idx + 1) % (sizeof(TIMEOUT_SECS) / sizeof(TIMEOUT_SECS[0]));
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_SET_CLOCK:
      _ui_prefs.clock24 = !_ui_prefs.clock24;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_SET_TZ: openTimezone(); break;
    case ACT_SET_DST:
      _ui_prefs.dst_rule = (_ui_prefs.dst_rule + 1) % DST_RULES;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_RADIO_EDIT: openRadioEditor(); break;
    case ACT_THEME: openTheme(); break;
    case ACT_THEME_MENU: openMenu(MENU_THEME); break;
    case ACT_THEME_BG:
      _ui_prefs.glass_bg = (_ui_prefs.glass_bg + 1) % 4;
      applyStyle();
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_THEME_STYLE:
      _ui_prefs.glass = !_ui_prefs.glass;
      applyStyle();
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_ABOUT: openAbout(); break;
    case ACT_TIME_MENU: openMenu(MENU_TIME); break;
    case ACT_NIGHT_MENU: openMenu(MENU_NIGHT); break;
    case ACT_SET_AOD:
      _ui_prefs.aod = !_ui_prefs.aod;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_NIGHT_DND:
      _ui_prefs.night_dnd = !_ui_prefs.night_dnd;
      saveUIPrefs();
      openMenu(MENU_NIGHT);
      break;
    case ACT_NIGHT_TOGGLE:
      _ui_prefs.night_on = !_ui_prefs.night_on;
      nightChanged();
      openMenu(MENU_NIGHT);
      break;
    case ACT_NIGHT_FROM: openHour(false); break;
    case ACT_NIGHT_UNTIL: openHour(true); break;
    case ACT_NIGHT_LEVEL:
      _ui_prefs.night_level = (_ui_prefs.night_level + 1) % NUM_NIGHT_LEVELS;
      nightChanged();
      openMenu(MENU_NIGHT);
      break;
    case ACT_POWER_MENU: openMenu(MENU_POWER); break;
    case ACT_SET_TOUCH_SND:  // Off -> Click -> Pop -> Tick, each played once to compare
      _ui_prefs.touch_snd = (_ui_prefs.touch_snd + 1) % 4;
      _audio_grace_until = millis() + 60000;
      saveUIPrefs();
      // play it once the audio is up (from Off it starts now: its pop and fade-in would
      // swallow a sound played at once)
      if (_ui_prefs.touch_snd) {
        sound.keepOpen(true, tapVolume());
        _tap_preview_at = millis() + 1500;  // at the latest; as soon as the audio is ready (loop)
      }
      openMenu(_menu_kind);
      break;
    case ACT_SOS: openSos(); break;
    case ACT_SHARE_QR: openQr(); break;
    case ACT_FLASHLIGHT: openFlashlight(); break;
    case ACT_BAND_SCAN: openScan(); break;
    case ACT_POWER_SAVE:  // the chip sleeping with the screen off: Off is the way back if it misbehaves
      _ui_prefs.power_save = !_ui_prefs.power_save;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_SOUND_MENU: openMenu(MENU_SOUND); break;
    case ACT_PAGES_MENU: openMenu(MENU_PAGES); break;
    case ACT_CLOCK_FACE:
      _ui_prefs.clock_face = (_ui_prefs.clock_face + 1) % NUM_CLOCK_FACES;
      saveUIPrefs();
      if (curr == menu) openMenu(_menu_kind);
      else { showAlert(CLOCK_FACE_NAMES[_ui_prefs.clock_face], 700); _next_refresh = 0; }
      break;
    case ACT_DISPLAY_MENU: openMenu(MENU_DISPLAY); break;
    case ACT_MOTION_MENU: openMenu(MENU_MOTION); break;
    case ACT_CLOCK_RESET:
      if (_clock_reset_tap && millis() - _clock_reset_tap < 3000) {
        _clock_reset_tap = 0;
        rtc_clock.reset();
        board.logEvent("Clock reset");
        showAlert("Clock reset: open the app", 2000);
      } else {
        _clock_reset_tap = millis();
        showAlert("Tap again to reset", 2500);
      }
      break;
    case ACT_SET_MESH_TIME:
      _ui_prefs.mesh_time = !_ui_prefs.mesh_time;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_SOS_CHAN: {
      ChannelDetails ch;
      int cur = sosChannel(_ui_prefs.sos_chan, ch);
      _ui_prefs.sos_chan = nextSosChannel(cur < 0 ? 0 : cur);
      saveUIPrefs();
      openMenu(MENU_SETTINGS);
      break;
    }
    case ACT_SIG_TOGGLE:
      if (signal_tester.running) signal_tester.stop(); else signal_tester.start();
      if (curr == menu) closeMenu();
      _next_refresh = 0;
      break;
    case ACT_SIG_FINDER:
      signal_tester.startFinder();
      if (curr == menu) closeMenu();
      _next_refresh = 0;
      break;
    case ACT_SIG_INTERVAL:
      _ui_prefs.sig_interval = (_ui_prefs.sig_interval + 1) % NUM_SIG_INTERVALS;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_SIG_LOCK:
      if (!signal_tester.toggleLock()) showAlert("No repeater heard yet", 1200);
      openMenu(_menu_kind);
      break;
    case ACT_SIG_CLEAR:
      signal_tester.clear();
      closeMenu();
      break;
    case ACT_BOT_TOGGLE:
      _ui_prefs.bot_on = !_ui_prefs.bot_on;
      saveUIPrefs();
      if (curr == menu) openMenu(_menu_kind);
      else { showAlert(_ui_prefs.bot_on ? "Bot on" : "Bot off", 800); _next_refresh = 0; }
      break;
    case ACT_BOT_DM:
      _ui_prefs.bot_dm = !_ui_prefs.bot_dm;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_BOT_CHAN:
      _ui_prefs.bot_chan = (_ui_prefs.bot_chan + 1) % 3;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_LOGS: openLogs(); break;
    case ACT_SET_LIFT:
      _ui_prefs.lift_wake = !_ui_prefs.lift_wake;
      motion.setActive(_ui_prefs.lift_wake || _ui_prefs.flip_quiet || _ui_prefs.rotation == 2);
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_SET_FLIP:
      _ui_prefs.flip_quiet = !_ui_prefs.flip_quiet;
      motion.setActive(_ui_prefs.lift_wake || _ui_prefs.flip_quiet || _ui_prefs.rotation == 2);
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_SET_ALERTS:
      _ui_prefs.alerts = (_ui_prefs.alerts + 1) % 3;
      saveUIPrefs();
      openMenu(_menu_kind);
      break;
    case ACT_SET_SOUND:
      // Off -> Low -> Medium -> High (stored 0, 1, 3, 2: Medium came later)
      _ui_prefs.sound = _ui_prefs.sound == 0 ? 1 : _ui_prefs.sound == 1 ? 3 : _ui_prefs.sound == 3 ? 2 : 0;
      _audio_grace_until = millis() + 60000;
      saveUIPrefs();
      sound.chime(SF32Sound::CHIME_TEST, (SF32Sound::Volume)_ui_prefs.sound);
      openMenu(_menu_kind);
      break;
    case ACT_SET_BT:
      if (isBluetoothEnabled()) disableBluetooth(); else enableBluetooth();
      if (_menu_kind == MENU_SETTINGS || _menu_kind == MENU_PAGE) openMenu(_menu_kind); else closeMenu();
      break;
  }
}

bool MenuScreen::handleInput(char c) {
  if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) {
    setSelected((_sel + 1) % _n);
    return true;
  }
  if (c == KEY_PREV || c == KEY_LEFT || c == KEY_UP) {
    setSelected((_sel + _n - 1) % _n);
    return true;
  }
  if (c == KEY_ENTER || c == KEY_SELECT) {
    _task->menuAction(_items[_sel].action);
    return true;
  }
  return false;
}

// Sideways, a page sticks to the finger, with the next one coming in beside it. Let go past a
// third of the way, or with a flick, and it goes on to that page; otherwise it springs back.
#define PAGE_DRAG_SLOP   26     // panel px before it counts: past what still counts as a tap (TAP_MAX_PX)
#define PAGE_DRAG_FLICK  0.35f  // panel px per ms
#define PAGE_SWIPE_MIN   60     // panel px: a quick touch this far is a swipe, whatever was shown
#define PAGE_SWIPE_MS    350
#define MENU_SWIPE_MIN   60     // panel px up or down on a page: the menu opens, finger still down
void UITask::pageDrag() {
  HomeScreen* h = (HomeScreen*)home;
  SF32Display* d = (SF32Display*)_display;
  if (!_drag_on) {
    if (!touch_input.fingerDown() || touch_input.touchConsumed() || _hold_frac > 0 || _hold_done) return;
    int dx = touch_input.fingerX() - touch_input.fingerX0(), dy = touch_input.fingerY() - touch_input.fingerY0();
    if (abs(dx) < PAGE_DRAG_SLOP || abs(dx) < 2 * abs(dy)) return;
    touch_input.consumeTouch();  // it's ours now: no tap or swipe when it lifts
    _drag_on = true;
    _drag_dir = 0;
    _drag_off = 0;
  }
  _auto_off = millis() + autoOffMillis();
  if (touch_input.fingerDown()) {
    int dx = touch_input.fingerX() - touch_input.fingerX0();
    int off = abs(dx) - PAGE_DRAG_SLOP;  // from where it took hold: no jump
    if (off < 0) off = 0;
    if (off > 478) off = 478;
    int dir = dx < 0 ? 1 : -1;  // finger going left: the next page
    if (dir != _drag_dir && off > 0) {  // the page on this side, drawn once
      if (_drag_dir) h->dragStep(-_drag_dir);
      h->dragStep(dir);
      _drag_dir = dir;
      d->cancelListFrame();
      d->waitFlush();
      _display->startFrame();
      d->setSquareLayout(true);
      curr->render(*_display);
      d->dragFrameDone();
    }
    off &= ~1;
    if (_drag_dir && off != _drag_off && !d->sendBusy()) {
      d->dragShow(off, _drag_dir > 0);
      _drag_off = off;
    }
    return;
  }
  // let go. Moves can arrive all at once (the UI was busy drawing the page coming in), with no
  // speed to read and maybe nothing shown yet, so judge by the whole touch as well: a quick swipe
  // always counts, as it did before pages followed the finger.
  _drag_on = false;
  _next_refresh = 0;
  int dx = touch_input.fingerX() - touch_input.fingerX0();
  int dir = dx < 0 ? 1 : -1;
  unsigned long ms = touch_input.lastTouchMs();
  bool quick = abs(dx) >= PAGE_SWIPE_MIN && ms < PAGE_SWIPE_MS;
  if (!_drag_dir) {  // it never got as far as showing anything
    if (quick) {
      h->dragStep(dir);
      d->setTransition(dir > 0 ? SF32Display::TRANSITION_LEFT : SF32Display::TRANSITION_RIGHT);
      touch_input.click(2);
    }
    return;
  }
  float v = touch_input.releaseVelocityX();  // < 0: going left
  if (v == 0 && ms > 0) v = (float)dx / ms;
  bool flick = _drag_dir > 0 ? v < -PAGE_DRAG_FLICK : v > PAGE_DRAG_FLICK;
  bool back_flick = _drag_dir > 0 ? v > PAGE_DRAG_FLICK : v < -PAGE_DRAG_FLICK;
  bool past = dir == _drag_dir && abs(dx) - PAGE_DRAG_SLOP > 480 / 3;
  bool commit = !back_flick && (flick || past || (quick && dir == _drag_dir));
  d->dragSettle(_drag_off, _drag_dir > 0, commit);
  if (commit) touch_input.click(2);
  else h->dragStep(-_drag_dir);
}

// Touch: vertical swipes open/close menus; in a menu, taps choose a row directly.
void UITask::handleTouchMenus() {
  HomeScreen* h = (HomeScreen*)home;
  bool page_taps = curr == home && h->wantsTaps();
  bool editor = isRadioScreen() || curr == theme_screen || curr == about_screen || curr == logs_screen ||
                curr == tz_screen || curr == hour_screen || curr == sos_screen ||
                curr == qr_screen || curr == scan_screen;
  touch_input.setDirectTaps(curr == menu || editor || page_taps);
  if (_drag_on && (curr != home || _display == NULL || !_display->isOn())) {
    _drag_on = false;  // something else took over mid-drag: the next frame draws what's current
    _next_refresh = 0;
  }
  if (_display == NULL || !_display->isOn()) return;

  // Hold on the clock to change its face: after a moment a ring fills around the finger,
  // and when it's full (0.6 s) the next face comes up, while still holding. Let go before
  // that: nothing at all (not a tap).
  if (curr == home && h->onClockPage() && !_aod) {
    unsigned long held = touch_input.stillHeldMs();
    if (held >= 250) {
      touch_input.consumeTouch();  // from here on its lift does nothing
      float f = (held - 250) / 350.0f;
      _hold_frac = f > 1 ? 1 : f;
      if (f >= 1 && !_hold_done) {
        _hold_done = true;
        _hold_frac = 0;
        // the next face, without the name pop-up (the face itself shows it changed)
        _ui_prefs.clock_face = (_ui_prefs.clock_face + 1) % NUM_CLOCK_FACES;
        saveUIPrefs();
        _next_refresh = 0;
        if (_ui_prefs.touch_snd) sound.chime(SF32Sound::CHIME_POP, tapVolume());  // a fuller "done" than a tap
      }
      if (!_hold_done) _next_refresh = millis() + 30;
      _auto_off = millis() + autoOffMillis();
    } else if (_hold_frac > 0 || _hold_done) {
      if (_hold_frac > 0) _next_refresh = 0;  // the ring goes
      // moved before the ring was full: a swipe after all, let it count
      if (_hold_frac > 0 && !_hold_done && touch_input.fingerDown()) touch_input.unconsumeTouch();
      _hold_frac = 0;
      if (!touch_input.fingerDown()) _hold_done = false;
    }
  }

  // Bluetooth, Advert: hold the icon. It acts while still held, so you know it took.
  if (curr == home && !_aod && h->iconHoldPage() && !touch_input.touchConsumed() && touch_input.stillHeldMs() >= 500) {
    int ix, iy;
    SF32Display::panelToLogical(touch_input.fingerX0(), touch_input.fingerY0(), ix, iy);
    if (h->iconHit(ix, iy)) {
      touch_input.consumeTouch();
      h->iconAction();
      if (_ui_prefs.touch_snd) sound.chime(SF32Sound::CHIME_POP, tapVolume());
      _auto_off = millis() + autoOffMillis();
      _next_refresh = 0;
    }
  }
  if (curr == home && !_aod) pageDrag();
  // Up or down on a page: the menu opens as soon as it's clearly that, not when the finger lifts.
  if (curr == home && !_aod && !_drag_on && _hold_frac == 0 && !_hold_done && touch_input.fingerDown() && !touch_input.touchConsumed()) {
    int mdx = touch_input.fingerX() - touch_input.fingerX0(), mdy = touch_input.fingerY() - touch_input.fingerY0();
    if (abs(mdy) >= MENU_SWIPE_MIN && abs(mdy) > 2 * abs(mdx)) {
      touch_input.consumeTouch();
      touch_input.click(2);
      openMenu(mdy > 0 ? MENU_POWER : MENU_PAGE);
      _auto_off = millis() + autoOffMillis();
      _next_refresh = 0;
    }
  }

  // Long lists (Settings, About): the finger drags them, a flick glides on.
  DragScroll* ds = NULL;
  if (curr == menu && ((MenuScreen*)menu)->scrolls()) ds = &((MenuScreen*)menu)->drag();
  else if (curr == about_screen && ((AboutScreen*)about_screen)->scrolls()) ds = &((AboutScreen*)about_screen)->drag();
  else if (curr == logs_screen && !((LogScreen*)logs_screen)->inDetail()) ds = &((LogScreen*)logs_screen)->drag();
  if (ds) {
    if (touch_input.fingerDown() && touch_input.touchConsumed()) {
      // the touch that opened this menu: it doesn't drag the list as well
    } else if (touch_input.fingerDown()) {
      if (!ds->active) ds->begin();
      // follow the finger after a small dead zone (taken off: no jump), so a tap doesn't move the list
      int dy = touch_input.fingerY() - touch_input.fingerY0();
      if (abs(dy) > 12) {
        ds->move(dy > 0 ? dy - 12 : dy + 12);
        _next_refresh = 0;
        _auto_off = millis() + autoOffMillis();
      }
    } else if (ds->active) {
      ds->end(touch_input.releaseVelocity());
      // that was a tap: it lands on what was under the finger when it came down, with no glide
      if (abs(touch_input.fingerY() - touch_input.fingerY0()) <= 25 && abs(touch_input.fingerX() - touch_input.fingerX0()) <= 25) {
        ds->vel = 0;
        ds->set(ds->start);
      }
      _next_refresh = 0;
    }
  }

  if (_aod) {  // any swipe or tap wakes the full UI (taps arrive as button presses)
    int tx2, ty2;
    if (touch_input.takeGesture() != SF32TouchInput::Gesture::None || touch_input.takeTap(tx2, ty2)) exitAOD();
    return;
  }

  auto g = touch_input.takeGesture();
  if (g != SF32TouchInput::Gesture::None && curr != splash) {
    if (ds) {
      // a list: swipes scroll it (above); pulling down from the top closes it
      if (g == SF32TouchInput::Gesture::SwipeDown && ds->from_top && !(curr == menu && _menu_kind == MENU_POWER)) {
        ds->vel = 0;
        if (curr == menu) closeMenu(); else if (curr == logs_screen) closeLogs(); else closeAbout();
      }
      // the power menu came down from the top: pushing it up from its end closes it
      if (g == SF32TouchInput::Gesture::SwipeUp && ds->from_bottom && curr == menu && _menu_kind == MENU_POWER) {
        ds->vel = 0;
        closeMenu();
      }
    } else if (curr == logs_screen) ((LogScreen*)logs_screen)->closeDetail();  // an entry's details: back to the list
    else if (curr == sos_screen) {}  // only its button leaves it
    else if (curr == qr_screen) closeQr();
    else if (curr == scan_screen) closeScan();
    else if (curr == menu) {
      // a panel goes back the way it came. The power menu came down from
      // the top: swipe up; menus that came up from the bottom: swipe down.
      bool from_top = _menu_kind == MENU_POWER;
      if (g == (from_top ? SF32TouchInput::Gesture::SwipeUp : SF32TouchInput::Gesture::SwipeDown)) closeMenu();
    }
    else if (curr == theme_screen) closeTheme();
    else if (curr == about_screen) closeAbout();
    else if (curr == tz_screen) ((TimezoneScreen*)tz_screen)->done();
  else if (curr == hour_screen) closeHour();
    else if (curr == radio_field) closeRadioField();      // keeps the new value
    else if (curr == radio_review) closeRadioReview();    // back to editing
    else if (curr == radio_edit) leaveRadioEditor();      // asks if there are changes
    else openMenu(g == SF32TouchInput::Gesture::SwipeDown ? MENU_POWER : MENU_PAGE);
    _auto_off = millis() + autoOffMillis();
    _next_refresh = 0;
  }

  int tx, ty;
  if (!touch_input.takeTap(tx, ty)) return;
  int x, y;
  SF32Display::panelToLogical(tx, ty, x, y);
  _auto_off = millis() + autoOffMillis();
  _next_refresh = 0;
  if (curr == radio_edit) {
    ((RadioEditScreen*)radio_edit)->handleTap(x, y);
  } else if (curr == radio_field) {
    ((RadioFieldScreen*)radio_field)->handleTap(x, y);
  } else if (curr == radio_review) {
    ((RadioReviewScreen*)radio_review)->handleTap(x, y);
  } else if (curr == theme_screen) {
    ((ThemeScreen*)theme_screen)->handleTap(x, y);
  } else if (curr == about_screen) {
    closeAbout();  // tap anywhere: back
  } else if (curr == logs_screen) {
    ((LogScreen*)logs_screen)->handleTap(x, y);  // an entry: its details; the header: back
  } else if (curr == tz_screen) {
    ((TimezoneScreen*)tz_screen)->handleTap(x, y);
  } else if (curr == hour_screen) {
    ((HourScreen*)hour_screen)->handleTap(x, y);
  } else if (curr == scan_screen) {
    ((ScanScreen*)scan_screen)->reset();  // tap: start over (clears the max hold)
  } else if (curr == qr_screen) {
    if (flashlightOn()) { ((QrScreen*)qr_screen)->toggleRed(); _auto_off = millis() + 5UL * 60 * 1000; }
    else closeQr();  // tap anywhere: back
  } else if (curr == sos_screen) {
    if (y >= 90) stopSos();  // the Cancel / Stop button (bottom)
  } else if (page_taps) {
    if (!h->handleTap(x, y)) {  // not on a button: a normal tap (next page)
      h->handleInput(KEY_NEXT);
      ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
    }
  } else if (curr == menu) {
    MenuScreen* m = (MenuScreen*)menu;
    int i = m->hitTest(x, y);
    if (i >= 0) {
      m->setSelected(i);
      menuAction(m->items_action(i));
    } else if (y > 26) {
      closeMenu();  // tap below the rows
    }
  }
}
