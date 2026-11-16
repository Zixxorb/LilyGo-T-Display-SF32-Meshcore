// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Settings screens: themes, About, Logs, Time zone, always-on display, Night mode hours.

// ---- SF32: colour themes ----

#define RGB(r, g, b) (ColorVal)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
// bg / text / text2 / warn / good: 0 = the usual (black background, light grey text, amber
// highlights, teal-green "good");
// grid: a shorter label for the Color grid when the name doesn't fit its swatch
struct UITheme { const char* name; ColorVal accent, card; bool rainbow; ColorVal bg, text, text2, warn, good; const char* grid; };
static const UITheme THEMES[] = {
  {"MeshCore", RGB(0x4C, 0x8D, 0xFF), RGB(0x1F, 0x29, 0x37)},
  {"Cyan",     RGB(0x38, 0xBD, 0xF8), RGB(0x14, 0x2A, 0x3A)},
  {"Teal",     RGB(0x2D, 0xD4, 0xBF), RGB(0x13, 0x2E, 0x2C)},
  {"Green",    RGB(0x4A, 0xDE, 0x80), RGB(0x17, 0x2D, 0x20)},
  {"Purple",   RGB(0xA7, 0x8B, 0xFA), RGB(0x27, 0x22, 0x3E)},
  {"Pink",     RGB(0xF4, 0x72, 0xB6), RGB(0x33, 0x1E, 0x2B)},
  {"Red",      RGB(0xF8, 0x71, 0x71), RGB(0x33, 0x1C, 0x1F)},
  {"Orange",   RGB(0xFB, 0x92, 0x3C), RGB(0x33, 0x24, 0x18)},
  {"Mono",     RGB(0xE5, 0xE7, 0xEB), RGB(0x26, 0x26, 0x28)},
  {"Rainbow",  RGB(0xFF, 0x3C, 0xAC), RGB(0x2A, 0x1E, 0x3A), true},  // a hue per page
  // chattamesh.com's colour scheme, in full: navy page, periwinkle accent, gold highlights
  // (lights the whole panel: more power with the screen on than the black-background themes)
  {"ChattaMesh", RGB(0x6E, 0x70, 0xE6), RGB(0x22, 0x27, 0x3A), false,
   RGB(0x1A, 0x1D, 0x2C), RGB(0xED, 0xEE, 0xF2), RGB(0x9D, 0xA4, 0xBA), RGB(0xF2, 0xC8, 0x61),
   RGB(0x4F, 0xB8, 0x60), "Chatta"},
};
static const int NUM_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);

static ColorVal lighten(ColorVal c, int d) {
  int r = ((c >> 11) & 0x1F) + d / 8, g = ((c >> 5) & 0x3F) + d / 4, b = (c & 0x1F) + d / 8;
  return (ColorVal)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b));
}

static void applyTheme(int i) {
  if (i < 0 || i >= NUM_THEMES) i = 0;
  UIColor::corp_blue = THEMES[i].accent;  // THEME_ACCENT
  UIColor::title_bkg = THEMES[i].card;    // THEME_CARD
  UIColor::popup_bkg = lighten(THEMES[i].card, 24);
  static const ColorVal TEXT = RGB(0xF3, 0xF4, 0xF6), TEXT2 = RGB(0x9C, 0xA3, 0xAF), WARN = RGB(0xF5, 0xA5, 0x24);
  UIColor::window_bkg = THEMES[i].bg;  // (0: black)
  UIColor::primary_txt = UIColor::title_txt = UIColor::popup_txt = THEMES[i].text ? THEMES[i].text : TEXT;
  UIColor::secondary_txt = THEMES[i].text2 ? THEMES[i].text2 : TEXT2;
  UIColor::warning_txt = THEMES[i].warn ? THEMES[i].warn : WARN;
  THEME_GOOD = THEMES[i].good ? THEMES[i].good : THEME_GOOD_DEFAULT;
  g_rainbow_theme = THEMES[i].rainbow;
  setRingTheme(THEMES[i].rainbow, THEMES[i].accent, THEMES[i].warn, THEMES[i].good);
}

