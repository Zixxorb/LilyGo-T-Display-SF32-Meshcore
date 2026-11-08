// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Time zone offset, screen resume, the UI settings file (/sf32_ui).

int32_t UITask::localOffsetSecs(uint32_t utc) const {
  int std_h = _node_prefs->tz_offset;
  return (std_h + (dstActive(_ui_prefs.dst_rule, std_h, utc) ? 1 : 0)) * 3600;
}

void UITask::resumeOrReset() {
  // woken within 10 s of turning off: carry on where it was; otherwise the clock
  if (isRadioScreen() && radioHasChanges()) return;  // unsaved radio changes: never drop them
  if (_sos_state != 0) { setCurrScreen(sos_screen); return; }  // an SOS in progress stays up
  if (curr != splash && millis() - _display_off_at > 10000UL) {
    ((HomeScreen*)home)->showClock();
    _menu_return = NULL;
    setCurrScreen(home);
  }
}

// ---- SF32: UI settings (own file; MeshCore's prefs have no room for them) ----

#define SF32_UI_PREFS_FILE  "/sf32_ui"
#define SF32_UI_PREFS_MAGIC_V1 0x53465531  // "SFU1": up to the theme
#define SF32_UI_PREFS_MAGIC_V2 0x53465532  // "SFU2": + alerts, sound, bt_off
#define SF32_UI_PREFS_MAGIC_V3 0x53465533  // "SFU3": + lift_wake, flip_quiet
#define SF32_UI_PREFS_MAGIC_V4 0x53465534  // "SFU4": + steps_on
#define SF32_UI_PREFS_MAGIC_V5 0x53465535  // "SFU5": + night dim
#define SF32_UI_PREFS_MAGIC_V6 0x53465536  // "SFU6": + aod
#define SF32_UI_PREFS_MAGIC_V7 0x53465537  // "SFU7": + bot
#define SF32_UI_PREFS_MAGIC_V8 0x53465538  // "SFU8": + touch sound, signal tester, SOS channel
#define SF32_UI_PREFS_MAGIC_V9 0x53465539  // "SFU9": + mesh time
#define SF32_UI_PREFS_MAGIC_V10 0x5346553A  // "SFU:": + night do not disturb
#define SF32_UI_PREFS_MAGIC_V11 0x5346553B  // "SFU;": + clock face, hidden pages
#define SF32_UI_PREFS_MAGIC_V12 0x5346553C  // "SFU<": + power saving (sleep)
#define SF32_UI_PREFS_MAGIC_V13 0x5346553D  // "SFU=": + auto adverts
#define SF32_UI_PREFS_MAGIC_V14 0x5346553E  // "SFU>": + Advert page long press: nearby / mesh
#define SF32_UI_PREFS_MAGIC_V15 0x5346553F  // "SFU?": + USB app
#define SF32_UI_PREFS_MAGIC_V16 0x53465540  // "SFU@": + rotation
#define SF32_UI_PREFS_MAGIC_V17 0x53465541  // "SFUA": + glass style and its colour
#define SF32_UI_PREFS_MAGIC_V18 0x53465542  // "SFUB": + glass background
#define SF32_UI_PREFS_MAGIC     0x53465543  // "SFUC": + battery colors

static const uint8_t  BRIGHTNESS_STEPS[] = {25, 50, 80, 100};
static const uint16_t TIMEOUT_SECS[] = {15, 30, 60, 300, 0};  // 0 = on while on USB power (else 60 s)

