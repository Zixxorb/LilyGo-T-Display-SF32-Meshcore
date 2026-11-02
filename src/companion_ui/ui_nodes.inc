// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// My Nodes (the Nodes page): the contacts starred as favourites in the app, with when each
// was last heard (its last advert), how far away (hops) and, only when asked (tap its row),
// its battery: one telemetry request to that node, nothing on a schedule, so the mesh
// isn't loaded with polling. The node answers only if it shares telemetry with us (the
// node's own telemetry setting in the app; repeaters usually need a login for it).
//
// The request goes out with MeshCore's own sendRequest(); the reply carries a tag the
// companion code doesn't know, so it comes to us through onUnhandledResponse() (and
// isn't pushed to the phone).

#define NODES_MAX        4       // rows on the page
#define NODES_ASK_GAP_MS 20000UL // at most one request this often
#define NODES_REPLY_MS   30000UL // no answer within this (or the mesh's estimate): "no reply"

struct NodeReading {
  uint8_t key[6];            // pub key prefix
  uint8_t state;             // 0 not asked, 1 asking, 2 answered, 3 no reply
  uint16_t mv;               // battery (0 unknown)
  int16_t temp10;            // temperature, 0.1 C (INT16_MIN unknown)
  uint32_t at;               // when it answered (our clock, epoch)
  uint32_t tag;              // the pending request
  unsigned long deadline;    // millis: give up
};

class MyNodes {
  NodeReading _r[NODES_MAX] = {};
  unsigned long _last_ask = 0;

public:
  // the starred contacts, in contact order (up to NODES_MAX); returns how many there are in all
  int favourites(ContactInfo* out, int max) {
    int n = 0, total = 0;
    ContactInfo c;
    // (MeshCore's iterator: getContactByIdx() counts its reserved anonymous slots, getNumContacts() doesn't)
    ContactsIterator it = the_mesh.startContactsIterator();
    while (it.hasNext(&the_mesh, c)) {
      if (!(c.flags & 1)) continue;  // flags bit 0: favourite
      if (n < max) out[n++] = c;
      total++;
    }
    return total;
  }

  NodeReading* reading(const uint8_t* pub_key, bool create) {
    for (auto& r : _r) if (r.state && memcmp(r.key, pub_key, 6) == 0) return &r;
    if (!create) return NULL;
    NodeReading* slot = &_r[0];  // a free slot, else the oldest answer
    for (auto& r : _r) {
      if (!r.state) { slot = &r; break; }
      if (r.state != 1 && r.at < slot->at) slot = &r;
    }
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->key, pub_key, 6);
    slot->temp10 = INT16_MIN;
    return slot;
  }

  // ask one node for its battery; false: too soon after the last one, or it couldn't be sent
  bool ask(const ContactInfo& c) {
    if (_last_ask && millis() - _last_ask < NODES_ASK_GAP_MS) return false;
    uint32_t tag, est;
    if (the_mesh.sendRequest(c, REQ_TYPE_GET_TELEMETRY_DATA, tag, est) == MSG_SEND_FAILED) return false;
    _last_ask = millis();
    NodeReading* r = reading(c.id.pub_key, true);
    r->state = 1;
    r->tag = tag;
    r->deadline = millis() + (est + 5000 > NODES_REPLY_MS ? est + 5000 : NODES_REPLY_MS);
    return true;
  }

  // a reply (onUnhandledResponse): true if it was ours
  bool onResponse(const ContactInfo& from, uint32_t tag, const uint8_t* data, uint8_t len) {
    NodeReading* r = reading(from.id.pub_key, false);
    if (!r || r->state != 1 || r->tag != tag) return false;
    r->state = 2;
    r->at = rtc_clock.getCurrentTime();
    // Cayenne LPP: [channel][type][value, big-endian]...; the node's own readings on channel 1
    int i = 0;
    while (i + 2 <= len) {
      uint8_t ch = data[i], type = data[i + 1];
      int size;
      switch (type) {
        case 0: case 1: case 102: case 104: case 120: case 142: size = 1; break;
        case 2: case 3: case 101: case 103: case 115: case 116: case 117: case 121: case 125: case 128: case 132: size = 2; break;
        case 135: size = 3; break;
        case 100: case 118: case 130: case 131: case 133: size = 4; break;
        case 113: case 134: size = 6; break;
        case 136: size = 9; break;
        default: size = -1;  // unknown: stop reading
      }
      if (size < 0 || i + 2 + size > len) break;
      const uint8_t* v = data + i + 2;
      if (ch == TELEM_CHANNEL_SELF && type == 116 && !r->mv) r->mv = (uint16_t)((v[0] << 8 | v[1]) * 10);  // 0.01 V
      if (ch == TELEM_CHANNEL_SELF && type == 103 && r->temp10 == INT16_MIN) r->temp10 = (int16_t)(v[0] << 8 | v[1]);
      i += 2 + size;
    }
    return true;
  }

  // requests that got no answer in time
  bool expire() {
    bool changed = false;
    for (auto& r : _r)
      if (r.state == 1 && (long)(millis() - r.deadline) >= 0) { r.state = 3; changed = true; }
    return changed;
  }
  bool waiting() const {
    for (auto& r : _r) if (r.state == 1) return true;
    return false;
  }
};

static MyNodes my_nodes;