// A 3x3 grid of swatches; tapping one applies it right away (live preview).
class ThemeScreen : public UIScreen {
public:
  ThemeScreen(UITask* task) : _task(task) {}

  int render(DisplayDriver& d) override {
    themeHeader(d, "Color", _task->getBattMilliVolts());
    int cur = _task->themeIndex();
    for (int i = 0; i < NUM_THEMES; i++) {
      Btn c = cell(i);
      d.setColor(THEMES[i].card);
      d.fillRect(c.x, c.y, c.w, c.h);
      d.setColor(THEMES[i].accent);
      if (THEMES[i].rainbow) {  // its swatch: a little rainbow
        for (int k = 0; k < c.w - 10; k++) { d.setColor(hsvColor(k * 360.0f / (c.w - 10), 0.8f, 1)); d.fillRect(c.x + 5 + k, c.y + 3, 1, 3); }
      } else {
        d.setColor(THEMES[i].accent);
        d.fillRect(c.x + 5, c.y + 3, c.w - 10, 3);
      }
      d.setTextSize(1);
      d.setColor(i == cur ? UIColor::primary_txt : UIColor::secondary_txt);
      d.drawTextCentered(c.x + c.w / 2, c.y + 8, THEMES[i].grid ? THEMES[i].grid : THEMES[i].name);
      if (i == cur) {
        d.setColor(UIColor::primary_txt);
        d.drawRect(c.x, c.y, c.w, c.h);
      }
    }
    drawBtn(d, B_DONE, "Done", 1, true);
    if (_sel >= 0) drawSelBar(d, _sel < NUM_THEMES ? cell(_sel) : B_DONE);
    return 2000;
  }

  bool handleTap(int x, int y) {
    for (int i = 0; i < NUM_THEMES; i++) {
      if (inBtn(cell(i), x, y)) { _task->setTheme(i); return true; }
    }
    if (inBtn(B_DONE, x, y)) _task->closeTheme();
    return true;
  }

  // button A: click = next swatch (applied), long press = done
  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) { _task->setTheme((_task->themeIndex() + 1) % NUM_THEMES); return true; }
    if (c == KEY_PREV || c == KEY_LEFT) { _task->setTheme((_task->themeIndex() + NUM_THEMES - 1) % NUM_THEMES); return true; }
    if (c == KEY_ENTER) { _task->closeTheme(); return true; }
    return false;
  }

private:
  static Btn cell(int i) { return Btn{4 + (i % 3) * 41, 27 + (i / 3) * 19, 38, 17}; }  // 3 x 4
  static constexpr Btn B_DONE = {4, 106, 120, 15};
  UITask* _task;
  int _sel = -1;
};

void UITask::setTheme(int i) {
  if (_ui_prefs.glass) _ui_prefs.glass_theme = (uint8_t)i;
  else _ui_prefs.theme = (uint8_t)i;
  applyStyle();
  saveUIPrefs();
}
void UITask::applyBattery() {
  g_batt_icon = _ui_prefs.batt_icon;
  g_chg_full_pct = CHG_LIMITS[_ui_prefs.chg_limit % NUM_CHG_LIMITS];
  board.setChargeLimit(CHG_LIMIT_MV[_ui_prefs.chg_limit % NUM_CHG_LIMITS]);
}
void UITask::applyStyle() {
  g_batt_colors = _ui_prefs.batt_colors;
  int i = themeIndex();
  bool glass = _ui_prefs.glass;
#ifdef SF32_SHOT_THEME  // README screenshot builds: Rainbow + Glass, whatever is saved (and nothing saved)
  for (int t = 0; t < NUM_THEMES; t++) if (THEMES[t].rainbow) i = t;
  glass = true;
#endif
  applyTheme(i);
  SF32Display::setCap30(lowPower());  // (applyStyle runs whenever low power mode changes)
  if (_display) ((SF32Display*)_display)->setGlass(glass && !lowPower(), THEMES[i].accent, THEMES[i].rainbow, _ui_prefs.glass_bg);
}
void UITask::openTheme() {
  _menu_return_theme = _menu_return;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(theme_screen);
}
void UITask::closeTheme() { closeToMenu(MENU_THEME); }  // Settings > Theme > Color
void UITask::closeToMenu(MenuKind kind) {
  _menu_return = _menu_return_theme;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  setCurrScreen(home);  // openMenu needs curr != menu to remember where to return
  UIScreen* back = _menu_return;
  openMenu(kind);
  _menu_return = back;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
}

