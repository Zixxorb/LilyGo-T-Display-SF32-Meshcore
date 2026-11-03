// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Mesh SOS: triple-press button A (or Send SOS in the swipe-down menu) starts a 5 s
// countdown (beeping; tap or press A to cancel), then sends an SOS to the channel chosen
// in Settings > SOS to with the node's location (as set in the app) and battery, and
// repeats it every 5 min, 6 times in all, until stopped.

#define SOS_COUNTDOWN_MS 5000UL
#define SOS_REPEAT_MS    (5UL * 60 * 1000)
#define SOS_SENDS        6

enum SosState : uint8_t { SOS_OFF, SOS_COUNTDOWN, SOS_ACTIVE, SOS_DONE };

// channel index for SOS: the chosen one if it still exists, else the first one
static int sosChannel(uint8_t pref, ChannelDetails& ch) {
  if (the_mesh.getChannel(pref, ch) && ch.name[0]) return pref;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
    if (the_mesh.getChannel(i, ch) && ch.name[0]) return i;
  return -1;
}

// next configured channel after `pref` (Settings > SOS to)
static uint8_t nextSosChannel(uint8_t pref) {
  ChannelDetails ch;
  for (int k = 1; k <= MAX_GROUP_CHANNELS; k++) {
    int i = (pref + k) % MAX_GROUP_CHANNELS;
    if (the_mesh.getChannel(i, ch) && ch.name[0]) return i;
  }
  return pref;
}

class SosScreen : public UIScreen {
  UITask* _task;
public:
  SosScreen(UITask* task) : _task(task) {}

  int render(DisplayDriver& display) override {
    char tmp[48];
    ChannelDetails ch;
    const char* chan = sosChannel(_task->uiPrefs().sos_chan, ch) >= 0 ? ch.name : "(no channel)";
    // red frame, title
    display.setColor(THEME_RED);
    display.fillRect(0, 0, 128, 3);
    display.fillRect(0, 125, 128, 3);
    display.setTextSize(2);
    display.drawTextCentered(64, 10, "SOS");

    uint8_t st = _task->sosState();
    if (st == SOS_COUNTDOWN) {
      int left = _task->sosSecondsLeft();
      display.setTextSize(5);
      display.setColor(UIColor::primary_txt);
      sprintf(tmp, "%d", left);
      display.drawTextCentered(64, 32, tmp);
      display.setTextSize(1);
      display.setColor(UIColor::secondary_txt);
      sprintf(tmp, "sending to %s", chan);
      display.drawTextCentered(64, 82, tmp);
      themeCard(display, 14, 96, 100, 22);
      display.setTextSize(2);
      display.setColor(UIColor::primary_txt);
      display.drawTextCentered(64, 100, "Cancel");
      return 200;
    }
    display.setTextSize(2);
    display.setColor(st == SOS_DONE ? UIColor::primary_txt : THEME_RED);
    bool none = st == SOS_DONE && _task->sosSent() == 0;  // no channel to send to
    display.drawTextCentered(64, 36, none ? "SOS not sent" : st == SOS_DONE ? "SOS finished" : "SOS sent");
    display.setTextSize(1);
    display.setColor(UIColor::secondary_txt);
    if (none) strcpy(tmp, "no channel set up: add one in the app");
    else sprintf(tmp, "to %s, %d of %d", chan, _task->sosSent(), SOS_SENDS);
    display.drawTextCentered(64, 60, tmp);
    if (st == SOS_ACTIVE) {
      int s = _task->sosSecondsLeft();
      sprintf(tmp, "again in %d:%02d", s / 60, s % 60);
      display.drawTextCentered(64, 72, tmp);
    }
    themeCard(display, 14, 96, 100, 22);
    display.setTextSize(2);
    display.setColor(UIColor::primary_txt);
    display.drawTextCentered(64, 100, st == SOS_DONE ? "Close" : "Stop");
    return 1000;
  }

