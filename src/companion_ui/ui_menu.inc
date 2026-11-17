// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Menu actions, drag scrolling, MenuScreen.

// ---- SF32: menus ----

enum MenuAction : uint8_t {
  ACT_CLOSE, ACT_SCREEN_OFF, ACT_REBOOT, ACT_POWER_OFF, ACT_SETTINGS,
  ACT_READ_MSGS, ACT_GOTO_TZ, ACT_PAGE_PRIMARY, ACT_BT_FORGET, ACT_DISC_NAMES,
  ACT_TZ_PLUS, ACT_TZ_MINUS, ACT_MSG_CLEAR, ACT_ADVERT,
  ACT_SET_BRIGHTNESS, ACT_SET_TIMEOUT, ACT_SET_CLOCK, ACT_SET_TZ, ACT_SET_BT,
  ACT_SET_DST, ACT_RADIO_EDIT, ACT_THEME, ACT_TIME_MENU, ACT_ABOUT, ACT_SET_ALERTS, ACT_SET_SOUND, ACT_SET_LIFT, ACT_SET_FLIP, ACT_POWER_MENU, ACT_LOGS, ACT_NIGHT_MENU, ACT_NIGHT_TOGGLE, ACT_NIGHT_FROM,
  ACT_NIGHT_UNTIL, ACT_NIGHT_LEVEL, ACT_SET_AOD, ACT_THEME_MENU, ACT_THEME_STYLE, ACT_THEME_BG, ACT_BATT_COLORS, ACT_MENU_BACK, ACT_BT_PAIR, ACT_BOT_MENU, ACT_POWER_OFF_MENU, ACT_BOT_MAX, ACT_BOT_REPEAT, ACT_BOT_GAP, ACT_BATT_ICON, ACT_BATT_MENU, ACT_BATT_GRAPH, ACT_BATT_WARN, ACT_CHG_LIMIT,
  ACT_BOT_TOGGLE, ACT_BOT_DM, ACT_BOT_CHAN,
  ACT_SET_TOUCH_SND, ACT_TAP_WAKE, ACT_LOW_POWER, ACT_LP_MENU, ACT_LP_AUTO, ACT_GLASS_LIGHT, ACT_GAMES_MENU, ACT_PET_SOUND, ACT_PET_RESET, ACT_PET_PLAY, ACT_PET_FRIENDS, ACT_SIG_TOGGLE, ACT_SIG_INTERVAL, ACT_SIG_LOCK, ACT_SIG_CLEAR, ACT_SOS, ACT_SOS_CHAN, ACT_SET_MESH_TIME, ACT_SHARE_QR, ACT_CLOCK_RESET, ACT_SOUND_MENU, ACT_DISPLAY_MENU, ACT_MOTION_MENU, ACT_NIGHT_DND,
  ACT_CLOCK_FACE, ACT_PAGES_MENU, ACT_BAND_SCAN, ACT_POWER_SAVE, ACT_FLASHLIGHT, ACT_ADVERTS_MENU, ACT_ADV_NEARBY, ACT_ADV_MESH, ACT_ADVERT_MESH, ACT_ADV_PAGE_MODE, ACT_USB_APP, ACT_SIG_FINDER, ACT_ROTATION, ACT_MUTE_MENU,
  ACT_CHAN_MUTE = 150,    // + row: Settings > Sound > Channels (UITask::_mute_rows: the channel)
  ACT_PAGE_TOGGLE = 200,  // + page index: Settings > Pages
};

static_assert(ACT_MUTE_MENU < ACT_CHAN_MUTE && ACT_CHAN_MUTE + 20 <= ACT_PAGE_TOGGLE, "menu action ranges overlap");

#define NUM_CLOCK_FACES 7
static const char* const CLOCK_FACE_NAMES[NUM_CLOCK_FACES] = {"Digital", "Analog", "Big", "Info", "Color", "Sun", "Starlit"};