// ---- SF32: About / credits ----

#ifndef SF32_CREDIT_PORT
  #define SF32_CREDIT_PORT "Zixxorb"  // port & design
#endif

struct AboutLine { uint8_t kind; const char* text; };  // 0 heading, 1 big, 2 small, 3 gap
static const AboutLine ABOUT_LINES[] = {
  {0, "Powered by"},
  {1, "VesperOS"},
  {2, "v" VESPER_VERSION " - MeshCore " FIRMWARE_VERSION},
  {2, "LilyGo T-Display-SF32"},
  {3, ""},
  {0, "Port & design"},
  {1, SF32_CREDIT_PORT},
  {0, "Written with"},
  {1, "Claude"},
  {2, "by Anthropic"},
  {0, "MeshCore"},
  {1, "Scott Powell"},
  {2, "rippleradios.com"},
  {2, "and MeshCore contributors"},
  {0, "Hardware & SDK"},
  {1, "LilyGo"},
  {2, "SiFli SDK, RT-Thread"},
  {0, "Libraries & font"},
  {2, "Crypto - Rhys Weatherley"},
  {2, "ed25519 - Orson Peters"},
  {2, "CayenneLPP - Electronic Cats"},
  {2, "base64 - Densaugeo"},
  {2, "QR codes - Project Nayuki"},
  {2, "Noto Sans - Google (OFL)"},
  {3, ""},
  {2, "Thank you!"},
};
static const int NUM_ABOUT = sizeof(ABOUT_LINES) / sizeof(ABOUT_LINES[0]);
static int aboutLineH(uint8_t kind) { return kind == 1 ? 18 : (kind == 0 ? 13 : (kind == 2 ? 10 : 6)); }

class AboutScreen : public UIScreen {
public:
  AboutScreen(UITask* task) : _task(task) {
    _content_h = 0;
    for (int i = 0; i < NUM_ABOUT; i++) _content_h += aboutLineH(ABOUT_LINES[i].kind);
  }
  void reset() { _ds = DragScroll(); _dirty = true; }
  int maxScroll() const { int m = _content_h - LIST_H; return m > 0 ? m : 0; }
  bool scrolls() const { return maxScroll() > 0; }
  void scrollBy(int dir) { _ds.max = maxScroll(); _ds.set(_ds.pos + dir * 60); }
  DragScroll& drag() { _ds.max = maxScroll(); return _ds; }

