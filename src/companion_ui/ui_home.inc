// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// HomeScreen: the pages (clock, status, battery, heard, radio, bluetooth, advert, bot, discover,
// signal, power).

// SF32: the order the pages come in (Settings > Pages > Order), kept in /sf32_order. The clock
// is always first. Pages by their HomePage index; a page this build has that the file doesn't
// (a new one) goes at the end, in its usual place among the rest.
#define PAGE_ORDER_FILE  "/sf32_order"
#define PAGE_ORDER_MAGIC 0x4F504653  // "SFPO"
struct PageOrder {
  uint8_t o[24];
  int n = 0;
  void ensure(int count) {
    if (n == count) return;
    struct { uint32_t magic; uint8_t n; uint8_t o[24]; } f = {};
    File file = InternalFS.open(PAGE_ORDER_FILE, "r");
    bool ok = file && file.read((uint8_t*)&f, sizeof(f)) > 5 && f.magic == PAGE_ORDER_MAGIC && f.n <= 24;
    bool have[24] = {};
    n = 0;
    o[n++] = 0;  // the clock
    have[0] = true;
    if (ok) for (int i = 0; i < f.n; i++) if (f.o[i] < count && !have[f.o[i]]) { have[f.o[i]] = true; o[n++] = f.o[i]; }
    for (int p = 0; p < count && n < 24; p++) if (!have[p]) o[n++] = p;
  }
  void save() {
    struct { uint32_t magic; uint8_t n; uint8_t o[24]; } f = {};
    f.magic = PAGE_ORDER_MAGIC;
    f.n = n;
    memcpy(f.o, o, n);
    File file = InternalFS.open(PAGE_ORDER_FILE, "w", true);
    if (file) file.write((const uint8_t*)&f, sizeof(f));
  }
  int pos(int page) const { for (int i = 0; i < n; i++) if (o[i] == page) return i; return 0; }
  void move(int from, int to) {  // positions; the clock (0) stays
    if (from < 1 || to < 1 || from >= n || to >= n || from == to) return;
    uint8_t p = o[from];
    if (from < to) memmove(o + from, o + from + 1, to - from);
    else memmove(o + to + 1, o + to, from - to);
    o[to] = p;
    save();
  }
  void reset(int count) { InternalFS.remove(PAGE_ORDER_FILE); n = 0; ensure(count); }
};
static PageOrder page_order;

class HomeScreen : public UIScreen {
  enum HomePage {
    CLOCK,     // SF32
    FIRST,
    BATTERY,   // SF32: history graph (ui_battery.inc)
    RECENT,
    RADIO,
    BLUETOOTH,
    ADVERT,
    BOT,       // SF32: auto-reply bot (ui_bot.inc)
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
#if UI_DISCOVER_SCREEN
    DISCOVERY,
#endif
    SIGNAL,    // SF32: signal tester (ui_signal.inc)
    ACTIVITY,  // SF32: packets heard per hour, channel use (ui_activity.inc)
    NODES,     // SF32: My Nodes: starred contacts (ui_nodes.inc)
    GAMES,     // SF32: the pet (ui_pet.inc). (Before Power: older settings files move Power's hide bit, ui_prefs.inc)
#ifndef UI_NO_HIBERNATE
    SHUTDOWN,
#endif
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  uint16_t _page_gen = 0;  // SF32: bumped whenever the page changes (a page "just opened")
  int8_t _pet_info = -1;  // SF32: the Starlit page's info card: 0 fed, 1 curious, 2 rested, 3 mood (-1: none)
  bool _shutdown_init;
  bool _tz_edit = false;  // SF32: time zone page in edit mode
  AdvertPath recent[UI_RECENT_LIST_SIZE];
#if UI_DISCOVER_SCREEN
  DiscoveredNode discovered[DISCOVERED_NODES_TABLE_SIZE]; // not circular, latest discovered nodes are not kept
  uint32_t disc_node_req_tag = 0;
  uint32_t disc_nodes_count = 0;
  uint32_t discovery_req_time = 0;
  bool discovery_disp_names = true; // by default desplay names if available (removes SNR_O)
#endif

  // ---- SF32: clock and time zone pages ----

  static int batteryPercent(uint16_t mv) { return sf32ShownPercent(mv); }

  // a real time: after 2025 and not MeshCore's guess after the clock was lost
  static bool timeIsSet(uint32_t now) { return now >= 1735689600UL && rtc_clock.timeKnown(); }

  // local time as "h:mm" + "AM"/"PM"
  void formatTime(uint32_t now, char* hm, char* ampm) {
    time_t t = (time_t)(now + _task->localOffsetSecs(now));
    struct tm tm;
    gmtime_r(&t, &tm);
    if (_task->uiPrefs().clock24) {
      sprintf(hm, "%d:%02d", tm.tm_hour, tm.tm_min);
      ampm[0] = 0;
      return;
    }
    int h12 = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
    sprintf(hm, "%d:%02d", h12, tm.tm_min);
    strcpy(ampm, tm.tm_hour < 12 ? "AM" : "PM");
  }


  // ---- SF32: clock faces (Display > Clock face, the Clock page menu, or long press) ----
  // 0 Digital (renderClock below), 1 Analog, 2 Big (stacked digits), 3 Info (time + cards),
  // 4 Color, 5 Sun (the day's arc, sunrise / sunset).
  // All keep the top bar (unread envelope, DND, battery).

  static void localTm(UITask* task, uint32_t now, struct tm& tm) {
    time_t t = (time_t)(now + task->localOffsetSecs(now));
    gmtime_r(&t, &tm);
  }
  static void dateText(char* buf, const struct tm& tm, bool short_form) {
    static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (short_form) sprintf(buf, "%s %d", days[tm.tm_wday], tm.tm_mday);
    else sprintf(buf, "%s, %s %d", days[tm.tm_wday], months[tm.tm_mon], tm.tm_mday);
  }

  void drawAnalogFace(DisplayDriver& d, uint32_t now) {
    SF32Display& sd = (SF32Display&)d;
    struct tm tm;
    localTm(_task, now, tm);
    const float cx = 64, cy = 69, R = 44, PI2 = 6.2831853f;
    for (int i = 0; i < 12; i++) {  // hour ticks, longer at 12/3/6/9
      float a = i * PI2 / 12, s = sinf(a), c = cosf(a);
      bool major = i % 3 == 0;
      float r0 = major ? R - 8 : R - 4;
      d.setColor(major ? UIColor::primary_txt : UIColor::secondary_txt);
      sd.drawLineAA(cx + s * r0, cy - c * r0, cx + s * R, cy - c * R, major ? 2.2f : 1.3f);
    }
    char tmp[16];
    dateText(tmp, tm, true);
    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    d.drawTextCentered((int)cx, (int)cy + 16, tmp);
    float ma = tm.tm_min * PI2 / 60, ha = ((tm.tm_hour % 12) + tm.tm_min / 60.0f) * PI2 / 12;
    d.setColor(UIColor::primary_txt);
    sd.drawLineAA(cx - sinf(ha) * 4, cy + cosf(ha) * 4, cx + sinf(ha) * 24, cy - cosf(ha) * 24, 3.4f);  // hour
    sd.drawLineAA(cx - sinf(ma) * 5, cy + cosf(ma) * 5, cx + sinf(ma) * 38, cy - cosf(ma) * 38, 2.2f);  // minute
    d.setColor(THEME_ACCENT);
    sd.drawLineAA(cx, cy, cx, cy, 4.2f);  // centre
  }

  void drawBigFace(DisplayDriver& d, uint32_t now) {
    struct tm tm;
    localTm(_task, now, tm);
    bool h24 = _task->uiPrefs().clock24;
    char tmp[24];
    int h = h24 ? tm.tm_hour : (tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12);
    sprintf(tmp, h24 ? "%02d" : "%d", h);
    d.setTextSize(5);
    d.setColor(UIColor::primary_txt);
    d.drawTextCentered(64, 22, tmp);
    sprintf(tmp, "%02d", tm.tm_min);
    d.setColor(THEME_ACCENT);
    d.drawTextCentered(64, 64, tmp);
    dateText(tmp, tm, false);
    if (!h24) strcat(tmp, tm.tm_hour < 12 ? "  AM" : "  PM");
    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    d.drawTextCentered(64, 108, tmp);
  }