  bool handleInput(char c) override {  // button A (or a tap): cancel / stop / close
    if (c == 0) return false;
    _task->stopSos();
    return true;
  }
};

void UITask::createSosScreen() { sos_screen = new SosScreen(this); }

void UITask::openSos() {
  if (_sos_state == SOS_COUNTDOWN || _sos_state == SOS_ACTIVE) {  // already on: just show it
    setCurrScreen(sos_screen);
    return;
  }
  if (_aod) exitAOD();
  if (_display && !_display->isOn()) _display->turnOn();
  _sos_state = SOS_COUNTDOWN;
  _sos_t = millis();
  _sos_sent = 0;
  _sos_beeped = 0xFF;
  _menu_return = NULL;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_UP);
  setCurrScreen(sos_screen);
  board.logEvent("SOS countdown");
}

void UITask::stopSos() {
  uint8_t was = _sos_state;
  _sos_state = SOS_OFF;
  if (was == SOS_COUNTDOWN) { board.logEvent("SOS cancelled"); showAlert("SOS cancelled", 1500); }
  else if (was == SOS_ACTIVE) { board.logEvent("SOS stopped"); showAlert("SOS stopped", 1500); }
  ((HomeScreen*)home)->showClock();
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_DOWN);
  setCurrScreen(home);
}

int UITask::sosSecondsLeft() const {
  long ms = _sos_state == SOS_COUNTDOWN ? (long)(_sos_t + SOS_COUNTDOWN_MS - millis()) : (long)(_sos_t - millis());
  return ms > 0 ? (int)((ms + 999) / 1000) : 0;
}

void UITask::sendSos() {
  ChannelDetails ch;
  if (sosChannel(_ui_prefs.sos_chan, ch) < 0) {
    board.logEvent("SOS failed: no channel");
    _sos_state = SOS_DONE;
    return;
  }
  char text[MAX_TEXT_LEN + 1], loc[40];
  double lat = _node_prefs->node_lat, lon = _node_prefs->node_lon;
  if (lat != 0 || lon != 0) snprintf(loc, sizeof(loc), "%.5f,%.5f", lat, lon);
  else strcpy(loc, "unknown");
  snprintf(text, sizeof(text), "SOS! I need help. Location: %s. Battery %d%%. (%d/%d)", loc,
           sf32ShownPercent(getBattMilliVolts()), _sos_sent + 1, SOS_SENDS);
  uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
#ifdef SF32_UI_TEST  // scripted test builds: never put a test SOS on the air
  bool ok = false;
  (void)ts;
#else
  bool ok = the_mesh.sendGroupMessage(ts, ch.channel, the_mesh.getNodeName(), text, strlen(text));
#endif
  _sos_sent++;
  char line[48];
  snprintf(line, sizeof(line), "SOS %s %d/%d: %s", ok ? "sent" : "failed", _sos_sent, SOS_SENDS, ch.name);
  board.logEvent(line);
  sound.tone(2000, 400, SF32Sound::VOL_HIGH);
  _sos_t = millis() + SOS_REPEAT_MS;
  _sos_state = _sos_sent >= SOS_SENDS ? SOS_DONE : SOS_ACTIVE;
}

void UITask::sosLoop() {
  if (_sos_state == SOS_COUNTDOWN) {
    _auto_off = millis() + autoOffMillis();  // keep the countdown visible
    uint8_t left = sosSecondsLeft();
    if (left != _sos_beeped) {  // a beep each second
      _sos_beeped = left;
      if (left > 0) sound.tone(1800, 120, SF32Sound::VOL_HIGH);
      if (curr == sos_screen) _next_refresh = 0;
    }
    if (millis() - _sos_t >= SOS_COUNTDOWN_MS) {
      sendSos();
      if (curr == sos_screen) _next_refresh = 0;
    }
  } else if (_sos_state == SOS_ACTIVE && (long)(millis() - _sos_t) >= 0) {
    sendSos();
    if (curr == sos_screen) _next_refresh = 0;
  }
}