  int render(DisplayDriver& d) override {
    themeHeader(d, "About", _task->getBattMilliVolts());
    _ds.max = maxScroll();
    bool moving = _ds.step();
    SF32Display& sd = (SF32Display&)d;
    if (_dirty) {  // the text, once, into the layer
      _dirty = false;
      sd.beginLayer(_content_h + 4);
      int y = 2;
      for (int i = 0; i < NUM_ABOUT; i++) {
        const AboutLine& l = ABOUT_LINES[i];
        if (l.kind != 3) {
          d.setTextSize(l.kind == 1 ? 2 : 1);
          d.setColor(l.kind == 0 ? THEME_ACCENT : (l.kind == 1 ? UIColor::primary_txt : UIColor::secondary_txt));
          d.drawTextCentered(62, y + (l.kind == 0 ? 3 : 0), l.text);
        }
        y += aboutLineH(l.kind);
      }
      sd.endLayer();
      sd.layerSpans();
    } else if (moving || _ds.active) {
      sd.setFlushBand(LIST_Y, LIST_H);  // only the list moves: send just its band
    }
    sd.composeLayer(0, LIST_Y, 124, LIST_H, (int)lroundf(_ds.pos * SF32Display::SCALE));
    if (scrolls()) {
      int thumb = LIST_H * LIST_H / (LIST_H + maxScroll());
      int ty = LIST_Y + (int)((LIST_H - thumb) * (_ds.pos / maxScroll()));
      d.setColor(THEME_DIM);
      d.fillRect(125, LIST_Y, 2, LIST_H);
      d.setColor(THEME_ACCENT);
      d.fillRect(125, ty, 2, thumb);
    }
    return moving ? 16 : 2000;
  }

  // button A: click = scroll down (wraps to the top), long press = back
  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      if (_ds.pos >= maxScroll()) scrollBy(-1000); else scrollBy(+1);
      return true;
    }
    if (c == KEY_PREV || c == KEY_LEFT) { scrollBy(-1); return true; }
    if (c == KEY_ENTER) { _task->closeAbout(); return true; }
    return false;
  }

private:
  static const int LIST_Y = 26, LIST_H = 98;
  UITask* _task;
  int _content_h;
  DragScroll _ds;
  bool _dirty = true;
};

void UITask::openAbout() {
  ((AboutScreen*)about_screen)->reset();
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(about_screen);  // _menu_return still says where the menu was opened
}
void UITask::closeAbout() {
  UIScreen* back = _menu_return ? _menu_return : home;
  _menu_return = NULL;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  setCurrScreen(back);
}

// ---- SF32: Settings > Logs (the board's event log, newest first) ----

#define LOG_SHOW 20
class LogScreen : public UIScreen {
public:
  LogScreen(UITask* task) : _task(task) {}
  void reload() {
    _n = board.readLog(_times, _texts, LOG_SHOW, _snaps);
    _ds = DragScroll();
    _dirty = true;
    _detail = -1;
  }
  bool inDetail() const { return _detail >= 0; }
  void closeDetail() { _detail = -1; _dirty = true; }

  // a tap: on an entry opens its details; in the details, back to the list
  void handleTap(int x, int y) {
    if (_detail >= 0) { closeDetail(); return; }
    if (y < LIST_Y) { _task->closeLogs(); return; }  // the header: back to Settings
    int i = (int)((y - LIST_Y + _ds.pos) / ENTRY_H);
    if (i >= 0 && i < _n) _detail = i;
  }
  int contentH() const { return _n ? _n * ENTRY_H : 30; }
  int maxScroll() const { int m = contentH() - LIST_H; return m > 0 ? m : 0; }
  bool scrolls() const { return maxScroll() > 0; }
  DragScroll& drag() { _ds.max = maxScroll(); return _ds; }