  // ---- your Starlit, on its page and on the Starlit clock face ----
  void petPrep(uint16_t mv, bool still) {  // up to now, and what the device is doing, for it to reflect
    int bp = mv > 0 ? sf32ShownPercent(mv) : -1;
    bool usb = board.isExternalPowered();
    pet.setContext((_task->sosState() ? PetGame::CTX_SOS : 0) | (_task->dndNow() ? PetGame::CTX_NIGHT : 0) |
                   (still ? PetGame::CTX_LOWPOWER : 0) | (_task->getUnreadCount() > 0 ? PetGame::CTX_MAIL : 0) |
                   (bp >= 0 && bp < 20 && !usb ? PetGame::CTX_LOWBATT : 0) | (usb && bp >= 0 && bp < 100 ? PetGame::CTX_CHARGING : 0));
    pet.update(true);
  }
  // the line under it: every 4 s in turn, its state or mood, then today's finds (`since`: ms shown)
  void petLine(char* out, int n, bool still, unsigned long since) {
    int k = 0;
    char tmp[64];
    while (k < 8 && today_finds.line(k, tmp, sizeof(tmp))) k++;  // (8 kinds of finds)
    int slot = still ? 0 : (int)((since / 4000) % (k + 1));
    if (slot > 0 && today_finds.line(slot - 1, out, n)) return;
    const char* cl = pet.contextLine();  // the device's state comes before its mood
    if (cl) StrHelper::strncpy(out, cl, n); else snprintf(out, n, "your Starlit is %s", PetGame::moodName(pet.mood()));
  }
  // its frames, paced from the frame's start (UITask counts a delay of 20 ms or less from the
  // frame's start, more from its end)
  static int petDelay(unsigned long t0, int period) {
    int left = period - (int)(millis() - t0);
    return period <= 20 ? period : (left > 20 ? left : 21);
  }
  // The Starlit face: the time and date at the top, your Starlit below (as on its page: it
  // reflects the device), and its line (state, mood, today's finds) at the bottom.
  int drawStarlitFace(DisplayDriver& d, uint32_t now) {
    unsigned long t0 = millis();
    struct tm tm;
    localTm(_task, now, tm);
    char hm[8], ampm[4], tmp[64];
    formatTime(now, hm, ampm);
    d.setTextSize(3);
    d.setColor(UIColor::primary_txt);
    d.setCursor(6, 24);
    d.print(hm);
    int x = 6 + d.getTextWidth(hm) + 3;
    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    if (ampm[0]) { d.setCursor(x, 24); d.print(ampm); }
    dateText(tmp, tm, true);
    d.setCursor(x, 33);
    d.print(tmp);
    uint16_t mv = _task->getBattMilliVolts();
    petPrep(mv, false);
    pet.draw(d, 64, 79, 0.84f, false, 0, -1, _node_prefs->path_hash_mode + 1);
    char raw[64];
    petLine(raw, sizeof(raw), false, millis());
    d.translateUTF8ToBlocks(tmp, raw, sizeof(tmp));  // (node names in it: shown as the rest of the UI shows them)
    d.setTextSize(1);
    d.setColor(THEME_DIM);
    if (d.getTextWidth(tmp) > 120) d.drawTextEllipsized(4, 108, 120, tmp); else d.drawTextCentered(64, 108, tmp);
    return petDelay(t0, 80);  // ~12 fps: gentle on the battery for the page seen most
  }
  void drawInfoFace(DisplayDriver& d, uint32_t now) {
    struct tm tm;
    localTm(_task, now, tm);
    char hm[8], ampm[4], tmp[24];
    formatTime(now, hm, ampm);
    d.setTextSize(3);
    d.setColor(UIColor::primary_txt);
    d.setCursor(6, 24);
    d.print(hm);
    int x = 6 + d.getTextWidth(hm) + 3;
    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    if (ampm[0]) { d.setCursor(x, 24); d.print(ampm); }
    dateText(tmp, tm, true);
    d.setCursor(x, 33);
    d.print(tmp);
    SunTimes st = sunToday(_task, _node_prefs, now);  // the next sunrise / sunset, if known
    if (st.kind == SunTimes::NORMAL) {
      bool day = now >= st.rise && now < st.set;
      char t[16];
      localClock(t, day ? st.set : (now < st.rise ? st.rise : st.rise + 86400));
      sprintf(tmp, "%s %s", day ? "sunset" : "sunrise", t);
      d.setColor(day ? 0xFB00 : 0xFD20);
      d.setCursor(x, 42);
      d.print(tmp);
    }
    // four cards: unread, battery, phone, last heard
    uint16_t mv = _task->getBattMilliVolts();
    themeCard(d, 4, 52, 58, 30);
    themeLabel(d, 9, 55, "Unread");
    sprintf(tmp, "%d", _task->getUnreadCount());
    themeValue(d, 9, 64, tmp);
    themeCard(d, 66, 52, 58, 30);
    themeLabel(d, 71, 55, "Battery");
    sprintf(tmp, "%d", mv ? batteryPercent(mv) : 0);
    themeValue(d, 71, 64, tmp, "%");
    themeCard(d, 4, 86, 58, 30);
    themeLabel(d, 9, 89, "Phone");
    themeValue(d, 9, 98, _task->hasConnection() ? "Yes" : "No");
    themeCard(d, 66, 86, 58, 30);
    themeLabel(d, 71, 89, "Last heard");
    AdvertPath last[1];
    memset(last, 0, sizeof(last));
    the_mesh.getRecentlyHeard(last, 1);
    if (last[0].name[0] && last[0].recv_timestamp && now >= last[0].recv_timestamp) formatAge(tmp, now - last[0].recv_timestamp);
    else strcpy(tmp, "-");
    themeValue(d, 71, 98, tmp);
  }