void UITask::loadUIPrefs() {
  File f = InternalFS.open(SF32_UI_PREFS_FILE, "r");
  SF32UIPrefs p;
  int n = f ? f.read((uint8_t*)&p, sizeof(p)) : 0;
  if (n == sizeof(p) && p.magic == SF32_UI_PREFS_MAGIC) {
    _ui_prefs = p;
  } else if (n >= 41 && p.magic == SF32_UI_PREFS_MAGIC_V18) {  // up to glass_bg
    _ui_prefs = p;
    _ui_prefs.batt_colors = SF32UIPrefs().batt_colors;
  } else if (n >= 40 && p.magic == SF32_UI_PREFS_MAGIC_V17) {  // up to glass_theme
    _ui_prefs = p;
    _ui_prefs.glass_bg = SF32UIPrefs().glass_bg;
    _ui_prefs.batt_colors = SF32UIPrefs().batt_colors;
  } else if (n >= 39 && p.magic == SF32_UI_PREFS_MAGIC_V16) {  // up to rotation
    _ui_prefs = p;
    _ui_prefs.glass = SF32UIPrefs().glass;
    _ui_prefs.glass_theme = SF32UIPrefs().glass_theme;
  } else if (n >= 38 && p.magic == SF32_UI_PREFS_MAGIC_V15) {  // up to usb_app
    _ui_prefs = p;
    _ui_prefs.rotation = SF32UIPrefs().rotation;
  } else if (n >= 37 && p.magic == SF32_UI_PREFS_MAGIC_V14) {  // up to adv_page_mesh
    _ui_prefs = p;
    _ui_prefs.usb_app = SF32UIPrefs().usb_app;
    _ui_prefs.rotation = SF32UIPrefs().rotation;
  } else if (n >= 36 && p.magic == SF32_UI_PREFS_MAGIC_V13) {  // up to adv_mesh
    _ui_prefs = p;
    _ui_prefs.adv_page_mesh = SF32UIPrefs().adv_page_mesh;
    _ui_prefs.usb_app = SF32UIPrefs().usb_app;
    _ui_prefs.rotation = SF32UIPrefs().rotation;
  } else if (n >= 34 && p.magic == SF32_UI_PREFS_MAGIC_V12) {  // up to power_save (the rest was padding)
    SF32UIPrefs defaults;
    _ui_prefs = p;
    _ui_prefs.adv_nearby = defaults.adv_nearby;
    _ui_prefs.adv_mesh = defaults.adv_mesh;
    _ui_prefs.adv_page_mesh = defaults.adv_page_mesh;
    _ui_prefs.usb_app = defaults.usb_app;
    _ui_prefs.rotation = defaults.rotation;
  } else if (n >= 32 && p.magic == SF32_UI_PREFS_MAGIC_V11) {  // up to hidden_pages (the rest: defaults)
    SF32UIPrefs defaults;
    _ui_prefs = p;
    _ui_prefs.power_save = defaults.power_save;
    _ui_prefs.adv_nearby = defaults.adv_nearby;
    _ui_prefs.adv_mesh = defaults.adv_mesh;
  } else if (n >= 29 && p.magic == SF32_UI_PREFS_MAGIC_V10) {  // up to night_dnd (the rest was padding)
    SF32UIPrefs defaults;
    _ui_prefs = p;
    _ui_prefs.clock_face = defaults.clock_face;
    _ui_prefs.hidden_pages = defaults.hidden_pages;
  } else if (n >= 28 && p.magic == SF32_UI_PREFS_MAGIC_V9) {  // up to mesh_time (the rest: defaults)
    _ui_prefs = p;
    _ui_prefs.night_dnd = SF32UIPrefs().night_dnd;
  } else if (n >= 27 && p.magic == SF32_UI_PREFS_MAGIC_V8) {  // up to sos_chan (the rest was padding)
    _ui_prefs = p;
    _ui_prefs.mesh_time = SF32UIPrefs().mesh_time;
  } else if (n >= 24 && p.magic == SF32_UI_PREFS_MAGIC_V7) {  // up to bot (the rest: defaults)
    _ui_prefs = p;
  } else if (n >= 21 && p.magic == SF32_UI_PREFS_MAGIC_V6) {  // up to aod (the rest was padding)
    SF32UIPrefs defaults;
    _ui_prefs = p;
    _ui_prefs.bot_on = defaults.bot_on;
    _ui_prefs.bot_dm = defaults.bot_dm;
    _ui_prefs.bot_chan = defaults.bot_chan;
  } else if (n >= 19 && p.magic == SF32_UI_PREFS_MAGIC_V5) {  // up to night dim
    uint8_t aod = _ui_prefs.aod;
    _ui_prefs = p;
    _ui_prefs.aod = aod;
  } else if (n >= 15 && p.magic == SF32_UI_PREFS_MAGIC_V4) {  // up to steps_on
    SF32UIPrefs defaults;
    _ui_prefs = p;
    _ui_prefs.night_on = defaults.night_on;
    _ui_prefs.night_from = defaults.night_from;
    _ui_prefs.night_until = defaults.night_until;
    _ui_prefs.night_level = defaults.night_level;
  } else if (n >= 14 && p.magic == SF32_UI_PREFS_MAGIC_V3) {  // up to flip_quiet
    uint32_t keep = _ui_prefs.magic;
    uint8_t steps = _ui_prefs.steps_on;
    _ui_prefs = p;
    _ui_prefs.magic = keep;
    _ui_prefs.steps_on = steps;
  } else if (n >= 12 && p.magic == SF32_UI_PREFS_MAGIC_V2) {  // up to bt_off: keep it all
    _ui_prefs.brightness_idx = p.brightness_idx;
    _ui_prefs.timeout_idx = p.timeout_idx;
    _ui_prefs.clock24 = p.clock24;
    _ui_prefs.dst_rule = p.dst_rule;
    _ui_prefs.theme = p.theme;
    _ui_prefs.alerts = p.alerts;
    _ui_prefs.sound = p.sound;
    _ui_prefs.bt_off = p.bt_off;
  } else if (n >= 9 && p.magic == SF32_UI_PREFS_MAGIC_V1) {  // older file: keep what it had
    _ui_prefs.brightness_idx = p.brightness_idx;
    _ui_prefs.timeout_idx = p.timeout_idx;
    _ui_prefs.clock24 = p.clock24;
    _ui_prefs.dst_rule = p.dst_rule;
    _ui_prefs.theme = p.theme;
  }
  if (_ui_prefs.theme >= NUM_THEMES) _ui_prefs.theme = 0;
  if (_ui_prefs.glass_theme >= NUM_THEMES) _ui_prefs.glass_theme = 0;
  _ui_prefs.glass = _ui_prefs.glass ? 1 : 0;
  if (_ui_prefs.glass_bg >= 4) _ui_prefs.glass_bg = 0;
  _ui_prefs.batt_colors = _ui_prefs.batt_colors ? 1 : 0;
  applyStyle();
#ifdef SF32_RAINBOW_TEST
  applyTheme(NUM_THEMES - 1);  // test builds: show Rainbow without saving it
#endif
  if (_ui_prefs.brightness_idx >= sizeof(BRIGHTNESS_STEPS)) _ui_prefs.brightness_idx = 2;
  if (_ui_prefs.timeout_idx >= sizeof(TIMEOUT_SECS) / sizeof(TIMEOUT_SECS[0])) _ui_prefs.timeout_idx = 0;
  if (_ui_prefs.bot_chan > 2) _ui_prefs.bot_chan = 1;
  if (_ui_prefs.touch_snd > 3) _ui_prefs.touch_snd = 1;
  if (_ui_prefs.sig_interval >= NUM_SIG_INTERVALS) _ui_prefs.sig_interval = 1;
  if (_ui_prefs.sos_chan >= MAX_GROUP_CHANNELS) _ui_prefs.sos_chan = 0;
  if (_ui_prefs.dst_rule >= DST_RULES) _ui_prefs.dst_rule = DST_OFF;
  if (_ui_prefs.rotation > 4) _ui_prefs.rotation = 0;
  applyBrightness();
}

void UITask::saveUIPrefs() {
  _ui_prefs.magic = SF32_UI_PREFS_MAGIC;
  File f = InternalFS.open(SF32_UI_PREFS_FILE, "w", true);
  if (f) f.write((const uint8_t*)&_ui_prefs, sizeof(_ui_prefs));
}

unsigned long UITask::autoOffMillis() const {
#ifdef SF32_UI_TEST
  return 300000UL;  // scripted test builds: the screen stays on through the script
#endif
  uint16_t secs = TIMEOUT_SECS[_ui_prefs.timeout_idx];
  if (secs == 0) return board.isExternalPowered() ? 24UL * 60 * 60 * 1000 : 60000UL;
  return secs * 1000UL;
}

static void timeoutLabel(char* buf, uint8_t idx) {
  uint16_t secs = TIMEOUT_SECS[idx];
  if (secs == 0) strcpy(buf, "never\non USB");  // on USB power: stays on (dims after 1 min); two lines in the menu
  else if (secs < 60) sprintf(buf, "%d s", secs);
  else sprintf(buf, "%d min", secs / 60);
}