  void formatWhen(char* when, uint32_t utc) {
    if (utc) {
      time_t t = (time_t)(utc + _task->localOffsetSecs(utc));
      struct tm tm;
      gmtime_r(&t, &tm);
      static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      if (_task->uiPrefs().clock24) sprintf(when, "%s %d  %d:%02d", months[tm.tm_mon], tm.tm_mday, tm.tm_hour, tm.tm_min);
      else sprintf(when, "%s %d  %d:%02d %s", months[tm.tm_mon], tm.tm_mday, tm.tm_hour % 12 ? tm.tm_hour % 12 : 12,
                   tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    } else {
      strcpy(when, "time unknown");
    }
  }

  // one entry: its text and the board as it was when it was logged
  void renderDetail(DisplayDriver& d) {
    int i = _detail;
    themeHeader(d, "Log entry", _task->getBattMilliVolts());
    char when[32], tmp[48];
    formatWhen(when, _times[i]);
    themeCard(d, 4, 27, 120, 30);
    themeLabel(d, 9, 30, when);
    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    d.setCursor(9, 40);
    d.printWordWrap(_texts[i], 110);

    // the snapshot: uptime_s,mV,usb,charge,chip_C,heap_free,heap_min_free,MHz[,extra...]
    long v[8];
    int nv = 0;
    const char* p = _snaps[i];
    while (*p && nv < 8) {
      char* e;
      v[nv++] = strtol(p, &e, 10);
      if (*e != ',' && *e != 0) break;
      p = *e ? e + 1 : e;
    }
    int y = 61;
    auto row = [&](const char* label, const char* value) {
      themeLabel(d, 6, y, label);
      d.setColor(UIColor::primary_txt);
      d.drawTextRightAlign(122, y, value);
      y += 10;
    };
    if (nv < 8) {
      themeLabel(d, 6, y, "No details: logged by an");
      themeLabel(d, 6, y + 10, "older firmware.");
    } else {
      unsigned long up = (unsigned long)v[0];
      if (up < 3600) sprintf(tmp, "%lum %lus", up / 60, up % 60);
      else if (up < 86400) sprintf(tmp, "%luh %lum", up / 3600, (up / 60) % 60);
      else sprintf(tmp, "%lud %luh", up / 86400, (up / 3600) % 24);
      row("Up for", tmp);
      if (v[1] > 0) sprintf(tmp, "%ld.%02ld V  %d%%", v[1] / 1000, (v[1] % 1000) / 10, sf32BatteryPercent((uint16_t)v[1]));
      else strcpy(tmp, "none");
      row("Battery", tmp);
      static const char* const chg[] = {"not charging", "pre-charge", "charging", "charged"};
      if (v[2] < 0) strcpy(tmp, "unknown");
      else if (v[2] == 0) strcpy(tmp, "battery");
      else sprintf(tmp, "USB, %s", v[3] >= 0 && v[3] < 4 ? chg[v[3]] : "?");
      row("Power", tmp);
      if (v[4]) sprintf(tmp, "%ld F  (%ld C)", v[4] * 9 / 5 + 32, v[4]);
      else strcpy(tmp, "?");
      row("Chip temp", tmp);
      sprintf(tmp, "%ld KB (low %ld)", v[5] / 1024, v[6] / 1024);
      row("Free memory", tmp);
      sprintf(tmp, "%ld MHz", v[7]);
      row("CPU", tmp);
      // anything after: name=value pairs (a start: why the chip started)
      if (*p) {
        int mode = -1;
        unsigned long wsr = 0;
        if (sscanf(p, "boot=%d,wsr=%lx", &mode, &wsr) == 2) {
          static const char* const modes[] = {"cold boot", "standby", "hibernate", "shutdown", "reboot"};
          sprintf(tmp, "%s, %08lx", mode >= 0 && mode < 5 ? modes[mode] : "?", wsr);
          row("Start/wake", tmp);
        } else {
          row("More", p);
        }
      }
    }
  }

  int render(DisplayDriver& d) override {
    if (_detail >= 0) { renderDetail(d); return 5000; }
    if (!((SF32Display&)d).listFrame()) themeHeader(d, "Logs", _task->getBattMilliVolts());  // (a list frame: just the list)
    _ds.max = maxScroll();
    bool moving = _ds.step();
    SF32Display& sd = (SF32Display&)d;
    if (_dirty) {
      _dirty = false;
      sd.beginLayer(contentH() + 2);
      if (_n == 0) {
        d.setTextSize(1);
        d.setColor(UIColor::secondary_txt);
        d.drawTextCentered(64, 10, "Nothing logged yet");
      }
      for (int i = 0; i < _n; i++) {
        int y = i * ENTRY_H;
        themeCard(d, 4, y, 120, ENTRY_H - 2);
        char when[32];
        formatWhen(when, _times[i]);
        themeLabel(d, 9, y + 3, when);
        d.setTextSize(1);
        d.setColor(UIColor::primary_txt);
        d.setCursor(9, y + 13);
        d.printWordWrap(_texts[i], 110);  // up to two lines
      }
      sd.endLayer();
      sd.layerRowsDone(0, contentH() + 2);  // for the graphics hardware's copy (cards, as in the menus)
      sd.layerSpans();
    } else if (moving || _ds.active) {
      sd.setFlushBand(LIST_Y, LIST_H);  // only the list moves: send just its band
    }
    int src = (int)lroundf(_ds.pos * SF32Display::SCALE);
    bool hw = sd.composeList(LIST_Y, LIST_H, src);  // the graphics hardware where it can (the first ~14)
    if (!hw) sd.composeLayer(4, LIST_Y, 120, LIST_H, src);
    if (sd.listFrame()) sd.setFlushBand(LIST_Y, LIST_H);
    // still moving, by hardware, with room to spare: the next frame is just the list, as in the menus
    if ((moving || _ds.active) && hw && src + (int)(LIST_H * SF32Display::SCALE) + 200 < SF32Display::listRows()) sd.nextFrameIsList();
    if (scrolls()) {
      int thumb = LIST_H * LIST_H / (LIST_H + maxScroll());
      int ty = LIST_Y + (int)((LIST_H - thumb) * (_ds.pos / maxScroll()));
      d.setColor(THEME_DIM);
      d.fillRect(125, LIST_Y, 2, LIST_H);
      d.setColor(THEME_ACCENT);
      d.fillRect(125, ty, 2, thumb);
    }
    return moving ? 16 : 5000;
  }

  // button A: click = scroll down (wraps), long press = back (in the details: back to the list)
  bool handleInput(char c) override {
    if (_detail >= 0) { if (c) closeDetail(); return c != 0; }
    _ds.max = maxScroll();
    if (c == KEY_NEXT || c == KEY_RIGHT) { _ds.set(_ds.pos >= maxScroll() ? 0 : _ds.pos + 48); return true; }
    if (c == KEY_PREV || c == KEY_LEFT) { _ds.set(_ds.pos - 48); return true; }
    if (c == KEY_ENTER) { _task->closeLogs(); return true; }
    return false;
  }

private:
  static const int LIST_Y = 27, LIST_H = 96, ENTRY_H = 34;
  UITask* _task;
  uint32_t _times[LOG_SHOW];
  char _texts[LOG_SHOW][48];
  char _snaps[LOG_SHOW][SF32Board::LOG_SNAP_LEN];
  int _n = 0, _detail = -1;
  DragScroll _ds;
  bool _dirty = true;
};

void UITask::openLogs() {
  ((LogScreen*)logs_screen)->reload();
  _menu_return_theme = _menu_return;  // where Settings was opened from
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(logs_screen);
}
void UITask::closeLogs() { closeToMenu(MENU_SETTINGS); }  // Settings > Logs

// ---- SF32: Settings > Clock > Time zone ----

class TimezoneScreen : public UIScreen {
public:
  TimezoneScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  int render(DisplayDriver& d) override {
    char tmp[32], tz[12];
    int off = _prefs->tz_offset;
    if (off == 0) strcpy(tz, "UTC"); else sprintf(tz, "UTC%+d", off);
    themeHeader(d, "Time zone", _task->getBattMilliVolts());
    themeCard(d, 4, 27, 120, 40);
    uint32_t now = rtc_clock.getCurrentTime();
    bool dst = dstActive(_task->uiPrefs().dst_rule, off, now);
    themeLabel(d, 9, 31, dst ? "Offset (+1 DST now)" : "Offset");
    if (rtc_clock.timeKnown() && now >= 1735689600UL) {  // local time preview
      time_t t = (time_t)(now + _task->localOffsetSecs(now));
      struct tm tm;
      gmtime_r(&t, &tm);
      if (_task->uiPrefs().clock24) sprintf(tmp, "%d:%02d", tm.tm_hour, tm.tm_min);
      else sprintf(tmp, "%d:%02d %s", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
      d.setTextSize(1);
      d.setColor(UIColor::secondary_txt);
      d.drawTextRightAlign(119, 31, tmp);
    }
    d.setTextSize(3);
    d.setColor(UIColor::primary_txt);
    d.setCursor(9, 41);
    d.print(tz);
    drawMinusPlus(d, B_MINUS, B_PLUS);
    drawBtn(d, B_DONE, "Done", 1, true);
    return 5000;
  }

  void bump(int delta) {
    int tz = _prefs->tz_offset + delta;
    if (tz > 14) tz = -12;
    if (tz < -12) tz = 14;
    _prefs->tz_offset = (int8_t)tz;
    the_mesh.savePrefs();  // right away: the screen may time out before Done
  }
  void open() {}
  void done() { _task->closeTimezone(); }  // already saved

  bool handleTap(int x, int y) {
    if (inBtn(B_MINUS, x, y)) bump(-1);
    else if (inBtn(B_PLUS, x, y)) bump(+1);
    else if (inBtn(B_DONE, x, y)) done();
    return true;
  }
  // button A: click = +1 h, double click = -1 h, long press = done
  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) { bump(+1); return true; }
    if (c == KEY_PREV || c == KEY_LEFT) { bump(-1); return true; }
    if (c == KEY_ENTER) { done(); return true; }
    return false;
  }

private:
  static constexpr Btn B_MINUS = {4, 72, 56, 24};
  static constexpr Btn B_PLUS = {68, 72, 56, 24};
  static constexpr Btn B_DONE = {4, 99, 120, 19};
  UITask* _task;
  NodePrefs* _prefs;
};

