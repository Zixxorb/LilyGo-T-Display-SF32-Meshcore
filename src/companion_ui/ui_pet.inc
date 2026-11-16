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
  bool boop() {  // a tap on it: a little startle and a giggle (stroking is petting)
    unsigned long now = millis();
    if (_react && now - _react_at < 600) return false;
    bump(_s.happy, 3);
    react(3, 900);
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
  // A Starlit: a small original critter with a glowing star mark, chibi proportions (a big head
  // on a small body) and big eyes (coloured irises, star-shaped glints). Its kind and colours come
  // from its seed: Drake (horns, wings, a spade tail), Fluff (tall ears, a big curled tail, cheek
  // tufts), Glimmer (feathery antennae, a fuzzy collar, spotted wings), Ember (round ears, a tail
  // tip that flickers like a flame). Built from shaded, lit parts so it looks rounded.
  // (cx, cy): the middle of the figure. still: one frame, no animation (low power mode).
  enum Kind { DRAKE, FLUFF, GLIMMER, EMBER };
  static const char* kindName(int k) { static const char* const N[] = {"Drake", "Fluff", "Glimmer", "Ember"}; return N[k & 3]; }
  int kind() const { return (_s.seed >> 3) & 3; }

  // (seed / kind_of: another look, e.g. a friend's pet; 0 / -1: this one's)
  void draw(DisplayDriver& d, float cx, float cy, float size, bool still, uint32_t seed = 0, int kind_of = -1) {
    SF32Display& sd = (SF32Display&)d;
    const uint32_t sd0 = seed ? seed : _s.seed;
    static const uint16_t HUES[] = {155, 22, 268, 205, 352, 45, 120, 300};  // mint, apricot, violet, sky, rose, gold, leaf, orchid
    float hue = HUES[sd0 & 7];
    int k = kind_of >= 0 ? kind_of : (int)((sd0 >> 3) & 3);
    ColorVal body = hsvColor(hue, 0.45f, 0.95f), deep = hsvColor(hue, 0.55f, 0.72f), belly = hsvColor(hue, 0.14f, 1.0f);
    ColorVal pink = hsvColor(350, 0.38f, 1.0f);
    static const uint16_t IRIS_H[] = {38, 268, 178, 22};  // amber, violet, teal, warm brown: they suit every body colour
    ColorVal iris = hsvColor(IRIS_H[(sd0 >> 9) & 3], (sd0 >> 9 & 3) == 3 ? 0.55f : 0.62f, (sd0 >> 9 & 3) == 3 ? 0.62f : 0.95f);
    ColorVal star = hsvColor(fmodf(hue + 200, 360), 0.35f, 1.0f);   // the star mark's glow
    const ColorVal ink = 0x18C4;

    unsigned long t = still ? 0 : millis();
    int m = mood(), react = reaction();
    float rk = react ? (t - _react_at) / (float)_react_len : 0;
    bool sleepy = m == SLEEPY;

    // ---- animation ----
    float br = sinf(t * 6.2832f / (sleepy ? 5200 : 3400));
    float sx = 1 - 0.02f * br, sy = 1 + 0.028f * br;
    float lift = 0, tilt = 0, headtilt = 0, gx = 0, gy = 0, flap = 0;
    float tail_speed = 1500;
    int act = 0;
    float ap = 0;
    if (!still && !react && !sleepy) {
      unsigned long slot = t / 8000, in = t % 8000;
      if (in < 1500) { act = 1 + (int)(((slot * 2654435761u) ^ sd0) % 5); ap = in / 1500.0f; }
    }
    if (act == 1) lift = sinf(ap * 3.1416f) * 4;                                  // a hop
    if (act == 2) gx = sinf(ap * 6.2832f);                                        // looks around
    if (act == 3) headtilt = sinf(ap * 3.1416f) * 0.22f;                          // tilts its head
    if (act == 4) flap = sinf(ap * 3.1416f * 6) * (1 - ap);                       // flutters its wings / flicks its tail
    if (act == 5) { sy *= 1 + 0.07f * sinf(ap * 3.1416f); sx *= 1 - 0.04f * sinf(ap * 3.1416f); }  // a stretch
    if (react == 1) {  // stroked: happy squish, fast tail, a flutter
      float q = sinf(rk * 3.1416f * 3) * (1 - rk);
      sy *= 1 - 0.08f * q; sx *= 1 + 0.06f * q;
      flap = sinf(rk * 40) * (1 - rk); tail_speed = 260; headtilt = sinf(rk * 6.2832f) * 0.12f;
    }
    if (react == 3) {  // booped: a startled little jump
      float q = sinf(rk * 3.1416f);
      lift = q * 3; sy *= 1 + 0.06f * q; sx *= 1 - 0.05f * q;
    }
    if (react == 2) {  // playing: bouncing
      float ph = fabsf(sinf(rk * 3.1416f * 4));
      lift = ph * 7; sy *= 1 + 0.05f * ph; sx *= 1 - 0.04f * ph;
      flap = sinf(rk * 50) * 0.8f; tail_speed = 300;
    }
    if (m == HUNGRY && !still && (t % 6000) < 600) cx += sinf(t * 0.12f) * 0.8f;  // a tummy rumble
    if (_gaze_at && t - _gaze_at < 3000) {
      float dx = _gaze_x - cx, dy = _gaze_y - cy, l = sqrtf(dx * dx + dy * dy);
      if (l > 1) { gx = dx / l; gy = dy / l; }
    } else if (!act && !still) {
      gx = sinf(t * 6.2832f / 7000) * 0.6f; gy = sinf(t * 6.2832f / 11000) * 0.3f;
    }
    float wag = still ? 0 : sinf(t * 6.2832f / tail_speed);
    float glow = still ? 0.6f : 0.6f + 0.4f * sinf(t * 6.2832f / 2400);  // the star's pulse
    if (sleepy) glow *= 0.4f;

    // ---- proportions: a big head on a small body ----
    float ground = cy + 22 * size;
    float bw = 11.5f * size * sx, bh = 9.5f * size * sy;        // body half-sizes
    float byc = ground - bh - 1.5f * size - lift;                 // body centre
    float hw = 16.5f * size * sx, hh = 14.5f * size * sy;        // head half-sizes
    float hyc = byc - bh - hh * 0.62f;                            // head centre (overlapping the body)
    float hxc = cx + sinf(headtilt) * 4;

    // ground shadow
    float air = lift / 8;
    sd.fillSoft(cx, ground, 15 * size * (1 - air * 0.2f), 3.2f * size, 0x0000, (int)(11 * (1 - air * 0.4f)));

    // ---- behind: tail, wings ----
    if (k == DRAKE) {  // a long tail curving out to a spade tip
      float px = cx + bw * 0.6f, py = byc + bh * 0.5f;
      d.setColor(deep);
      for (int i = 1; i <= 6; i++) {
        float u = i / 6.0f, a = -0.3f + u * 1.4f + wag * 0.35f * u;
        float nx = cx + bw * 0.6f + u * 15 * size, ny = byc + bh * 0.5f - sinf(a) * 8 * u * size;
        sd.drawLineAA(px, py, nx, ny, 3.2f * size * (1 - u * 0.5f));
        px = nx; py = ny;
      }
      sd.fillShaded(px + 1.5f, py - 1, 3.4f * size, 2.4f * size, deep, 0.4f, -0.6f + wag * 0.3f);
    } else if (k == FLUFF) {  // a big fluffy tail curling up behind
      for (int i = 0; i < 4; i++) {
        float u = i / 3.0f, a = wag * 0.25f;
        sd.fillShaded(cx + bw * 0.9f + u * 6 * size + sinf(a) * 3 * u, byc - u * 12 * size, (6.5f - u * 1.2f) * size, (7.5f - u * 1.5f) * size, i == 3 ? belly : body, 0.3f, 0.4f + a);
      }
    } else if (k == EMBER) {  // a short tail with a flickering flame tip
      float tx = cx + bw * 1.1f + wag * 2, ty = byc + bh * 0.1f;
      d.setColor(deep);
      sd.drawLineAA(cx + bw * 0.5f, byc + bh * 0.5f, tx, ty, 3.0f * size);
      float fl = still ? 0 : sinf(t * 0.025f) * 0.15f + sinf(t * 0.041f) * 0.1f;
      sd.fillSoft(tx, ty - 3, 7 * size, 8 * size, hsvColor(30, 0.8f, 1.0f), 7);
      sd.fillShaded(tx, ty - 3 * size, 3.4f * size * (1 + fl), 5.2f * size * (1 - fl), hsvColor(25, 0.85f, 1.0f), 0.3f, fl);
      sd.fillShaded(tx, ty - 2 * size, 1.8f * size, 3.0f * size * (1 + fl), hsvColor(50, 0.6f, 1.0f), 0.2f, -fl);
    }
    if (k == DRAKE || k == GLIMMER) {  // wings behind the body
      for (int side = -1; side <= 1; side += 2) {
        float a = side * (0.9f + flap * 0.35f);
        float wx = cx + side * (bw + 4 * size), wy = byc - bh * 0.4f;
        if (k == DRAKE) {
          sd.fillShaded(wx, wy, 5.2f * size, 9 * size, deep, 0.3f, a);
          d.setColor(hsvColor(hue, 0.6f, 0.55f));
          sd.drawLineAA(cx + side * bw * 0.8f, wy + 2, wx + side * sinf(fabsf(a)) * 7, wy - 7 * size, 0.9f);
        } else {
          ColorVal wc = hsvColor(fmodf(hue + 30, 360), 0.28f, 1.0f);
          sd.fillShaded(wx, wy - 1, 6.5f * size, 8.5f * size, wc, 0.25f, a);
          sd.fillShaded(wx + side * 1.5f, wy - 1, 2.2f * size, 2.2f * size, iris, 0.6f);  // an eye-spot
        }
      }
    }

    // ---- body, belly, paws, feet ----
    sd.fillShaded(cx, byc, bw, bh, body, 0.4f, tilt);
    sd.fillShaded(cx, byc + bh * 0.2f, bw * 0.62f, bh * 0.7f, belly, 0.2f);
    for (int side = -1; side <= 1; side += 2) {
      float step = act == 1 || react ? 0 : 0;
      sd.fillShaded(cx + side * bw * 0.5f, ground - 2.2f * size - lift - step, 4.4f * size, 2.6f * size, deep, 0.3f);  // feet
      sd.fillShaded(cx + side * bw * 0.8f, byc - bh * 0.05f, 2.6f * size, 3.6f * size, body, 0.3f, side * 0.4f);  // paws
    }
    if (k == GLIMMER) {  // a fuzzy collar where head meets body
      ColorVal fz = hsvColor(hue, 0.12f, 1.0f);
      for (int i = -3; i <= 3; i++) sd.fillShaded(cx + i * bw * 0.27f, byc - bh * 0.75f + fabsf((float)i) * 0.8f, 3.4f * size, 2.8f * size, fz, 0.15f);
    }

    // ---- head features behind the head ----
    float twitch = 0;
    if (!still && !sleepy) { unsigned long s2 = t % 5000; if (s2 < 220) twitch = sinf(s2 / 220.0f * 3.1416f) * 0.3f; }
    float droop = sleepy ? 0.35f : 0;
    for (int side = -1; side <= 1; side += 2) {
      float ex = hxc + side * hw * 0.6f, ey = hyc - hh * 0.7f;
      float tw = side > 0 ? twitch : 0;
      if (k == FLUFF) {          // tall pointed ears with a fluffy inside
        float a = side * (0.28f + droop) + tw + headtilt;
        sd.fillShaded(ex + side * 1, ey - 5 * size, 5.2f * size, 11 * size, body, 0.3f, a);
        sd.fillShaded(ex + side * 1, ey - 4 * size, 2.6f * size, 7 * size, belly, 0.1f, a);
      } else if (k == EMBER) {   // small round ears
        sd.fillShaded(ex, ey, 5.0f * size, 5.0f * size, body, 0.3f);
        sd.fillShaded(ex, ey + 0.6f, 2.8f * size, 2.8f * size, pink, 0.1f);
      } else if (k == DRAKE) {   // little horns
        float a = side * 0.45f + headtilt;
        sd.fillShaded(ex - side * 1.5f, ey - 2 * size, 2.2f * size, 6 * size, hsvColor(40, 0.18f, 1.0f), 0.6f, a);
      } else {                   // GLIMMER: feathery antennae
        float sway = still ? 0 : sinf(t * 6.2832f / 2800 + side) * 1.5f;
        d.setColor(deep);
        float x0 = hxc + side * hw * 0.3f, y0 = hyc - hh * 0.85f;
        float x1 = x0 + side * 6 * size + sway, y1 = y0 - 12 * size;
        sd.drawLineAA(x0, y0, x1, y1, 1.0f);
        for (int f = 1; f <= 5; f++) {
          float u = f / 6.0f, fx = x0 + (x1 - x0) * u, fy = y0 + (y1 - y0) * u;
          sd.drawLineAA(fx, fy, fx + side * 2.6f * size * (1 - u * 0.4f), fy + 1.6f * size, 0.8f);
          sd.drawLineAA(fx, fy, fx - side * 1.8f * size * (1 - u * 0.4f), fy + 1.4f * size, 0.8f);
        }
      }
    }

    // ---- head ----
    sd.fillShaded(hxc, hyc, hw, hh, body, 0.45f, headtilt);
    if (k == FLUFF) {  // cheek tufts
      for (int side = -1; side <= 1; side += 2)
        sd.fillShaded(hxc + side * hw * 0.92f, hyc + hh * 0.35f, 4.2f * size, 3.2f * size, belly, 0.15f, side * 0.5f);
    }
    // the star mark on the forehead, glowing
    float stx = hxc, sty = hyc - hh * 0.62f;
    sd.fillSoft(stx, sty, 6 * size, 6 * size, star, (int)(10 * glow));
    starShape(sd, stx, sty, 2.6f * size, hsvColor(fmodf(hue + 200, 360), 0.25f + 0.25f * glow, 1.0f));

    // ---- face ----
    float fy = hyc + hh * 0.08f, eg = 6.8f * size * sx;
    float exl = hxc - eg + gx * 1.2f, exr = hxc + eg + gx * 1.2f, eyy = fy + gy * 0.9f;
    float erx = 4.0f * size, ery = 4.8f * size;
    bool blink = !still && blinking(t);
    bool petted = react == 1, booped = react == 3;
    sd.fillSoft(hxc - eg - 4, fy + 5.5f, 4.0f, 2.4f, pink, (m == HAPPY || petted) ? 12 : 7);
    sd.fillSoft(hxc + eg + 4, fy + 5.5f, 4.0f, 2.4f, pink, (m == HAPPY || petted) ? 12 : 7);
    if (petted) {                               // "^ ^"
      d.setColor(ink);
      for (int s2 = 0; s2 < 2; s2++) arcLine(sd, s2 ? exr : exl, eyy + 1.2f, 3.2f * size, false, 2.0f);
    } else if ((sleepy || blink) && !booped) {  // closed
      d.setColor(ink);
      for (int s2 = 0; s2 < 2; s2++) arcLine(sd, s2 ? exr : exl, eyy - 0.6f, 3.0f * size, true, 1.7f);
    } else {                                    // open: white, a coloured iris, a pupil, star glints
      float wide = booped ? 1.18f : 1.0f;
      for (int s2 = 0; s2 < 2; s2++) {
        float ex = s2 ? exr : exl;
        sd.fillShaded(ex, eyy, erx * wide, ery * wide, 0xFFFF, 0.1f);
        float ix = ex + gx * erx * 0.22f, iy = eyy + gy * ery * 0.2f;
        sd.fillShaded(ix, iy + 0.3f, erx * 0.78f, ery * 0.78f, iris, 0.5f);
        sd.fillShaded(ix, iy + 0.3f, erx * (booped ? 0.28f : 0.42f), ery * (booped ? 0.3f : 0.45f), ink, 0.4f);
        d.setColor(0xFFFF);
        sd.drawLineAA(ix - erx * 0.32f, iy - ery * 0.32f, ix - erx * 0.32f, iy - ery * 0.32f, erx * 0.55f);
        sparkle(sd, ix + erx * 0.34f, iy + ery * 0.36f, erx * 0.2f, 0xFFFF);
        if (m == BORED) sd.fillShaded(ex, eyy - ery * 0.78f, erx * 1.3f, ery * 0.6f, body, 0.2f);
        if (m == SAD) {
          d.setColor(ink);
          float side = s2 ? 1 : -1;
          sd.drawLineAA(ex - side * erx * 0.9f, eyy - ery - 1.4f, ex + side * erx * 0.6f, eyy - ery - 2.8f, 1.3f);
        }
      }
    }
    // mouth
    float my = fy + 7.0f * size;
    d.setColor(ink);
    if (petted || booped || (m == HAPPY && react == 2) || (sleepy && !still && (t % 7000) < 1200)) {  // open (a yawn when sleepy)
      float o = sleepy ? 1.5f : (booped ? 0.8f : 1.0f);
      sd.fillShaded(hxc, my + 0.8f, 2.5f * size * o, 2.0f * size * o, ink, 0.2f);
      sd.fillShaded(hxc, my + 1.5f * o, 1.5f * size * o, 0.9f * size * o, pink, 0.1f);
    } else if (m == HUNGRY) {
      sd.fillShaded(hxc, my + 0.6f, 1.4f * size, 1.5f * size, ink, 0.2f);
    } else if (m == SAD) {
      arcLine(sd, hxc, my + 2.2f, 2.3f * size, false, 1.4f);  // a frown
    } else if (m == BORED) {
      sd.drawLineAA(hxc - 1.8f, my + 0.4f, hxc + 1.8f, my + 0.4f, 1.3f);
    } else {  // a little smile
      arcLine(sd, hxc, my + 1.4f, 2.4f * size, true, 1.4f);
    }
    if (k == DRAKE) {  // tiny fangs peeking out
      d.setColor(0xFFFF);
      sd.drawLineAA(hxc - 2.4f, my + 0.6f, hxc - 2.2f, my + 1.8f, 0.9f);
      sd.drawLineAA(hxc + 2.4f, my + 0.6f, hxc + 2.2f, my + 1.8f, 0.9f);
    }

    if (still) return;
    // ---- around it ----
    if (petted) {
      for (int k2 = 0; k2 < 3; k2++) {
        float hp = rk * 1.4f - k2 * 0.25f;
        if (hp <= 0 || hp >= 1) continue;
        heart(sd, hxc + (k2 - 1) * 10 + sinf(hp * 7 + k2) * 2, hyc - hh - 2 - hp * 18, 2.3f * (1 - hp * 0.3f), hsvColor(350, 0.6f, 1.0f), 15);
      }
    }
    if (booped && rk < 0.7f) {  // a little "!"
      d.setColor(UIColor::warning_txt);
      sd.drawLineAA(hxc + hw + 2, hyc - hh, hxc + hw + 1.6f, hyc - hh + 5, 1.4f);
      sd.drawLineAA(hxc + hw + 1.4f, hyc - hh + 7.4f, hxc + hw + 1.4f, hyc - hh + 7.4f, 1.6f);
    }
    if (react == 2) {  // a star-ball bouncing beside it, and sparkles
      float ph = fabsf(cosf(rk * 3.1416f * 5));
      float bx2 = cx + bw + 14, by2 = ground - 3.4f - ph * 15;
      sd.fillSoft(bx2, ground, 4, 1.4f, 0x0000, 8);
      sd.fillSoft(bx2, by2, 6, 6, star, 6);
      sd.fillShaded(bx2, by2, 3.4f, 3.4f, THEME_ACCENT, 0.8f);
      for (int k2 = 0; k2 < 3; k2++) {
        float sp = fmodf(rk * 3 + k2 * 0.33f, 1);
        sparkle(sd, hxc + (k2 - 1) * 16, hyc - hh - 4 - sp * 10, 2.2f * (1 - sp), UIColor::warning_txt);
      }
    }
    if (sleepy) {
      for (int k2 = 0; k2 < 3; k2++) {
        float zp = fmodf(t / 3000.0f + k2 / 3.0f, 1);
        d.setColor(UIColor::secondary_txt);
        d.setTextSize(1);
        d.setCursor((int)(hxc + hw * 0.7f + zp * 8 + k2), (int)(hyc - hh - zp * 14));
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
  // a small five-pointed star (five tapering spokes and a centre)
  static void starShape(SF32Display& sd, float x, float y, float r, ColorVal c) {
    sd.setColor(c);
    for (int i = 0; i < 5; i++) {
      float a = -1.5708f + i * 1.2566f;
      sd.drawLineAA(x, y, x + cosf(a) * r, y + sinf(a) * r, r * 0.42f);
    }
    sd.drawLineAA(x, y, x, y, r * 0.75f);
  }
  static void sparkle(SF32Display& sd, float x, float y, float r, ColorVal c) {
    if (r <= 0.2f) return;
    sd.setColor(c);
    sd.drawLineAA(x - r, y, x + r, y, 0.7f);
    sd.drawLineAA(x, y - r, x, y + r, 0.7f);
  }
};
static PetGame pet;
