// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// The pet (Games page): a small original creature, its look made from this node's public key, so
// every node's pet is its own. It's fed by the mesh traffic the radio hears anyway, grows curious
// from adverts and new nodes, rests while the screen is off, and likes being petted and played
// with. It never transmits, and does nothing at all while the screen is off: time away is caught
// up in one step when it's next looked at. No death, no nagging: low moods just recover.

#define PET_FILE  "/sf32_pet"
#define PET_MAGIC 0x31544550  // "PET1"

struct PetSave {
  uint32_t magic;
  uint16_t size;
  uint8_t fed, curious, rested, happy;  // 0..100
  uint32_t seed;     // the look (from the public key the first time; kept if the key changes)
  uint32_t rtc;      // RTC time of the last update (0: unknown) for the time between boots
  uint32_t born;     // RTC time it hatched (0: unknown)
  uint8_t day_fed, day_cur;  // gained today (daily caps)
  uint16_t pad;
  uint32_t day_ms;   // when today started (uptime)
  uint32_t sum;      // check
};

class PetGame {
public:
  // ---- state ----
  void begin() {
    if (_begun) return;
    _begun = true;
    File f = InternalFS.open(PET_FILE, "r");
    PetSave s;
    int n = f ? f.read((uint8_t*)&s, sizeof(s)) : 0;
    if (n == sizeof(s) && s.magic == PET_MAGIC && s.size == sizeof(s) && s.sum == check(s)) {
      _s = s;
      // the time it was switched off or rebooted: rest and needs, as if the screen was off
      uint32_t now = rtc_clock.getCurrentTime();
      if (_s.rtc && rtc_clock.timeKnown() && now > _s.rtc) {
        uint32_t gap = now - _s.rtc;
        if (gap > 7UL * 24 * 3600) gap = 7UL * 24 * 3600;  // longer: it hibernated
        age(gap * 1000UL, false);
      }
    } else {
      fresh();
    }
    _last_ms = millis();
    _last_pkts = radio_driver.getPacketsRecv();
    if (!_s.day_ms) _s.day_ms = millis() | 1;
  }
  void fresh() {
    memset(&_s, 0, sizeof(_s));
    _s.magic = PET_MAGIC;
    _s.size = sizeof(_s);
    _s.fed = 70; _s.curious = 60; _s.rested = 80; _s.happy = 70;
    uint32_t h = 2166136261u;  // FNV-1a over the public key
    for (int i = 0; i < PUB_KEY_SIZE; i++) { h ^= the_mesh.self_id.pub_key[i]; h *= 16777619u; }
    _s.seed = h ? h : 1;
    _s.born = rtc_clock.timeKnown() ? rtc_clock.getCurrentTime() : 0;
    _s.day_ms = millis() | 1;
    _dirty = true;
  }
  void reset() {  // Settings > Games > Reset pet: fresh needs, the same look
    uint32_t seed = _s.seed;
    fresh();
    _s.seed = seed;
    save();
  }
  void save() {
    if (!_begun || !_dirty) return;
    _s.rtc = rtc_clock.timeKnown() ? rtc_clock.getCurrentTime() : 0;
    _s.sum = check(_s);
    File f = InternalFS.open(PET_FILE, "w", true);
    if (f) f.write((const uint8_t*)&_s, sizeof(_s));
    _dirty = false;
  }

  // Bring it up to now: the time since the last update, as screen-on or screen-off time.
  // Delta-based, so calling it more often changes nothing (render() runs during page drags too).
  void update(bool screen_on) {
    begin();
    unsigned long now = millis();
    unsigned long d = now - _last_ms;
    if (d >= 1000) {
      age(d, _was_on);
      _last_ms = now;
    }
    _was_on = screen_on;
    // the day's caps: every 24 h of uptime
    if (now - _s.day_ms > 24UL * 3600 * 1000) { _s.day_ms = now | 1; _s.day_fed = _s.day_cur = 0; _dirty = true; }
    // fed by the packets the radio heard
    uint32_t pk = radio_driver.getPacketsRecv();
    if (pk != _last_pkts) {
      uint32_t got = pk - _last_pkts;
      _last_pkts = pk;
      gain(_s.fed, _s.day_fed, got * 3, 70);
    }
    // curious about adverts and new nodes (counted by onAdvert)
    if (_adverts || _new_nodes) {
      gain(_s.curious, _s.day_cur, _adverts * 2 + _new_nodes * 15, 60);
      _adverts = _new_nodes = 0;
    }
  }
  void onScreen(bool on) {  // screen on/off (UITask::powerCare): catch up, and save when it goes dark
    if (!_begun) return;
    update(on);
    if (!on) save();
  }
  void onAdvert(bool is_new) {  // from the mesh (any time): just counted, in RAM
    if (_adverts < 250) _adverts++;
    if (is_new && _new_nodes < 50) _new_nodes++;
  }

