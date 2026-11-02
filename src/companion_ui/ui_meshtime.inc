// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Time from the mesh: while the clock isn't known (after the battery ran flat, the
// battery switch, a fresh flash), use the times in received adverts. Each advert carries
// its node's clock (signed, so it can't be changed on the way). Only nodes already in the
// contacts count (new identities are free to make), and a node's clock can be wrong,
// so it takes at least 3 different ones agreeing within 5 minutes; their middle
// value, less 2 minutes, sets the clock. Behind rather than ahead because MeshCore only
// lets the phone move the clock forward, so the next phone sync can always correct it.
// Settings > Clock > Mesh time turns it off; Settings > Clock > Reset clock is the way
// back from any wrong time (MeshCore only moves the clock forward otherwise).

#define MESH_TIME_SAMPLES    8
#define MESH_TIME_NEEDED     3
#define MESH_TIME_SPREAD_S   (5 * 60)         // agreeing = within this of each other
#define MESH_TIME_MAX_AGE_MS (30UL * 60 * 1000) // forget samples older than this
#define MESH_TIME_MARGIN_S   120              // set this much behind the agreed time
#define MESH_TIME_MIN        1735689600UL      // 2025-01-01: older = a node that doesn't know

class MeshTimeCollector {
  struct Sample { uint8_t key[4]; uint32_t ts; unsigned long at; };
  Sample _s[MESH_TIME_SAMPLES];
  int _n = 0;

  static int cmp(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : x > y;
  }

public:
  // an advert from `pub_key` stamped `ts`; returns a time to set (0: not yet)
  uint32_t add(const uint8_t* pub_key, uint32_t ts) {
    if (ts < MESH_TIME_MIN) return 0;
    unsigned long now = millis();
    // drop old samples, and this node's earlier one
    int j = 0;
    for (int i = 0; i < _n; i++) {
      if (now - _s[i].at > MESH_TIME_MAX_AGE_MS || memcmp(_s[i].key, pub_key, 4) == 0) continue;
      _s[j++] = _s[i];
    }
    _n = j;
    if (_n == MESH_TIME_SAMPLES) {  // full: replace the oldest
      memmove(_s, _s + 1, sizeof(Sample) * (MESH_TIME_SAMPLES - 1));
      _n--;
    }
    memcpy(_s[_n].key, pub_key, 4);
    _s[_n].ts = ts;
    _s[_n].at = now;
    _n++;
    if (_n < MESH_TIME_NEEDED) return 0;

    // each sample's estimate of "now", sorted
    uint32_t est[MESH_TIME_SAMPLES];
    for (int i = 0; i < _n; i++) est[i] = _s[i].ts + (now - _s[i].at) / 1000;
    qsort(est, _n, sizeof(est[0]), cmp);
    // the largest group within MESH_TIME_SPREAD_S of each other
    int best_lo = 0, best_len = 0;
    for (int lo = 0, hi = 0; lo < _n; lo++) {
      if (hi < lo) hi = lo;
      while (hi + 1 < _n && est[hi + 1] - est[lo] <= MESH_TIME_SPREAD_S) hi++;
      if (hi - lo + 1 > best_len) { best_len = hi - lo + 1; best_lo = lo; }
    }
    if (best_len < MESH_TIME_NEEDED) return 0;
    return est[best_lo + best_len / 2] - MESH_TIME_MARGIN_S;  // the group's middle
  }
};

static MeshTimeCollector mesh_time;