// Drag scrolling for lists (logical px): the list follows the finger; a flick keeps it
// gliding and slowing down. Lists draw into SF32Display's layer once and copy the visible
// slice each frame, so this runs at the panel's frame rate.
struct DragScroll {
  float pos = 0, vel = 0, start = 0;
  int max = 0;
  bool active = false, from_top = false, from_bottom = false;
  unsigned long last = 0;

  void clamp() { if (pos > max) pos = max; if (pos < 0) pos = 0; }
  void set(float p) { pos = p; clamp(); }
  void begin() { active = true; start = pos; from_top = pos < 1; from_bottom = pos > max - 1; vel = 0; }
  void move(int finger_dy_px) { set(start - finger_dy_px / SF32Display::SCALE); }
  // a flick: a bit livelier than the finger's speed, as on phones
  void end(float v_px_per_ms) { active = false; vel = -1.3f * v_px_per_ms / SF32Display::SCALE; last = millis(); }
  bool step() {  // momentum; true while it still moves
    if (active) return true;
    if (fabsf(vel) < 0.02f) { vel = 0; return false; }
    unsigned long now = millis();
    float dt = now - last;
    last = now;
    if (dt > 50) dt = 50;
    set(pos + vel * dt);
    vel *= expf(-dt / 500.0f);  // glide ~0.5 s
    if (pos <= 0 || pos >= max) vel = 0;
    return true;
  }
};

// A list of tappable rows (title on top, like the pages). Touch: tap a row. Button A /
// taps without coordinates: click = next row, double click = previous, long press = choose.
class MenuScreen : public UIScreen {
public:
  struct Item {
    char label[28];
    char value[16];
    uint8_t action;
  };

  MenuScreen(UITask* task) : _task(task) {}

  void begin(const char* title) {
    strncpy(_title, title, sizeof(_title) - 1);
    _title[sizeof(_title) - 1] = 0;
    _n = 0;
    _sel = 0;
    _dirty = true;  // compared with what's drawn at the next render: only changes are redrawn
  }
  // keep the scroll position when the same menu is rebuilt (a setting changed)
  void resetScroll() { _ds = DragScroll(); }
  void add(const char* label, uint8_t action, const char* value = "") {
    if (_n >= MAX_ITEMS) return;
    strncpy(_items[_n].label, label, sizeof(_items[_n].label) - 1);
    _items[_n].label[sizeof(_items[_n].label) - 1] = 0;
    strncpy(_items[_n].value, value, sizeof(_items[_n].value) - 1);
    _items[_n].value[sizeof(_items[_n].value) - 1] = 0;
    _items[_n].action = action;
    _n++;
  }
  void setSelected(int i) { if (i >= 0 && i < _n) { _sel = i; _dirty = true; ensureVisible(i); } }
  uint8_t items_action(int i) const { return _items[i].action; }
  int selected() const { return _sel; }
  const char* title() const { return _title; }

  // Row geometry, logical px (square layout): big rows; menus with more rows than fit
  // the list area scroll (swipe up/down, or following the selection with button A).
  static const int ROW_H = 22, ROW_GAP = 2, LIST_Y = 27, LIST_H = 96;  // 4 rows fit
  int rowY(int i) const { return LIST_Y + i * (ROW_H + ROW_GAP) - (int)(_ds.pos + 0.5f); }
  int maxScroll() const { int h = _n * (ROW_H + ROW_GAP) - ROW_GAP - LIST_H; return h > 0 ? h : 0; }
  bool scrolls() const { return maxScroll() > 0; }

  // dir > 0: show later rows (button / no-touch use)
  void scrollBy(int dir) { _ds.set(_ds.pos + dir * 2 * (ROW_H + ROW_GAP)); }
  void ensureVisible(int i) {
    int top = i * (ROW_H + ROW_GAP), bottom = top + ROW_H;
    if (top < _ds.pos) _ds.set(top);
    if (bottom > _ds.pos + LIST_H) _ds.set(bottom - LIST_H);
  }
  DragScroll& drag() { _ds.max = maxScroll(); return _ds; }

