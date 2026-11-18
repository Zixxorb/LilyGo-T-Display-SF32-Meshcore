// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Starlits meeting: when two Vesper boards are near each other, their Starlits greet.
//
// Opening the Starlit page sends one small "hello" (at most once a minute): a MeshCore raw
// custom packet sent zero-hop, so only boards that hear it directly get it. Repeaters never
// pass these on (MeshCore delivers raw packets locally only), and other firmware ignores them
// (a phone app may receive it as raw data and do nothing). A Vesper board that hears a hello
// with a strong signal (about the same room) answers once, so both see each other.
//
// A meeting: the other Starlit comes to visit on the Starlit page (beside yours, hearts), both
// get happier and more curious (once an hour per friend), and it goes into today's finds and
// the log. With the screen off, the visit waits for the next time the page is opened.
//
// The hello (32 bytes): "STL1", type (1 hello, 2 answer), the sender's key prefix (6), its
// Starlit's seed (4, the same FNV-1a of its key that Friends uses), its mood, its name (16).

#define MEET_TYPE_HELLO   1
#define MEET_TYPE_ANSWER  2
#define MEET_NEAR_RSSI    (-75)       // dBm: a signal this strong is "near" (same room, a few metres)
#define MEET_HELLO_GAP_MS 60000UL     // at most one hello a minute (opening the page)
#define MEET_ANSWER_GAP_MS 20000UL    // and one answer every 20 s
#define MEET_SHOW_MS      20000UL     // the visit on the page
#define MEET_WAIT_MS      (30UL * 60 * 1000)  // a visit not seen yet (screen off) waits this long
#define MEET_REWARD_MS    (60UL * 60 * 1000)  // happiness from the same friend once an hour

struct __attribute__((packed)) MeetMsg {
  char magic[4];
  uint8_t type;
  uint8_t key[6];
  uint32_t seed;
  uint8_t mood;
  char name[16];
  uint32_t nonce;  // different every time: MeshCore drops a packet it has seen (the same bytes) for hours
};
#define MEET_MIN_LEN 32  // (older hellos had no nonce)

class StarlitMeet {
  struct Visit { bool on; bool shown; unsigned long at, shown_at; uint32_t seed; int8_t rssi; char name[16]; };
  struct Seen { uint8_t key[6]; unsigned long at; };
  Visit _v = {};
  Seen _seen[8] = {};
  unsigned long _last_hello = 0, _last_answer = 0;
  uint32_t _met = 0;

  bool send(uint8_t type, uint32_t delay_ms) {
    MeetMsg m = {};
    memcpy(m.magic, "STL1", 4);
    m.type = type;
    memcpy(m.key, the_mesh.self_id.pub_key, 6);
    m.seed = pet.stats().seed;
    m.mood = (uint8_t)pet.mood();
    StrHelper::strncpy(m.name, the_mesh.getNodeName(), sizeof(m.name));
    m.nonce = the_mesh.getRNG()->nextInt(1, 0x7FFFFFFF) ^ millis();
    mesh::Packet* pkt = the_mesh.createRawData((const uint8_t*)&m, sizeof(m));
    if (!pkt) return false;
    the_mesh.sendZeroHop(pkt, delay_ms);
    return true;
  }
  // once an hour per friend: true if this meeting counts
  bool reward(const uint8_t* key) {
    unsigned long now = millis();
    int slot = 0;
    for (int i = 0; i < 8; i++) {
      if (_seen[i].at && memcmp(_seen[i].key, key, 6) == 0) {
        if (now - _seen[i].at < MEET_REWARD_MS) return false;
        slot = i;
        break;
      }
      if (!_seen[i].at || _seen[i].at < _seen[slot].at) slot = i;
    }
    memcpy(_seen[slot].key, key, 6);
    _seen[slot].at = now | 1;
    return true;
  }

public:
  // the Starlit page was opened: say hello (not in low power mode; at most once a minute)
  void hello() {
    if (_last_hello && millis() - _last_hello < MEET_HELLO_GAP_MS) return;
    if (send(MEET_TYPE_HELLO, 0)) _last_hello = millis() | 1;
  }
  // a raw packet from the mesh (UITask::onRawDataRecv): ours if it starts "STL1".
  // Returns a log line when a meeting counts (else NULL).
  const char* onRaw(const uint8_t* data, int len, int rssi, bool answer_allowed) {
    if (len < MEET_MIN_LEN) return NULL;
    MeetMsg m = {};
    memcpy(&m, data, len < (int)sizeof(m) ? len : sizeof(m));
    if (memcmp(m.magic, "STL1", 4) != 0 || (m.type != MEET_TYPE_HELLO && m.type != MEET_TYPE_ANSWER)) return NULL;
    if (memcmp(m.key, the_mesh.self_id.pub_key, 6) == 0) return NULL;  // our own, heard back
    if (rssi < MEET_NEAR_RSSI) return NULL;  // too far to count as meeting
    m.name[sizeof(m.name) - 1] = 0;
    unsigned long now = millis();
    if (m.type == MEET_TYPE_HELLO && answer_allowed && (!_last_answer || now - _last_answer >= MEET_ANSWER_GAP_MS)) {
      if (send(MEET_TYPE_ANSWER, 400 + (now & 1023))) _last_answer = now | 1;  // (a short random wait: not on top of theirs)
    }
    _v.on = true;
    _v.shown = false;
    _v.at = now;
    _v.seed = m.seed ? m.seed : 1;
    _v.rssi = (int8_t)(rssi < -127 ? -127 : rssi);
    StrHelper::strncpy(_v.name, m.name[0] ? m.name : "a Starlit", sizeof(_v.name));
    if (!reward(m.key)) return NULL;
    _met++;
    pet.meet();
    today_finds.met(_v.name);
    static char line[48];
    snprintf(line, sizeof(line), "Met %s's Starlit (%d dBm)", _v.name, rssi);
    return line;
  }
  // the visit to show on the page now (false: none): starts when first drawn
  bool visiting() {
    if (!_v.on) return false;
    unsigned long now = millis();
    if (!_v.shown) {
      if (now - _v.at > MEET_WAIT_MS) { _v.on = false; return false; }
      _v.shown = true;
      _v.shown_at = now;
      pet.greet();  // a happy wiggle as it arrives
    }
    if (now - _v.shown_at > MEET_SHOW_MS) { _v.on = false; return false; }
    return true;
  }
  unsigned long visitAge() const { return millis() - _v.shown_at; }
  uint32_t visitSeed() const { return _v.seed; }
  const char* visitName() const { return _v.name; }
};
static StarlitMeet starlit_meet;

#ifdef SF32_REMOTE_TOUCH
// debug builds: write a seed here (tools/sf32_debug_uart.py mem_write32) to fake a nearby
// Starlit's hello, so a meeting can be tried with one board
volatile uint32_t sf32_fake_meet = 0;
volatile uint32_t sf32_fake_msg = 0;  // non-zero: a pretend direct message (UITask::loop)
#endif
