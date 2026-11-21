// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// The radio settings editor (overview, value screens, review).

// ---- SF32: radio settings editor ----
//
// Overview (RadioEditScreen): one card per setting, changed ones tinted amber, and
// "Apply N changes". Tapping a card opens that setting's own screen (RadioFieldScreen:
// saved vs new value, -/+, Done). Apply shows the changes (old -> new) to confirm
// (RadioReviewScreen). Nothing touches the radio until then; leaving with unsaved changes
// asks Keep editing / Discard / Apply. Left/right swipes do nothing in these screens.

struct RadioPreset { const char* name; float freq, bw; uint8_t sf, cr; };
// MeshCore community presets in this board's 902-928 MHz band (api.meshcore.nz config)
static const RadioPreset RADIO_PRESETS[] = {
  {"USA/Canada", 910.525f, 62.5f, 7, 5},
  {"Australia (Narrow)", 916.575f, 62.5f, 7, 7},
  {"New Zealand (Narrow)", 917.375f, 62.5f, 7, 5},
};
static const int NUM_PRESETS = sizeof(RADIO_PRESETS) / sizeof(RADIO_PRESETS[0]);
static const float BW_STEPS[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f};
static const int NUM_BW = sizeof(BW_STEPS) / sizeof(BW_STEPS[0]);
static const float FREQ_STEPS[] = {0.025f, 0.1f, 1.0f, 0.001f};
static const int NUM_FREQ_STEPS = sizeof(FREQ_STEPS) / sizeof(FREQ_STEPS[0]);
#define FREQ_MIN 902.0f  // the board's RF front end / antenna band
#define FREQ_MAX 928.0f
#define TX_MIN   -9
static const ColorVal THEME_CHANGED = ((0x3D & 0xF8) << 8) | ((0x2E & 0xFC) << 3) | (0x12 >> 3);  // amber tint

static bool feq(float a, float b) { return fabsf(a - b) < 0.0005f; }

enum RadioField { RF_PRESET, RF_FREQ, RF_BW, RF_SF, RF_CR, RF_TX, RF_COUNT };
static const char* const RF_TITLES[RF_COUNT] = {"Preset", "Frequency", "Bandwidth", "Spreading f.", "Coding rate", "TX power"};
static const char* const RF_SHORT[RF_COUNT] = {"Preset", "Freq", "BW", "SF", "CR", "TX"};

struct RadioValues {
  float freq, bw;
  uint8_t sf, cr;
  int8_t tx;
  int presetIndex() const {
    for (int i = 0; i < NUM_PRESETS; i++) {
      auto& p = RADIO_PRESETS[i];
      if (feq(freq, p.freq) && feq(bw, p.bw) && sf == p.sf && cr == p.cr) return i;
    }
    return -1;
  }
  bool differs(int f, const RadioValues& o) const {
    switch (f) {
      case RF_PRESET: return presetIndex() != o.presetIndex();
      case RF_FREQ: return !feq(freq, o.freq);
      case RF_BW: return !feq(bw, o.bw);
      case RF_SF: return sf != o.sf;
      case RF_CR: return cr != o.cr;
      case RF_TX: return tx != o.tx;
    }
    return false;
  }
  int changes(const RadioValues& o) const {
    int n = 0;
    for (int f = RF_FREQ; f < RF_COUNT; f++) n += differs(f, o);
    return n;
  }
  void format(int f, char* buf, bool units = true) const {
    switch (f) {
      case RF_PRESET: { int i = presetIndex(); strcpy(buf, i >= 0 ? RADIO_PRESETS[i].name : "Custom"); break; }
      case RF_FREQ: sprintf(buf, units ? "%.3f MHz" : "%.3f", freq); break;
      case RF_BW: sprintf(buf, units ? "%g kHz" : "%g", bw); break;
      case RF_SF: sprintf(buf, "%d", sf); break;
      case RF_CR: sprintf(buf, "%d", cr); break;
      case RF_TX: sprintf(buf, units ? "%d dBm" : "%d", tx); break;
    }
  }
  void bump(int f, int dir, int freq_step) {
    switch (f) {
      case RF_PRESET: {
        int i = presetIndex();
        i = i < 0 ? (dir > 0 ? 0 : NUM_PRESETS - 1) : (i + dir + NUM_PRESETS) % NUM_PRESETS;
        auto& p = RADIO_PRESETS[i];
        freq = p.freq; bw = p.bw; sf = p.sf; cr = p.cr;
        break;
      }
      case RF_FREQ: {
        float v = roundf((freq + dir * FREQ_STEPS[freq_step]) * 1000) / 1000;
        freq = v < FREQ_MIN ? FREQ_MIN : (v > FREQ_MAX ? FREQ_MAX : v);
        break;
      }
      case RF_BW: {
        int j = 0;
        while (j < NUM_BW - 1 && !feq(BW_STEPS[j], bw) && BW_STEPS[j] < bw) j++;
        if (!feq(BW_STEPS[j], bw) && dir < 0 && j > 0) j++;  // between steps
        j += dir;
        bw = BW_STEPS[j < 0 ? 0 : (j >= NUM_BW ? NUM_BW - 1 : j)];
        break;
      }
      case RF_SF: sf = constrain(sf + dir, 5, 12); break;
      case RF_CR: cr = constrain(cr + dir, 5, 8); break;
      case RF_TX: tx = constrain(tx + dir, TX_MIN, MAX_LORA_TX_POWER); break;
    }
  }
};

