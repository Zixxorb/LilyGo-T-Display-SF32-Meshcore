// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Ping a repeater or room server (node card > Ping): a MeshCore trace sent out along the route
// to it and back the same way. Each repeater on the way adds the signal (SNR) it heard the
// trace at, so the card shows the round trip time and every link's SNR, both directions.
// The route: the contact's stored path if it has one, else the path its latest advert came
// in by (MeshCore keeps the last 16), reversed. Only repeaters and room servers pass a trace
// on, so chat nodes can't be pinged. Nothing is sent until Ping is tapped.

#define PING_MAX_HOPS   15   // repeaters between us and it (the trace goes out and back: 2n+1)
#define PING_MAX_PATH   (2 * PING_MAX_HOPS + 1)
#define PING_GAP_MS     3000UL

class NodePing {
public:
  enum { IDLE, WAITING, DONE, NO_REPLY };
  uint8_t state = IDLE;
  uint8_t key[6] = {};
  uint8_t hs = 1;                        // bytes per hop hash in the trace
  uint8_t n = 0;                         // hops in the trace (2 * route + 1)
  uint8_t path[PING_MAX_PATH * 2] = {};  // the hashes, out and back
  int8_t snr[PING_MAX_PATH + 1] = {};    // SNR x4 at each hop; the last: back to us
  uint32_t rtt_ms = 0;

  bool isFor(const uint8_t* pub_key) const { return state != IDLE && memcmp(key, pub_key, 6) == 0; }
  static bool pingable(const ContactInfo& c) { return c.type == ADV_TYPE_REPEATER || c.type == ADV_TYPE_ROOM; }

  // The route to it, nearest hop first (false: none known). hs_out: bytes per hash used.
  static bool route(const ContactInfo& c, uint8_t* out, int& hops, uint8_t& hs_out) {
    const uint8_t* src = NULL;
    uint8_t plen = 0;
    bool reverse = false;
    if (c.out_path_len != OUT_PATH_UNKNOWN) {
      src = c.out_path;
      plen = c.out_path_len;
    } else {
      static AdvertPath recent[16];  // (static: ~1.7 KB, off the UI stack)
      int nr = the_mesh.getRecentlyHeard(recent, 16);
      for (int i = 0; i < nr; i++) {
        if (recent[i].recv_timestamp && memcmp(recent[i].pubkey_prefix, c.id.pub_key, sizeof(recent[i].pubkey_prefix)) == 0) {
          static uint8_t adv[MAX_PATH_SIZE];
          memcpy(adv, recent[i].path, sizeof(adv));
          src = adv;
          plen = recent[i].path_len;
          reverse = true;  // an advert's path runs from it to us
          break;
        }
      }
      if (!src) return false;
    }
    hops = plen & 63;
    uint8_t chs = (plen >> 6) + 1;  // the stored hash size (1-3 bytes)
    if (hops > PING_MAX_HOPS) return false;
    hs_out = chs >= 2 ? 2 : 1;  // a trace takes 1, 2, 4 or 8 bytes; a prefix of each is enough
    for (int i = 0; i < hops; i++) {
      int from = reverse ? hops - 1 - i : i;
      memcpy(&out[i * hs_out], &src[from * chs], hs_out);
    }
    return true;
  }

  // false: too soon, still waiting, no route, or it couldn't be sent (why: in *why)
  bool start(const ContactInfo& c, const char** why) {
    if (state == WAITING) { *why = "Still waiting for it"; return false; }
    if (_last && millis() - _last < PING_GAP_MS) { *why = "Wait a few seconds"; return false; }
    uint8_t r[PING_MAX_HOPS * 2];
    int hops;
    uint8_t h;
    if (!route(c, r, hops, h)) { *why = "No route to it yet"; return false; }
    // out along the route, it, then back the same way
    int k = 0;
    for (int i = 0; i < hops; i++) memcpy(&path[(k++) * h], &r[i * h], h);
    memcpy(&path[(k++) * h], c.id.pub_key, h);
    for (int i = hops - 1; i >= 0; i--) memcpy(&path[(k++) * h], &r[i * h], h);
    uint32_t t;
    the_mesh.getRNG()->random((uint8_t*)&t, sizeof(t));
    mesh::Packet* pkt = the_mesh.createTrace(t, 0, h == 2 ? 1 : 0);  // flags: hash size as 1 << n
    if (!pkt) { *why = "Couldn't send (busy)"; return false; }
    the_mesh.sendDirect(pkt, path, k * h);
    memcpy(key, c.id.pub_key, 6);
    hs = h;
    n = k;
    tag = t;
    state = WAITING;
    rtt_ms = 0;
    _last = _sent = millis();
    unsigned long wait = 4000UL + 1500UL * k;
    _deadline = _sent + (wait > 30000UL ? 30000UL : wait);
    return true;
  }

  // the trace came back (onTraceRecv): true if it was ours
  bool onTrace(uint32_t t, uint8_t flags, const uint8_t* snrs, uint8_t path_len, float final_snr) {
    if (state != WAITING && state != NO_REPLY) return false;  // (late, after "no reply": still shown)
    if (t != tag) return false;
    int count = path_len >> (flags & 3);
    if (count > PING_MAX_PATH) count = PING_MAX_PATH;
    memcpy(snr, snrs, count);
    n = count;
    snr[count] = (int8_t)(final_snr * 4);
    rtt_ms = millis() - _sent;
    state = DONE;
    return true;
  }
  bool expire() {  // true when it just ran out
    if (state != WAITING || (long)(millis() - _deadline) < 0) return false;
    state = NO_REPLY;
    return true;
  }
  unsigned long waited() const { return millis() - _sent; }

private:
  uint32_t tag = 0;
  unsigned long _sent = 0, _last = 0, _deadline = 0;
};
static NodePing node_ping;

void UITask::onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
  if (node_ping.onTrace(tag, flags, path_snrs, path_len, pkt->getSNR())) _next_refresh = 0;
}