  int hitTest(int x, int y) const {
    if (x < 4 || x >= 124 || y < LIST_Y - 1 || y >= LIST_Y + LIST_H + 1) return -1;
    for (int i = 0; i < _n; i++) {
      if (y >= rowY(i) - 1 && y < rowY(i) + ROW_H + 2) return i;
    }
    return -1;
  }

  int render(DisplayDriver& display) override {
#ifdef SF32_FRAME_LOG
    rt_tick_t m0 = rt_tick_get();
#endif
    if (!((SF32Display&)display).listFrame()) themeHeader(display, _title, _task->getBattMilliVolts());  // (a list frame only sends the list)
#ifdef SF32_FRAME_LOG
    rt_tick_t m1 = rt_tick_get();
    uint32_t mrows = _marquee_rows;
    bool was_dirty = _dirty;
#endif
    _ds.max = maxScroll();
    bool moving = _ds.step();
    SF32Display& sd = (SF32Display&)display;
    if (_entering && sd.transitionPending()) _entering = false;  // a slide instead: no glide on top

    // The rows live in the layer, redrawn only when they change. A new list draws just the
    // rows in view first (it appears sooner), then the rest a few per frame. A label too
    // long for its row scrolls sideways; only that row is redrawn, and only when it moves.
    uint32_t all = _n >= 32 ? 0xFFFFFFFFUL : (1UL << _n) - 1;
    int first = (int)(_ds.pos / (ROW_H + ROW_GAP)), last = (int)((_ds.pos + LIST_H - 1) / (ROW_H + ROW_GAP));
    bool fresh = false;
    if (_dirty) {  // rebuilt: still the list in the layer? then only redraw rows that changed
      _dirty = false;
      int vs = valueSize(display);
      // the theme's colours (Rainbow changes them with the page it was opened from): rows
      // drawn in other colours can't be kept
      uint32_t colors = ((uint32_t)THEME_CARD << 16) ^ (uint32_t)THEME_ACCENT ^ ((uint32_t)UIColor::primary_txt << 5) ^ (sd.glassOn() ? 0x5A5A5A5AUL : 0);
      bool vs_changed = vs != _vs || colors != _colors;
      _vs = vs;
      _colors = colors;
      if (sd.layerGen() == _layer_gen && _n == _shown_n && !vs_changed) {
        for (int i = 0; i < _n; i++) {
          if (memcmp(&_items[i], &_shown[i], sizeof(Item)) != 0 || (i == _sel) != (i == _shown_sel))
            _drawn &= ~(1UL << i);
        }
      } else {  // a different list: start over, the rows in view first
        fresh = true;
        _drawn = 0;
        _marquee_rows = 0;
        _marquee_set = false;
        sd.beginLayer(_n * (ROW_H + ROW_GAP), false);  // each row clears its own strip
        _layer_gen = sd.layerGen();
        // (gliding in: each row is drawn when its turn comes, so the first frame comes at once)
        if (!_entering) for (int i = first; i <= last && i < _n; i++) drawRow(display, i);
        sd.endLayer();
      }
      memcpy(_shown, _items, sizeof(Item) * _n);
      _shown_n = _n;
      _shown_sel = _sel;
    }
    if (!fresh && !_entering) {  // (nothing else while rows glide in: the frames are for that)
      bool urgent = _ds.active || moving;  // scrolling: the rest are needed now
      // only the list changes while it scrolls: send just its band to the panel
      if (urgent) sd.setFlushBand(LIST_Y, LIST_H);
      uint32_t due = (_marquee_rows && (long)(millis() - _marquee_at) >= 0) ? _marquee_rows : 0;
      if ((_drawn & all) != all || due) {
        if (due) _marquee_set = false;  // the redraw below sets the next time
        sd.resumeLayer();
        for (int i = first; i <= last && i < _n; i++)  // rows in view: always now
          if (!(_drawn & (1UL << i))) drawRow(display, i);
        int budget = urgent ? _n : 3;                  // the others: a few per frame
        for (int i = 0; i < _n && budget > 0; i++) {
          if (!(_drawn & (1UL << i))) { drawRow(display, i); budget--; }
        }
        for (int i = 0; i < _n; i++) if ((due & (1UL << i)) && (_drawn & (1UL << i))) drawRow(display, i);
        sd.endLayer();
      }
    }
#ifdef SF32_FRAME_LOG
    rt_tick_t m2 = rt_tick_get();
#endif
    if (_entering) {
      // rows glide up into place, one after another
      unsigned long el = millis() - _enter_at;
      bool busy = false;
      for (int i = first, k = 0; i <= last && i < _n; i++, k++) {
        long since = (long)el - k * 30;
        if (since <= 0) { busy = true; continue; }
        if (!(_drawn & (1UL << i))) {  // its turn
          sd.resumeLayer();
          drawRow(display, i);
          sd.endLayer();
        }
        float t = since / 150.0f;
        if (t < 1) busy = true; else t = 1;
        int y = rowY(i) + (int)((1 - t) * (1 - t) * (1 - t) * 24 + 0.5f), h = ROW_H, src = i * (ROW_H + ROW_GAP);
        if (y < LIST_Y) { h -= LIST_Y - y; src += LIST_Y - y; y = LIST_Y; }
        if (y + h > LIST_Y + LIST_H) h = LIST_Y + LIST_H - y;
        if (h > 0 && !sd.composeList(y, h, (int)lroundf(src * SF32Display::SCALE)))
          sd.composeLayer(4, y, 120, h, (int)lroundf(src * SF32Display::SCALE));
      }
      if (!_enter_first) sd.setFlushBand(LIST_Y, LIST_H);  // the first frame has the new header to send
      _enter_first = false;
      if (!busy) _entering = false;
    } else {
      int src = (int)lroundf(_ds.pos * SF32Display::SCALE);
      // only as far as the rows go: below the last one the layer still holds an older list's
      int h = _n * (ROW_H + ROW_GAP) - (int)_ds.pos;
      if (h > LIST_H) h = LIST_H;
      if (h > 0 && !sd.composeList(LIST_Y, h, src)) sd.composeLayer(4, LIST_Y, 120, h, src);
    }
#ifdef SF32_FRAME_LOG
    rt_kprintf("menu: header %d, rows %d (dirty %d, marquee %lx), compose %d ms\n", (int)(m1 - m0), (int)(m2 - m1),
               was_dirty, (unsigned long)mrows, (int)(rt_tick_get() - m2));
#endif

    if (_handle) {  // a grab handle: swipe up to put the menu back
      display.setColor(THEME_DIM);
      display.fillRect(52, 124, 24, 2);
    }
    if (scrolls()) {  // scrollbar on the right edge
      int track = LIST_H, total = track + maxScroll();
      int thumb = track * track / total;
      int ty = LIST_Y + (int)((track - thumb) * (_ds.pos / maxScroll()));
      display.setColor(THEME_DIM);
      display.fillRect(125, LIST_Y, 2, track);
      display.setColor(THEME_ACCENT);
      display.fillRect(125, ty, 2, thumb);
    }
    // still moving: the next frame only changes the list. The frame that was told so must go out
    // as just its band, moving or not: nothing else in it was drawn.
    if (sd.listFrame()) sd.setFlushBand(LIST_Y, LIST_H);
    if (!_entering && (moving || _ds.active)) sd.nextFrameIsList();
    if (_entering || moving || (_drawn & all) != all) return 16;  // rows gliding in / follow the finger / glide / finish the rows
    if (_marquee_rows) {                              // a long label: when it next moves
      long wait = (long)(_marquee_at - millis());
      return wait < 16 ? 16 : wait > 1000 ? 1000 : (int)wait;
    }
    return 1000;
  }