void UITask::openTimezone() {
  ((TimezoneScreen*)tz_screen)->open();
  _tz_return = curr;  // a menu (Clock / page menu) or a page
  _tz_return_kind = _menu_kind;
  _tz_menu_return = _menu_return;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(tz_screen);
}
void UITask::closeTimezone() {
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  if (_tz_return == menu) {  // back into the menu it came from, rebuilt (new offset shown)
    setCurrScreen(home);
    openMenu((MenuKind)_tz_return_kind);
    _menu_return = _tz_menu_return;
    ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  } else {
    setCurrScreen(_tz_return ? _tz_return : home);
  }
}

// ---- SF32: always-on display ----
// Shown instead of switching the screen off: time, date and the unread envelope on black,
// very dim, redrawn once a minute and moved to a new spot each minute (burn-in).

class AodScreen : public UIScreen {
public:
  AodScreen(UITask* task) : _task(task) {}

  int render(DisplayDriver& d) override {
    char hm[12], tmp[32];
    uint32_t now = rtc_clock.getCurrentTime();
    // a different spot each minute, within +-10 logical px
    static const int8_t SPOTS[][2] = {{0, 0}, {-8, -6}, {7, 5}, {-5, 8}, {9, -7}, {-9, 3}, {4, -9}, {6, 9}};
    int k = (now / 60) % 8;
    int ox = SPOTS[k][0], oy = SPOTS[k][1];
    const ColorVal TXT = RGB(0xC8, 0xCC, 0xD2);
    bool known = rtc_clock.timeKnown() && now >= 1735689600UL;
    struct tm tm;
    if (known) {
      time_t t = (time_t)(now + _task->localOffsetSecs(now));
      gmtime_r(&t, &tm);
      if (_task->uiPrefs().clock24) sprintf(hm, "%d:%02d", tm.tm_hour, tm.tm_min);
      else sprintf(hm, "%d:%02d", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min);
    } else {
      strcpy(hm, "--:--");
    }
    d.setTextSize(5);
    d.setColor(TXT);
    d.drawTextCentered(64 + ox, 38 + oy, hm);
    if (known) {
      static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      sprintf(tmp, "%s, %s %d", days[tm.tm_wday], months[tm.tm_mon], tm.tm_mday);
      d.setTextSize(1);
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(64 + ox, 82 + oy, tmp);
    }
    int unread = _task->getUnreadCount();
    if (unread > 0) {  // envelope + count, centred under the date
      static const uint8_t envelope[] = {0xFF, 0xFF, 0x7F, 0xFE, 0xBF, 0xFD, 0xDF, 0xFB, 0xEF, 0xF7, 0xF7, 0xEF,
                                         0xFB, 0xDF, 0xFD, 0xBF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      sprintf(tmp, "%d", unread);
      d.setTextSize(2);
      int w = 16 + 3 + d.getTextWidth(tmp);
      int x = 64 + ox - w / 2;
      d.setColor(UIColor::warning_txt);
      d.drawXbm(x, 98 + oy, envelope, 16, 12);
      d.setCursor(x + 19, 96 + oy);
      d.print(tmp);
    }
    if (_task->dndNow()) {  // do not disturb (night hours)
      d.setTextSize(1);
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(64 + ox, 114 + oy, "DND");
    }
    // until the next minute
    return known ? (int)(60 - now % 60) * 1000 + 50 : 60000;
  }

  bool handleInput(char c) override {  // any key: back to the full UI
    _task->exitAOD();
    return true;
  }

private:
  UITask* _task;
};

// ---- SF32: night dim (hour screen; the logic is near powerCare) ----

static const uint8_t NIGHT_LEVELS[] = {5, 10, 25, 50};
#define AOD_BRIGHTNESS 6  // always-on display
#define NUM_NIGHT_LEVELS 4

static void fmtHour(char* out, int h, bool h24) {
  if (h24) sprintf(out, "%02d:00", h);
  else sprintf(out, "%d %s", h % 12 ? h % 12 : 12, h < 12 ? "AM" : "PM");
}

// From / Until: an hour with -/+
class HourScreen : public UIScreen {
public:
  HourScreen(UITask* task) : _task(task) {}
  void open(uint8_t* hour, const char* title) { _hour = hour; _title = title; }

