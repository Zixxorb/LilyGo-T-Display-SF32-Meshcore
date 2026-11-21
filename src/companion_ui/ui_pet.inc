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

// Which path-hash size each node uses (1, 2 or 3 bytes), learnt from its flood adverts and messages
// (and channel messages whose sender is a contact by name), and whether the bot nudged it: a sender
// writes its own setting into the top two bits of path_len. Known for sure when those bits say 2-3
// bytes, or say 1 byte on an advert that came through repeaters; a direct (zero-hop) advert with
// them at 0 tells nothing. Remembered in a tiny file (5 bytes a node, the 64 heard most recently).
#define HSZ_FILE "/sf32_hsz"
class HashSizes {
public:
  // a node's path_len as heard; true if a node the bot nudged has just moved up from 1 byte
  bool note(const uint8_t* pub_key, uint8_t path_len) {
    uint8_t sz = (path_len >> 6) + 1, hops = path_len & 63;
    if (sz > 3) return false;                // reserved (MeshCore drops these anyway)
    load();
    int at = find(pub_key);
    if (at > 0) at = toFront(at);            // heard again, even directly: the newest (strangers don't push friends out)
    if (sz == 1 && hops == 0) return false;  // a direct advert: its size isn't known from this
    if (at >= 0 && (_e[at].v & SIZE) == sz) return false;
    bool upgraded = at >= 0 && (_e[at].v & SIZE) == 1 && (_e[at].v & NUDGED) && sz >= 2;
    if (at < 0) at = add(pub_key);
    _e[at].v = (_e[at].v & NUDGED) | sz;
    _dirty = true;
    return upgraded;
  }
  void begin() { load(); }  // at boot (not lazily from a mesh hook while the screen is off)
  uint8_t get(const uint8_t* pub_key) { load(); int at = find(pub_key); return at >= 0 ? (_e[at].v & SIZE) : 0; }
  // the bot sent it a reminder (it sends 1-byte hashes)
  void setNudged(const uint8_t* pub_key) {
    load();
    int at = find(pub_key);
    if (at < 0) { at = add(pub_key); _e[at].v = 1; }
    if (_e[at].v & NUDGED) return;
    _e[at].v |= NUDGED;
    _dirty = true;
  }
  bool nudged(const uint8_t* pub_key) { load(); int at = find(pub_key); return at >= 0 && (_e[at].v & NUDGED); }
  // the mesh's health, of the nodes known: on 2-3 bytes, on 1 byte, nudged by the bot
  void counts(int& multi, int& one, int& nudges) {
    load();
    multi = one = nudges = 0;
    for (int i = 0; i < _n; i++) {
      if ((_e[i].v & SIZE) >= 2) multi++; else if ((_e[i].v & SIZE) == 1) one++;
      if (_e[i].v & NUDGED) nudges++;
    }
  }
  void save() {
    if (!_dirty) return;
    File f = InternalFS.open(HSZ_FILE, "w", true);
    if (f) { f.write((const uint8_t*)&_n, 1); f.write((const uint8_t*)_e, _n * sizeof(Entry)); }
    _dirty = false;
  }
private:
  struct Entry { uint8_t key[4]; uint8_t v; };  // v: the size (1-3) in bits 0-1, bit 7 nudged
  static const uint8_t SIZE = 0x03, NUDGED = 0x80;
  static const int N = 64;
  Entry _e[N];
  uint8_t _n = 0;
  bool _loaded = false, _dirty = false;
  int find(const uint8_t* k) const { for (int i = 0; i < _n; i++) if (memcmp(_e[i].key, k, 4) == 0) return i; return -1; }
  int toFront(int at) {  // (order only: saved with the next change)
    Entry e = _e[at];
    for (int i = at; i > 0; i--) _e[i] = _e[i - 1];
    _e[0] = e;
    return 0;
  }
  int add(const uint8_t* k) {  // a new one: at the front, the oldest dropped
    if (_n < N) _n++;
    for (int i = _n - 1; i > 0; i--) _e[i] = _e[i - 1];
    memcpy(_e[0].key, k, 4);
    _e[0].v = 0;
    return 0;
  }
  void load() {
    if (_loaded) return;
    _loaded = true;
    File f = InternalFS.open(HSZ_FILE, "r");
    if (!f) return;
    uint8_t n = 0;
    if (f.read(&n, 1) == 1 && n <= N && f.read((uint8_t*)_e, n * sizeof(Entry)) == (int)(n * sizeof(Entry))) _n = n;
    for (int i = 0; i < _n; i++) {  // (damaged: keep what's sound)
      uint8_t sz = _e[i].v & SIZE;
      if (sz < 1 || (_e[i].v & ~(SIZE | NUDGED))) { _n = i; break; }
    }
  }
};
static HashSizes hash_sizes;


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
    if (d >= 60000) {  // whole minutes (age() works in minutes); the rest carries over: updated
      unsigned long used = d - d % 60000;  // every frame on screen, it used to lose it all
      age(used, _was_on);
      _last_ms += used;
    }
    _was_on = screen_on;
    // the day's caps: every 24 h of uptime
    if (now - _s.day_ms > 24UL * 3600 * 1000) { _s.day_ms = now | 1; _s.day_fed = _s.day_cur = 0; _dirty = true; }
    // fed by the packets the radio heard
    uint32_t pk = radio_driver.getPacketsRecv();
    if (pk != _last_pkts) {
      uint32_t got = pk - _last_pkts;
      _last_pkts = pk;
      today_finds.packets(got);
      gain(_s.fed, _s.day_fed, got * 3, 70);
    }
    // curious about adverts and new nodes (counted by onAdvert)
    if (_adverts || _new_nodes) {
      gain(_s.curious, _s.day_cur, _adverts * 2 + _new_nodes * 15, 60);
      _adverts = _new_nodes = 0;
    }
  }
  void onScreen(bool on) {  // screen on/off (UITask::powerCare): catch up (saving: the caller, throttled)
    if (!_begun) return;
    update(on);
  }
  void onAdvert(bool is_new) {  // from the mesh (any time): just counted, in RAM
    today_finds.advert(is_new);
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
#ifdef SF32_REMOTE_TOUCH
    react(3, 9000);  // debug builds: long enough to screenshot
#else
    react(3, 900);
#endif
    return true;
  }
  void play() {  // a hold on it (or Play in its menu)
    bump(_s.happy, 18);
    _s.rested = _s.rested > 6 ? _s.rested - 4 : 2;
    _dirty = true;
    react(2, 3200);
  }
  int reaction() const { return (_react && millis() - _react_at < _react_len) ? _react : 0; }
  // another Starlit came near (ui_meet.inc): happier and more curious (the caller limits how often)
  void meet() { bump(_s.happy, 10); bump(_s.curious, 10); }
  void greet() { react(1, 1600); }  // the visitor arrives: a happy wiggle
  // hearts floating up between two Starlits (t: ms into the visit)
  void drawHearts(DisplayDriver& d, float x, float y, unsigned long t) {
    SF32Display& sd = (SF32Display&)d;
    for (int k2 = 0; k2 < 3; k2++) {
      float p = fmodf(t / 1800.0f + k2 / 3.0f, 1.0f);
      heart(sd, x + sinf(p * 7 + k2 * 2) * 3, y - p * 22, 2.4f * (1 - p * 0.4f), hsvColor(350, 0.6f, 1.0f), (int)(15 * (1 - p)));
    }
  }
  void look(int x, int y) { _gaze_x = x; _gaze_y = y; _gaze_at = millis() | 1; }

  // ---- mood ----
  enum Mood { CONTENT, HAPPY, EXCITED, SLEEPY, HUNGRY, BORED, SAD };
  // What the rest of the device is doing, so your Starlit reflects it (set by its page each frame;
  // friends' Starlits ignore it). In priority order for the bottom line.
  enum Ctx { CTX_SOS = 1, CTX_NIGHT = 2, CTX_LOWPOWER = 4, CTX_MAIL = 8, CTX_LOWBATT = 16, CTX_CHARGING = 32 };
  void setContext(uint8_t c) { _ctx = c; }
  uint8_t context() const { return _ctx; }
  // the line under its bars for the device's state (NULL: nothing to say, show its mood)
  const char* contextLine() const {
    if (_ctx & CTX_SOS) return "it's worried: SOS is on";
    if (_ctx & CTX_MAIL) return "it has mail: tap it";
    if (_ctx & CTX_NIGHT) return "it's asleep: night hours";
    if (_ctx & CTX_LOWPOWER) return "it's napping: low power";
    if (_ctx & CTX_LOWBATT) return "it's drowsy: battery low";
    if (_ctx & CTX_CHARGING) return "it's charging up";
    return NULL;
  }
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
  // hsz: its node's path-hash size (0 not known): a faint aura in its colour
  void draw(DisplayDriver& d, float cx, float cy, float size, bool still, uint32_t seed = 0, int kind_of = -1, int hsz = 0) {
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
    bool other = seed != 0;  // a friend's: calm, its own blinks, not looking at your finger
    if (other) { m = CONTENT; react = 0; }
    float rk = react ? (t - _react_at) / (float)_react_len : 0;
    const uint8_t ctx = other ? 0 : _ctx;
    const bool worried = ctx & CTX_SOS, mail = (ctx & CTX_MAIL) && !worried;
    const bool asleep = (ctx & (CTX_NIGHT | CTX_LOWPOWER)) && !worried && !mail;  // eyes shut (a reaction wakes it a moment)
    bool sleepy = m == SLEEPY || asleep || ((ctx & CTX_LOWBATT) && !worried && !mail);
    if (worried) m = SAD;           // brows up, a frown
    else if (mail) m = EXCITED;     // perked up

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
    if (!other && _gaze_at && t - _gaze_at < 3000) {
      float dx = _gaze_x - cx, dy = _gaze_y - cy, l = sqrtf(dx * dx + dy * dy);
      if (l > 1) { gx = dx / l; gy = dy / l; }
    } else if (!act && !still) {
      gx = sinf(t * 6.2832f / 7000) * 0.6f; gy = sinf(t * 6.2832f / 11000) * 0.3f;
    }
    float wag = still ? 0 : sinf(t * 6.2832f / tail_speed);
    float glow = still ? 0.6f : 0.6f + 0.4f * sinf(t * 6.2832f / 2400);  // the star's pulse
    if (sleepy) glow *= 0.4f;

    // ---- its build: each kind its own shape, each one a little different (from its seed) ----
    float vh = 0.92f + ((sd0 >> 12) % 5) * 0.04f, vb = 0.92f + ((sd0 >> 15) % 5) * 0.04f;
    float ground = cy + 22 * size;
    float hover = (k == GLIMMER && !still) ? 3 + sinf(t * 6.2832f / 1800) * 2.2f : (k == GLIMMER ? 3 : 0);
    float bw, bh, hw, hh, byc, hyc;
    if (k == DRAKE) {          // sturdy and upright: a big body, a snouted head
      bw = 13 * vb; bh = 12 * vb; hw = 14 * vh; hh = 12.5f * vh;
    } else if (k == FLUFF) {   // a puffball: head and body in one round orb
      bw = 0; bh = 0; hw = 19 * vh; hh = 17.5f * vh;
    } else if (k == GLIMMER) { // floats: a big head over a small tapering wisp
      bw = 5.5f * vb; bh = 9 * vb; hw = 15.5f * vh; hh = 14.5f * vh;
    } else {                   // EMBER: squat and wide, the head sunk into the body
      bw = 18.5f * vb; bh = 10 * vb; hw = 15 * vh; hh = 12 * vh;
    }
    bw *= size * sx; bh *= size * sy; hw *= size * sx; hh *= size * sy;
    if (k == FLUFF) { hyc = ground - hh - 2.5f * size - lift; byc = hyc + hh * 0.45f; }
    else if (k == GLIMMER) { hyc = ground - 14 * size - hh * 0.62f - lift - hover; byc = hyc + hh * 0.72f + bh * 0.55f; }
    else if (k == EMBER) { byc = ground - bh - 2.5f * size - lift; hyc = byc - bh * 0.5f - hh * 0.38f; }
    else { byc = ground - bh - 2.5f * size - lift; hyc = byc - bh - hh * 0.5f; }
    float hxc = cx + sinf(headtilt) * 4;

    // its parts: the kind sets the shape and face; ears/horns, tail, wings and markings mix,
    // each its kind's own about half the time (from a second hash of the seed: no new data)
    uint32_t mx = sd0 * 2246822519u; mx ^= mx >> 15; mx *= 3266489917u; mx ^= mx >> 13;
    auto pick = [&](int own, int n, int sh) { return ((mx >> sh) & 1) ? own : (own + 1 + (int)((mx >> (sh + 1)) % (n - 1))) % n; };
    enum { TOP_HORNS, TOP_CRYSTAL, TOP_STALKS, TOP_FLAME };   // what's on its head
    enum { TAIL_SPADE, TAIL_COMET, TAIL_FLAME, TAIL_NONE };  // (the floating Glimmer: none)
    enum { WING_NONE, WING_BACK, WING_PETAL };
    static const uint8_t OWN_TOP[4] = {TOP_HORNS, TOP_CRYSTAL, TOP_STALKS, TOP_FLAME};
    static const uint8_t OWN_TAIL[4] = {TAIL_SPADE, TAIL_COMET, TAIL_NONE, TAIL_FLAME};
    static const uint8_t OWN_WING[4] = {WING_BACK, WING_NONE, WING_PETAL, WING_NONE};
    int top = pick(OWN_TOP[k], 4, 0);
    int tail = k == GLIMMER ? TAIL_NONE : pick(OWN_TAIL[k] == TAIL_FLAME ? 2 : OWN_TAIL[k], 3, 3);
    int wing = k == GLIMMER ? WING_PETAL : pick(OWN_WING[k], 3, 6);
    int marks = (mx >> 9) % 3;  // 0 none, 1 spots, 2 stripes
    float abw = bw > 0 ? bw : hw * 0.8f;  // where a tail or back wings attach (the Fluff: its orb)

    // its node's path-hash size: a faint aura round all of it (1 byte amber, 2 cyan, 3 violet)
    if (hsz >= 1 && hsz <= 3) {
      static const float AURA_HUE[3] = {35, 185, 275};
      // round its own head and body (not down to the ground), so it moves as it does: hops, bobs, sways, breathes
      float top = hyc - hh, bot = hyc + hh > byc + bh ? hyc + hh : byc + bh;
      float ay = (top + bot) / 2, ah = (bot - top) / 2 + 9 * size, aw = (hw > bw ? hw : bw) + 12 * size;
      float pulse = still ? 1 : 0.85f + 0.15f * sinf(t * 6.2832f / 3000);
      sd.fillSoft((hxc + cx) / 2, ay, aw, ah, hsvColor(AURA_HUE[hsz - 1], 0.8f, 1.0f), (int)(16 * pulse));
    }
    // under it: a shadow (a soft glow for the floating Glimmer)
    float air = (lift + hover) / 8;
    if (k == GLIMMER) sd.fillSoft(cx, ground, 12 * size * (1 - air * 0.15f), 3.0f * size, star, (int)(7 * (1 - air * 0.3f)));
    else sd.fillSoft(cx, ground, (k == EMBER ? 19 : 15) * size * (1 - air * 0.2f), 3.2f * size, 0x0000, (int)(11 * (1 - air * 0.4f)));

    // ---- behind: tail, wings ----
    if (tail == TAIL_SPADE) {  // a long tail curving out to a spade tip
      float px = cx + abw * 0.6f, py = byc + bh * 0.5f;
      d.setColor(deep);
      for (int i = 1; i <= 6; i++) {
        float u = i / 6.0f, a = -0.3f + u * 1.4f + wag * 0.35f * u;
        float nx = cx + abw * 0.6f + u * 15 * size, ny = byc + bh * 0.5f - sinf(a) * 8 * u * size;
        sd.drawLineAA(px, py, nx, ny, 3.2f * size * (1 - u * 0.5f));
        px = nx; py = ny;
      }
      sd.fillShaded(px + 1.5f, py - 1, 3.4f * size, 2.4f * size, deep, 0.4f, -0.6f + wag * 0.3f);
    } else if (tail == TAIL_COMET) {  // a comet tail: glowing orbs trailing up behind, smaller and fainter
      for (int i = 4; i >= 0; i--) {
        float u = i / 4.0f, a = wag * 0.3f * u;
        float ox = cx + abw * 0.8f + u * 9 * size + sinf(a) * 4, oy = byc - u * 15 * size;
        float rr = (4.4f - u * 2.6f) * size;
        sd.fillSoft(ox, oy, rr * 2.2f, rr * 2.2f, star, (int)(9 - u * 5));
        sd.fillShaded(ox, oy, rr, rr, i == 0 ? body : star, 0.7f);
      }
    } else if (tail == TAIL_FLAME) {  // a short tail with a flickering flame tip
      float tx = cx + abw * 1.1f + wag * 2, ty = byc + bh * 0.1f;
      d.setColor(deep);
      sd.drawLineAA(cx + abw * 0.5f, byc + bh * 0.5f, tx, ty, 3.0f * size);
      float fl = still ? 0 : sinf(t * 0.025f) * 0.15f + sinf(t * 0.041f) * 0.1f;
      sd.fillSoft(tx, ty - 3, 7 * size, 8 * size, hsvColor(30, 0.8f, 1.0f), 7);
      sd.fillShaded(tx, ty - 3 * size, 3.4f * size * (1 + fl), 5.2f * size * (1 - fl), hsvColor(25, 0.85f, 1.0f), 0.3f, fl);
      sd.fillShaded(tx, ty - 2 * size, 1.8f * size, 3.0f * size * (1 + fl), hsvColor(50, 0.6f, 1.0f), 0.2f, -fl);
    }
    if (wing != WING_NONE) {  // wings: leathery ones from its back, or petals from the sides of its head
      if (k == GLIMMER && !still) flap += sinf(t * 6.2832f / 1300) * 0.35f;  // always a gentle flutter (it floats)
      bool back = wing == WING_BACK;
      for (int side = -1; side <= 1; side += 2) {
        float a = side * (0.9f + flap * 0.35f);
        float wx = back ? cx + side * (abw + 4 * size) : cx + side * (hw * 0.95f), wy = back ? (bw > 0 ? byc - bh * 0.4f : hyc + hh * 0.1f) : hyc + hh * 0.35f;
        if (back) {
          sd.fillShaded(wx, wy, 5.2f * size, 9 * size, deep, 0.3f, a);
          d.setColor(hsvColor(hue, 0.6f, 0.55f));
          sd.drawLineAA(cx + side * abw * 0.8f, wy + 2, wx + side * sinf(fabsf(a)) * 7, wy - 7 * size, 0.9f);
        } else {  // petals: two each side, pointed, with a light vein
          ColorVal wc = hsvColor(fmodf(hue + 30, 360), 0.3f, 1.0f), wc2 = hsvColor(fmodf(hue + 60, 360), 0.22f, 1.0f);
          sd.fillShaded(wx + side * 1, wy - 3, 3.6f * size, 9.5f * size, wc, 0.5f, a - side * 0.25f);
          sd.fillShaded(wx + side * 2, wy + 3, 3.0f * size, 7.0f * size, wc2, 0.5f, a + side * 0.55f);
          d.setColor(hsvColor(fmodf(hue + 30, 360), 0.15f, 1.0f));
          sd.drawLineAA(cx + side * hw * 0.8f, wy, wx + side * sinf(fabsf(a)) * 8, wy - 8 * size, 0.6f);
        }
      }
    }

    // ---- the body ----
    if (k == GLIMMER) {  // a tapering wisp that sways, a little glowing bead at its tip
      float sw = still ? 0 : sinf(t * 6.2832f / 2200) * 0.25f;
      sd.fillShaded(cx, byc, bw, bh, body, 0.4f, sw);
      float tx = cx + sinf(sw) * bh * 0.9f, ty = byc + bh * 0.95f;
      sd.fillSoft(tx, ty, 4 * size, 4 * size, star, (int)(5 + 4 * glow));
      sd.fillShaded(tx, ty, 1.6f * size, 1.6f * size, star, 0.9f);
    } else if (k != FLUFF) {
      sd.fillShaded(cx, byc, bw, bh, body, 0.4f, tilt);
      sd.fillShaded(cx, byc + bh * 0.2f, bw * 0.6f, bh * 0.68f, belly, 0.2f);
      if (k == DRAKE) {  // belly scales
        d.setColor(hsvColor(hue, 0.22f, 0.85f));
        for (int i = 0; i < 3; i++) { float yy = byc + bh * (-0.1f + i * 0.28f); sd.drawLineAA(cx - bw * 0.4f, yy, cx + bw * 0.4f, yy, 0.7f); }
      }
    }
    if (k != GLIMMER) {  // feet (the Fluff's tiny), paws
      float fw = k == EMBER ? 5.6f : (k == FLUFF ? 3.4f : 4.8f), fx = k == FLUFF ? hw * 0.38f : bw * 0.5f;
      for (int side = -1; side <= 1; side += 2) {
        sd.fillShaded(cx + side * fx, ground - 2.2f * size - lift, fw * size, 2.6f * size, deep, 0.3f);
        if (k == DRAKE) {  // little claws
          d.setColor(hsvColor(40, 0.15f, 1.0f));
          for (int c2 = -1; c2 <= 1; c2++) sd.drawLineAA(cx + side * fx + c2 * 1.6f, ground - 1.0f - lift, cx + side * fx + c2 * 1.6f, ground - 1.0f - lift, 1.0f);
        }
        if (k != FLUFF) sd.fillShaded(cx + side * bw * 0.85f, byc - bh * 0.05f, 2.6f * size, 3.6f * size, body, 0.3f, side * 0.4f);
      }
    }

    // ---- head features behind the head ----
    float twitch = 0;
    if (!still && !sleepy) { unsigned long s2 = t % 5000; if (s2 < 220) twitch = sinf(s2 / 220.0f * 3.1416f) * 0.3f; }
    float droop = sleepy ? 0.35f : 0;
    for (int side = -1; side <= 1; side += 2) {
      float ex = hxc + side * hw * 0.6f, ey = hyc - hh * 0.7f;
      float tw = side > 0 ? twitch : 0;
      if (top == TOP_CRYSTAL) {  // crystal ears: two glossy shards each side
        float a = side * (0.3f + droop) + tw + headtilt;
        ColorVal gem = hsvColor(fmodf(hue + 190, 360), 0.35f, 1.0f);
        sd.fillShaded(ex + side * 1.5f, ey - 5 * size, 2.6f * size, 9.5f * size, gem, 1.0f, a);
        sd.fillShaded(ex - side * 2.5f, ey - 3 * size, 1.9f * size, 6.5f * size, gem, 1.0f, a - side * 0.35f);
        if (!still) sd.fillSoft(ex + side * 1.5f, ey - 8 * size, 3.5f * size, 4.5f * size, 0xFFFF, (int)(3 + 3 * glow));
      } else if (top == TOP_FLAME) {  // flame tufts that flicker
        float fl = still ? 0 : sinf(t * 0.03f + side * 1.7f) * 0.18f + sinf(t * 0.047f + side) * 0.1f;
        sd.fillSoft(ex, ey - 3 * size, 5 * size, 7 * size, hsvColor(30, 0.8f, 1.0f), 6);
        sd.fillShaded(ex, ey - 2.5f * size, 3.4f * size * (1 + fl), 6.0f * size * (1 - fl), hsvColor(22, 0.85f, 1.0f), 0.3f, side * 0.25f + fl);
        sd.fillShaded(ex, ey - 1.5f * size, 1.8f * size, 3.6f * size * (1 + fl), hsvColor(48, 0.6f, 1.0f), 0.2f, side * 0.2f - fl);
      } else if (top == TOP_HORNS) {  // little horns
        float a = side * 0.45f + headtilt;
        sd.fillShaded(ex - side * 1.5f, ey - 2 * size, 2.2f * size, 6 * size, hsvColor(40, 0.18f, 1.0f), 0.6f, a);
      } else {                   // curling light-stalks with glowing orb tips
        float sway = still ? 0 : sinf(t * 6.2832f / 2800 + side) * 1.5f;
        d.setColor(deep);
        float px = hxc + side * hw * 0.3f, py = hyc - hh * 0.85f;
        for (int i = 1; i <= 6; i++) {  // a curl outward, then back in at the top
          float u = i / 6.0f, a = u * 2.2f;
          float nx = hxc + side * (hw * 0.3f + sinf(a) * 7 * size) + sway * u, ny = hyc - hh * 0.85f - u * 12 * size;
          sd.drawLineAA(px, py, nx, ny, 1.2f - u * 0.4f);
          px = nx; py = ny;
        }
        sd.fillSoft(px, py, 5 * size, 5 * size, star, (int)(5 + 5 * glow));
        sd.fillShaded(px, py, 2.0f * size, 2.0f * size, star, 0.9f);
      }
    }

    // ---- head ----
    sd.fillShaded(hxc, hyc, hw, hh, body, 0.45f, headtilt);
    if (k == FLUFF) sd.fillShaded(hxc, hyc + hh * 0.48f, hw * 0.55f, hh * 0.38f, belly, 0.2f);  // the orb's light front
    if (k == DRAKE) {  // a lighter snout with two nostrils
      sd.fillShaded(hxc, hyc + hh * 0.42f, hw * 0.52f, hh * 0.36f, belly, 0.3f, headtilt);
      d.setColor(deep);
      sd.drawLineAA(hxc - 2, hyc + hh * 0.3f, hxc - 2, hyc + hh * 0.3f, 1.1f);
      sd.drawLineAA(hxc + 2, hyc + hh * 0.3f, hxc + 2, hyc + hh * 0.3f, 1.1f);
    }
    if (marks == 1) {  // spots: a few darker freckles up each side of its head
      for (int side = -1; side <= 1; side += 2) {
        sd.fillShaded(hxc + side * hw * 0.58f, hyc - hh * 0.42f, 1.7f * size, 1.5f * size, deep, 0.3f);
        sd.fillShaded(hxc + side * hw * 0.74f, hyc - hh * 0.12f, 1.2f * size, 1.1f * size, deep, 0.3f);
        sd.fillShaded(hxc + side * hw * 0.4f, hyc - hh * 0.72f, 1.0f * size, 0.9f * size, deep, 0.3f);
      }
    } else if (marks == 2) {  // stripes: short darker bands on its cheeks
      d.setColor(deep);
      for (int side = -1; side <= 1; side += 2)
        for (int i = 0; i < 2; i++) {
          float yy = hyc - hh * 0.05f + i * hh * 0.22f;
          sd.drawLineAA(hxc + side * hw * 0.97f, yy, hxc + side * hw * 0.72f, yy + hh * 0.06f, 1.4f * size);
        }
    }
    // the star mark on the forehead, glowing
    float stx = hxc, sty = hyc - hh * 0.62f;
    sd.fillSoft(stx, sty, 6 * size, 6 * size, star, (int)(10 * glow));
    starShape(sd, stx, sty, 2.6f * size, hsvColor(fmodf(hue + 200, 360), 0.25f + 0.25f * glow, 1.0f));

    // ---- face ----
    float fy = hyc + hh * (k == DRAKE ? -0.12f : (k == FLUFF ? -0.06f : 0.06f)), eg = (k == EMBER ? 7.6f : 6.8f) * size * sx;
    float exl = hxc - eg + gx * 1.2f, exr = hxc + eg + gx * 1.2f, eyy = fy + gy * 0.9f;
    float erx = 4.0f * size, ery = 4.8f * size;
    bool blink = !still && blinking(t, sd0);
    bool petted = react == 1, booped = react == 3;
    sd.fillSoft(hxc - eg - 4, fy + 5.5f, 4.0f, 2.4f, pink, (m == HAPPY || petted) ? 12 : 7);
    sd.fillSoft(hxc + eg + 4, fy + 5.5f, 4.0f, 2.4f, pink, (m == HAPPY || petted) ? 12 : 7);
    if (petted) {                               // "^ ^"
      d.setColor(ink);
      for (int s2 = 0; s2 < 2; s2++) arcLine(sd, s2 ? exr : exl, eyy + 1.2f, 3.2f * size, false, 2.0f);
    } else if ((sleepy || blink || asleep) && !booped && !mail) {  // closed
      d.setColor(ink);
      for (int s2 = 0; s2 < 2; s2++) arcLine(sd, s2 ? exr : exl, eyy - 0.6f, 3.0f * size, true, 1.7f);
    } else {                                    // open: white, a coloured iris, a pupil, star glints
      if (booped) { erx *= 1.15f; ery *= 1.15f; }  // startled: the whole eye opens wider (the same eye, bigger)
      for (int s2 = 0; s2 < 2; s2++) {
        float ex = s2 ? exr : exl;
        sd.fillShaded(ex, eyy, erx, ery, 0xFFFF, 0.1f);
        float ix = ex + gx * erx * 0.22f, iy = eyy + gy * ery * 0.2f;
        sd.fillShaded(ix, iy + 0.3f, erx * 0.78f, ery * 0.78f, iris, 0.5f);
        sd.fillShaded(ix, iy + 0.3f, erx * (booped ? 0.36f : 0.42f), ery * (booped ? 0.39f : 0.45f), ink, 0.4f);
        d.setColor(0xFFFF);
        sd.drawLineAA(ix - erx * 0.32f, iy - ery * 0.32f, ix - erx * 0.32f, iy - ery * 0.32f, erx * 0.55f);
        sparkle(sd, ix + erx * 0.34f, iy + ery * 0.36f, erx * 0.2f, 0xFFFF);
        if (m == BORED) sd.fillShaded(ex, eyy - ery * 0.78f, erx * 1.3f, ery * 0.6f, body, 0.2f);
        if (m == SAD) {
          d.setColor(ink);
          float side = s2 ? 1 : -1;
          sd.drawLineAA(ex - side * erx * 0.9f, eyy - ery - 3.0f, ex + side * erx * 0.6f, eyy - ery - 1.2f, 1.3f);  // inner ends up: worried, not cross
        }
      }
    }
    // mouth
    float my = k == DRAKE ? hyc + hh * 0.52f : fy + 7.0f * size;
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

    // ---- what the device is doing ----
    if (sleepy) {  // z's: from beside its head (clear of antennae and stalks), up and out along a slant,
                   // growing as they go and fading at the end; evenly spaced, so they never overlap
      ColorVal zc = hsvColor(fmodf(hue + 200, 360), 0.22f, 0.95f), bgc = UIColor::window_bkg;
      for (int z = 0; z < 3; z++) {
        float zp = still ? 0.15f + z * 0.33f : fmodf(t / 3600.0f + z / 3.0f, 1.0f);
        if (still && z == 2) continue;
        float fade = zp > 0.65f ? (1 - zp) / 0.35f : (zp < 0.1f ? zp / 0.1f : 1);  // in and out
        int a = (int)(fade * 16);
        int r = (((zc >> 11) & 31) * a + ((bgc >> 11) & 31) * (16 - a)) >> 4;
        int g = (((zc >> 5) & 63) * a + ((bgc >> 5) & 63) * (16 - a)) >> 4;
        int b = ((zc & 31) * a + (bgc & 31) * (16 - a)) >> 4;
        d.setColor((ColorVal)((r << 11) | (g << 5) | b));
        float zs = (1.3f + zp * 1.5f) * size;  // half its size
        float zx = hxc + hw * 1.05f + zp * 13 * size, zy = hyc - hh * 0.15f - zp * 22 * size;
        sd.drawLineAA(zx - zs, zy - zs, zx + zs, zy - zs, 0.8f + zp * 0.3f);
        sd.drawLineAA(zx + zs, zy - zs, zx - zs, zy + zs, 0.8f + zp * 0.3f);
        sd.drawLineAA(zx - zs, zy + zs, zx + zs, zy + zs, 0.8f + zp * 0.3f);
      }
    }
    if (mail) {  // holds a little envelope in front of it, bobbing
      float ex2 = cx, ey2 = (k == FLUFF ? hyc + hh * 0.62f : (k == GLIMMER ? byc + bh * 0.35f : byc + bh * 0.05f)) + (still ? 0 : sinf(t * 6.2832f / 900) * 0.8f);
      float ew = 7.5f * size, eh = 5.2f * size;
      d.setColor(0xFFFF);
      d.fillRect((int)(ex2 - ew), (int)(ey2 - eh), (int)(ew * 2), (int)(eh * 2));
      d.setColor(THEME_ACCENT);
      sd.drawLineAA(ex2 - ew, ey2 - eh, ex2, ey2 + eh * 0.15f, 1.0f);
      sd.drawLineAA(ex2, ey2 + eh * 0.15f, ex2 + ew, ey2 - eh, 1.0f);
      for (int side = -1; side <= 1; side += 2)  // its paws holding it
        sd.fillShaded(ex2 + side * ew * 0.95f, ey2 + eh * 0.3f, 2.4f * size, 2.2f * size, body, 0.3f);
    }
    if ((ctx & CTX_CHARGING) && !worried && !asleep && !still) {  // now and then, a spark rising off it
      unsigned long ph = t % 2400;
      if (ph < 1200) {
        float p = ph / 1200.0f, side = ((t / 2400) & 1) ? 1 : -1;
        sparkle(sd, cx + side * (hw * 0.7f + p * 4), hyc - p * 14, (1.8f - p) * size, hsvColor(45, 0.55f, 1.0f));
      }
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
  }

private:
  PetSave _s;
  bool _begun = false, _dirty = false, _was_on = true;
  unsigned long _last_ms = 0;
  uint32_t _last_pkts = 0;
  uint8_t _adverts = 0, _new_nodes = 0, _ctx = 0;
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
  bool blinking(unsigned long t, uint32_t seed) const {
    unsigned long slot = t / 4000;
    uint32_t r = (uint32_t)(slot * 2654435761u) ^ seed;
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
public:
  // the look of another node's Starlit, from its public key (the same rule as our own)
  static uint32_t seedOf(const uint8_t* pub_key) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < PUB_KEY_SIZE; i++) { h ^= pub_key[i]; h *= 16777619u; }
    return h ? h : 1;
  }
};
static PetGame pet;

