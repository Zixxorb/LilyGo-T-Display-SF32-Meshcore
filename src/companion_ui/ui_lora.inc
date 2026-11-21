// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// LoRa networks (Radio page menu): which LoRa networks are on the air nearby. The radio checks
// each one's settings in turn with channel activity detection (a quick "is LoRa with this
// spreading factor and bandwidth being sent here?", SF32SX1262Radio::cadProbe), and the list
// shows when each was last heard and how strong. It can't read them (they're encrypted, and
// on other settings): it only tells they're there. A little of each frame, then the radio goes
// back to our channel, so the mesh keeps receiving (less well while this is open).
//
// US settings: Meshtastic's presets (each one's frequency comes from a hash of its name, as
// their firmware does it: LongFast 906.875 MHz), MeshCore's US default, and LoRaWAN (The
// Things Network, Helium and most others use sub-band 2: devices on 903.9..905.3 MHz, SF7..10,
// hopping, so it's caught less often; gateways answer on 923.3..927.5 MHz, 500 kHz, IQ inverted).

#define LORA_BUDGET_MS 40   // radio time per frame off our channel

struct LoraNet {
  const char* name;
  uint32_t hz;
  float bw_khz;
  uint8_t sf;
  uint8_t kind;  // 0 fixed, 1 ours (MeshCore's settings), 2 LoRaWAN devices, 3 LoRaWAN gateways
};
static const LoraNet LORA_NETS[] = {
  {"Your mesh",    0,         0,     0,  1},
  {"MeshCore US",  910525000, 62.5f, 7,  0},
  {"MT LongFast",  906875000, 250,   11, 0},
  {"MT MedFast",   913125000, 250,   9,  0},
  {"MT MedSlow",   914875000, 250,   10, 0},
  {"MT ShortFast", 918875000, 250,   7,  0},
  {"MT Turbo",     926750000, 500,   7,  0},
  {"LoRaWAN",      903900000, 125,   7,  2},
  {"LoRaWAN gw",   923300000, 500,   7,  3},
};
#define NUM_LORA_NETS (int)(sizeof(LORA_NETS) / sizeof(LORA_NETS[0]))

class LoraScreen : public UIScreen {
  UITask* _task;
  struct S { unsigned long seen; uint16_t hits; int8_t rssi; uint8_t rot; } _s[NUM_LORA_NETS];
  int _next = 0;
  uint32_t _probes = 0;

  // this one's settings now (LoRaWAN: the next channel and spreading factor of its 32)
  void params(int i, uint32_t& hz, float& bw, uint8_t& sf, bool& inv) {
    const LoraNet& n = LORA_NETS[i];
    hz = n.hz; bw = n.bw_khz; sf = n.sf; inv = false;
    uint8_t r = _s[i].rot++;
    if (n.kind == 1) {
      hz = radio_driver.frequencyHz();
      bw = radio_driver.getBandwidth();
      sf = radio_driver.getSpreadingFactor();
    } else if (n.kind == 2) {
      hz = 903900000UL + 200000UL * (r % 8);
      sf = 7 + (r / 8) % 4;
    } else if (n.kind == 3) {
      hz = 923300000UL + 600000UL * (r % 8);
      sf = 7 + (r / 8) % 4;
      inv = true;
    }
  }
  bool same_as_ours(int i) {  // MeshCore US when ours is it: one row
    const LoraNet& n = LORA_NETS[i];
    return n.kind == 0 && n.hz / 1000 == radio_driver.frequencyHz() / 1000 &&
           n.sf == radio_driver.getSpreadingFactor() && (int)(n.bw_khz * 10) == (int)(radio_driver.getBandwidth() * 10);
  }

public:
  LoraScreen(UITask* task) : _task(task) { reset(); }
  void reset() { memset(_s, 0, sizeof(_s)); _next = 0; _probes = 0; }

  void step() {
    if (!radio_driver.isInRecvMode() || radio_driver.isReceiving()) return;  // (busy: next frame)
    unsigned long t0 = millis();
    do {
      int i = _next;
      _next = (_next + 1) % NUM_LORA_NETS;
      if (same_as_ours(i)) continue;
      uint32_t hz; float bw; uint8_t sf; bool inv;
      params(i, hz, bw, sf, inv);
      int rssi = 0;
      int r = radio_driver.cadProbe(hz, bw, sf, inv, &rssi);
      _probes++;
      if (r == 1) {
        _s[i].seen = millis() | 1;
        if (_s[i].hits < 9999) _s[i].hits++;
        _s[i].rssi = (int8_t)(rssi < -127 ? -127 : rssi > 0 ? 0 : rssi);
      }
    } while (millis() - t0 < LORA_BUDGET_MS);
    radio_driver.restoreParams();  // back to our channel, listening
  }

  int render(DisplayDriver& display) override {
    step();
    SF32Display& d = (SF32Display&)display;
    themeHeader(display, "LoRa nets", _task->getBattMilliVolts());
    d.setTextSize(1);
    char tmp[16];
    int y = 25;
    unsigned long now = millis();
    for (int i = 0; i < NUM_LORA_NETS; i++) {
      if (same_as_ours(i)) continue;
      const S& s = _s[i];
      bool recent = s.seen && now - s.seen < 120000UL;
      d.setColor(recent ? UIColor::primary_txt : UIColor::secondary_txt);
      d.setCursor(4, y);
      d.print(LORA_NETS[i].name);
      if (s.seen) {
        unsigned long a = (now - s.seen) / 1000;
        if (a < 60) sprintf(tmp, "%lus", a);
        else if (a < 3600) sprintf(tmp, "%lum", a / 60);
        else sprintf(tmp, "%luh", a / 3600);
        d.drawTextRightAlign(UI_RX(124), y, tmp);
        sprintf(tmp, "%d", s.rssi);
        d.setColor(s.rssi >= -90 ? THEME_GOOD : s.rssi >= -110 ? UIColor::warning_txt : UIColor::secondary_txt);
        d.drawTextRightAlign(102, y, tmp);
      } else {
        d.setColor(THEME_DIM);
        d.drawTextRightAlign(UI_RX(124), y, "--");
      }
      y += 10;
    }
    d.setColor(UIColor::secondary_txt);
    sprintf(tmp, "%lu checks", (unsigned long)_probes);
    d.setCursor(4, 117);
    d.print(_probes ? tmp : "listening...");
    d.drawTextRightAlign(UI_RX(124), 117, "tap: clear");
    return 60;
  }

  bool handleInput(char c) override {  // button A: back
    if (c == 0) return false;
    _task->closeLora();
    return true;
  }
};

void UITask::openLora() {
  if (!lora_screen) lora_screen = new LoraScreen(this);
  _lora_return = _menu_return ? _menu_return : home;
  _menu_return = NULL;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(lora_screen);
}

void UITask::closeLora() {
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  setCurrScreen(_lora_return ? _lora_return : home);
  _lora_return = NULL;
}
