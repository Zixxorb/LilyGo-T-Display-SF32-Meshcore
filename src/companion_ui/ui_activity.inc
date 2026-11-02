// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Mesh activity (the Activity page): packets heard per hour for the last 24 hours, and how
// busy the channel is (the share of the time the radio spent receiving or sending).
// Counted from the radio's own totals once a minute: no extra traffic. RAM only (starts
// over at boot).

#define ACT_HOURS     24
#define ACT_BUCKET_MS (60UL * 60 * 1000)
#define ACT_SAMPLE_MS 60000UL

class MeshActivity {
  uint16_t _pkts[ACT_HOURS] = {};   // per hour, [ACT_HOURS - 1] = the current one
  uint32_t _air_ms[ACT_HOURS] = {}; // radio time receiving + sending, per hour
  int _hours = 1;                   // hours with data (the current one included)
  unsigned long _bucket_at = 0, _next = 0;
  uint32_t _last_recv = 0, _last_rx_air = 0, _last_tx_air = 0;
  bool _started = false;

  void take() {
    uint32_t recv = radio_driver.getPacketsRecv();
    uint32_t rx = the_mesh.getReceiveAirTime(), tx = the_mesh.getTotalAirTime();
    if (_started) {
      _pkts[ACT_HOURS - 1] += (uint16_t)(recv - _last_recv);
      _air_ms[ACT_HOURS - 1] += (rx - _last_rx_air) + (tx - _last_tx_air);
    }
    _last_recv = recv;
    _last_rx_air = rx;
    _last_tx_air = tx;
    _started = true;
  }

public:
  void loop() {
    unsigned long now = millis();
    if (!_started) { _bucket_at = now; _next = now + ACT_SAMPLE_MS; take(); return; }
    if ((long)(now - _next) < 0) return;
    _next = now + ACT_SAMPLE_MS;
    take();
    while (now - _bucket_at >= ACT_BUCKET_MS) {  // a new hour
      _bucket_at += ACT_BUCKET_MS;
      memmove(_pkts, _pkts + 1, sizeof(_pkts[0]) * (ACT_HOURS - 1));
      memmove(_air_ms, _air_ms + 1, sizeof(_air_ms[0]) * (ACT_HOURS - 1));
      _pkts[ACT_HOURS - 1] = 0;
      _air_ms[ACT_HOURS - 1] = 0;
      if (_hours < ACT_HOURS) _hours++;
    }
  }

  int hours() const { return _hours; }
  uint16_t packets(int i) const { return _pkts[i]; }  // i: 0 oldest .. ACT_HOURS - 1 now
  // the last 60 minutes, whatever the hour boundaries: this hour so far plus the right
  // share of the one before
  uint32_t lastHourPackets() const {
    unsigned long into = millis() - _bucket_at;
    if (_hours < 2) return _pkts[ACT_HOURS - 1];
    return _pkts[ACT_HOURS - 1] + (uint32_t)_pkts[ACT_HOURS - 2] * (ACT_BUCKET_MS - into) / ACT_BUCKET_MS;
  }
  uint32_t dayPackets() const {
    uint32_t n = 0;
    for (int i = 0; i < ACT_HOURS; i++) n += _pkts[i];
    return n;
  }
  // percent of this hour (so far) the channel was in use; -1 too early to say
  int busyPercentNow() const {
    unsigned long into = millis() - _bucket_at;
    if (into < 5 * 60000UL) {  // the first minutes of an hour: use the last full one
      if (_hours < 2) return into < 60000UL ? -1 : (int)(_air_ms[ACT_HOURS - 1] * 100 / into);
      return (int)(_air_ms[ACT_HOURS - 2] * 100 / ACT_BUCKET_MS);
    }
    return (int)(_air_ms[ACT_HOURS - 1] * 100 / into);
  }
};

static MeshActivity mesh_activity;

static void drawActivityGraph(DisplayDriver& d, int x, int y, int w, int h) {
  uint16_t top = 1;
  for (int i = 0; i < ACT_HOURS; i++) if (mesh_activity.packets(i) > top) top = mesh_activity.packets(i);
  // scale: a round number at or above the busiest hour
  static const uint16_t steps[] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 65535};
  uint16_t scale = 10;
  for (uint16_t s : steps) { scale = s; if (s >= top) break; }
  d.setColor(THEME_DIM);
  d.fillRect(x, y + h, w, 1);
  d.fillRect(x, y, w, 1);
  int bw = w / ACT_HOURS;  // bar + gap
  int first = ACT_HOURS - mesh_activity.hours();
  for (int i = first; i < ACT_HOURS; i++) {
    uint16_t v = mesh_activity.packets(i);
    if (!v) continue;
    int bh = (int)((uint32_t)v * h / scale);
    if (bh < 1) bh = 1;
    d.setColor(i == ACT_HOURS - 1 ? THEME_GOOD : THEME_ACCENT);  // the current hour: still filling
    d.fillRect(x + i * bw, y + h - bh, bw - 1, bh);
  }
  char tmp[12];
  d.setTextSize(1);
  d.setColor(UIColor::secondary_txt);
  sprintf(tmp, "%u", scale);
  d.drawTextRightAlign(x + w, y - 9, tmp);
  d.setCursor(x, y + h + 4);
  d.print("24h");
  d.drawTextCentered(x + w / 2, y + h + 4, "12h");
  d.drawTextRightAlign(x + w, y + h + 4, "now");
}
