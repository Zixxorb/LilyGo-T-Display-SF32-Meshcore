// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Today's finds: what the radio found today (local calendar day), for the Starlit page's
// bottom line (and what the Signal page's finder and tester found). Counted in RAM from the mesh hooks (nothing else while the screen is off); kept
// in a tiny file, saved with the pet at screen-off, so a reboot doesn't lose the day.
#define FINDS_FILE  "/sf32_finds"
#define FINDS_MAGIC 0x33444E46  // "FND3"
static uint32_t findsToday();  // UITask.cpp: the local day number (uptime days while the time isn't known)
class TodayFinds {
public:
  void begin() { load(); }  // at boot (not lazily from a mesh hook while the screen is off)
  void packets(uint32_t n) { if (n) { roll(); _f.packets += n; _dirty = true; } }
  void advert(bool is_new) { roll(); if (_f.adverts < 60000) _f.adverts++; if (is_new && _f.new_nodes < 60000) _f.new_nodes++; _dirty = true; }
  void hops(uint8_t h, const char* name) {  // a flood packet's hop count, and who sent it if known
    roll();
    if (h <= _f.far_hops) return;
    _f.far_hops = h;
    StrHelper::strncpy(_f.far_name, name ? name : "", sizeof(_f.far_name));
    _dirty = true;
  }
  // the signal tools (Signal page): the finder's strongest reply today, the tester's best link
  void finder(const char* name, int rssi) {
    roll();
    if (_f.find_rssi && rssi <= _f.find_rssi) return;
    _f.find_rssi = (int8_t)(rssi < -127 ? -127 : rssi);
    StrHelper::strncpy(_f.find_name, name, sizeof(_f.find_name));
    _dirty = true;
  }
  void link(const char* name, float db) {
    roll();
    int q = (int)lroundf(db * 4);
    if (_f.link_set && q <= _f.link_q4) return;
    _f.link_set = 1;
    _f.link_q4 = (int16_t)q;
    StrHelper::strncpy(_f.link_name, name, sizeof(_f.link_name));
    _dirty = true;
  }
  void upgraded(const char* name) {  // a node the bot nudged moved up from 1-byte path hashes
    roll();
    StrHelper::strncpy(_f.up_name, name, sizeof(_f.up_name));
    _dirty = true;
  }
  // its k-th find as a line (k from 0); false: no more
  bool line(int k, char* out, int n) {
    roll();
    char tmp[64];
    int i = 0;
    for (int which = 0; which < 7; which++) {
      tmp[0] = 0;
      if (which == 0 && _f.new_nodes) snprintf(tmp, sizeof(tmp), "today: met %u new node%s", _f.new_nodes, _f.new_nodes == 1 ? "" : "s");
      if (which == 1 && _f.far_hops) {
        // the hops first: a long name is what gets shortened
        if (_f.far_name[0]) snprintf(tmp, sizeof(tmp), "farthest: %u hop%s, %s", _f.far_hops, _f.far_hops == 1 ? "" : "s", _f.far_name);
        else snprintf(tmp, sizeof(tmp), "farthest today: %u hop%s", _f.far_hops, _f.far_hops == 1 ? "" : "s");
      }
      if (which == 2 && _f.adverts) snprintf(tmp, sizeof(tmp), "today: heard %u advert%s", _f.adverts, _f.adverts == 1 ? "" : "s");
      if (which == 3 && _f.packets) snprintf(tmp, sizeof(tmp), "today: %lu packets heard", (unsigned long)_f.packets);
      if (which == 4 && _f.find_rssi) snprintf(tmp, sizeof(tmp), "found: %d dBm, %s", _f.find_rssi, _f.find_name);
      if (which == 6 && _f.up_name[0]) snprintf(tmp, sizeof(tmp), "upgraded to 2-byte: %s", _f.up_name);
      if (which == 5 && _f.link_set) snprintf(tmp, sizeof(tmp), "best link: %+d dB, %s", (int)lroundf(_f.link_q4 / 4.0f), _f.link_name);
      if (tmp[0] && i++ == k) { StrHelper::strncpy(out, tmp, n); return true; }
    }
    return false;
  }
  void save() {
    if (!_dirty) return;
    File f = InternalFS.open(FINDS_FILE, "w", true);
    if (f) f.write((const uint8_t*)&_f, sizeof(_f));
    _dirty = false;
  }
private:
  struct F { uint32_t magic, day, packets; uint16_t adverts, new_nodes; uint8_t far_hops, link_set; int8_t find_rssi; uint8_t pad; int16_t link_q4, pad2;
             char far_name[24], find_name[24], link_name[24], up_name[24]; };
  F _f = {};
  bool _loaded = false, _dirty = false;
  // a new day (or the first use): start over
  void roll() {
    load();
    uint32_t d = findsToday();
    if (d == _f.day) return;
    memset(&_f, 0, sizeof(_f));
    _f.magic = FINDS_MAGIC;
    _f.day = d;
    _dirty = true;
  }
  void load() {
    if (_loaded) return;
    _loaded = true;
    _f.magic = FINDS_MAGIC;  // (no file yet: a fresh record; roll() gives it today)
    File f = InternalFS.open(FINDS_FILE, "r");
    F g;
    if (f && f.read((uint8_t*)&g, sizeof(g)) == (int)sizeof(g) && g.magic == FINDS_MAGIC) {
      g.far_name[sizeof(g.far_name) - 1] = g.find_name[sizeof(g.find_name) - 1] = g.link_name[sizeof(g.link_name) - 1] = g.up_name[sizeof(g.up_name) - 1] = 0;
      _f = g;
    }
  }
};
static TodayFinds today_finds;