// Friends: the Starlits of your favourite nodes and the 5 others heard most recently (each drawn
// from its key; nothing is sent or stored), in a grid that scrolls like the menus; tap one to meet
// it (big, with its name and kind). Four at a time; a tap shows the next four; swipe right or the button: back.
class FriendsScreen : public UIScreen {
public:
  FriendsScreen(UITask* task) : _task(task) {}
  // favourites first (starred in the app), then the 5 others heard most recently
  void reset() {
    _n = 0;
    _page = 0;
    _detail_on = false;
    _card_only = false;
    _layer_ok = false;
    _ds = DragScroll();
    uint8_t recent[RECENT][KEY_LEN];
    uint32_t rts[RECENT];
    int nr = 0;
    // MeshCore's iterator (getContactByIdx() counts its reserved anonymous slots, getNumContacts() doesn't)
    ContactInfo c;
    ContactsIterator it = the_mesh.startContactsIterator();
    uint8_t favs[FRIENDS_MAX][KEY_LEN];
    int nf = 0;
    while (it.hasNext(&the_mesh, c)) {
      if (c.flags & 1) { if (nf < FRIENDS_MAX) memcpy(favs[nf++], c.id.pub_key, KEY_LEN); continue; }  // flags bit 0: favourite
      uint32_t ts = c.last_advert_timestamp;  // the newest RECENT of the rest, kept in order
      int k = nr < RECENT ? nr++ : RECENT;
      if (k == RECENT) { if (ts <= rts[RECENT - 1]) continue; k = RECENT - 1; }
      while (k > 0 && rts[k - 1] < ts) { rts[k] = rts[k - 1]; memcpy(recent[k], recent[k - 1], KEY_LEN); k--; }
      rts[k] = ts; memcpy(recent[k], c.id.pub_key, KEY_LEN);
    }
    // the favourites, then the recent ones (favourites give way only past FRIENDS_MAX in all)
    int keep_f = nf < FRIENDS_MAX - nr ? nf : FRIENDS_MAX - nr;
    for (int i = 0; i < keep_f; i++) memcpy(_keys[_n++], favs[i], KEY_LEN);
    for (int i = 0; i < nr && _n < FRIENDS_MAX; i++) memcpy(_keys[_n++], recent[i], KEY_LEN);
  }
  bool scrolls() const { return !_detail_on && maxScroll() > 0; }
  DragScroll& drag() { _ds.max = maxScroll(); return _ds; }
  // One card per node (from here, or from the Nodes page): its Starlit, name, kind, when it was
  // last heard and how far, its path-hash size, and its battery on request (the Battery button);
  // a repeater or room server's also has Ping (a trace there and back: ui_ping.inc).
  void openCard(const ContactInfo& c, bool only) {  // only: opened by itself (back closes it)
    memcpy(_card_key, c.id.pub_key, PUB_KEY_SIZE);
    _detail_on = true;
    _card_only = only;
    _ping_view = false;
  }
  bool cardOnly() const { return _card_only; }
  static const int CARD_BTN_Y = 112;
  // a tap: on a friend, its card; on a card: the Battery (or Ping) button asks, anywhere else is
  // back; on the ping view: ping again (returns false when that leaves the screen)
  bool handleTap(int x, int y) {
    if (_detail_on) {
      ContactInfo* c = the_mesh.lookupContactByPubKey(_card_key, PUB_KEY_SIZE);
      if (c && _ping_view) { ping(*c); return true; }
      if (c && y >= CARD_BTN_Y - 2) {
        if (NodePing::pingable(*c) && x >= UI_CX) {
          if (ping(*c)) _ping_view = true;
        } else if (!my_nodes.ask(*c)) {
          _task->showAlert(my_nodes.waiting() ? "Still waiting for a reply" : "Wait a few seconds", 1200);
        }
        return true;
      }
      return back();
    }
    if (y < TOP) return true;
    int row = (int)((y - TOP + _ds.pos) / ROW_H), i = row * 2 + (x >= UI_CX ? 1 : 0);
    ContactInfo* c = i >= 0 && i < _n ? the_mesh.lookupContactByPubKey(_keys[i], KEY_LEN) : NULL;
    if (c) openCard(*c, false);
    return true;
  }
  bool ping(const ContactInfo& c) {
    const char* why = "";
    if (node_ping.start(c, &why)) return true;
    _task->showAlert(why, 1400);
    return false;
  }
  bool back() {  // swipe right / the button: out of the ping view, then a card (false: leave the screen)
    if (_detail_on && _ping_view) { _ping_view = false; return true; }
    if (!_detail_on || _card_only) return false;
    _detail_on = false;
    return true;
  }
  int render(DisplayDriver& d) override {
    SF32Display& sd = (SF32Display&)d;
    bool still = _task->lowPower();
    ContactInfo* cp = _detail_on ? the_mesh.lookupContactByPubKey(_card_key, PUB_KEY_SIZE) : NULL;
    if (cp && _ping_view) return renderPing(d, *cp);
    if (cp) {  // a node's card
      const ContactInfo& c = *cp;
      themeHeader(d, "Node", _task->getBattMilliVolts());
      uint32_t seed = PetGame::seedOf(c.id.pub_key);
      uint8_t hs = hash_sizes.get(c.id.pub_key);
      pet.draw(d, UI_CX, 47, 0.78f, still, seed, -1, hs);
      char name[sizeof(c.name)], tmp[48], heard[16];
      d.translateUTF8ToBlocks(name, c.name, sizeof(name));
      d.setTextSize(1);
      d.setColor(UIColor::primary_txt);
      if (d.getTextWidth(name) > UI_W - 8) d.drawTextEllipsized(4, 72, UI_W - 8, name); else d.drawTextCentered(UI_CX, 72, name);
      d.setColor(UIColor::secondary_txt);
      sprintf(tmp, "%s Starlit%s", PetGame::kindName((seed >> 3) & 3), (c.flags & 1) ? "  -  favourite" : "");
      d.drawTextCentered(UI_CX, 82, tmp);
      uint32_t now = rtc_clock.getCurrentTime();
      if (c.lastmod && now >= c.lastmod) formatAge(heard, now - c.lastmod); else strcpy(heard, "-");
      int hops = c.out_path_len & 63;
      if (c.out_path_len == OUT_PATH_UNKNOWN) sprintf(tmp, "heard %s  -  flood", heard);
      else if (hops == 0) sprintf(tmp, "heard %s  -  direct", heard);
      else sprintf(tmp, "heard %s  -  %d hop%s", heard, hops, hops == 1 ? "" : "s");
      d.drawTextCentered(UI_CX, 92, tmp);
      if (hs) sprintf(tmp, "%d-byte path hashes%s", hs, hs == 1 && hash_sizes.nudged(c.id.pub_key) ? ", nudged" : "");
      else strcpy(tmp, "path hash size not known yet");
      d.setColor(hs >= 2 ? THEME_ACCENT : (hs == 1 ? UIColor::warning_txt : THEME_DIM));
      d.drawTextCentered(UI_CX, 102, tmp);
      // the Battery button: asks it (once, when tapped); shows the answer
      NodeReading* r = my_nodes.reading(c.id.pub_key, false);
      if (NodePing::pingable(c)) {  // a repeater / room server: Battery and Ping, side by side
        ColorVal bc2 = UIColor::primary_txt;
        if (r && r->state == 1) strcpy(tmp, "asking...");
        else if (r && r->state == 3) { strcpy(tmp, "no reply"); bc2 = UIColor::warning_txt; }
        else if (r && r->state == 2 && r->mv) {
          int pct = sf32BatteryPercent(r->mv);
          sprintf(tmp, "%d.%02dV %d%%", r->mv / 1000, (r->mv % 1000) / 10, pct);
          bc2 = pct <= 15 ? UIColor::warning_txt : THEME_ACCENT;
        } else if (r && r->state == 2) strcpy(tmp, "private");
        else strcpy(tmp, "Battery");
        themeCard(d, 4, CARD_BTN_Y, UI_HALF_W, 14);
        d.setColor(bc2);
        if (d.getTextWidth(tmp) > UI_HALF_W - 4) d.drawTextEllipsized(6, CARD_BTN_Y + 3, UI_HALF_W - 4, tmp); else d.drawTextCentered(4 + UI_HALF_W / 2, CARD_BTN_Y + 3, tmp);
        themeCard(d, UI_C2, CARD_BTN_Y, UI_HALF_W, 14);
        uint8_t rt[PING_MAX_HOPS * 2]; int hops; uint8_t h;
        d.setColor(NodePing::route(c, rt, hops, h) ? UIColor::primary_txt : THEME_DIM);
        d.drawTextCentered(UI_C2 + UI_HALF_W / 2, CARD_BTN_Y + 3, "Ping");
        if (my_nodes.expire()) {}
        return still ? 60000 : 80;
      }
      ColorVal bc = UIColor::primary_txt;
      if (r && r->state == 1) strcpy(tmp, "Battery: asking...");
      else if (r && r->state == 3) { strcpy(tmp, "Battery: no reply (tap to retry)"); bc = UIColor::warning_txt; }
      else if (r && r->state == 2 && r->mv) {
        int pct = sf32BatteryPercent(r->mv);
        char age[12] = "";
        if (r->at && now >= r->at) formatAge(age, now - r->at);
        sprintf(tmp, "Battery %d.%02dV %d%%  %s", r->mv / 1000, (r->mv % 1000) / 10, pct, age);
        bc = pct <= 15 ? UIColor::warning_txt : THEME_ACCENT;
      } else if (r && r->state == 2) strcpy(tmp, "Battery: not shared");
      else strcpy(tmp, "Battery: tap to ask");
      themeCard(d, 4, CARD_BTN_Y, UI_W - 8, 14);
      d.setColor(bc);
      if (d.getTextWidth(tmp) > 114) d.drawTextEllipsized(7, CARD_BTN_Y + 3, 114, tmp); else d.drawTextCentered(UI_CX, CARD_BTN_Y + 3, tmp);
      if (my_nodes.expire()) {}  // (a request that timed out shows as "no reply")
      return still ? 60000 : 80;
    }
    ContactInfo c;
    _detail_on = false;
    themeHeader(d, "Friends", _task->getBattMilliVolts());
    d.setTextSize(1);
    if (_n == 0) {
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(UI_CX, 50, "No friends yet: the Starlits of");
      d.drawTextCentered(UI_CX, 62, "your favourite nodes and the");
      d.drawTextCentered(UI_CX, 72, "ones heard lately show up here");
      return 5000;
    }
    _ds.max = maxScroll();
    bool moving = _ds.step();
    // a still gallery, drawn once into the layer (tap one to see it move); the layer slides when scrolled
    if (!_layer_ok || sd.layerGen() != _lgen) {
      sd.beginLayer(rowsH() + 4);
      for (int i = 0; i < _n; i++) drawFriend(d, i, (i / 2) * ROW_H + 24, true);
      sd.endLayer();
      sd.layerSpans();
      _lgen = sd.layerGen();
      _layer_ok = true;
    } else if (moving || _ds.active) {
      sd.setFlushBand(TOP, 128 - TOP);  // only the grid moves
    }
    sd.composeSprites(0, TOP, UI_RX(124), 128 - TOP, (int)lroundf(_ds.pos * SF32Display::SCALE));
    scrollBar(d);
    return moving || _ds.active ? 16 : 60000;  // still: nothing to redraw
  }
  // the ping view: round trip and each link's SNR (you > first hop > ... > it > ... > you)
  void pingLabel(DisplayDriver& d, int k, const ContactInfo& c, char* out) {
    if (k < 0 || k >= node_ping.n) { strcpy(out, "you"); return; }
    if (k == node_ping.n / 2) {  // it
      char nm[sizeof(c.name)];
      d.translateUTF8ToBlocks(nm, c.name, sizeof(nm));
      snprintf(out, 9, "%s", nm);
      return;
    }
    const uint8_t* p = &node_ping.path[k * node_ping.hs];
    if (node_ping.hs == 2) sprintf(out, "%02x%02x", p[0], p[1]); else sprintf(out, "%02x", p[0]);
  }
  void pingRow(DisplayDriver& d, int r, int y, const ContactInfo& c) {
    char a[12], b[12], tmp[32];
    pingLabel(d, r - 1, c, a);
    pingLabel(d, r, c, b);
    snprintf(tmp, sizeof(tmp), "%s > %s", a, b);
    d.setColor(UIColor::secondary_txt);
    d.drawTextEllipsized(6, y, UI_W - 52, tmp);
    float s = node_ping.snr[r] / 4.0f;
    sprintf(tmp, "%.1f dB", s);
    d.setColor(s >= 0 ? THEME_GOOD : (s >= -7 ? UIColor::primary_txt : UIColor::warning_txt));
    d.drawTextRightAlign(UI_RX(122), y, tmp);
  }
  int renderPing(DisplayDriver& d, const ContactInfo& c) {
    themeHeader(d, "Ping", _task->getBattMilliVolts());
    char name[sizeof(c.name)], tmp[40];
    d.translateUTF8ToBlocks(name, c.name, sizeof(name));
    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    if (d.getTextWidth(name) > UI_W - 8) d.drawTextEllipsized(4, 26, UI_W - 8, name); else d.drawTextCentered(UI_CX, 26, name);
    if (node_ping.expire()) _task->showAlert("No reply", 1200);
    if (!node_ping.isFor(c.id.pub_key) || node_ping.state == NodePing::WAITING) {
      d.setColor(UIColor::secondary_txt);
      sprintf(tmp, "Pinging...  %lu s", node_ping.waited() / 1000);
      d.drawTextCentered(UI_CX, 56, tmp);
      int hops = node_ping.n / 2;
      if (hops == 0) strcpy(tmp, "direct");
      else sprintf(tmp, "via %d repeater%s", hops, hops == 1 ? "" : "s");
      d.setColor(THEME_DIM);
      d.drawTextCentered(UI_CX, 70, tmp);
      return 250;
    }
    if (node_ping.state == NodePing::NO_REPLY) {
      d.setColor(UIColor::warning_txt);
      d.drawTextCentered(UI_CX, 54, "No reply");
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(UI_CX, 68, "out of range, busy, or the");
      d.drawTextCentered(UI_CX, 78, "route has changed");
      d.setColor(THEME_DIM);
      d.drawTextCentered(UI_CX, 112, "tap to ping again");
      return 1000;
    }
    // done: the round trip, then each link
    d.setTextSize(2);
    d.setColor(THEME_ACCENT);
    sprintf(tmp, "%lu.%02lu s", (unsigned long)(node_ping.rtt_ms / 1000), (unsigned long)(node_ping.rtt_ms % 1000) / 10);
    d.drawTextCentered(UI_CX, 38, tmp);
    d.setTextSize(1);
    int rows = node_ping.n + 1, y = 60;
    if (rows <= 6) {
      for (int r = 0; r < rows; r++, y += 10) pingRow(d, r, y, c);
    } else {  // a long route: the first three links and the last two
      for (int r = 0; r < 3; r++, y += 10) pingRow(d, r, y, c);
      d.setColor(THEME_DIM);
      d.drawTextCentered(UI_CX, y, "...");
      y += 10;
      for (int r = rows - 2; r < rows; r++, y += 10) pingRow(d, r, y, c);
    }
    if (rows <= 5) {
      d.setColor(THEME_DIM);
      d.drawTextCentered(UI_CX, 118, "tap to ping again");
    }
    return 60000;
  }
  void drawFriend(DisplayDriver& d, int i, float y, bool still) {
    ContactInfo* cp = the_mesh.lookupContactByPubKey(_keys[i], KEY_LEN);  // (by key: a list that changes can't shift it)
    if (!cp) return;
    const ContactInfo& c = *cp;
    float x = UI_CX + (i % 2 ? 1 : -1) * (30 + (UI_W - 128) / 4);  // (the SF32: 34, 94)
    pet.draw(d, x, y, 0.6f, still, PetGame::seedOf(c.id.pub_key), -1, hash_sizes.get(c.id.pub_key));
    char name[sizeof(c.name)];
    d.translateUTF8ToBlocks(name, c.name, sizeof(name));
    d.setTextSize(1);
    d.setColor((c.flags & 1) ? UIColor::primary_txt : UIColor::secondary_txt);  // favourites a little brighter
    if (d.getTextWidth(name) > 58) d.drawTextEllipsized((int)x - 29, (int)y + 16, 58, name);
    else d.drawTextCentered((int)x, (int)y + 16, name);
    if (hash_sizes.nudged(c.id.pub_key) && hash_sizes.get(c.id.pub_key) == 1) {  // the bot nudged it: a small amber bubble
      SF32Display& sd = (SF32Display&)d;
      float bx = x + 15, by = y - 13;
      sd.fillShaded(bx, by, 3.6f, 2.8f, UIColor::warning_txt, 0.2f);
      d.setColor(UIColor::warning_txt);
      sd.drawLineAA(bx - 1.6f, by + 2.2f, bx - 2.8f, by + 4.2f, 1.1f);  // its tail
    }
  }
  void scrollBar(DisplayDriver& d) {  // as in the menus
    if (maxScroll() <= 0) return;
    int track = 128 - TOP - 4, thumb = track * (128 - TOP) / (rowsH()), ty = TOP + (int)((track - thumb) * (_ds.pos / maxScroll()));
    d.setColor(THEME_DIM);
    d.fillRect(UI_RX(125), TOP, 2, track);
    d.setColor(THEME_ACCENT);
    d.fillRect(UI_RX(125), ty, 2, thumb);
  }
  bool handleInput(char c) override {  // the button: back (out of meeting one first)
    if (c == 0) return false;
    if (!back()) _task->closeFriends();
    return true;
  }
private:
  static const int FRIENDS_MAX = 24, RECENT = 10, TOP = 24, ROW_H = 58;  // (24: 12 rows, what SF32Display's list layer holds)
  UITask* _task;
  static const int KEY_LEN = 8;  // a public key prefix: plenty to tell contacts apart
  uint8_t _keys[FRIENDS_MAX][KEY_LEN];
  int _n = 0, _page = 0, _detail = 0;
  bool _detail_on = false, _layer_ok = false, _card_only = false, _ping_view = false;
  uint8_t _card_key[PUB_KEY_SIZE] = {};
  uint32_t _lgen = 0;
  DragScroll _ds;
  int rowsH() const { return ((_n + 1) / 2) * ROW_H; }
  int maxScroll() const { int m = rowsH() - (128 - TOP); return m > 0 ? m : 0; }
};

void UITask::openFriends() {
  if (!friends_screen) friends_screen = new FriendsScreen(this);
  ((FriendsScreen*)friends_screen)->reset();
  _menu_return = NULL;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(friends_screen);
}
void UITask::openNodeCard(const ContactInfo& c) {  // a node's card by itself (from the Nodes page)
  if (!friends_screen) friends_screen = new FriendsScreen(this);
  ((FriendsScreen*)friends_screen)->reset();
  ((FriendsScreen*)friends_screen)->openCard(c, true);
  _menu_return = NULL;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  setCurrScreen(friends_screen);
}
void UITask::closeFriends() {
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  setCurrScreen(home);
}
