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
  // The pet at logical (cx, cy) (the middle of its body), built from shaded, lit parts so it
  // looks rounded: body and belly, feet, arms, ears (bear, cat, bunny or an antenna), maybe a
  // tail, big glossy eyes; a soft shadow under it. still: one frame, no animation (low power).
  void draw(DisplayDriver& d, float cx, float cy, float size, bool still) {
    SF32Display& sd = (SF32Display&)d;
    const uint32_t sd0 = _s.seed;
    static const uint16_t HUES[] = {150, 25, 265, 200, 8, 48, 110, 330};  // mint, peach, lavender, sky, coral, lemon, sage, rose
    float hue = HUES[sd0 & 7];
    int ears = (sd0 >> 3) & 3;           // 0 bear, 1 cat, 2 bunny, 3 antenna
    bool tail = (sd0 >> 5) & 1, spots = ((sd0 >> 6) & 3) == 0;
    float eye_s = 0.92f + ((sd0 >> 8) % 3) * 0.08f;
    ColorVal body = hsvColor(hue, 0.42f, 0.95f), deep = hsvColor(hue, 0.5f, 0.78f), belly = hsvColor(hue, 0.16f, 1.0f);
    ColorVal pink = hsvColor(350, 0.38f, 1.0f);
    const ColorVal ink = 0x18C4;  // near-black, a little blue

    unsigned long t = still ? 0 : millis();
    int m = mood(), react = reaction();
    float rk = react ? (t - _react_at) / (float)_react_len : 0;  // 0..1 through a reaction
    bool sleepy = m == SLEEPY;

    // ---- animation ----
    float br = sinf(t * 6.2832f / (sleepy ? 5200 : 3600));    // breathing
    float sx = 1 - 0.022f * br, sy = 1 + 0.03f * br;
    float lift = 0, tilt = 0;                                   // hop height, body sway (radians)
    float gx = 0, gy = 0;                                       // where it looks (-1..1)
    float arm_l = 0.35f, arm_r = -0.35f;                       // arm angles
    // an idle action in the first 1.4 s of every 9 s (not when sleepy or reacting)
    int act = 0;
    float ap = 0;
    if (!still && !react && !sleepy) {
      unsigned long slot = t / 9000, in = t % 9000;
      if (in < 1400) { act = 1 + (int)(((slot * 2654435761u) ^ sd0) % 4); ap = in / 1400.0f; }
    }
    if (act == 1) lift = sinf(ap * 3.1416f) * 4;                                     // a hop
    if (act == 2) { gx = sinf(ap * 6.2832f); }                                       // looks around
    if (act == 3) tilt = sinf(ap * 6.2832f * 2) * 0.12f * (1 - ap);                 // a wiggle
    if (act == 4) { sy *= 1 + 0.08f * sinf(ap * 3.1416f); sx *= 1 - 0.05f * sinf(ap * 3.1416f); arm_l = 1.4f * sinf(ap * 3.1416f) + 0.35f; arm_r = -arm_l; }  // a stretch
    if (react == 1) {  // petted: a happy squish, arms waving
      float q = sinf(rk * 3.1416f * 3) * (1 - rk);
      sy *= 1 - 0.10f * q; sx *= 1 + 0.07f * q;
      arm_l = 0.35f + 0.9f * fabsf(sinf(rk * 18)); arm_r = -arm_l;
    }
    if (react == 2) {  // playing: jumping, stretched at the top
      float ph = fabsf(sinf(rk * 3.1416f * 4));
      lift = ph * 7; sy *= 1 + 0.06f * ph; sx *= 1 - 0.04f * ph;
      arm_l = 1.2f * ph + 0.3f; arm_r = -arm_l;
    }
    if (m == HUNGRY && !still && (t % 6000) < 600) cx += sinf(t * 0.12f) * 0.8f;  // a tummy rumble
    if (_gaze_at && t - _gaze_at < 3000) {  // looks at where it was touched
      float dx = _gaze_x - cx, dy = _gaze_y - cy, l = sqrtf(dx * dx + dy * dy);
      if (l > 1) { gx = dx / l; gy = dy / l; }
    } else if (!act && !still) {
      gx = sinf(t * 6.2832f / 7000) * 0.6f; gy = sinf(t * 6.2832f / 11000) * 0.3f;
    }

    float W = 20 * size * sx, H = 18 * size * sy;  // body half-sizes
    float ground = cy + 18 * size + 3;
    float by = cy - lift + (18 * size - H);         // the body's centre (it squashes onto the ground)
    float bx = cx;

    // ---- the ground shadow (smaller and fainter in the air) ----
    float air = lift / 12;
    sd.fillSoft(cx, ground, W * (1.05f - air * 0.3f), 3.4f * size, 0x0000, (int)(11 * (1 - air * 0.5f)));

    // ---- behind the body: tail, ears ----
    if (tail) {
      float wag = still ? 0 : sinf(t * 6.2832f / (react ? 300 : 1400)) * 0.35f;
      sd.fillShaded(bx + W * 0.95f, by + H * 0.35f, 5.5f * size, 4.2f * size, deep, 0.35f, 0.5f + wag);
    }
    float twitch = 0;
    if (!still && !sleepy) { unsigned long s2 = t % 5000; if (s2 < 220) twitch = sinf(s2 / 220.0f * 3.1416f) * 0.35f; }
    float droop = sleepy ? 0.35f : 0;
    for (int side = -1; side <= 1; side += 2) {
      float ex = bx + side * W * 0.55f, ey = by - H * 0.82f;
      float tw = side > 0 ? twitch : 0;
      if (ears == 0) {         // bear: round
        sd.fillShaded(ex, ey, 6 * size, 6 * size, body, 0.3f);
        sd.fillShaded(ex, ey + 0.6f, 3.4f * size, 3.4f * size, pink, 0.15f);
      } else if (ears == 1) {  // cat: pointed, leaning out
        float a = side * (0.42f + droop) + tw;
        sd.fillShaded(ex + side * 1.5f, ey - 2, 4.6f * size, 8.5f * size, body, 0.3f, a);
        sd.fillShaded(ex + side * 1.5f, ey - 1, 2.4f * size, 5.2f * size, pink, 0.1f, a);
      } else if (ears == 2) {  // bunny: long
        float a = side * (0.18f + droop * 2) + tw;
        sd.fillShaded(ex - side * 1.5f + sinf(a) * 9, ey - 8 + droop * 8, 4.2f * size, 12 * size, body, 0.3f, a);
        sd.fillShaded(ex - side * 1.5f + sinf(a) * 9, ey - 7 + droop * 8, 2.0f * size, 8.5f * size, pink, 0.1f, a);
      }
    }
    if (ears == 3) {  // an antenna with a glowing tip
      float sway = still ? 0 : sinf(t * 6.2832f / 2600) * 2.5f + (react ? sinf(t * 0.02f) * 2 : 0);
      float ax = bx + sway, ay = by - H - 9 * size;
      d.setColor(deep);
      sd.drawLineAA(bx, by - H + 2, ax, ay, 1.3f);
      sd.fillSoft(ax, ay, 7 * size, 7 * size, THEME_ACCENT, still ? 5 : 5 + (int)(3 * sinf(t * 0.004f)));
      sd.fillShaded(ax, ay, 2.6f * size, 2.6f * size, THEME_ACCENT, 0.9f);
    }

    // ---- arms (at the sides) ----
    for (int side = -1; side <= 1; side += 2) {
      float a = side < 0 ? arm_l : arm_r;
      float ax = bx + side * W * 0.92f, ay = by + H * 0.15f;
      sd.fillShaded(ax + side * sinf(fabsf(a)) * 2, ay - fabsf(a) * 3, 3.0f * size, 5.2f * size, deep, 0.25f, side * (0.5f + fabsf(a)));
    }

    // ---- the body ----
    sd.fillShaded(bx, by, W, H, body, 0.45f, tilt);
    sd.fillShaded(bx, by + H * 0.32f, W * 0.62f, H * 0.55f, belly, 0.2f, tilt);
    if (spots) {
      ColorVal sp = hsvColor(hue, 0.55f, 0.82f);
      sd.fillShaded(bx - W * 0.6f, by - H * 0.15f, 2.4f, 2.0f, sp, 0.1f);
      sd.fillShaded(bx + W * 0.66f, by + H * 0.05f, 1.8f, 1.6f, sp, 0.1f);
      sd.fillShaded(bx + W * 0.42f, by - H * 0.55f, 1.5f, 1.3f, sp, 0.1f);
    }

    // ---- feet (in front) ----
    for (int side = -1; side <= 1; side += 2) {
      float step = (react == 2 || act == 1) ? 0 : (act == 3 ? sinf(ap * 6.2832f * 2 + side) * 1.2f : 0);
      sd.fillShaded(bx + side * W * 0.45f, by + H * 0.92f - step, 5.2f * size, 3.2f * size, deep, 0.3f);
    }

    // ---- the face ----
    float fy = by - H * 0.12f, eg = 7.2f * size * sx;
    float exl = bx - eg + gx * 1.3f, exr = bx + eg + gx * 1.3f, eyy = fy + gy * 1.0f;
    float erx = 3.5f * size * eye_s, ery = 4.4f * size * eye_s;
    bool blink = !still && blinking(t);
    // blush
    sd.fillSoft(bx - eg - 3.5f, fy + 5, 4.2f, 2.6f, pink, (m == HAPPY || m == EXCITED) ? 12 : 7);
    sd.fillSoft(bx + eg + 3.5f, fy + 5, 4.2f, 2.6f, pink, (m == HAPPY || m == EXCITED) ? 12 : 7);
    d.setColor(ink);
    if (m == EXCITED && react == 1) {  // "^ ^" while being petted (otherwise open: it looks at you)
      for (int s2 = 0; s2 < 2; s2++) arcLine(sd, s2 ? exr : exl, eyy + 1.2f, 3.0f * size, false, 1.9f);
    } else if (sleepy || blink) {      // closed: a gentle curve
      for (int s2 = 0; s2 < 2; s2++) arcLine(sd, s2 ? exr : exl, eyy - 0.6f, 2.8f * size, true, 1.6f);
    } else {                           // open: big, dark and glossy
      for (int s2 = 0; s2 < 2; s2++) {
        float ex = s2 ? exr : exl;
        sd.fillShaded(ex, eyy, erx, ery, ink, 0.6f);
        d.setColor(0xFFFF);
        sd.drawLineAA(ex - erx * 0.35f, eyy - ery * 0.38f, ex - erx * 0.35f, eyy - ery * 0.38f, erx * 0.8f);
        sd.drawLineAA(ex + erx * 0.35f, eyy + ery * 0.35f, ex + erx * 0.35f, eyy + ery * 0.35f, erx * 0.35f);
        if (m == BORED) {  // heavy lids
          sd.fillShaded(ex, eyy - ery * 0.75f, erx * 1.35f, ery * 0.62f, body, 0.2f);
        }
        if (m == SAD) {  // worried brows
          d.setColor(ink);
          float side = s2 ? 1 : -1;
          sd.drawLineAA(ex - side * erx * 0.9f, eyy - ery - 1.6f, ex + side * erx * 0.6f, eyy - ery - 3.0f, 1.3f);
        }
      }
    }
    // the mouth
    float my = fy + 6.2f * size;
    d.setColor(ink);
    if (m == EXCITED || (m == HAPPY && react) || (sleepy && !still && (t % 7000) < 1200)) {  // open (a yawn when sleepy)
      float o = sleepy ? 1.6f : 1.0f;
      sd.fillShaded(bx, my + 0.8f, 2.6f * size * o, 2.0f * size * o, ink, 0.2f);
      sd.fillShaded(bx, my + 1.6f * o, 1.6f * size * o, 0.9f * size * o, pink, 0.1f);
    } else if (m == HUNGRY) {  // a little "o"
      sd.fillShaded(bx, my + 0.6f, 1.5f * size, 1.6f * size, ink, 0.2f);
    } else if (m == SAD) {
      arcLine(sd, bx, my + 1.6f, 2.4f * size, true, 1.4f);
    } else if (m == BORED) {
      sd.drawLineAA(bx - 1.8f, my + 0.4f, bx + 1.8f, my + 0.4f, 1.3f);
    } else {  // a soft "w" smile
      arcLine(sd, bx - 1.4f, my, 1.5f * size, false, 1.3f);
      arcLine(sd, bx + 1.4f, my, 1.5f * size, false, 1.3f);
    }

    if (still) return;
    // ---- around it ----
    if (react == 1) {  // hearts float up
      for (int k = 0; k < 3; k++) {
        float hp = rk * 1.4f - k * 0.25f;
        if (hp <= 0 || hp >= 1) continue;
        float hx = bx + (k - 1) * 9 + sinf(hp * 7 + k) * 2, hy = by - H - 2 - hp * 22;
        heart(sd, hx, hy, 2.3f * (1 - hp * 0.3f), hsvColor(350, 0.6f, 1.0f), (int)(15 * (1 - hp)));
      }
    }
    if (react == 2) {  // a shiny ball bouncing beside it, and sparkles
      float ph = fabsf(cosf(rk * 3.1416f * 5));
      float ballx = bx + W + 9, bally = ground - 3.5f - ph * 16;
      sd.fillSoft(ballx, ground, 4, 1.4f, 0x0000, 8);
      sd.fillShaded(ballx, bally, 3.6f, 3.6f, THEME_ACCENT, 0.8f);
      for (int k = 0; k < 3; k++) {
        float sp = fmodf(rk * 3 + k * 0.33f, 1);
        sparkle(sd, bx + (k - 1) * 16, by - H - 4 - sp * 10, 2.2f * (1 - sp), UIColor::warning_txt);
      }
    }
    if (sleepy) {  // z's drifting up
      for (int k = 0; k < 3; k++) {
        float zp = fmodf(t / 3000.0f + k / 3.0f, 1);
        d.setColor(UIColor::secondary_txt);
        d.setTextSize(1);
        d.setCursor((int)(bx + W * 0.6f + zp * 8 + k), (int)(by - H - zp * 16));
        d.print("z");
      }
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
  // a short curve: up (smile, "^") or down (frown, closed eye)
  static void arcLine(SF32Display& sd, float cx, float cy, float r, bool down, float width) {
    float px = 0, py = 0;
    for (int i = 0; i <= 6; i++) {
      float a = 3.1416f * (0.1f + 0.8f * i / 6);
      float x = cx - cosf(a) * r, y = down ? cy + sinf(a) * r * 0.55f : cy - sinf(a) * r * 0.55f;
      if (i) sd.drawLineAA(px, py, x, y, width);
      px = x; py = y;
    }
  }
  static void heart(SF32Display& sd, float x, float y, float r, ColorVal c, int alpha) {
    if (alpha <= 0) return;
    sd.setColor(c);
    sd.drawLineAA(x - r * 0.5f, y, x - r * 0.5f, y, r * 1.15f);
    sd.drawLineAA(x + r * 0.5f, y, x + r * 0.5f, y, r * 1.15f);
    sd.drawLineAA(x - r * 0.75f, y + r * 0.25f, x, y + r * 1.2f, r * 0.75f);
    sd.drawLineAA(x + r * 0.75f, y + r * 0.25f, x, y + r * 1.2f, r * 0.75f);
  }
  static void sparkle(SF32Display& sd, float x, float y, float r, ColorVal c) {
    if (r <= 0.2f) return;
    sd.setColor(c);
    sd.drawLineAA(x - r, y, x + r, y, 0.7f);
    sd.drawLineAA(x, y - r, x, y + r, 0.7f);
  }
};
static PetGame pet;