  // a time of day (UTC) as local "6:52 AM" / "18:52"
  void localClock(char* buf, uint32_t utc) {
    struct tm tm;
    localTm(_task, utc, tm);
    if (_task->uiPrefs().clock24) sprintf(buf, "%d:%02d", tm.tm_hour, tm.tm_min);
    else sprintf(buf, "%d:%02d %s", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
  }

  // Sun: the time, then the day as an arc from sunrise to sunset with the sun on it (below
  // the horizon at night), sunrise / sunset and how long until the next one
  // a little sun: a glowing disc with eight rays
  static void drawSunIcon(SF32Display& sd, float x, float y) {
    sd.fillSoft(x, y, 9, 9, 0xFFE0, 9);  // its glow
    sd.setColor(0xFE60);
    for (int i = 0; i < 8; i++) {
      float a = i * 0.785398f;
      sd.drawLineAA(x + cosf(a) * 4.6f, y + sinf(a) * 4.6f, x + cosf(a) * 6.6f, y + sinf(a) * 6.6f, 1.1f);
    }
    sd.setColor(0xFFE0);
    sd.drawLineAA(x, y, x, y, 6.4f);
  }
  // the moon with its phase: drawn a pixel row at a time, lit beyond the terminator (the curve
  // between day and night on it), the rest a faint grey (earthshine). Lit on the right while it
  // grows, as seen from the northern hemisphere.
  static void drawMoonIcon(SF32Display& sd, float x, float y, float r, float lit, bool waxing) {
    const ColorVal LIGHT = 0xEF7D, DARK = 0x3186;
    sd.fillSoft(x, y, r * 2.4f, r * 2.4f, LIGHT, (int)(2 + 5 * lit));  // a soft glow, brighter when full
    float side = waxing ? 1 : -1, step = 1.0f / SF32Display::SCALE;  // one panel row
    for (float j = -r + step / 2; j < r; j += step) {
      float half = r * sqrtf(1 - (j / r) * (j / r));
      sd.setColor(DARK);
      sd.drawLineAA(x - half, y + j, x + half, y + j, step * 1.15f);
      float xt = (1 - 2 * lit) * half;  // the terminator on this row
      float a = x + side * xt, b = x + side * half;
      if (fabsf(b - a) > step * 0.3f) { sd.setColor(LIGHT); sd.drawLineAA(a, y + j, b, y + j, step * 1.15f); }
    }
  }

  void drawSunFace(DisplayDriver& d, uint32_t now) {
    SF32Display& sd = (SF32Display&)d;
    struct tm tm;
    localTm(_task, now, tm);
    char hm[8], ampm[4], tmp[32];
    formatTime(now, hm, ampm);
    d.setTextSize(3);
    d.setColor(UIColor::primary_txt);
    int w = d.getTextWidth(hm);
    d.setTextSize(1);
    int wa = ampm[0] ? d.getTextWidth(ampm) + 3 : 0;
    d.setTextSize(3);
    d.setCursor(64 - (w + wa) / 2, 24);
    d.print(hm);
    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    if (ampm[0]) { d.setCursor(64 - (w + wa) / 2 + w + 3, 26); d.print(ampm); }
    dateText(tmp, tm, false);
    d.drawTextCentered(64, 48, tmp);

    SunTimes st = sunToday(_task, _node_prefs, now);
    if (st.kind == SunTimes::NONE) {
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(64, 76, "set this node's location");
      d.drawTextCentered(64, 86, "in the app for sun times");
      return;
    }
    // the arc: a half circle over the horizon, sunrise on the left, sunset on the right
    const float cx = 64, cy = 96, R = 36, PI = 3.14159265f;
    d.setColor(THEME_DIM);
    sd.drawLineAA(cx - R - 8, cy, cx + R + 8, cy, 1.0f);  // the horizon
    for (int i = 0; i <= 36; i++) {
      float a = PI * i / 36;
      float x = cx - cosf(a) * R, y = cy - sinf(a) * R;
      sd.drawLineAA(x, y, x, y, 1.2f);
    }
    if (st.kind == SunTimes::NORMAL) {
      bool day = now >= st.rise && now < st.set;
      float f = day ? (float)(now - st.rise) / (st.set - st.rise) : 0;
      if (day) {  // the part of the day gone: brighter
        d.setColor(0xFD20);  // orange
        for (int i = 0; i <= (int)(36 * f); i++) {
          float a = PI * i / 36;
          float x = cx - cosf(a) * R, y = cy - sinf(a) * R;
          sd.drawLineAA(x, y, x, y, 1.6f);
        }
      }
      // the moon, where it really is: on the arc between its own rise and set, with its phase
      MoonNow mn = moonNow(_node_prefs, now);
      if (mn.known && mn.up) {
        float a = PI * mn.f;
        drawMoonIcon(sd, cx - cosf(a) * R, cy - sinf(a) * R, 4.2f, mn.lit, mn.waxing);
      }
      if (day) {  // the sun, on top
        float a = PI * f;
        drawSunIcon(sd, cx - cosf(a) * R, cy - sinf(a) * R);
      }
      // times: sunrise left, sunset right
      d.setTextSize(1);
      d.setColor(0xFD20);
      localClock(tmp, st.rise);
      d.setCursor(4, cy + 4);
      d.print(tmp);
      d.setColor(0xFB00);
      localClock(tmp, st.set);
      d.drawTextRightAlign(124, cy + 4, tmp);
      // next event
      uint32_t next = day ? st.set : (now < st.rise ? st.rise : st.rise + 86400);
      int mins = (int)((next - now) / 60);
      sprintf(tmp, "%s in %dh %02dm", day ? "sunset" : "sunrise", mins / 60, mins % 60);
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(64, cy + 16, tmp);
    } else {
      d.setTextSize(1);
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(64, cy + 8, st.kind == SunTimes::ALL_DAY ? "the sun doesn't set today" : "the sun doesn't rise today");
    }
  }

  // Color: a rainbow ring of 60 minute dots (bright up to now, dim after) turning with the
  // time, each digit its own colour, on true black: made to show off the AMOLED
  void drawColorFace(DisplayDriver& d, uint32_t now) {
    SF32Display& sd = (SF32Display&)d;
    struct tm tm;
    localTm(_task, now, tm);
    const float cx = 64, cy = 70, R = 46, PI2 = 6.2831853f;
    float rot = tm.tm_hour * 30 + tm.tm_min * 0.5f;  // the colours turn slowly through the day
    for (int i = 0; i < 60; i++) {
      float a = i * PI2 / 60, x = cx + sinf(a) * R, y = cy - cosf(a) * R;
      bool lit = i <= tm.tm_min, five = i % 5 == 0;
      d.setColor(ringColor(i * 6 + rot, lit ? 0.85f : 0.9f, lit ? 1.0f : 0.28f));  // the theme's colours (Rainbow: all)
      sd.drawLineAA(x, y, x, y, lit ? (five ? 4.2f : 3.0f) : (five ? 2.8f : 2.0f));
    }
    float ha = ((tm.tm_hour % 12) + tm.tm_min / 60.0f) * PI2 / 12;  // the hour: a white dot inside
    d.setColor(UIColor::primary_txt);
    sd.drawLineAA(cx + sinf(ha) * (R - 9), cy - cosf(ha) * (R - 9), cx + sinf(ha) * (R - 9), cy - cosf(ha) * (R - 9), 3.4f);
    char hm[8], ampm[4], tmp[24];
    formatTime(now, hm, ampm);
    d.setTextSize(3);
    int w = d.getTextWidth(hm);
    d.setCursor(64 - w / 2, 54);
    for (int k = 0; hm[k]; k++) {  // a colour per digit
      char ch[2] = {hm[k], 0};
      d.setColor(hm[k] == ':' ? UIColor::primary_txt : ringColor(rot + 40 + k * 55, 0.75f, 1.0f));
      d.print(ch);
    }
    dateText(tmp, tm, true);
    if (ampm[0]) { strcat(tmp, "  "); strcat(tmp, ampm); }
    d.setTextSize(1);
    d.setColor(ringColor(rot + 200, 0.5f, 1.0f));
    d.drawTextCentered(64, 82, tmp);
  }

  // which pages are shown (Settings > Pages); the clock always is
  bool pageVisible(int p) const { return p == HomePage::CLOCK || !(_task->uiPrefs().hidden_pages & (1u << p)); }
  void stepPage(int dir) {  // in the chosen order (Settings > Pages > Order)
    _pet_info = -1;  // (the Starlit's info card closes when the page changes)
    _page_gen++;
    page_order.ensure(HomePage::Count);
    int at = page_order.pos(_page);
    for (int k = 0; k < HomePage::Count; k++) {
      at = (at + HomePage::Count + dir) % HomePage::Count;
      _page = page_order.o[at];
      if (pageVisible(_page)) break;
    }
  }
  void drawPageDots(DisplayDriver& d) {  // only the shown pages, in their order
    int n = 0, idx = 0;
    page_order.ensure(HomePage::Count);
    for (int k = 0; k < HomePage::Count; k++) {
      int p = page_order.o[k];
      if (!pageVisible(p)) continue;
      if (p == _page) idx = n;
      n++;
    }
    themeDots(d, idx, n);
  }

public:
  static int pageCount() { return HomePage::Count; }
  static const char* pageName(int p) {
    switch (p) {
      case HomePage::CLOCK: return "Clock";
      case HomePage::FIRST: return "Status";
      case HomePage::BATTERY: return "Battery";
      case HomePage::RECENT: return "Heard";
      case HomePage::RADIO: return "Radio";
      case HomePage::BLUETOOTH: return "Bluetooth";
      case HomePage::ADVERT: return "Advert";
      case HomePage::BOT: return "Bot";
#if ENV_INCLUDE_GPS == 1
      case HomePage::GPS: return "GPS";
#endif
#if UI_SENSORS_PAGE == 1
      case HomePage::SENSORS: return "Sensors";
#endif
#if UI_DISCOVER_SCREEN
      case HomePage::DISCOVERY: return "Discover";
#endif
      case HomePage::SIGNAL: return "Signal";
      case HomePage::ACTIVITY: return "Activity";
      case HomePage::NODES: return "Nodes";
      case HomePage::GAMES: return "Starlit";
#ifndef UI_NO_HIBERNATE
      case HomePage::SHUTDOWN: return "Power";
#endif
      default: return "?";
    }
  }
  void nextFace() { _task->menuAction(ACT_CLOCK_FACE); }
  bool onClockPage() const { return _page == HomePage::CLOCK; }

  int renderClock(DisplayDriver& display) {
    ((SF32Display&)display).setSquareLayout(true);  // 128x128 over the whole panel
    char tmp[40];
    const int W = display.width();

    // top left: unread messages
    int unread = _task->getUnreadCount();
    if (unread > 0) {
      static const uint8_t envelope[] = {0xFF, 0xFF, 0x7F, 0xFE, 0xBF, 0xFD, 0xDF, 0xFB, 0xEF, 0xF7, 0xF7, 0xEF,
                                         0xFB, 0xDF, 0xFD, 0xBF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      display.setColor(UIColor::warning_txt);
      themeEnvelope(display, 4, 5, envelope);
      display.setTextSize(2);
      sprintf(tmp, "%d", unread);
      display.setCursor(23, 3);
      display.print(tmp);
    }

    // top right: battery
    uint16_t mv = _task->getBattMilliVolts();
    int bx = g_batt_icon ? W - 4 - 3 - 22 : W - 4 + 3, by = 6;  // no icon: the % ends at the right margin
    int bpct = mv > 0 ? batteryPercent(mv) : -1;
    ColorVal outline = bpct >= 0 && g_batt_colors ? battColor(bpct, UIColor::primary_txt) : UIColor::primary_txt;
    display.setColor(outline);
    if (g_batt_icon) {
      display.drawRect(bx, by, 22, 11);
      display.fillRect(bx + 22, by + 3, 3, 5);
    }
    display.setTextSize(SF32Display::TEXT_BATTERY);  // 1.5x: closer to the battery's size
    if (bpct >= 0) {
      display.setColor(battColor(bpct, UIColor::primary_txt));
      if (g_batt_icon) display.fillRect(bx + 2, by + 2, (18 * bpct + 50) / 100, 7);
      sprintf(tmp, "%d%%", bpct);
    } else {
      strcpy(tmp, "--");  // no battery
    }
    display.setColor(bpct >= 0 ? battColor(bpct, UIColor::primary_txt) : outline);  // low shows even without the icon
    int tw = display.getTextWidth(tmp);
    display.setCursor(bx - 3 - tw, 6);  // centred on the battery
    display.print(tmp);
    // the status icons (the same row as in page headers), clear of the unread count; the
    // envelope is left out here: the clock has its own, bigger and tappable, top left
    display.setTextSize(2);
    sprintf(tmp, "%d", unread);
    int min_x = unread > 0 ? 23 + display.getTextWidth(tmp) + 3 : 4;
    themeStatus(display, bx - 3 - tw - 3, 8, min_x, g_status & ~ST_MAIL);
    if (mv > 0) {  // voltage under the battery, small
      display.setTextSize(1);
      display.setColor(UIColor::secondary_txt);
      sprintf(tmp, "%d.%02dV", mv / 1000, (mv % 1000) / 10);
      display.drawTextRightAlign(W - 4, 20, tmp);
    }

    // SF32: the other clock faces (with the time known)
    uint32_t now = _rtc->getCurrentTime();
    // low power mode: always the Digital face, dimmer (few lit pixels on the AMOLED)
    bool lp = _task->lowPower();
    int face = lp ? 0 : _task->uiPrefs().clock_face % NUM_CLOCK_FACES;
    if (face != 0 && timeIsSet(now)) {
      if (face == 1) drawAnalogFace(display, now);
      else if (face == 2) drawBigFace(display, now);
      else if (face == 3) drawInfoFace(display, now);
      else if (face == 4) drawColorFace(display, now);
      else if (face == 6) { int ms = drawStarlitFace(display, now); drawPageDots(display); return ms; }
      else drawSunFace(display, now);
      drawPageDots(display);
      int to_minute = (int)(60 - now % 60) * 1000 + 50;
      return to_minute < 15000 ? to_minute : 15000;
    }

    // time and date (the Digital face)
    if (timeIsSet(now)) {
      char hm[8], ampm[4];
      formatTime(now, hm, ampm);
      display.setTextSize(5);
      int w1 = display.getTextWidth(hm);
      display.setTextSize(2);
      int w2 = display.getTextWidth(ampm);
      int x = (W - (w1 + (w2 ? 3 + w2 : 0))) / 2;
      display.setColor(lp ? UIColor::secondary_txt : UIColor::primary_txt);
      display.setTextSize(5);
      int ty = lp ? 29 : 34;  // (low power: a little higher, room for its label)
      display.setCursor(x, ty);
      display.print(hm);
      display.setTextSize(2);
      display.setColor(UIColor::secondary_txt);
      display.setCursor(x + w1 + 3, ty + 19);
      display.print(ampm);

      static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      time_t t = (time_t)(now + _task->localOffsetSecs(now));
      struct tm tm;
      gmtime_r(&t, &tm);
      sprintf(tmp, "%s, %s %d%s", days[tm.tm_wday], months[tm.tm_mon], tm.tm_mday,
              rtc_clock.timeFromMesh() ? "  (mesh)" : "");  // from adverts until the phone syncs
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(W / 2, lp ? 73 : 80, tmp);
    } else {
      display.setTextSize(5);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(W / 2, 34, "--:--");
      display.setTextSize(1);
      display.drawTextCentered(W / 2, 82, _task->uiPrefs().mesh_time ? "waiting for the app or mesh" : "connect the app to set the time");
    }

    // low power mode: say so, between the date and the bottom line
    bool pin_shown = _task->isBluetoothEnabled() && !_task->hasConnection() && the_mesh.getBLEPin() != 0 && bluetooth_interface.pairingOpen();
    if (lp && !pin_shown) {
      display.setTextSize(1);
      display.setColor(THEME_ACCENT);
      display.drawTextCentered(W / 2, 92, "Low power");
    }

    // bottom: BLE PIN while not connected, else the node name
    display.setTextSize(1);
    if (!_task->isBluetoothEnabled()) {
      display.setTextSize(1);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(W / 2, 104, "Bluetooth off");
    } else if (!_task->hasConnection() && the_mesh.getBLEPin() != 0 && bluetooth_interface.pairingOpen()) {
      display.setTextSize(2);
      display.setColor(UIColor::warning_txt);
      sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
      display.drawTextCentered(W / 2, 100, tmp);
    } else {
      char name[sizeof(_node_prefs->node_name)];
      display.translateUTF8ToBlocks(name, _node_prefs->node_name, sizeof(name));
      display.setColor(_task->hasConnection() ? UIColor::corp_blue : UIColor::secondary_txt);
      if (display.getTextWidth(name) > 120) display.drawTextEllipsized(4, 104, 120, name);
      else display.drawTextCentered(W / 2, 104, name);
    }

    drawPageDots(display);
    // only minutes are shown: redraw at the next minute, or in 15 s (battery, Bluetooth)
    if (!timeIsSet(now)) return 15000;
    int to_minute = (int)(60 - now % 60) * 1000 + 50;
    return to_minute < 15000 ? to_minute : 15000;
  }

public:
  // SF32: the clock is the default page; back to it when the screen goes dark.
  void showClock() { _page = HomePage::CLOCK; _page_gen++; }

  // SF32: menus
  void showRadio() { _page = HomePage::RADIO; _page_gen++; }
  int currentPage() const { return _page; }
  bool gamesPage() const { return _page == HomePage::GAMES; }
  // the middle of the page is the page's own (stroking the Starlit): page swipes start near an edge there
  bool edgeSwipesOnly() const { return _page == HomePage::GAMES; }

  // Touch: pages with on-screen buttons take taps directly (see UITask::handleTouchMenus).
  void dragStep(int dir) { stepPage(dir); }  // a page being dragged in (UITask::pageDrag)
  // Pages with an action: holding the icon does it (UITask::handleTouchMenus). Signal and Discover
  // show results instead of their icon once they've run: there, hold anywhere. Taps never change
  // the page (only swipes do).
  bool iconHoldPage() const {
    return _page == HomePage::BLUETOOTH || _page == HomePage::ADVERT || _page == HomePage::BOT || _page == HomePage::SIGNAL
        || _page == HomePage::GAMES  // hold the pet: play
#if UI_DISCOVER_SCREEN
        || _page == HomePage::DISCOVERY
#endif
#ifndef UI_NO_HIBERNATE
        || _page == HomePage::SHUTDOWN
#endif
        ;
  }
  bool iconHit(int x, int y) const {
    if (_page == HomePage::SIGNAL && (signal_tester.running || signal_tester.pings || signal_tester.hist_n || signal_tester.finder)) return true;
#if UI_DISCOVER_SCREEN
    if (_page == HomePage::DISCOVERY && disc_nodes_count > 0) return true;
#endif
    if (_page == HomePage::GAMES) return _pet_info < 0 && x >= 30 && x < 98 && y >= 24 && y < 88;  // the pet (not under an info card)
    return x >= 32 && x < 96 && y >= 24 && y < 86;
  }
  void iconAction() {
#ifndef UI_NO_HIBERNATE
    if (_page == HomePage::SHUTDOWN) { _task->openMenu(UITask::MENU_POWER); return; }  // powering off takes a 2nd tap
#endif
    handleInput(KEY_ENTER);
  }
  // The Starlit page's info card: what a need means, how it rises and falls, where it is now
  void drawPetInfo(DisplayDriver& display) {
    const PetSave& ps = pet.stats();
    static const char* const TITLES[] = {"Fed", "Curious", "Rested", "Mood"};
    char tmp[48];
    int k = _pet_info;
    themeCard(display, 4, 24, 120, 96);
    display.setTextSize(1);
    display.setColor(THEME_ACCENT);
    display.drawTextLeftAlign(9, 28, TITLES[k]);
    uint8_t v = k == 0 ? ps.fed : k == 1 ? ps.curious : k == 2 ? ps.rested : ps.happy;
    if (k == 3) snprintf(tmp, sizeof(tmp), "%s, happy %d%%", PetGame::moodName(pet.mood()), v);
    else snprintf(tmp, sizeof(tmp), "%d%%", v);
    display.setColor(v < 25 ? UIColor::warning_txt : UIColor::primary_txt);
    display.drawTextRightAlign(119, 28, tmp);
    if (k < 3) {  // its bar, bigger
      display.setColor(THEME_DIM);
      display.fillRect(9, 39, 110, 3);
      display.setColor(v < 25 ? UIColor::warning_txt : THEME_ACCENT);
      display.fillRect(9, 39, (110 * v + 50) / 100, 3);
    }
    static const char* const TEXT[] = {
      "Fed by the mesh: every packet your radio hears is a bite (+3). Goes down 1 every 30 min. Under 25: hungry.",
      "Grows with adverts heard (+2) and new nodes found (+15). Goes down 1 every 20 min. Under 25: bored.",
      "Rests while the screen is off (+1 every 2 min). Screen time (-1 every 3 min) and play (-4) tire it. Under 25: sleepy.",
      "Pet it (+8), boop it (+3) or play (+18) for happiness; it fades 1 every 15 min. Happy when all its needs are high.",
    };
    display.setColor(UIColor::primary_txt);
    display.setCursor(9, k < 3 ? 46 : 39);
    display.printWordWrap(TEXT[k], 110);
    display.setColor(UIColor::secondary_txt);
    if (k == 0) snprintf(tmp, sizeof(tmp), "today: +%d of 70 max", ps.day_fed);
    else if (k == 1) snprintf(tmp, sizeof(tmp), "today: +%d of 60 max", ps.day_cur);
    else if (pet.contextLine()) snprintf(tmp, sizeof(tmp), "now: %s", pet.contextLine());  // the device's state shows on it
    else tmp[0] = 0;
    if (tmp[0]) display.drawTextCentered(64, 101, tmp);
    display.drawTextCentered(64, 110, "tap: back");
  }

  // what holding the icon does, for a short tap on it (NULL: no hint)
  const char* holdHint() const {
    switch (_page) {
      case HomePage::ADVERT: return _task->uiPrefs().adv_page_mesh ? "Hold to send to mesh" : "Hold to send advert";
      case HomePage::BLUETOOTH: return _task->isBluetoothEnabled() ? "Hold to turn off" : "Hold to turn on";
      case HomePage::BOT: return _task->uiPrefs().bot_on ? "Hold to turn off" : "Hold to turn on";
      case HomePage::SIGNAL: return signal_tester.running || signal_tester.finder ? "Hold to stop" : "Hold to start";
#if UI_DISCOVER_SCREEN
      case HomePage::DISCOVERY: return "Hold to scan";
#endif
#ifndef UI_NO_HIBERNATE
      case HomePage::SHUTDOWN: return "Hold for power";
#endif
      default: return NULL;
    }
  }
  // true if the tap hit something; otherwise it acts as a normal tap (next page)
  bool handleTap(int x, int y) {
    if (_page == HomePage::FIRST) {  // Status: the Unread card opens the messages
      if (x >= 4 && x < 62 && y >= 27 && y < 67 && _task->getUnreadCount() > 0) { _task->showMessages(); return true; }
      return false;
    }
    if (_page == HomePage::CLOCK) {  // the unread envelope (top left): the messages
      if (x < 64 && y < 30 && _task->getUnreadCount() > 0) { _task->showMessages(); return true; }
      // the Starlit face: your Starlit holding mail ("tap it") opens it too
      bool starlit_face = !_task->lowPower() && _task->uiPrefs().clock_face % NUM_CLOCK_FACES == 6 && timeIsSet(_rtc->getCurrentTime());
      if (starlit_face && x >= 30 && x < 98 && y >= 50 && y < 106 && _task->getUnreadCount() > 0) { _task->showMessages(); return true; }
      return false;
    }
    if (_page == HomePage::GAMES) {  // the pet: pet it (it looks at where you touched)
      if (_pet_info >= 0) { _pet_info = -1; return true; }  // an info card open: any tap closes it
      if (y >= 86 && y < 105) { int k = (x - 4) / 41; _pet_info = k < 0 ? 0 : (k > 2 ? 2 : k); return true; }  // a need's bar: what it means
      if (y >= 105) { _pet_info = 3; return true; }  // the line under them: its mood
      if (x < 30 || x >= 98 || y < 24 || y >= 88) return false;
      if (pet.context() & PetGame::CTX_MAIL) { _task->showMessages(); return true; }  // it's holding a message: open it
      pet.look(x, y);
      if (pet.boop()) _task->petSound(3);  // a tap: a boop (stroking it is petting)
      return true;
    }
    if (_page == HomePage::NODES) {  // a node's row: its card (Starlit, details, battery on request)
      ContactInfo fav[NODES_MAX];
      int n = my_nodes.favourites(fav, NODES_MAX), shown = n < NODES_MAX ? n : NODES_MAX;
      int row = (y - 27) / 24;
      if (y < 27 || row >= shown) return false;
      _task->openNodeCard(fav[row]);
      return true;
    }
    return false;
  }

  // the page's own action (what long press does on it)
  void pagePrimary() { handleInput(KEY_ENTER); }
  void toggleDiscoveryNames() {
#if UI_DISCOVER_SCREEN
    discovery_disp_names = !discovery_disp_names;
#endif
  }

  // swipe-up menu for the current page (Settings and Close are added by the caller)
  void buildContextMenu(MenuScreen& m) {
    switch (_page) {
      case HomePage::CLOCK:
        m.begin("Clock");
        if (_task->getUnreadCount() > 0) m.add("Messages", ACT_READ_MSGS);
        m.add("Share QR", ACT_SHARE_QR);
        m.add("Clock menu", ACT_TIME_MENU);  // = Settings > Clock (face, zone, format)
        break;
      case HomePage::FIRST:
        m.begin("Status");
        if (_task->getUnreadCount() > 0) m.add("Messages", ACT_READ_MSGS);
        m.add("Share QR", ACT_SHARE_QR);
        m.add("Bluetooth", ACT_SET_BT, _task->isBluetoothEnabled() ? "On" : "Off");
        break;
      case HomePage::BATTERY:
        m.begin("Battery");
        m.add("Battery menu", ACT_BATT_MENU);  // = Settings > Battery (short: longer rows scroll)
        break;
      case HomePage::BLUETOOTH:
        m.begin("Bluetooth");
        m.add("Bluetooth", ACT_SET_BT, _task->isBluetoothEnabled() ? "On" : "Off");  // as on the Status page
        m.add("Pair phone", ACT_BT_PAIR);  // opens pairing for 2 min (shows the PIN)
        m.add("Forget phones", ACT_BT_FORGET);
        break;
      case HomePage::ADVERT:
      case HomePage::RECENT:
        m.begin(_page == HomePage::ADVERT ? "Advert" : "Heard");
        m.add("Send nearby", ACT_ADVERT);
        m.add("Send to mesh", ACT_ADVERT_MESH);
        if (_page == HomePage::ADVERT) m.add("Advert menu", ACT_ADVERTS_MENU);  // = Settings > Adverts (hold, auto)
        break;
#if UI_DISCOVER_SCREEN
      case HomePage::DISCOVERY:
        m.begin("Discover");
        m.add("Scan now", ACT_PAGE_PRIMARY);
        m.add(discovery_disp_names ? "Show keys" : "Show names", ACT_DISC_NAMES);
        break;
#endif
      case HomePage::BOT: {
        m.begin("Bot");
        m.add("Auto-reply", ACT_BOT_TOGGLE, _task->uiPrefs().bot_on ? "On" : "Off");  // the page's own toggle
        m.add("Bot menu", ACT_BOT_MENU);  // = Settings > Bot (DMs, channels)
        break;
      }
      case HomePage::SIGNAL: {
        char iv[8];
        sprintf(iv, "%d s", SIG_INTERVALS[_task->uiPrefs().sig_interval % NUM_SIG_INTERVALS]);
        m.begin("Signal");
        if (signal_tester.finder) m.add("Stop finder", ACT_SIG_TOGGLE);
        else {
          m.add(signal_tester.running ? "Stop test" : "Start test", ACT_SIG_TOGGLE);
          m.add("Finder", ACT_SIG_FINDER);  // warmer / colder to one repeater
        }
        m.add("Every", ACT_SIG_INTERVAL, iv);
        m.add("Target", ACT_SIG_LOCK, signal_tester.locked ? signal_tester.lock_name : "Best");
        m.add("Clear results", ACT_SIG_CLEAR);
        break;
      }
      case HomePage::RADIO:
        m.begin("Radio");
        m.add("Edit radio", ACT_RADIO_EDIT);
        m.add("RX gain", ACT_RX_GAIN, _task->rxBoost() ? "Boost" : "Normal");  // low power mode: Normal while on
        m.add("Band scan", ACT_BAND_SCAN);
        m.add("Send nearby", ACT_ADVERT);
        m.add("Send to mesh", ACT_ADVERT_MESH);
        break;
      case HomePage::GAMES:
        m.begin("Starlit");
        m.add("Play", ACT_PET_PLAY);
        m.add("Friends", ACT_PET_FRIENDS);  // the Starlits of the nodes heard
        m.add("Starlit menu", ACT_GAMES_MENU);  // the same submenu as Settings > Starlit
        break;
#ifndef UI_NO_HIBERNATE
      case HomePage::SHUTDOWN:
        m.begin("Power");
        m.add("Reboot / off", ACT_POWER_OFF_MENU);  // as in the swipe-down Power menu
        break;
#endif
      default:
        m.begin(pageName(_page));
        break;
    }
  }

private:

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;

  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0),
       _shutdown_init(false), sensors_lpp(200) {  }

#if UI_DISCOVER_SCREEN
  bool sendDiscoverRequest() {
    uint8_t cmd_bytes[6];
    cmd_bytes[0] = CTL_TYPE_NODE_DISCOVER_REQ | 1; // DISCOVER_REQ | prefix only
    cmd_bytes[1] = 0xFF;     // Repeaters
    the_mesh.getRNG()->random((uint8_t *) &disc_node_req_tag, 4);  // generate random tag
    memcpy(&cmd_bytes[2], &disc_node_req_tag, 4);
    disc_nodes_count = 0;
    mesh::Packet* req = the_mesh.createControlData(cmd_bytes, sizeof(cmd_bytes));
    if (req) {
      the_mesh.sendZeroHop(req);
      discovery_req_time = millis();
      return true;
    }
    return false;
  }

  void handleDiscoverResponse(const mesh::Packet* packet) {
    if (disc_nodes_count < DISCOVERED_NODES_TABLE_SIZE && memcmp(&packet->payload[2], &disc_node_req_tag, 4) == 0) {
      auto d = &discovered[disc_nodes_count++];
      memcpy(d->pubkey_prefix, &packet->payload[6], 8);
      d->type = packet->payload[0] & 0xF;
      d->snr_out = ((int8_t)packet->payload[1]) / 4.0;
      d->snr_in = radio_driver.getLastSNR();
      ContactInfo* c = the_mesh.lookupContactByPubKey(&packet->payload[6], 8);
      if (c != NULL) {
        strncpy(d->name, c->name, 32);
      } else {
        d->name[0] = 0;
      }
    }
  }
#endif

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    if (!pageVisible(_page)) _page = HomePage::CLOCK;  // SF32: hidden meanwhile (Settings > Pages)
    if (g_rainbow_theme) {  // SF32: Rainbow theme: this page's own hue (by its place among the shown)
      int n = 0, idx = 0;
      for (int p = 0; p < HomePage::Count; p++) {
        if (!pageVisible(p)) continue;
        if (p == _page) idx = n;
        n++;
      }
      float hue = 330 + idx * 360.0f / (n ? n : 1);
      UIColor::corp_blue = hsvColor(hue, 0.7f, 1.0f);   // THEME_ACCENT
      UIColor::title_bkg = hsvColor(hue, 0.55f, 0.2f);  // THEME_CARD
      UIColor::popup_bkg = hsvColor(hue, 0.5f, 0.3f);
    }
    if (_page == HomePage::CLOCK) return renderClock(display);  // SF32: own full-screen layout

    // SF32: the other pages, restyled in the "MeshCore" theme (same data as ui-new).
    // GPS / sensor pages aren't built for this board and aren't restyled.
    char tmp[80];
    const int W = display.width();
    uint16_t mv = _task->getBattMilliVolts();
    int next_render = 5000;
    if (_page == HomePage::BLUETOOTH && bluetooth_interface.pairingOpen()) next_render = 1000;  // the pairing countdown

    if (_page == HomePage::FIRST) {
      themeHeader(display, "Status", mv);
      themeCard(display, 4, 27, 58, 40);
      themeLabel(display, 9, 31, "Unread");
      display.setTextSize(3);
      display.setColor(_task->getUnreadCount() > 0 ? UIColor::warning_txt : UIColor::primary_txt);
      sprintf(tmp, "%d", _task->getUnreadCount());
      display.setCursor(9, 41);
      display.print(tmp);

      themeCard(display, 66, 27, 58, 40);
      themeLabel(display, 71, 31, "For the app");
      display.setTextSize(3);
      display.setColor(UIColor::primary_txt);
      sprintf(tmp, "%d", _task->getMsgCount());
      display.setCursor(71, 41);
      display.print(tmp);

      themeCard(display, 4, 72, 120, 40);
      themeLabel(display, 9, 76, "Bluetooth");
      display.setTextSize(2);
      display.setCursor(9, 89);
      if (_task->hasConnection()) {
        display.setColor(THEME_ACCENT);
        display.print("Connected");
      } else if (!_task->isBluetoothEnabled()) {
        display.setColor(UIColor::secondary_txt);
        display.print("Off");
      } else if (the_mesh.getBLEPin() != 0 && bluetooth_interface.pairingOpen()) {
        display.setColor(UIColor::warning_txt);
        sprintf(tmp, "Pin %d", the_mesh.getBLEPin());
        display.print(tmp);
        unsigned long left = (bluetooth_interface.pairingLeftMs() + 999) / 1000;
        sprintf(tmp, "pairing %lu:%02lu", left / 60, left % 60);
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextRightAlign(119, 76, tmp);
      } else if (the_mesh.getBLEPin() != 0) {
        display.setColor(UIColor::primary_txt);
        display.print("Waiting");
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextRightAlign(119, 89, "new phone:");  // (beside "Waiting": the label's line has no room)
        display.drawTextRightAlign(119, 98, "Pair phone");
      } else {
        display.setColor(UIColor::primary_txt);
        display.print("Waiting");
      }
    } else if (_page == HomePage::RECENT) {
      themeHeader(display, "Heard", mv);
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      themeCard(display, 4, 27, 120, 88);
      int y = 33, rows = 0;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        if (rows > 0) {
          display.setColor(THEME_DIM);
          display.drawRect(9, y - 6, 110, 1);
        }
        formatAge(tmp, _rtc->getCurrentTime() - a->recv_timestamp);
        display.setTextSize(1);
        int tw = display.getTextWidth(tmp);
        char name[sizeof(a->name)];
        display.translateUTF8ToBlocks(name, a->name, sizeof(name));
        display.setColor(UIColor::primary_txt);
        display.drawTextEllipsized(9, y, 110 - tw - 4, name);
        display.setColor(UIColor::secondary_txt);
        display.drawTextRightAlign(119, y, tmp);
        y += 21;
        rows++;
      }
      if (rows == 0) {
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 62, "Nothing heard yet");
      }
    } else if (_page == HomePage::RADIO) {
      themeHeader(display, "Radio", mv);
      themeCard(display, 4, 27, 120, 28);
      themeLabel(display, 9, 30, "Frequency");
      sprintf(tmp, "%.3f", _node_prefs->freq);
      themeValue(display, 9, 39, tmp, "MHz");

      const char* labels[3] = {"BW kHz", "SF", "CR"};
      char vals[3][12];
      sprintf(vals[0], "%g", _node_prefs->bw);
      sprintf(vals[1], "%d", _node_prefs->sf);
      sprintf(vals[2], "%d", _node_prefs->cr);
      for (int i = 0; i < 3; i++) {
        int x = 4 + i * 41;
        themeCard(display, x, 58, 38, 28);
        themeLabel(display, x + 5, 61, labels[i]);
        display.setTextSize(2);
        display.setColor(UIColor::primary_txt);
        display.setCursor(x + 5, 70);
        display.print(vals[i]);
      }

      themeCard(display, 4, 89, 58, 28);
      themeLabel(display, 9, 92, "TX power");
      sprintf(tmp, "%d", _node_prefs->tx_power_dbm);
      themeValue(display, 9, 101, tmp, "dBm");

      themeCard(display, 66, 89, 58, 28);
      themeLabel(display, 71, 92, "Noise floor");
      sprintf(tmp, "%d", radio_driver.getNoiseFloor());
      themeValue(display, 71, 101, tmp, "dBm");
    } else if (_page == HomePage::BLUETOOTH) {
      themeHeader(display, "Bluetooth", mv);
      bool on = _task->isBluetoothEnabled();
      themeBigIcon(display, on ? ICON_BLUETOOTH : ICON_BLUETOOTH_OFF, on ? bluetooth_on : bluetooth_off, on ? THEME_ACCENT : THEME_DIM);
      display.setTextSize(2);
      if (_task->hasConnection()) {
        display.setColor(THEME_ACCENT);
        display.drawTextCentered(W / 2, 83, "Connected");
      } else {
        display.setColor(on ? UIColor::primary_txt : UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 83, on ? "On" : "Off");
      }
      display.setTextSize(1);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(W / 2, 104, on ? "hold icon: turn off" : "hold icon: turn on");
    } else if (_page == HomePage::ADVERT) {
      themeHeader(display, "Advert", mv);
      themeBigIcon(display, ICON_ADVERT, advert_icon, THEME_ACCENT);
      display.setTextSize(2);
      display.setColor(UIColor::primary_txt);
      display.drawTextCentered(W / 2, 83, "Send advert");
      display.setTextSize(1);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(W / 2, 104, _task->uiPrefs().adv_page_mesh ? "hold icon: send to the mesh" : "hold icon: send nearby");
    } else if (_page == HomePage::BATTERY) {
      themeHeader(display, "Battery", mv);
      bool usb = board.isExternalPowered();
      int pct = mv ? sf32ShownPercent(mv) : 0;
      display.setTextSize(3);
      display.setColor(g_batt_colors && mv ? battColor(pct, UIColor::primary_txt)
                       : usb ? THEME_GOOD : pct <= 15 ? UIColor::warning_txt : UIColor::primary_txt);
      sprintf(tmp, "%d%%", pct);
      display.setCursor(6, 28);
      display.print(tmp);
      display.setTextSize(1);
      display.setColor(UIColor::secondary_txt);
      // the chip's temperature (deg F), then the voltage: "4.28 V  88°F" (the font has no
      // degree sign: a small ring)
      int tf = board.chipTempC() * 9 / 5 + 32;
      char num[8];
      sprintf(num, "%d", tf);
      int nw = display.getTextWidth(num), fw = display.getTextWidth("F");
      int tx = 122 - (nw + 3 + fw);
      display.setCursor(tx, 30);
      display.print(num);
      SF32Display& sdd = (SF32Display&)display;
      float rx = tx + nw + 1.5f;
      for (int k = 0; k < 8; k++) {  // the degree ring, as a stroke (a cleared middle shows as a dot on Glass backdrops)
        float a0 = k * 0.785398f, a1 = (k + 1) * 0.785398f;
        sdd.drawLineAA(rx + 0.8f * cosf(a0), 31.5f + 0.8f * sinf(a0), rx + 0.8f * cosf(a1), 31.5f + 0.8f * sinf(a1), 0.45f);
      }
      display.setCursor(tx + nw + 3, 30);
      display.print("F");
      sprintf(tmp, "%d.%02d V", mv / 1000, (mv % 1000) / 10);
      display.drawTextRightAlign(tx - 5, 30, tmp);
      float rate = batt_history.drainPerHour();
      if (usb) strcpy(tmp, board.chargeState() == 3 || pct >= g_chg_full_pct ? "charged" : "charging");
      else if (rate > 0) {  // until the low-battery power-off (3.55 V, SF32Board.cpp)
        float left = (sf32BatteryPercentF(mv) - sf32BatteryPercentF(3550)) / rate;
        if (left < 0) left = 0;
        int h = (int)left, m = (int)((left - h) * 60);
        if (h >= 48) sprintf(tmp, "~%d days left", (h + 12) / 24);
        else if (h >= 10) sprintf(tmp, "~%d h left", h);
        else sprintf(tmp, "~%dh %02dm left", h, m);
      } else strcpy(tmp, "estimating...");  // the first hour or so off the charger
      display.setColor(usb ? THEME_GOOD : UIColor::secondary_txt);
      display.drawTextRightAlign(122, 41, tmp);
      themeCard(display, 4, 55, 120, 62);
      if (batt_history.count() == 0) {
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 74, "history starts a minute");
        display.drawTextCentered(W / 2, 84, "after power on");
      } else {
        drawBatteryGraph(display, 10, 60, 108, 40, BATT_GRAPH_HOURS[_task->uiPrefs().batt_graph % NUM_BATT_GRAPH]);
      }
    } else if (_page == HomePage::GAMES) {
      unsigned long pet_t0 = millis();
      themeHeader(display, "Starlit", mv);
      bool still = _task->lowPower();
      petPrep(mv, still);
      static unsigned long last_seen = 0, opened = 0;
      static uint16_t seen_gen = 0xFFFF;
      // just opened (a page change, or back on after the screen was off): the hint, and a hello
      bool entered = _page_gen != seen_gen || (!still && millis() - last_seen > 1500);
      seen_gen = _page_gen;
      last_seen = millis();
      if (entered) { opened = millis(); _task->starlitHello(); }  // say hello to any Starlit nearby
      if (_pet_info >= 0) {  // an info card about one of its needs, or its mood (tap: back)
        drawPetInfo(display);
        return 60000;
      }
      bool visit = starlit_meet.visiting();  // a nearby Vesper's Starlit came to see it (ui_meet.inc)
      if (visit) {  // side by side, the visitor bobbing, hearts between them
        pet.setContext(pet.context() & ~(PetGame::CTX_NIGHT | PetGame::CTX_LOWPOWER | PetGame::CTX_LOWBATT));  // a visitor wakes it
        unsigned long va = starlit_meet.visitAge();
        float bob = still ? 0 : fabsf(sinf(va * 6.2832f / 900)) * -3;
        pet.draw(display, 40, 60, 0.82f, still, 0, -1, _node_prefs->path_hash_mode + 1);
        pet.draw(display, 92, 60 + bob, 0.78f, still, starlit_meet.visitSeed(), -1, 0);
        if (!still) pet.drawHearts(display, 66, 52, va);
      } else {
        pet.draw(display, 64, 57, 1.08f, still, 0, -1, _node_prefs->path_hash_mode + 1);  // (its ring: your own hash size)
      }
      // its needs: three small bars
      const PetSave& ps = pet.stats();
      static const char* const L[] = {"Fed", "Curious", "Rested"};
      uint8_t v[3] = {ps.fed, ps.curious, ps.rested};
      for (int k = 0; k < 3; k++) {
        int bx = 6 + k * 41;
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(bx + 18, 90, L[k]);
        display.setColor(THEME_DIM);  // (not the card colour: Glass would make it a frosted pane)
        display.fillRect(bx, 100, 36, 3);
        display.setColor(v[k] < 25 ? UIColor::warning_txt : THEME_ACCENT);
        display.fillRect(bx, 100, (36 * v[k] + 50) / 100, 3);
      }
      display.setColor(THEME_DIM);
      // its kind and mood; the controls for the first few seconds on the page
      char mtxt[64];
      if (visit) {
        char vn[20];
        display.translateUTF8ToBlocks(vn, starlit_meet.visitName(), sizeof(vn));
        snprintf(mtxt, sizeof(mtxt), "%s's Starlit came to visit", vn);
      } else if (millis() - opened < 5000) strcpy(mtxt, "stroke: pet, tap: boop, hold: play");
      else {
        char raw[64];
        petLine(raw, sizeof(raw), still, millis() - opened - 5000);
        display.translateUTF8ToBlocks(mtxt, raw, sizeof(mtxt));  // (node names in it)
      }
      if (display.getTextWidth(mtxt) > W - 8) display.drawTextEllipsized(4, 108, W - 8, mtxt);
      else display.drawTextCentered(W / 2, 108, mtxt);
      // paced from this frame's start (not its end): ~18 fps idle, as fast as it draws (~28) in a reaction
      // (UITask counts a delay of 20 ms or less from the frame's start, more from its end)
      next_render = still ? (visit ? 2000 : 60000) : petDelay(pet_t0, pet.reaction() ? 16 : 55);  // (a visit ends on time)
    } else if (_page == HomePage::NODES) {
      themeHeader(display, "Nodes", mv);
      ContactInfo fav[NODES_MAX];
      int total = my_nodes.favourites(fav, NODES_MAX), shown = total < NODES_MAX ? total : NODES_MAX;
      if (total == 0) {
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 50, "Star contacts as favourites");
        display.drawTextCentered(W / 2, 61, "in the app to list them here");
        display.drawTextCentered(W / 2, 80, "tap one for its card");
      }
      uint32_t now = rtc_clock.getCurrentTime();
      for (int i = 0; i < shown; i++) {
        const ContactInfo& c = fav[i];
        int y = 27 + i * 24;
        themeCard(display, 4, y, 120, 22);
        char name[sizeof(c.name)];
        display.translateUTF8ToBlocks(name, c.name, sizeof(name));
        display.setTextSize(1);
        // right: the battery, as asked
        NodeReading* r = my_nodes.reading(c.id.pub_key, false);
        char bat[24] = "";
        ColorVal bc = UIColor::secondary_txt;
        if (r && r->state == 1) strcpy(bat, "asking...");
        else if (r && r->state == 3) { strcpy(bat, "no reply"); bc = UIColor::warning_txt; }
        else if (r && r->state == 2 && r->mv) {
          int pct = sf32BatteryPercent(r->mv);
          sprintf(bat, "%d.%02dV %d%%", r->mv / 1000, (r->mv % 1000) / 10, pct);
          bc = pct <= 15 ? UIColor::warning_txt : THEME_ACCENT;
        } else if (r && r->state == 2) strcpy(bat, "no battery data");
        else strcpy(bat, ">");  // (tap: its card)
        int bw = display.getTextWidth(bat);
        display.setColor(UIColor::primary_txt);
        display.drawTextEllipsized(8, y + 3, 112 - bw - 4, name);
        display.setColor(bc);
        display.drawTextRightAlign(120, y + 3, bat);
        // second line: last heard, hops, and how old the battery reading is
        char heard[16], line[48];
        if (c.lastmod && now >= c.lastmod) formatAge(heard, now - c.lastmod); else strcpy(heard, "-");
        if (c.out_path_len == OUT_PATH_UNKNOWN) sprintf(line, "heard %s  -  flood", heard);
        else {
          int hops = c.out_path_len & 63;
          if (hops == 0) sprintf(line, "heard %s  -  direct", heard);
          else sprintf(line, "heard %s  -  %d hop%s", heard, hops, hops == 1 ? "" : "s");
        }
        display.setColor(UIColor::secondary_txt);
        display.setCursor(8, y + 12);
        display.print(line);
        if (r && r->state == 2 && r->at && now >= r->at) {
          char age[12];
          formatAge(age, now - r->at);
          display.drawTextRightAlign(120, y + 12, age);
        }
      }
      if (total > shown) {
        char more[32];
        sprintf(more, "+%d more starred", total - shown);
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 27 + shown * 24, more);
      }
      next_render = my_nodes.waiting() ? 1000 : 30000;
    } else if (_page == HomePage::ACTIVITY) {
      themeHeader(display, "Activity", mv);
      // last hour / last 24 h / channel use
      themeCard(display, 4, 27, 38, 28);
      themeLabel(display, 8, 30, "1 hour");
      themeCard(display, 45, 27, 38, 28);
      themeLabel(display, 49, 30, "24 h");
      themeCard(display, 86, 27, 38, 28);
      themeLabel(display, 90, 30, "Busy");
      display.setTextSize(1);
      display.setColor(UIColor::primary_txt);
      sprintf(tmp, "%lu", (unsigned long)mesh_activity.lastHourPackets());
      display.setTextSize(2);
      display.setCursor(8, 40);
      display.print(tmp);
      sprintf(tmp, "%lu", (unsigned long)mesh_activity.dayPackets());
      display.setTextSize(mesh_activity.dayPackets() >= 1000 ? 1 : 2);
      display.setCursor(49, mesh_activity.dayPackets() >= 1000 ? 44 : 40);
      display.print(tmp);
      int busy = mesh_activity.busyPercentNow();
      if (busy < 0) strcpy(tmp, "-");
      else sprintf(tmp, "%d%%", busy);
      display.setTextSize(2);
      display.setColor(busy >= 25 ? THEME_RED : busy >= 10 ? UIColor::warning_txt : UIColor::primary_txt);
      display.setCursor(90, 40);
      display.print(tmp);
      themeCard(display, 4, 58, 120, 60);
      themeLabel(display, 8, 61, "packets per hour");
      drawActivityGraph(display, 10, 73, 108, 30);
      next_render = 30000;
    } else if (_page == HomePage::SIGNAL && signal_tester.finder) {
      // the finder: the target, its signal strength big, warmer / colder
      SignalTester& st = signal_tester;
      themeHeader(display, "Finder", mv);
      display.setTextSize(1);
      display.setColor(UIColor::secondary_txt);
      char nm[40];
      if (st.locked) {
        display.translateUTF8ToBlocks(nm, st.lock_name, sizeof(nm));
        display.drawTextEllipsized(6, 28, 116, nm);
      } else display.drawTextCentered(W / 2, 28, "finding a repeater...");
      if (st.rssi_n > 0) {
        sprintf(tmp, "%d", st.last_rssi);
        display.setTextSize(4);
        display.setColor(st.last_ok ? UIColor::primary_txt : UIColor::secondary_txt);
        int w = display.getTextWidth(tmp);
        display.setCursor((W - w) / 2 - 8, 40);
        display.print(tmp);
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.setCursor((W + w) / 2 - 6, 58);
        display.print("dBm");
        const char* word = st.trend > 0 ? "warmer" : st.trend < 0 ? "colder" : (st.rssi_n >= 3 ? "about the same" : "keep walking");
        display.setTextSize(2);
        display.setColor(st.trend > 0 ? THEME_GOOD : st.trend < 0 ? THEME_RED : UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 76, st.last_ok ? word : "no reply");
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        sprintf(tmp, "best %d dBm  -  SNR %.1f dB", st.best_rssi, st.last_in);
        display.drawTextCentered(W / 2, 98, tmp);
      } else {
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 60, st.pings ? "no reply yet" : "pinging...");
      }
      sprintf(tmp, "every 5 s  -  hold to stop");
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(W / 2, 110, tmp);
      next_render = 1000;
    } else if (_page == HomePage::SIGNAL) {
      themeHeader(display, "Signal", mv);
      SignalTester& st = signal_tester;
      if (!st.running && st.pings == 0 && st.hist_n == 0) {
        themeBigIcon(display, ICON_ADVERT, advert_icon, THEME_ACCENT);
        display.setTextSize(2);
        display.setColor(UIColor::primary_txt);
        display.drawTextCentered(W / 2, 83, "Signal test");
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 104, "hold icon: ping nearby repeaters");
      } else {
        themeCard(display, 4, 27, 58, 28);
        themeLabel(display, 9, 30, "We hear");
        themeCard(display, 66, 27, 58, 28);
        themeLabel(display, 71, 30, "They hear");
        if (st.last_ok) {
          display.setColor(signalColor(st.last_in));
          sprintf(tmp, "%.1f", st.last_in);
          themeValue(display, 9, 39, tmp, "dB");
          display.setColor(signalColor(st.last_out));
          sprintf(tmp, "%.1f", st.last_out);
          themeValue(display, 71, 39, tmp, "dB");
        } else {
          display.setTextSize(2);
          display.setColor(st.pings ? UIColor::warning_txt : UIColor::secondary_txt);
          display.setCursor(9, 39);
          display.print("--");
          display.setCursor(71, 39);
          display.print("--");
        }
        // who answered, and how many
        display.setTextSize(1);
        display.setColor(UIColor::primary_txt);
        if (st.pings == 0) strcpy(tmp, "pinging...");
        else if (st.last_ok) {
          char filtered[32];
          display.translateUTF8ToBlocks(filtered, st.last_name, sizeof(filtered));
          snprintf(tmp, sizeof(tmp), "%s%s", st.locked ? "> " : "", filtered);
        } else strcpy(tmp, st.locked ? "no reply from target" : "no reply");
        display.drawTextEllipsized(6, 59, 80, tmp);
        display.setColor(UIColor::secondary_txt);
        sprintf(tmp, "%u/%u", st.replies, st.pings);
        display.drawTextRightAlign(122, 59, tmp);
        themeCard(display, 4, 70, 120, 34);
        drawSignalGraph(display, 8, 73, 112, 28);
        display.setColor(UIColor::secondary_txt);
        if (st.running) sprintf(tmp, "next ping in %d s  -  hold to stop", st.secsToNextPing());
        else strcpy(tmp, "stopped  -  hold to start");
        display.drawTextCentered(W / 2, 108, tmp);
        if (st.running) next_render = 1000;
      }
    } else if (_page == HomePage::BOT) {
      themeHeader(display, "Bot", mv);
      bool on = _task->uiPrefs().bot_on;
      bool paused = on && !_task->botOn();  // low power mode: it doesn't reply (its setting stays)
      themeBigIcon(display, ICON_BOT, bot_icon, on && !paused ? THEME_ACCENT : THEME_DIM);
      display.setTextSize(2);
      display.setColor(on && !paused ? UIColor::primary_txt : UIColor::secondary_txt);
      display.drawTextCentered(W / 2, 83, paused ? "Paused" : on ? "On" : "Off");
      display.setTextSize(1);
      display.setColor(UIColor::secondary_txt);
      if (on && !paused && _task->botCount() > 0) {
        char filtered[32];
        display.translateUTF8ToBlocks(filtered, _task->botLastName(), sizeof(filtered));
        int heard = _task->botLastHeard();  // a channel reply: did a repeater pass it on?
        char hs[16] = "";
        if (heard == -1) strcpy(hs, " (...)");
        else if (heard == 0) strcpy(hs, " (not heard)");
        else if (heard > 0) sprintf(hs, " (heard %dx)", heard);
        int fl = strlen(filtered), hl = strlen(hs);
        if (hl) {  // keep the result whole: the name gives way
          int keep = (int)sizeof(filtered) - 1 - hl;
          if (fl > 9) fl = 9;
          if (fl > keep) fl = keep;
          memcpy(filtered + fl, hs, hl + 1);
          tmp[0] = 0;  // the line is the last reply and its result ("Gz1 (heard 2x)")
        } else {
          sprintf(tmp, "%u sent, last: ", _task->botCount());
        }
        int tw = display.getTextWidth(tmp);
        int nw = display.getTextWidth(filtered);
        if (tw + nw > 116) nw = 116 - tw;
        int x = (W - tw - nw) / 2;
        display.setCursor(x, 99);
        display.print(tmp);
        display.setColor(UIColor::primary_txt);
        display.drawTextEllipsized(x + tw, 99, nw, filtered);
      } else {
        display.drawTextCentered(W / 2, 99, paused ? "in low power mode" : on ? "hold icon: turn off" : "hold icon: turn on");
      }
      {  // the mesh's health: the nodes whose path-hash size is known
        int multi, one, nudges;
        hash_sizes.counts(multi, one, nudges);
        if (multi + one > 0) {
          if (nudges) sprintf(tmp, "2-byte: %d  1-byte: %d  nudged: %d", multi, one, nudges);
          else sprintf(tmp, "known: %d on 2-byte, %d on 1-byte", multi, one);
          display.setColor(one ? UIColor::warning_txt : THEME_DIM);
          display.drawTextCentered(W / 2, 108, tmp);
        }
      }
#if UI_DISCOVER_SCREEN
    } else if (_page == HomePage::DISCOVERY) {
      themeHeader(display, "Discover", mv);
      int count = disc_nodes_count;
      bool scanning = millis() < discovery_req_time + 5000;
      if (count > 0) {
        themeCard(display, 4, 27, 120, 88);
        int y = 33;
        for (int i = 0; i < count; i++, y += 21) {
          char name[32];
          auto a = &discovered[i];
          if ((a->name[0] == 0) || !discovery_disp_names) {
            mesh::Utils::toHex(name, a->pubkey_prefix, 4);
          } else {
            strncpy(name, a->name, 32);
          }
          if (i > 0) {
            display.setColor(THEME_DIM);
            display.drawRect(9, y - 6, 110, 1);
          }
          char snr_s[16];
          sprintf(snr_s, "%.1f / %.1f", a->snr_out, a->snr_in);
          display.setTextSize(1);
          int sw = display.getTextWidth(snr_s);
          char filtered[sizeof(name)];
          display.translateUTF8ToBlocks(filtered, name, sizeof(filtered));
          display.setColor(UIColor::primary_txt);
          display.drawTextEllipsized(9, y, 110 - sw - 4, filtered);
          display.setColor(UIColor::secondary_txt);
          display.drawTextRightAlign(119, y, snr_s);
        }
      } else {
        themeBigIcon(display, ICON_ADVERT, advert_icon, scanning ? UIColor::warning_txt : THEME_ACCENT);
        display.setTextSize(2);
        display.setColor(scanning ? UIColor::warning_txt : UIColor::primary_txt);
        display.drawTextCentered(W / 2, 83, scanning ? "Scanning..." : "Repeaters");
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 104, scanning ? "listening for replies" : "hold icon: scan nearby");
      }
      if (scanning) next_render = 1000;  // more frequent updates just after the request