  // ---- care (taps on the Games page) ----
  bool pet() {  // a tap on it
    unsigned long now = millis();
    if (now - _react_at < 1200 && _react) return false;
    bump(_s.happy, 8);
    react(1, 1600);
    return true;
  }
  void play() {  // a hold on it (or Play in its menu)
    bump(_s.happy, 18);
    _s.rested = _s.rested > 6 ? _s.rested - 4 : 2;
    _dirty = true;
    react(2, 3200);
  }
  int reaction() const { return (_react && millis() - _react_at < _react_len) ? _react : 0; }
  void look(int x, int y) { _gaze_x = x; _gaze_y = y; _gaze_at = millis() | 1; }

  // ---- mood ----
  enum Mood { CONTENT, HAPPY, EXCITED, SLEEPY, HUNGRY, BORED, SAD };
  int mood() const {
    if (reaction()) return EXCITED;
    if (_s.rested < 25) return SLEEPY;
    if (_s.fed < 25) return HUNGRY;
    if (_s.happy < 20) return SAD;
    if (_s.curious < 25) return BORED;
    if (_s.fed > 60 && _s.happy > 60 && _s.rested > 50 && _s.curious > 50) return HAPPY;
    return CONTENT;
  }
  static const char* moodName(int m) {
    static const char* const N[] = {"content", "happy", "excited", "sleepy", "hungry", "bored", "a bit sad"};
    return N[m];
  }
  const PetSave& stats() const { return _s; }
  uint32_t seed() const { return _s.seed; }

