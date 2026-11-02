// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Signal tester (the Signal page): pings the repeaters in direct range (a zero-hop
// discovery request, like the Discover page) every few seconds and shows how well the
// best one (or a chosen one) hears us and we hear it, with a beep per ping whose pitch
// follows the signal. For walking around to find dead spots. Stops by itself after 30 min.
// Finder (Signal menu > Finder): locked onto one repeater, a ping every 5 s, and whether
// you're getting warmer or colder by its signal strength (RSSI: keeps rising as you get
// close, where SNR tops out). No compass on the board, so no direction, just the trend.

#define SIG_ROUND_MS    5000UL               // replies counted for this long after a ping
#define SIG_MAX_RUN_MS  (30UL * 60 * 1000)   // auto stop (airtime, battery)
#define SIG_HIST        40
#define SIG_NONE        -128                 // no reply that round

static const uint8_t SIG_INTERVALS[] = {10, 15, 30, 60};  // seconds
#define NUM_SIG_INTERVALS (sizeof(SIG_INTERVALS) / sizeof(SIG_INTERVALS[0]))
#define SIG_FINDER_MS   5000UL               // finder: ping interval (zero hop, nearby only)

class SignalTester {
public:
  bool running = false;
  uint16_t pings = 0, replies = 0;  // finished pings, and those answered
  // last finished round
  bool last_ok = false;
  float last_in = 0, last_out = 0;  // dB: we hear them / they hear us
  uint8_t last_heard = 0;           // repeaters that answered
  char last_name[32] = "";
  // target: the best repeater each round, or one locked in
  bool locked = false;
  uint8_t lock_prefix[8];
  char lock_name[32] = "";
  int8_t hist[SIG_HIST];  // link quality per ping (min of in/out, dB x4), SIG_NONE if none
  int hist_n = 0;
  // finder
  bool finder = false;
  int last_rssi = 0, best_rssi = -200;  // dBm, the target's reply as we hear it
  int trend = 0;                        // +1 warmer, -1 colder, 0 about the same / unknown
  int8_t rssi_hist[4];                  // the last few replies, for the trend
  int rssi_n = 0;

  SignalTester() { clear(); }

  void clear() {
    rssi_n = 0;
    trend = 0;
    best_rssi = -200;
    pings = replies = 0;
    hist_n = 0;
    last_ok = false;
    last_heard = 0;
    last_name[0] = 0;
  }
  void start() {
    running = true;
    _started = millis();
    _next_ping = millis();
    _round_open = false;
  }
  void stop() { running = false; _round_open = false; finder = false; }
  void startFinder() {  // locked onto the current target (or the first to answer)
    clear();
    finder = true;
    start();
  }

  // a bit more than a full round: what to show as "next ping in"
  int secsToNextPing() const {
    long ms = (long)(_next_ping - millis());
    return ms > 0 ? (int)((ms + 999) / 1000) : 0;
  }

  // lock onto the best repeater of the last round, or back to "best"
  bool toggleLock() {
    if (locked) { locked = false; return true; }
    if (!last_ok) return false;
    memcpy(lock_prefix, _last_prefix, 8);
    StrHelper::strncpy(lock_name, last_name, sizeof(lock_name));
    locked = true;
    return true;
  }

  // true when the page should redraw
  bool loop(uint8_t interval_idx, SF32Sound::Volume vol) {
    if (!running) return false;
    if (millis() - _started > SIG_MAX_RUN_MS) { stop(); return true; }
    if (_round_open && millis() - _sent_at >= SIG_ROUND_MS) {
      finishRound(vol);
      return true;
    }
    if (!_round_open && (long)(millis() - _next_ping) >= 0) {
      ping();
      _next_ping = millis() + (finder ? SIG_FINDER_MS : SIG_INTERVALS[interval_idx % NUM_SIG_INTERVALS] * 1000UL);
      return true;
    }
    return false;
  }

  void onResponse(const mesh::Packet* packet) {
    if (!_round_open || memcmp(&packet->payload[2], &_tag, 4) != 0) return;
    const uint8_t* prefix = &packet->payload[6];
    if (locked && memcmp(prefix, lock_prefix, 8) != 0) { _heard++; return; }
    float in = radio_driver.getLastSNR(), out = ((int8_t)packet->payload[1]) / 4.0f;
    int rssi = (int)radio_driver.getLastRSSI();
    _heard++;
    if (!_got || fminf(in, out) > fminf(_in, _out)) {
      _got = true;
      _in = in;
      _out = out;
      _rssi = rssi;
      memcpy(_prefix, prefix, 8);
    }
  }

private:
  unsigned long _started = 0, _next_ping = 0, _sent_at = 0;
  bool _round_open = false, _got = false;
  uint32_t _tag = 0;
  uint8_t _heard = 0;
  float _in = 0, _out = 0;
  int _rssi = 0;
  uint8_t _prefix[8], _last_prefix[8];