#endif
#ifndef UI_NO_HIBERNATE
    } else if (_page == HomePage::SHUTDOWN) {
      themeHeader(display, "Power", mv);
      if (_shutdown_init) {
        display.setTextSize(2);
        display.setColor(UIColor::warning_txt);
        display.drawTextCentered(W / 2, 60, "Powering off...");
      } else {
        themeBigIcon(display, ICON_POWER, power_icon, UIColor::secondary_txt);
        display.setTextSize(2);
        display.setColor(UIColor::primary_txt);
        display.drawTextCentered(W / 2, 83, "Power off");
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(W / 2, 104, "hold icon: power options");
      }
#endif
    }
    drawPageDots(display);
    return next_render;
  }

  bool handleInput(char c) override {
    if (c == KEY_LEFT || c == KEY_PREV) {
      stepPage(-1);  // SF32: skipping hidden pages
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      stepPage(1);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::CLOCK) {  // SF32: long press: next clock face
      nextFace();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isBluetoothEnabled()) {  // toggle Bluetooth on/off
        _task->disableBluetooth();
      } else {
        _task->enableBluetooth();
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::SIGNAL) {
      _task->menuAction(ACT_SIG_TOGGLE);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::BOT) {
      _task->menuAction(ACT_BOT_TOGGLE);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      bool mesh_wide = _task->uiPrefs().adv_page_mesh;
      if (!_task->sendAdvert(mesh_wide)) _task->showAlert("Advert failed", 1000);
      else { _task->showAlert(mesh_wide ? "Advert sent to mesh" : "Advert sent nearby", 1200); _task->eventSound(SF32Sound::CHIME_ADVERT); }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
#if UI_DISCOVER_SCREEN
    if (c == KEY_ENTER && _page == HomePage::DISCOVERY) {
      if (millis() > discovery_req_time + 5000) { // rate limiter
        sendDiscoverRequest();
      }
      return true;
    }
    if (c == KEY_SELECT && _page == HomePage::DISCOVERY) {
      discovery_disp_names = !discovery_disp_names;
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::GAMES) {  // hold the pet (or the button's long press): play
      pet.play();
      _task->petSound(2);
      return true;
    }
#ifndef UI_NO_HIBERNATE
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _task->openMenu(UITask::MENU_POWER);  // the button's long press as the icon's hold: the Power menu
      return true;
    }
#endif
    return false;
  }
};

#ifndef UI_MSG_PREVIEW_SIZE
  #define UI_MSG_PREVIEW_SIZE 78
#endif