  int render(DisplayDriver& d) override {
    char tmp[16];
    themeHeader(d, _title, _task->getBattMilliVolts());
    themeCard(d, 4, 27, 120, 40);
    themeLabel(d, 9, 31, "Hour");
    fmtHour(tmp, *_hour, _task->uiPrefs().clock24);
    d.setTextSize(3);
    d.setColor(UIColor::primary_txt);
    d.setCursor(9, 41);
    d.print(tmp);
    drawMinusPlus(d, B_MINUS, B_PLUS);
    drawBtn(d, B_DONE, "Done", 1, true);
    return 5000;
  }
  void bump(int delta) {
    *_hour = (uint8_t)((*_hour + 24 + delta) % 24);
    _task->nightChanged();
  }
  bool handleTap(int x, int y) {
    if (inBtn(B_MINUS, x, y)) bump(-1);
    else if (inBtn(B_PLUS, x, y)) bump(+1);
    else if (inBtn(B_DONE, x, y)) _task->closeHour();
    return true;
  }
  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) { bump(+1); return true; }
    if (c == KEY_PREV || c == KEY_LEFT) { bump(-1); return true; }
    if (c == KEY_ENTER) { _task->closeHour(); return true; }
    return false;
  }

private:
  static constexpr Btn B_MINUS = {4, 72, 56, 24};
  static constexpr Btn B_PLUS = {68, 72, 56, 24};
  static constexpr Btn B_DONE = {4, 99, 120, 19};
  UITask* _task;
  uint8_t* _hour = nullptr;
  const char* _title = "";
};