static RadioValues radioFromPrefs(const NodePrefs* p) {
  RadioValues v;
  v.freq = p->freq; v.bw = p->bw; v.sf = p->sf; v.cr = p->cr; v.tx = p->tx_power_dbm;
  return v;
}

// text that fits in max_w at the largest size <= max_size; cy >= 0: centre it on that line
static void drawFitted(DisplayDriver& d, int x, int y, int max_w, const char* text, int max_size, bool right = false,
                       int cy = -1) {
  int size = max_size;
  d.setTextSize(size);
  while (size > 1 && d.getTextWidth(text) > max_w) d.setTextSize(--size);
  if (cy >= 0) y = cy - 4 * size;
  if (right) d.drawTextRightAlign(x, y, text);
  else { d.setCursor(x, y); d.print(text); }
}

class RadioEditScreen : public UIScreen {
public:
  RadioValues v;

  RadioEditScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void load() { v = radioFromPrefs(_prefs); _sel = 0; }
  RadioValues saved() const { return radioFromPrefs(_prefs); }
  int changes() const { return v.changes(saved()); }

  int render(DisplayDriver& d) override {
    char tmp[24];
    RadioValues sv = saved();
    themeHeader(d, "Edit radio", _task->getBattMilliVolts());
    for (int f = 0; f < RF_COUNT; f++) {
      const Btn& c = CELLS[f];
      bool diff = f != RF_PRESET && v.differs(f, sv);
      d.setColor(diff ? THEME_CHANGED : THEME_CARD);
      d.fillRect(c.x, c.y, c.w, c.h);
      v.format(f, tmp, false);
      if (f == RF_PRESET || f == RF_FREQ || f == RF_TX) {  // one line: label left, value right
        const char* label = f == RF_FREQ ? "Freq MHz" : (f == RF_TX ? "TX dBm" : "Preset");
        themeLabel(d, c.x + 5, c.y + (c.h - 8) / 2, label);
        d.setTextSize(1);
        int lw = d.getTextWidth(label);
        d.setColor(f == RF_PRESET ? THEME_ACCENT : (diff ? UIColor::warning_txt : UIColor::primary_txt));
        drawFitted(d, c.x + c.w - 5, 0, c.w - lw - 14, tmp, f == RF_PRESET ? 1 : 2, true, c.y + c.h / 2);
      } else {                                              // label above value
        themeLabel(d, c.x + 5, c.y + 3, f == RF_BW ? "BW kHz" : RF_SHORT[f]);
        d.setTextSize(2);
        d.setColor(diff ? UIColor::warning_txt : UIColor::primary_txt);
        d.setCursor(c.x + 5, c.y + 11);
        d.print(tmp);
      }
    }
    int n = changes();
    if (n > 0) sprintf(tmp, n == 1 ? "Apply 1 change" : "Apply %d changes", n);
    else strcpy(tmp, "No changes");
    drawBtn(d, CELLS[RF_COUNT], tmp, 1, n > 0);
    drawSelBar(d, CELLS[_sel]);
    return 2000;
  }

  void activate(int i) {
    if (i < RF_COUNT) _task->openRadioField(i);
    else if (changes() > 0) _task->openRadioReview(false);
  }

  bool handleTap(int x, int y) {
    for (int i = 0; i < NUM_CELLS; i++) {
      if (inBtn(CELLS[i], x, y)) {
        _sel = i;
        activate(i);
        return true;
      }
    }
    return false;
  }

  // button A: click = next card, double click = previous, long press = open it
  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) { _sel = (_sel + 1) % NUM_CELLS; return true; }
    if (c == KEY_PREV || c == KEY_LEFT) { _sel = (_sel + NUM_CELLS - 1) % NUM_CELLS; return true; }
    if (c == KEY_ENTER) { activate(_sel); return true; }
    return false;
  }