  void startEnter() { _entering = _enter_first = true; _enter_at = millis(); }  // rows glide in

  // a hint at the bottom edge: this menu came down from the top, swipe up to put it back
  void setHandle(bool on) { _handle = on; }

  bool handleInput(char c) override;  // after UITask's methods

private:
  // one size for all the values in a menu (mixed sizes look untidy): big if every label
  // still fits beside its value at that size, else small. Two-line values are always small.
  int valueSize(DisplayDriver& display) {
    display.setTextSize(2);
    for (int i = 0; i < _n; i++) {
      if (!_items[i].value[0] || strchr(_items[i].value, '\n')) continue;
      if (10 + display.getTextWidth(_items[i].label) + 6 + display.getTextWidth(_items[i].value) > 119) return 1;
    }
    return 2;
  }

  void drawRow(DisplayDriver& display, int i) {
    drawRowBody(display, i);
    ((SF32Display&)display).layerRowsDone(i * (ROW_H + ROW_GAP), ROW_H + ROW_GAP);
  }
  void drawRowBody(DisplayDriver& display, int i) {
    _drawn |= 1UL << i;
    _marquee_rows &= ~(1UL << i);  // set again below if its label still scrolls
    int y = i * (ROW_H + ROW_GAP);
    ((SF32Display&)display).clearRows(y, ROW_H + ROW_GAP);  // its strip, gap included
    themeCard(display, 4, y, 120, ROW_H);
    if (i == _sel) {  // selection: accent bar on the left
      display.setColor(THEME_ACCENT);
      display.fillRect(4, y + 2, 2, ROW_H - 4);
    }
    int right = 119;
    const char* nl = strchr(_items[i].value, '\n');
    if (nl) {  // a two-line value ("never\non USB"): small, stacked
      char first[16];
      int k = nl - _items[i].value;
      memcpy(first, _items[i].value, k);
      first[k] = 0;
      display.setTextSize(1);
      display.setColor(THEME_ACCENT);
      display.drawTextRightAlign(119, y + 3, first);
      display.drawTextRightAlign(119, y + 12, nl + 1);
      int w = display.getTextWidth(first), w2 = display.getTextWidth(nl + 1);
      right = 119 - (w > w2 ? w : w2) - 6;
    } else if (_items[i].value[0]) {  // value: the menu's size (valueSize)
      display.setTextSize(_vs);
      display.setColor(THEME_ACCENT);
      display.drawTextRightAlign(119, y + (ROW_H - 8 * _vs) / 2, _items[i].value);
      right = 119 - display.getTextWidth(_items[i].value) - 6;
    }
    display.setTextSize(2);
    display.setColor(_items[i].action == ACT_POWER_OFF ? UIColor::warning_txt : UIColor::primary_txt);
    int wait = drawMarquee(display, 10, y + (ROW_H - 16) / 2, right - 10, _items[i].label);
    if (wait) {  // the soonest any scrolling label moves again
      _marquee_rows |= 1UL << i;
      unsigned long at = millis() + wait;
      if (!_marquee_set || (long)(at - _marquee_at) < 0) _marquee_at = at;
      _marquee_set = true;
    }
  }

private:
  static const int MAX_ITEMS = 20;  // Settings has 15
  UITask* _task;
  char _title[16] = "";
  Item _items[MAX_ITEMS];
  int _n = 0, _sel = 0;
  DragScroll _ds;
  bool _dirty = true, _handle = false;
  int _vs = 2;  // the values' text size in this menu (valueSize)
  bool _entering = false, _enter_first = false;
  unsigned long _enter_at = 0;
  uint32_t _colors = 0;  // theme colours the rows in the layer were drawn in
  uint32_t _marquee_rows = 0;  // rows whose label scrolls sideways
  unsigned long _marquee_at = 0;  // when one of them next moves
  bool _marquee_set = false;
  uint32_t _drawn = 0;  // rows drawn into the layer since it was last cleared
  // what the layer shows (to redraw only rows that changed when a menu is rebuilt)
  Item _shown[MAX_ITEMS];
  int _shown_n = -1, _shown_sel = -1;
  uint32_t _layer_gen = 0;
};