  // ---- drawing ----
  // The pet at logical (cx, cy), size 1 = ~44 logical across. t: millis (animation); still: no
  // animation (low power mode).
  void draw(DisplayDriver& d, float cx, float cy, float size, bool still) {
    SF32Display& sd = (SF32Display&)d;
    uint32_t sd0 = _s.seed;
    static const uint16_t HUES[] = {150, 25, 265, 200, 5, 50, 110, 330};  // mint, peach, lavender, sky, coral, lemon, sage, rose
    float hue = HUES[sd0 & 7];
    int shape = (sd0 >> 3) % 3;   // round, bean, tall
    int top = (sd0 >> 5) % 4;     // none, ears, antenna, tuft
    bool cheeks = (sd0 >> 7) & 1;
    float eye_gap = 7.5f + ((sd0 >> 8) % 3) * 1.5f;
    float eye_d = 9.5f + ((sd0 >> 10) % 3) * 1.3f;
    ColorVal body = hsvColor(hue, 0.42f, 0.95f), dark = hsvColor(hue, 0.55f, 0.55f);

    unsigned long t = millis();
    int m = mood(), react = reaction();
    float breath = still ? 0 : sinf(t * 6.2832f / 4200) * 0.025f;
    float hop = 0;
    if (!still && react == 2) hop = -fabsf(sinf((t - _react_at) * 6.2832f / 700)) * 7;  // playing: bouncing
    else if (!still && react == 1) hop = -fabsf(sinf((t - _react_at) * 6.2832f / 500)) * 3 * (1 - (t - _react_at) / (float)_react_len);
    cy += hop;
    float w = 44 * size * (1 + breath), h = 44 * size * (1 - breath);
    if (shape == 1) { w *= 1.12f; h *= 0.9f; }
    if (shape == 2) { w *= 0.88f; h *= 1.1f; }

    // on top: ears / antenna / tuft (drawn first, the body over their base)
    d.setColor(body);
    float tx = cx, ty = cy - h / 2;
    if (top == 1) {
      sd.drawLineAA(cx - w * 0.28f, ty + 2, cx - w * 0.36f, ty - 6 * size, 9 * size);
      sd.drawLineAA(cx + w * 0.28f, ty + 2, cx + w * 0.36f, ty - 6 * size, 9 * size);
    } else if (top == 2) {
      float sway = still ? 0 : sinf(t * 6.2832f / 2600) * 2;
      d.setColor(dark);
      sd.drawLineAA(tx, ty + 2, tx + sway, ty - 9 * size, 1.4f);
      d.setColor(THEME_ACCENT);
      sd.drawLineAA(tx + sway, ty - 10 * size, tx + sway, ty - 10 * size, 4.5f * size);
    } else if (top == 3) {
      d.setColor(dark);
      for (int k = -1; k <= 1; k++) sd.drawLineAA(tx + k * 3, ty + 2, tx + k * 4.5f, ty - 6 * size, 2.0f);
    }

    // body: a rounded blob (a thick segment with round ends)
    d.setColor(body);
    if (w >= h) sd.drawLineAA(cx - (w - h) / 2, cy, cx + (w - h) / 2, cy, h);
    else sd.drawLineAA(cx, cy - (h - w) / 2, cx, cy + (h - w) / 2, w);
    // a soft belly
    d.setColor(hsvColor(hue, 0.22f, 1.0f));
    sd.drawLineAA(cx, cy + h * 0.16f, cx, cy + h * 0.16f, w * 0.5f);

    // eyes
    float ey = cy - h * 0.1f, ex1 = cx - eye_gap * size, ex2 = cx + eye_gap * size;
    float ed = eye_d * size;
    bool blink = !still && blinking(t);
    const ColorVal ink = 0x18C4;  // near-black, a little blue
    if (m == HAPPY || m == EXCITED) {  // "^ ^"
      d.setColor(ink);
      for (int s = 0; s < 2; s++) {
        float ex = s ? ex2 : ex1;
        sd.drawLineAA(ex - ed * 0.42f, ey + ed * 0.15f, ex, ey - ed * 0.3f, 2.2f);
        sd.drawLineAA(ex, ey - ed * 0.3f, ex + ed * 0.42f, ey + ed * 0.15f, 2.2f);
      }
    } else if (m == SLEEPY || blink) {  // closed
      d.setColor(ink);
      for (int s = 0; s < 2; s++) {
        float ex = s ? ex2 : ex1;
        sd.drawLineAA(ex - ed * 0.4f, ey + 1, ex + ed * 0.4f, ey + 1, 2.0f);
      }
      if (m == SLEEPY && !still) {  // a little "z" drifting up
        float zt = (t % 2400) / 2400.0f;
        d.setColor(UIColor::secondary_txt);
        d.setTextSize(1);
        d.setCursor((int)(cx + w * 0.45f + zt * 4), (int)(cy - h * 0.55f - zt * 8));
        d.print("z");
      }
    } else {  // open: white, a pupil that looks around (or at the last touch), a glint
      float gx = 0, gy = 0;
      if (_gaze_at && t - _gaze_at < 3000) {
        float dx = _gaze_x - cx, dy = _gaze_y - ey, l = sqrtf(dx * dx + dy * dy);
        if (l > 1) { gx = dx / l; gy = dy / l; }
      } else if (!still) {
        gx = sinf(t * 6.2832f / 7000); gy = sinf(t * 6.2832f / 11000) * 0.4f;
      }
      for (int s = 0; s < 2; s++) {
        float ex = s ? ex2 : ex1;
        d.setColor(0xFFFF);
        sd.drawLineAA(ex, ey, ex, ey, ed);
        d.setColor(ink);
        float px = ex + gx * ed * 0.18f, py = ey + gy * ed * 0.18f;
        sd.drawLineAA(px, py, px, py, ed * 0.55f);
        d.setColor(0xFFFF);
        sd.drawLineAA(px - ed * 0.1f, py - ed * 0.12f, px - ed * 0.1f, py - ed * 0.12f, ed * 0.18f);
        if (m == BORED || m == SAD) {  // heavy lids: the top of the eye in body colour
          d.setColor(body);
          float lid = m == SAD ? 0.15f : 0.05f;
          sd.drawLineAA(ex - ed * 0.6f, ey - ed * 0.42f + (s ? -lid : lid) * ed, ex + ed * 0.6f, ey - ed * 0.42f + (s ? lid : -lid) * ed, ed * 0.55f);
        }
      }
    }

    // cheeks
    if (cheeks || m == HAPPY || m == EXCITED) {
      d.setColor(hsvColor(350, 0.45f, 1.0f));
      sd.drawLineAA(ex1 - ed * 0.55f, ey + ed * 0.75f, ex1 - ed * 0.55f, ey + ed * 0.75f, ed * 0.45f);
      sd.drawLineAA(ex2 + ed * 0.55f, ey + ed * 0.75f, ex2 + ed * 0.55f, ey + ed * 0.75f, ed * 0.45f);
    }

    // mouth
    float my = ey + ed * 0.95f;
    d.setColor(ink);
    if (m == HUNGRY) {  // a small open "o"
      sd.drawLineAA(cx, my + 1, cx, my + 1, 4.2f * size);
    } else if (m == SAD) {  // a little frown
      arc(sd, cx, my + 3, 3.6f * size, true);
    } else if (m == SLEEPY || m == BORED) {  // flat
      sd.drawLineAA(cx - 2.5f * size, my, cx + 2.5f * size, my, 1.6f);
    } else {  // a smile (bigger when happy)
      arc(sd, cx, my - 1, (m == CONTENT ? 3.2f : 4.4f) * size, false);
    }

    // playing: a ball bouncing beside it
    if (react == 2 && !still) {
      float bt = (t - _react_at) * 6.2832f / 700;
      float bx = cx + w * 0.7f, by = cy + h * 0.35f - fabsf(cosf(bt)) * 12;
      d.setColor(THEME_ACCENT);
      sd.drawLineAA(bx, by, bx, by, 6);
    }
  }

private:
  PetSave _s;
  bool _begun = false, _dirty = false, _was_on = true;
  unsigned long _last_ms = 0;
  uint32_t _last_pkts = 0;
  uint8_t _adverts = 0, _new_nodes = 0;
  int _react = 0;
  unsigned long _react_at = 0, _react_len = 0;
  int _gaze_x = 64, _gaze_y = 60;
  unsigned long _gaze_at = 0;