private:
  static const int NUM_CELLS = RF_COUNT + 1;
  static constexpr Btn CELLS[NUM_CELLS] = {
    {4, 27, 120, 18},                                      // preset
    {4, 48, 120, 24},                                      // frequency
    {4, 75, 38, 25}, {45, 75, 38, 25}, {86, 75, 38, 25},   // BW, SF, CR
    {4, 103, 56, 18},                                      // TX power
    {63, 103, 61, 18},                                     // Apply
  };
  UITask* _task;
  NodePrefs* _prefs;
  int _sel = 0;
};
constexpr Btn RadioEditScreen::CELLS[];

// one setting: saved vs new value, -/+ (and step size for the frequency), Done
class RadioFieldScreen : public UIScreen {
public:
  RadioFieldScreen(UITask* task, RadioEditScreen* edit) : _task(task), _edit(edit) {}
  void open(int field) { _field = field; }

  int render(DisplayDriver& d) override {
    char tmp[32], saved[24];
    RadioValues sv = _edit->saved();
    bool diff = _edit->v.differs(_field, sv);
    themeHeader(d, RF_TITLES[_field], _task->getBattMilliVolts());

    d.setColor(diff ? THEME_CHANGED : THEME_CARD);
    d.fillRect(4, 27, 120, 44);
    themeLabel(d, 9, 31, diff ? "New" : "Current");
    sv.format(_field, saved);
    sprintf(tmp, "saved: %s", saved);
    d.setColor(UIColor::secondary_txt);
    drawFitted(d, 119, 31, 80, tmp, 1, true);
    _edit->v.format(_field, tmp, _field != RF_FREQ);
    d.setColor(diff ? UIColor::warning_txt : UIColor::primary_txt);
    drawFitted(d, 9, 44, 110, tmp, 3);

    drawMinusPlus(d, B_MINUS, B_PLUS);
    if (_field == RF_FREQ) {
      sprintf(tmp, "Step %g", FREQ_STEPS[_step]);
      drawBtn(d, B_STEP, tmp, 1, false);
      drawBtn(d, B_DONE, "Done", 1, true);
    } else {
      drawBtn(d, B_DONE_WIDE, "Done", 1, true);
    }
    return 2000;
  }

  bool handleTap(int x, int y) {
    if (inBtn(B_MINUS, x, y)) _edit->v.bump(_field, -1, _step);
    else if (inBtn(B_PLUS, x, y)) _edit->v.bump(_field, +1, _step);
    else if (_field == RF_FREQ && inBtn(B_STEP, x, y)) _step = (_step + 1) % NUM_FREQ_STEPS;
    else if (inBtn(_field == RF_FREQ ? B_DONE : B_DONE_WIDE, x, y)) _task->closeRadioField();
    return true;
  }

  // button A: click = +, double click = -, long press = done
  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) { _edit->v.bump(_field, +1, _step); return true; }
    if (c == KEY_PREV || c == KEY_LEFT) { _edit->v.bump(_field, -1, _step); return true; }
    if (c == KEY_ENTER) { _task->closeRadioField(); return true; }
    return false;
  }

private:
  static constexpr Btn B_MINUS = {4, 76, 56, 22};
  static constexpr Btn B_PLUS = {68, 76, 56, 22};
  static constexpr Btn B_STEP = {4, 101, 58, 18};
  static constexpr Btn B_DONE = {66, 101, 58, 18};
  static constexpr Btn B_DONE_WIDE = {4, 101, 120, 18};
  UITask* _task;
  RadioEditScreen* _edit;
  int _field = RF_FREQ;
  int _step = 0;
};

// the list of changes, old -> new; to confirm Apply, or when leaving with changes
class RadioReviewScreen : public UIScreen {
public:
  RadioReviewScreen(UITask* task, RadioEditScreen* edit) : _task(task), _edit(edit) {}
  void open(bool leaving) { _leaving = leaving; _sel = leaving ? 0 : 1; }

  int render(DisplayDriver& d) override {
    char a[24], b[24], line[48];
    RadioValues sv = _edit->saved();
    themeHeader(d, _leaving ? "Unsaved" : "Apply?", _task->getBattMilliVolts());
    themeCard(d, 4, 27, UI_W - 8, 68);
    int y = 31;
    for (int f = RF_FREQ; f < RF_COUNT; f++) {
      if (!_edit->v.differs(f, sv)) continue;
      sv.format(f, a, false);
      _edit->v.format(f, b, false);
      themeLabel(d, 9, y, RF_SHORT[f]);
      sprintf(line, "%s -> %s", a, b);
      d.setColor(UIColor::warning_txt);
      drawFitted(d, 119, y, 90, line, 1, true);
      y += 12;
    }
    if (_leaving) {
      d.setTextSize(1);
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(UI_CX, 84, "not applied yet");
      drawBtn(d, L_KEEP, "Keep", 1, false);
      drawBtn(d, L_DISCARD, "Discard", 1, false);
      drawBtn(d, L_APPLY, "Apply", 1, true);
      drawSelBar(d, _sel == 0 ? L_KEEP : (_sel == 1 ? L_DISCARD : L_APPLY));
    } else {
      d.setTextSize(1);
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(UI_CX, 84, "other nodes must match");
      drawBtn(d, A_BACK, "Back", 1, false);
      drawBtn(d, A_APPLY, "Apply", 1, true);
      drawSelBar(d, _sel == 0 ? A_BACK : A_APPLY);
    }
    return 2000;
  }

