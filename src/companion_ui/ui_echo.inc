// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Heard repeats, and resending (Settings > Resend): every channel message this board sends
// (from the phone app, the bot or SOS) is remembered by its packet hash. When a repeater
// passes it on, the board hears the same packet again with a longer path: a "repeat", the
// proof it got out. With Resend on, a message with no repeat heard within a few seconds is
// sent again, byte for byte: repeaters that already passed it on recognise it and drop it,
// so only the ones that missed it carry it. The radio driver hands over every packet sent
// and received (SF32SX1262Radio::raw_tap); this runs in the main loop with the mesh.

#define ECHO_N        4      // messages followed at a time (the newest win)
#define ECHO_WAIT_MS  6000   // + 3 x its airtime: how long to listen for a repeat
#define ECHO_KEEP_MS  60000  // repeats still counted (for the result) this long after sending
#define ECHO_TAG_BOT  1

static const uint8_t RESEND_TRIES[] = {0, 1, 2, 3};  // Settings > Resend: Off, 1x, 2x, 3x
#define NUM_RESEND 4

class EchoTracker {
  struct E {
    bool used, waiting_tx, done;
    uint8_t hash[MAX_HASH_SIZE];
    uint8_t len, echoes, tries, tag;
    unsigned long at;  // when it (or its last resend) went out
    uint8_t bytes[MAX_TRANS_UNIT];
  };
  E _e[ECHO_N] = {};
  uint8_t _next_tag = 0;
  struct R { bool ready; uint8_t echoes, tries; } _result[2] = {};  // by tag
  uint32_t _resent = 0, _heard = 0;

  E* find(const uint8_t* h) {
    for (int i = 0; i < ECHO_N; i++) if (_e[i].used && memcmp(_e[i].hash, h, MAX_HASH_SIZE) == 0) return &_e[i];
    return NULL;
  }
  void finish(E& e) {
    e.done = true;
    if (e.tag && e.tag < 2) { _result[e.tag] = {true, e.echoes, e.tries}; }
  }

public:
  // the next channel message sent is this one (the bot's reply)
  void tagNext(uint8_t tag) { _next_tag = tag; if (tag < 2) _result[tag].ready = false; }  // (a fresh tag: no old result)
  bool takeResult(uint8_t tag, uint8_t& echoes, uint8_t& tries) {
    if (tag >= 2 || !_result[tag].ready) return false;
    _result[tag].ready = false;
    echoes = _result[tag].echoes;
    tries = _result[tag].tries;
    return true;
  }
  uint32_t resentCount() const { return _resent; }

  void onRaw(bool tx, const uint8_t* bytes, int len) {
    if (len <= 0 || len > MAX_TRANS_UNIT) return;
    // only channel messages matter: tell from the header byte before decoding the whole packet
    if (((bytes[0] >> PH_TYPE_SHIFT) & PH_TYPE_MASK) != PAYLOAD_TYPE_GRP_TXT) return;
    if (!tx) {  // a repeat only matters while something of ours is being followed
      bool any = false;
      for (int i = 0; i < ECHO_N; i++) any |= _e[i].used;
      if (!any) return;
    }
    mesh::Packet p;
    if (!p.readFrom(bytes, len)) return;
    if (p.getPayloadType() != PAYLOAD_TYPE_GRP_TXT || !p.isRouteFlood()) return;
    uint8_t h[MAX_HASH_SIZE];
    p.calculatePacketHash(h);
    E* e = find(h);
    unsigned long now = millis();
    if (tx) {
      if (p.getPathHashCount() != 0) return;  // passing someone else's on (not ours)
      if (e) { e->waiting_tx = false; e->at = now; return; }  // our resend went out
      E* slot = &_e[0];
      for (int i = 0; i < ECHO_N; i++) {
        if (!_e[i].used) { slot = &_e[i]; break; }
        if (_e[i].at < slot->at) slot = &_e[i];  // else the oldest
      }
      if (slot->used && !slot->done) finish(*slot);
      memset(slot, 0, sizeof(*slot));
      slot->used = true;
      memcpy(slot->hash, h, MAX_HASH_SIZE);
      slot->len = len;
      memcpy(slot->bytes, bytes, len);
      slot->at = now;
      slot->tag = _next_tag;
      _next_tag = 0;
    } else if (e && p.getPathHashCount() >= 1 && now - e->at < ECHO_KEEP_MS) {
      if (e->echoes < 255) e->echoes++;
      _heard++;
    }
  }

  void loop(uint8_t resend_max) {
    unsigned long now = millis();
    for (int i = 0; i < ECHO_N; i++) {
      E& e = _e[i];
      if (!e.used || e.done) continue;
      if (e.waiting_tx) {  // queued resend: give up if it never went out
        if (now - e.at > 30000) finish(e);
        continue;
      }
      if (now - e.at < ECHO_WAIT_MS + 3 * radio_driver.getEstAirtimeFor(e.len)) continue;
      if (e.echoes == 0 && e.tries < resend_max) {
        mesh::Packet* pkt = the_mesh.obtainNewPacket();
        if (pkt == NULL) continue;  // the pool's full: next time round
        if (!pkt->readFrom(e.bytes, e.len)) { the_mesh.releasePacket(pkt); finish(e); continue; }
        the_mesh.sendPacket(pkt, 1, 0);
        e.tries++;
        e.waiting_tx = true;
        e.at = now;
        _resent++;
      } else {
        finish(e);
      }
    }
  }
};
static EchoTracker echo_tracker;
static void echoRawTap(bool tx, const uint8_t* bytes, int len) { echo_tracker.onRaw(tx, bytes, len); }