  static uint32_t check(const PetSave& s) {
    const uint8_t* p = (const uint8_t*)&s;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(PetSave, sum); i++) { h ^= p[i]; h *= 16777619u; }
    return h;
  }
  // needs over time: on-screen time tires it a little, off-screen time is sleep. Closed form,
  // with floors: it never falls below "a bit low", whatever the gap.
  void age(unsigned long ms, bool screen_on) {
    unsigned long min = ms / 60000;
    if (!min) return;
    auto down = [](uint8_t& v, unsigned long amount, int floor) { v = (long)v - (long)amount < floor ? (v < floor ? v : floor) : v - amount; };
    down(_s.fed, min / 30, 15);
    down(_s.curious, min / 20, 10);
    down(_s.happy, min / 15, 10);
    if (screen_on) down(_s.rested, min / 3, 5);
    else _s.rested = _s.rested + min / 2 > 100 ? 100 : _s.rested + min / 2;
    _dirty = true;
  }
  void gain(uint8_t& v, uint8_t& today, uint32_t want, int cap) {
    uint32_t room = today < cap ? cap - today : 0;
    if (want > room) want = room;
    if (!want) return;
    today += want;
    v = v + want > 100 ? 100 : v + want;
    _dirty = true;
  }
  void bump(uint8_t& v, int by) { v = v + by > 100 ? 100 : v + by; _dirty = true; }
  void react(int kind, unsigned long len) { _react = kind; _react_at = millis(); _react_len = len; }
  // a blink of ~140 ms every 2-6 s (from the time, no state)
  bool blinking(unsigned long t) const {
    unsigned long slot = t / 4000;
    uint32_t r = (uint32_t)(slot * 2654435761u) ^ _s.seed;
    unsigned long at = slot * 4000 + (r % 3000);
    return t >= at && t < at + 140;
  }
  // a small arc as a mouth: a smile (or, frown, a frown)
  static void arc(SF32Display& sd, float cx, float cy, float r, bool frown) {
    float px = 0, py = 0;
    for (int i = 0; i <= 6; i++) {
      float a = 3.1416f * (0.15f + 0.7f * i / 6);
      float x = cx - cosf(a) * r, y = frown ? cy - sinf(a) * r * 0.6f : cy + sinf(a) * r * 0.6f;
      if (i) sd.drawLineAA(px, py, x, y, 1.6f);
      px = x; py = y;
    }
  }
};
static PetGame pet;