  void choose(int i) {
    if (_leaving) {
      if (i == 0) _task->closeRadioReview();
      else if (i == 1) _task->discardRadio();
      else _task->applyRadio();
    } else {
      if (i == 0) _task->closeRadioReview();
      else _task->applyRadio();
    }
  }

  bool handleTap(int x, int y) {
    if (_leaving) {
      if (inBtn(L_KEEP, x, y)) choose(0);
      else if (inBtn(L_DISCARD, x, y)) choose(1);
      else if (inBtn(L_APPLY, x, y)) choose(2);
    } else {
      if (inBtn(A_BACK, x, y)) choose(0);
      else if (inBtn(A_APPLY, x, y)) choose(1);
    }
    return true;
  }

  bool handleInput(char c) override {
    int n = _leaving ? 3 : 2;
    if (c == KEY_NEXT || c == KEY_RIGHT) { _sel = (_sel + 1) % n; return true; }
    if (c == KEY_PREV || c == KEY_LEFT) { _sel = (_sel + n - 1) % n; return true; }
    if (c == KEY_ENTER) { choose(_sel); return true; }
    return false;
  }

private:
  static constexpr Btn L_KEEP = {4, 99, 38, 19};
  static constexpr Btn L_DISCARD = {45, 99, 38, 19};
  static constexpr Btn L_APPLY = {86, 99, 38, 19};
  static constexpr Btn A_BACK = {4, 99, 58, 19};
  static constexpr Btn A_APPLY = {66, 99, 58, 19};
  UITask* _task;
  RadioEditScreen* _edit;
  bool _leaving = false;
  int _sel = 0;
};

void UITask::createEditors() {
  theme_screen = new ThemeScreen(this);
  about_screen = new AboutScreen(this);
  logs_screen = new LogScreen(this);
  tz_screen = new TimezoneScreen(this, _node_prefs);
  hour_screen = new HourScreen(this);
  aod_screen = new AodScreen(this);
  createSosScreen();
  radio_edit = new RadioEditScreen(this, _node_prefs);
  radio_field = new RadioFieldScreen(this, (RadioEditScreen*)radio_edit);
  radio_review = new RadioReviewScreen(this, (RadioEditScreen*)radio_edit);
}

bool UITask::isRadioScreen() const {
  return curr != NULL && (curr == radio_edit || curr == radio_field || curr == radio_review);
}
bool UITask::radioHasChanges() const {
  return radio_edit && ((RadioEditScreen*)radio_edit)->changes() > 0;
}

void UITask::openRadioEditor() {
  ((RadioEditScreen*)radio_edit)->load();
  _menu_return = NULL;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(radio_edit);
}
void UITask::leaveRadioEditor() {  // swipe up/down on the overview
  if (radioHasChanges()) openRadioReview(true);
  else discardRadio();
}
void UITask::discardRadio() {
  ((RadioEditScreen*)radio_edit)->load();
  ((HomeScreen*)home)->showRadio();
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  setCurrScreen(home);
}
void UITask::openRadioField(int field) {
  ((RadioFieldScreen*)radio_field)->open(field);
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(radio_field);
}
void UITask::closeRadioField() {
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  setCurrScreen(radio_edit);
}
void UITask::openRadioReview(bool leaving) {
  ((RadioReviewScreen*)radio_review)->open(leaving);
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_UP);
  setCurrScreen(radio_review);
}
void UITask::closeRadioReview() {
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_DOWN);
  setCurrScreen(radio_edit);
}
void UITask::applyRadio() {
  RadioValues& v = ((RadioEditScreen*)radio_edit)->v;
  _node_prefs->freq = v.freq;
  _node_prefs->bw = v.bw;
  _node_prefs->sf = v.sf;
  _node_prefs->cr = v.cr;
  _node_prefs->tx_power_dbm = v.tx;
  the_mesh.savePrefs();
  radio_driver.setParams(_node_prefs->freq, _node_prefs->bw, _node_prefs->sf, _node_prefs->cr);
  radio_driver.setTxPower(_node_prefs->tx_power_dbm);
  discardRadio();  // back to the Radio page (nothing left to discard)
  showAlert("Radio settings applied", 1500);
}