  void ping() {
    uint8_t cmd[6];
    cmd[0] = CTL_TYPE_NODE_DISCOVER_REQ | 1;  // prefix only
    cmd[1] = 0xFF;                            // repeaters
    the_mesh.getRNG()->random((uint8_t*)&_tag, 4);
    memcpy(&cmd[2], &_tag, 4);
    mesh::Packet* req = the_mesh.createControlData(cmd, sizeof(cmd));
    if (!req) return;
    the_mesh.sendZeroHop(req);
    _sent_at = millis();
    _round_open = true;
    _got = false;
    _heard = 0;
  }

  void finishRound(SF32Sound::Volume vol) {
    _round_open = false;
    pings++;
    last_ok = _got;
    last_heard = _heard;
    int8_t q = SIG_NONE;
    if (_got) {
      replies++;
      last_in = _in;
      last_out = _out;
      memcpy(_last_prefix, _prefix, 8);
      ContactInfo* c = the_mesh.lookupContactByPubKey(_prefix, 8);
      if (c) StrHelper::strncpy(last_name, c->name, sizeof(last_name));
      else mesh::Utils::toHex(last_name, _prefix, 4);
      float link = fminf(_in, _out);
      q = (int8_t)constrain((int)lroundf(link * 4), -127, 127);
      if (finder) {
        if (!locked) {  // the first to answer becomes the one to find
          memcpy(lock_prefix, _prefix, 8);
          StrHelper::strncpy(lock_name, last_name, sizeof(lock_name));
          locked = true;
        }
        // warmer / colder: this reply against the average of the few before (3 dB either way)
        last_rssi = _rssi;
        if (rssi_n >= 2) {
          int sum = 0, n = rssi_n < 4 ? rssi_n : 4;
          for (int k = 0; k < n; k++) sum += rssi_hist[k];
          float avg = (float)sum / n;
          trend = _rssi > avg + 3 ? 1 : (_rssi < avg - 3 ? -1 : 0);
        }
        memmove(rssi_hist + 1, rssi_hist, 3);
        rssi_hist[0] = (int8_t)constrain(_rssi, -128, 127);
        if (rssi_n < 4) rssi_n++;
        if (_rssi > best_rssi) best_rssi = _rssi;
        // pitch follows the signal strength: -120 dBm low, -40 high
        sound.tone(400 + (constrain(_rssi, -120, -40) + 120) * 20, 90, vol);
      } else {
        // pitch follows the link: -15 dB low, +10 dB high
        int hz = 500 + (int)((constrain(link, -15.0f, 10.0f) + 15.0f) * 60.0f);
        sound.tone(hz, 90, vol);
      }
    } else {
      sound.tone(260, 110, vol, 2);  // no reply: two low beeps
    }
    if (hist_n < SIG_HIST) hist[hist_n++] = q;
    else { memmove(hist, hist + 1, SIG_HIST - 1); hist[SIG_HIST - 1] = q; }
  }
};

static SignalTester signal_tester;

static ColorVal signalColor(float db) {
  return db >= 0 ? THEME_GOOD : db >= -8 ? THEME_ACCENT : UIColor::warning_txt;
}

// recent pings as bars: -20 dB at the bottom, +12 dB at the top; no reply = a red tick
static void drawSignalGraph(DisplayDriver& d, int x, int y, int w, int h) {
  d.setColor(THEME_DIM);
  int zero = y + h - (20 * h) / 32;  // 0 dB line
  d.fillRect(x, zero, w, 1);
  int bw = w / SIG_HIST;
  int x0 = x + w - bw * signal_tester.hist_n;
  for (int i = 0; i < signal_tester.hist_n; i++) {
    int bx = x0 + i * bw;
    int8_t q = signal_tester.hist[i];
    if (q == SIG_NONE) {
      d.setColor(THEME_RED);
      d.fillRect(bx, y + h - 2, bw > 1 ? bw - 1 : 1, 2);
      continue;
    }
    float db = q / 4.0f;
    int bh = (int)lroundf((constrain(db, -20.0f, 12.0f) + 20.0f) * h / 32.0f);
    if (bh < 1) bh = 1;
    d.setColor(signalColor(db));
    d.fillRect(bx, y + h - bh, bw > 1 ? bw - 1 : 1, bh);
  }
}
