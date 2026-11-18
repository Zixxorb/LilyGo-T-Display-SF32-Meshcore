// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Battery use (Settings > Battery > Usage): what has used the battery since it was unplugged,
// as shares of the total. The board can't measure current, so it's an estimate: the time in
// each state times a typical current for it. Where we measured (the night battery tests:
// docs in memory, ~16-17 mA overnight, touch scanning ~6 mA, a connected phone <1 mA) the
// numbers follow those; the screen's are a reasonable guess for an AMOLED this size.
// Counted in RAM from the main loop; a reboot or a charge starts over.

enum UseKind { USE_SCREEN, USE_SYSTEM, USE_LISTEN, USE_SEND, USE_TOUCH, USE_AWAKE, USE_BT, USE_N };
static const char* const USE_NAMES[USE_N] = {"Screen", "System", "Radio listen", "Radio send", "Tap to wake", "Kept awake", "Bluetooth"};

// typical currents, mA
#define USE_MA_SYSTEM      10.0f   // the board itself (rails, chip asleep): always
#define USE_MA_LISTEN       5.3f   // SX1262 receiving, boosted gain (4.6 at normal gain)
#define USE_MA_LISTEN_LOW   4.6f
#define USE_MA_SEND       118.0f   // SX1262 sending at full power, for its airtime
#define USE_MA_TOUCH        6.0f   // the touch chip scanning with the screen dark (tap to wake)
#define USE_MA_AWAKE        2.5f   // the chip kept awake with the screen dark (Sleep off, sound)
#define USE_MA_BT_LINK      0.8f   // Bluetooth: a phone connected
#define USE_MA_BT_ADV       0.4f   //   advertising, waiting for one
#define USE_MA_SCREEN_CPU  12.0f   // the screen on: the chip running the UI
static float useScreenMa(int brightness) { return 8.0f + 0.55f * brightness; }  // the panel at that brightness

void UITask::usageLoop() {
  unsigned long now = millis();
  bool usb = board.isExternalPowered();
  if (usb) {  // charging: nothing counts; unplugged after a real charge, start over
    if (!_use_usb_at) _use_usb_at = now | 1;
    _use_last = now;
    return;
  }
  if (_use_usb_at) {
    if (now - _use_usb_at > 60000UL || !_use_ms) {  // (a blip off and on USB keeps the counts)
      memset(_use, 0, sizeof(_use));
      _use_ms = 0;
      _use_tx0 = radio_driver.getPacketsSent();
    }
    _use_usb_at = 0;
    _use_last = now;
  }
  if (!_use_last) { _use_last = now; _use_tx0 = radio_driver.getPacketsSent(); return; }
  if (now - _use_last < 200) return;  // (often enough: states change slowly)
  float h = (now - _use_last) / 3600000.0f;  // hours since the last pass
  _use_last = now;
  _use_ms += (uint32_t)(h * 3600000.0f);
  bool on = _display && _display->isOn();
  _use[USE_SYSTEM] += USE_MA_SYSTEM * h;
  bool low_gain = lowPower() || !(_node_prefs && _node_prefs->rx_boosted_gain);
  _use[USE_LISTEN] += (low_gain ? USE_MA_LISTEN_LOW : USE_MA_LISTEN) * h;
  if (on) _use[USE_SCREEN] += (useScreenMa(_applied_brightness > 0 ? _applied_brightness : 80) + USE_MA_SCREEN_CPU) * h;
  else {
    if (_ui_prefs.tap_wake) _use[USE_TOUCH] += USE_MA_TOUCH * h;
    if (!_ui_prefs.power_save || sound.running()) _use[USE_AWAKE] += USE_MA_AWAKE * h;
  }
  if (isBluetoothEnabled()) _use[USE_BT] += (hasConnection() ? USE_MA_BT_LINK : USE_MA_BT_ADV) * h;
  uint32_t tx = radio_driver.getPacketsSent();
  if (tx != _use_tx0) {  // each packet sent: its airtime at full power (a typical 60-byte packet)
    _use[USE_SEND] += (tx - _use_tx0) * USE_MA_SEND * (radio_driver.getEstAirtimeFor(60) / 3600000.0f);
    _use_tx0 = tx;
  }
}

// the Usage list: how long, the estimated average draw, then each part, biggest first
void UITask::buildUsageMenu(MenuScreen& m) {
  char v[16];
  m.begin("Battery use");
  float total = 0;
  for (int i = 0; i < USE_N; i++) total += _use[i];
  if (board.isExternalPowered() || _use_ms < 60000UL || total <= 0) {
    m.add(board.isExternalPowered() ? "Charging now" : "Counting...", ACT_INFO);
    m.add("Shows use since unplug", ACT_INFO);
    m.add("Back", ACT_MENU_BACK);
    return;
  }
  unsigned long mins = _use_ms / 60000UL;
  if (mins >= 60) sprintf(v, "%luh %02lum", mins / 60, mins % 60); else sprintf(v, "%lu min", mins);
  m.add("Since unplug", ACT_INFO, v);
  sprintf(v, "~%d mA", (int)(total / (_use_ms / 3600000.0f) + 0.5f));
  m.add("Average", ACT_INFO, v);
  int order[USE_N];
  for (int i = 0; i < USE_N; i++) order[i] = i;
  for (int i = 1; i < USE_N; i++)  // biggest first
    for (int j = i; j > 0 && _use[order[j]] > _use[order[j - 1]]; j--) { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
  for (int k = 0; k < USE_N; k++) {
    int i = order[k];
    int pct = (int)(100.0f * _use[i] / total + 0.5f);
    if (_use[i] <= 0) continue;
    if (pct < 1) strcpy(v, "<1%"); else sprintf(v, "%d%%", pct);
    m.add(USE_NAMES[i], ACT_INFO, v);
  }
  m.add("Back", ACT_MENU_BACK);
}
