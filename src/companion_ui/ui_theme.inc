// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// The "MeshCore" theme: colours, cards, header, values, scrolling labels, page dots.

// ---- the layout's width ----
// Screens are laid out in UI pixels, 128 x 128 on the SF32. The T-Deck's are wider (170 x 128,
// SF32Display::W), so x positions are written by what they hang from: the left (x as it is),
// the middle (UI_CX, UI_CX - 20) or the right (UI_RX(124): 4 from the right edge, = 124 on the
// SF32). On the SF32 they're the same numbers as before.
#ifdef VESPER_TDECK
#define UI_BOARD "T-Deck"
#else
#define UI_BOARD "T-Display-SF32"  // the board, as the UI names it (About, splash)
#endif
#define UI_W  SF32Display::W
#define UI_CX (UI_W / 2)
#define UI_RX(x) (UI_W - (128 - (x)))
// cards side by side, 4 from the edges and 4 apart (3 for three): the SF32's 58 / 38 wide
#define UI_HALF_W  ((UI_W - 12) / 2)
#define UI_C2      (8 + UI_HALF_W)                  // the second of two (66)
#define UI_THIRD_W ((UI_W - 14) / 3)
#define UI_C3(k)   (4 + (k) * (UI_THIRD_W + 3))     // the k-th of three (4, 45, 86)

// ---- SF32 "MeshCore" theme (all screens use the full-panel layout: UI_W x 128) ----
// Page title + blue accent bar + compact battery on top, rounded slate cards for content,
// page dots at the bottom. Colours: UIColor in SF32Display.cpp.
#define THEME_CARD   UIColor::title_bkg
#define THEME_ACCENT UIColor::corp_blue
static const ColorVal THEME_DIM = ((0x4B & 0xF8) << 8) | ((0x55 & 0xFC) << 3) | (0x63 >> 3);
// hue (degrees), saturation, value (0..1) -> RGB565 (the Color clock face, Rainbow theme)
static ColorVal hsvColor(float h, float s, float v) {
  h = fmodf(h, 360.0f);
  if (h < 0) h += 360.0f;
  float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c, r, g, b;
  if (h < 60) { r = c; g = x; b = 0; } else if (h < 120) { r = x; g = c; b = 0; }
  else if (h < 180) { r = 0; g = c; b = x; } else if (h < 240) { r = 0; g = x; b = c; }
  else if (h < 300) { r = x; g = 0; b = c; } else { r = c; g = 0; b = x; }
  int R = (int)((r + m) * 255), G = (int)((g + m) * 255), B = (int)((b + m) * 255);
  return (ColorVal)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
}
// the Rainbow theme is on: each home page gets its own hue (HomeScreen::render)
static bool g_rainbow_theme = false;

// The Color clock face's ring and digits: the theme's own colours (applyTheme sets the stops;
// Rainbow: none, the whole spectrum as always). deg as the rainbow used it: 0..360 around.
static ColorVal g_ring_stops[3];
static int g_ring_nstops = 0;
static ColorVal mix565(ColorVal a, ColorVal b, float t) {
  int ar = a >> 11, ag = (a >> 5) & 0x3F, ab = a & 0x1F, br = b >> 11, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
  return (ColorVal)((r << 11) | (g << 5) | bl);
}
static ColorVal ringColor(float deg, float sat, float val) {
  if (!g_ring_nstops) return hsvColor(deg, sat, val);
  float t = fmodf(deg, 360.0f);
  if (t < 0) t += 360.0f;
  t = t / 360.0f * g_ring_nstops;  // round the stops and back to the first
  int k = (int)t;
  float f = (t - k - 0.65f) / 0.35f;  // each colour solid for most of its part, a short blend into the next
  f = f < 0 ? 0 : (f > 1 ? 1 : f);  // (a long blend between far-apart colours goes muddy)
  ColorVal c = mix565(g_ring_stops[k % g_ring_nstops], g_ring_stops[(k + 1) % g_ring_nstops], f);
  return val < 0.99f ? mix565(0, c, val) : c;  // (the unlit dots: dimmed)
}
// the stops for a theme: its accent and colours either side of its hue (greys for a grey accent)
static void setRingTheme(bool rainbow, ColorVal accent, ColorVal second, ColorVal third) {
  g_ring_nstops = 0;
  if (rainbow) return;
  g_ring_nstops = 3;
  if (second && third) {  // a theme with its own extra colours (ChattaMesh: gold, green)
    g_ring_stops[0] = accent; g_ring_stops[1] = second; g_ring_stops[2] = third;
    return;
  }
  float r = (accent >> 11) / 31.0f, g = ((accent >> 5) & 0x3F) / 63.0f, b = (accent & 0x1F) / 31.0f;
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn, h = 0;
  if (mx <= 0 || d / mx < 0.15f) {  // grey (Mono): light and darker greys
    g_ring_stops[0] = accent; g_ring_stops[1] = mix565(0, accent, 0.55f); g_ring_stops[2] = mix565(accent, 0xFFFF, 0.5f);
    return;
  }
  if (mx == r) h = 60 * fmodf((g - b) / d, 6); else if (mx == g) h = 60 * ((b - r) / d + 2); else h = 60 * ((r - g) / d + 4);
  g_ring_stops[0] = hsvColor(h - 28, 0.8f, 1); g_ring_stops[1] = accent; g_ring_stops[2] = hsvColor(h + 28, 0.8f, 1);
}

static const ColorVal THEME_GOOD_DEFAULT = ((0x34 & 0xF8) << 8) | ((0xD3 & 0xFC) << 3) | (0x99 >> 3);
static ColorVal THEME_GOOD = THEME_GOOD_DEFAULT;  // charging, strong signal (a theme may set its own: applyTheme)
static const ColorVal THEME_RED = ((0xEF & 0xF8) << 8) | ((0x44 & 0xFC) << 3) | (0x44 >> 3);   // SOS
static const ColorVal THEME_YELLOW = ((0xFA & 0xF8) << 8) | ((0xCC & 0xFC) << 3) | (0x15 >> 3);
static const ColorVal THEME_GREEN = ((0x30 & 0xF8) << 8) | ((0xD1 & 0xFC) << 3) | (0x58 >> 3);  // true green (THEME_GOOD is teal)

// Settings > Theme > Battery colors: the battery icon and % coloured by the charge left
static bool g_batt_colors = false;
static bool g_batt_icon = true;  // Settings > Battery > Show: false = the % only
static ColorVal battColor(int pct, ColorVal normal) {
  if (!g_batt_colors) return pct <= 15 ? UIColor::warning_txt : normal;
  return pct <= 20 ? THEME_RED : pct <= 50 ? THEME_YELLOW : THEME_GREEN;
}

// Li-ion open-circuit voltage -> charge, roughly (better than linear at the ends).
// charge left from the battery voltage (resting LiPo curve), fractional (time-left estimates)
static float sf32BatteryPercentF(uint16_t mv) {
  static const uint16_t curve[][2] = {{4180, 100}, {4100, 90}, {4000, 79}, {3900, 66}, {3800, 52},
                                      {3750, 42}, {3700, 31}, {3650, 21}, {3600, 13}, {3500, 6}, {3300, 0}};
  if (mv >= curve[0][0]) return 100;
  for (unsigned i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
    if (mv >= curve[i][0]) {
      return curve[i][1] + (float)(mv - curve[i][0]) * (curve[i - 1][1] - curve[i][1]) / (curve[i - 1][0] - curve[i][0]);
    }
  }
  return 0;
}
static int sf32BatteryPercent(uint16_t mv) { return (int)sf32BatteryPercentF(mv); }
// This board's own charge: 0 % at its low-battery power-off (3550 mV, ~9.5 % on the curve
// above), not at the cell's 3.3 V. (Other nodes' batteries keep the plain curve.)
static float sf32OwnPercentF(uint16_t mv) {
  static const float empty = sf32BatteryPercentF(SF32_BATT_EMPTY_MV);
  float p = (sf32BatteryPercentF(mv) - empty) * 100.0f / (100.0f - empty);
  return p < 0 ? 0 : p;
}

// The charge shown for this board's own battery (header, Battery page, warnings, log).
// On battery it's the voltage. While charging the charger lifts the reading ~0.33 V (measured:
// 3.87 V on battery read 4.21 V minutes after plugging in), so the voltage alone would jump
// from 60 % to 85+ % on plugging in: instead it carries on
// from the last reading before USB and climbs at a realistic pace (charger ~200 mA into
// 480 mAh, slower near full), pulled up only by the voltage minus that lift; 100 % only
// when the charger says it's done. Just after unplugging the voltage still reads high:
// for half an hour it's never shown above what it was.
#define CHG_LIFT_MV        330
#define CHG_RATE_PCT_H     35.0f   // below 80 %
#define CHG_RATE_TOP_PCT_H 15.0f   // 80 % and up (the charger tapers)
#define CHG_SETTLE_MS      (30UL * 60 * 1000)
// Settings > Battery > Charge to: the limits, their charger voltage, and the % shown when done
static const uint8_t CHG_LIMITS[] = {100, 90, 80};
static const uint16_t CHG_LIMIT_MV[] = {4200, 4095, 3990};  // on the charger's 15 mV steps
#define NUM_CHG_LIMITS 3
static int g_chg_full_pct = 100;
// Settings > Bot: replies a day in all, how soon one sender can get another, the least time
// between any two replies (ui_bot.inc)
static const uint8_t BOT_MAX_DAY[] = {5, 10, 20, 50};
static const uint8_t BOT_REPEAT_H[] = {12, 24, 168};
static const uint8_t BOT_GAP_MIN[] = {1, 5, 15};
// Settings > Battery > Low alert: the warning level (0 off); the second, "charge soon"
// alert comes at 10 % (5 % when the first is at 10 %)
static const uint8_t BATT_WARN_LEVELS[] = {0, 10, 15, 20, 30};
#define NUM_BATT_WARN 5
static const uint8_t LP_AUTO_LEVELS[] = {0, 10, 15, 20, 30};  // low power mode on by itself at this % (0: never)
#define NUM_LP_AUTO 5
static int sf32ShownPercent(uint16_t mv) {
  static float est = -1;  // -1: none yet
  static unsigned long last = 0, unplugged_at = 0;
  static bool was_usb = false;
  if (mv == 0) return 0;
  unsigned long now = millis();
  float dt_h = last ? (now - last) / 3600000.0f : 0;
  last = now;
  bool usb = board.isExternalPowered();
  if (usb) {
    float floor = sf32OwnPercentF(mv > CHG_LIFT_MV ? mv - CHG_LIFT_MV : 0);
    if (est < 0) est = floor;  // started on USB: nothing better to go on
    uint8_t st = board.chargeState();
    if (st == 3) est = floor > g_chg_full_pct ? floor : g_chg_full_pct;  // charge done (to the limit, or above it if lowered since)
    else if (st == 1 || st == 2) {
      // after a full charge the charger tops up now and then: stay at the top through it
      float cap = est >= g_chg_full_pct ? g_chg_full_pct : g_chg_full_pct - 1;
      while (dt_h > 0) {       // (in steps: the pace changes at 80 %)
        float step = dt_h > 0.05f ? 0.05f : dt_h;
        est += step * (est < 80 ? CHG_RATE_PCT_H : CHG_RATE_TOP_PCT_H);
        dt_h -= step;
      }
      if (floor > est) est = floor;
      if (est > cap) est = cap;
    }
  } else {
    if (was_usb) unplugged_at = now | 1;
    float volt = sf32OwnPercentF(mv);
    if (est >= 0 && unplugged_at && now - unplugged_at < CHG_SETTLE_MS) { if (volt < est) est = volt; }
    else { est = volt; unplugged_at = 0; }
  }
  was_usb = usb;
  return (int)est;
}

// ---- icons in the Glass style: drawn as smooth strokes, the big ones on a round pane ----
enum ThemeIcon { ICON_ADVERT, ICON_POWER, ICON_BLUETOOTH, ICON_BLUETOOTH_OFF, ICON_BOT };

static void iconArc(SF32Display& d, float cx, float cy, float r, float from_deg, float to_deg, float width) {
  const int N = 18;
  float px = 0, py = 0;
  for (int i = 0; i <= N; i++) {
    float a = (from_deg + (to_deg - from_deg) * i / N) * 0.0174533f;
    float x = cx + sinf(a) * r, y = cy - cosf(a) * r;
    if (i) d.drawLineAA(px, py, x, y, width);
    px = x; py = y;
  }
}

// The icons used big on a page: a pane and strokes in Glass, the bitmap otherwise. In the
// colour given; greys would be lost on glass, so they go a step lighter.
static void themeBigIcon(DisplayDriver& display, ThemeIcon icon, const uint8_t* bits, ColorVal color) {
  SF32Display& d = (SF32Display&)display;
  if (!d.glassOn()) {
    display.setColor(color);
    d.drawXbmScaled(UI_CX - 24, 30, bits, 32, 32, 1.5f);
    return;
  }
  const float cx = UI_CX, cy = 54;
  d.glassBadge(UI_CX - 26, 28, 52);
  display.setColor(color == UIColor::secondary_txt ? UIColor::primary_txt : (color == THEME_DIM ? UIColor::secondary_txt : color));
  switch (icon) {
    case ICON_ADVERT:
      d.drawLineAA(cx, cy, cx, cy, 6.5f);
      iconArc(d, cx, cy, 10, 52, 128, 2.6f);
      iconArc(d, cx, cy, 10, -52, -128, 2.6f);
      iconArc(d, cx, cy, 17, 56, 124, 2.6f);
      iconArc(d, cx, cy, 17, -56, -124, 2.6f);
      break;
    case ICON_POWER:
      iconArc(d, cx, cy + 1, 13, 38, 322, 3);
      d.drawLineAA(cx, cy - 15, cx, cy - 2, 3);
      break;
    case ICON_BLUETOOTH_OFF:
      d.drawLineAA(cx - 13, cy - 13, cx + 13, cy + 13, 2.6f);
      // fall through: the same symbol, struck out
    case ICON_BLUETOOTH:
      d.drawLineAA(cx - 8, cy - 8, cx + 8, cy + 8, 2.8f);
      d.drawLineAA(cx + 8, cy + 8, cx, cy + 16, 2.8f);
      d.drawLineAA(cx, cy + 16, cx, cy - 16, 2.8f);
      d.drawLineAA(cx, cy - 16, cx + 8, cy - 8, 2.8f);
      d.drawLineAA(cx + 8, cy - 8, cx - 8, cy + 8, 2.8f);
      break;
    case ICON_BOT:
      d.drawLineAA(cx - 12, cy - 6, cx + 12, cy - 6, 2.6f);
      d.drawLineAA(cx + 12, cy - 6, cx + 12, cy + 12, 2.6f);
      d.drawLineAA(cx + 12, cy + 12, cx - 12, cy + 12, 2.6f);
      d.drawLineAA(cx - 12, cy + 12, cx - 12, cy - 6, 2.6f);
      d.drawLineAA(cx, cy - 6, cx, cy - 13, 2.6f);
      d.drawLineAA(cx, cy - 15, cx, cy - 15, 4.5f);
      d.drawLineAA(cx - 5, cy + 1, cx - 5, cy + 1, 4.5f);
      d.drawLineAA(cx + 5, cy + 1, cx + 5, cy + 1, 4.5f);
      d.drawLineAA(cx - 4, cy + 7, cx + 4, cy + 7, 2);
      break;
  }
}

// The small charging bolt (8 x 8 at x, y) and the unread envelope (16 x 12): smooth in Glass.
static void themeBolt(DisplayDriver& display, int x, int y) {
  SF32Display& d = (SF32Display&)display;
  if (!d.glassOn()) { display.drawXbm(x, y, charging_icon, 8, 8); return; }
  d.drawLineAA(x + 5.2f, y + 0.3f, x + 1.6f, y + 4.4f, 1.5f);
  d.drawLineAA(x + 1.6f, y + 4.4f, x + 6.2f, y + 3.6f, 1.5f);
  d.drawLineAA(x + 6.2f, y + 3.6f, x + 2.6f, y + 7.7f, 1.5f);
}
// The status row: small icons in one style, always just left of the battery's % (clock and
// page headers), most important nearest the battery. Set from UITask before every frame.
enum { ST_USB = 1, ST_LOWPOWER = 2, ST_DND = 4, ST_MAIL = 8, ST_BT = 16, ST_SOS = 32 };  // (SOS: a red chip, first)
static uint8_t g_status = 0;
static void themeLeaf(DisplayDriver& display, int x, int y) {  // 8x8, from its stem (bottom left) to its tip
  SF32Display& d = (SF32Display&)display;
  d.setColor(THEME_GOOD);
  const float x0 = x + 1.0f, y0 = y + 7.4f, x1 = x + 7.6f, y1 = y + 0.6f;
  for (int i = 0; i < 6; i++) {  // tapered strokes along the midrib: a filled leaf
    float a = i / 6.0f, b = (i + 1) / 6.0f, m = (a + b) / 2;
    d.drawLineAA(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a, x0 + (x1 - x0) * b, y0 + (y1 - y0) * b, 0.6f + 3.4f * sinf(m * 3.1416f));
  }
  d.drawLineAA(x0 - 0.6f, y0 + 0.6f, x0 + 0.8f, y0 - 0.8f, 1.0f);  // the stem
}
static void themeMoon(DisplayDriver& display, int x, int y) {  // 8x8: a crescent (night hours: do not disturb)
  SF32Display& d = (SF32Display&)display;
  d.setColor(THEME_ACCENT);
  const float cx = x + 4.6f, cy = y + 4.0f, r = 3.2f;
  for (int i = 0; i < 7; i++) {  // tapered strokes round the left of a circle: thick in the middle
    float a0 = 1.75f + i * 0.4f, a1 = a0 + 0.4f, m = (i + 0.5f) / 7;
    d.drawLineAA(cx + cosf(a0) * r, cy - sinf(a0) * r, cx + cosf(a1) * r, cy - sinf(a1) * r, 0.7f + 2.0f * sinf(m * 3.1416f));
  }
}
static void themeMail(DisplayDriver& display, int x, int y) {  // 8x8: a small envelope (unread messages)
  SF32Display& d = (SF32Display&)display;
  d.setColor(UIColor::warning_txt);
  float x0 = x + 0.5f, x1 = x + 7.5f, y0 = y + 1.5f, y1 = y + 6.5f;
  d.drawLineAA(x0, y0, x1, y0, 1.0f); d.drawLineAA(x1, y0, x1, y1, 1.0f);
  d.drawLineAA(x1, y1, x0, y1, 1.0f); d.drawLineAA(x0, y1, x0, y0, 1.0f);
  d.drawLineAA(x0, y0, x + 4, y + 4.4f, 1.0f); d.drawLineAA(x + 4, y + 4.4f, x1, y0, 1.0f);
}
static void themeBluetooth(DisplayDriver& display, int x, int y) {  // 8x8: the Bluetooth rune (a phone connected)
  SF32Display& d = (SF32Display&)display;
  d.setColor(hsvColor(212, 0.55f, 1.0f));
  const float m = x + 3.8f;
  d.drawLineAA(m, y + 0.4f, m, y + 7.6f, 0.9f);
  d.drawLineAA(x + 1.0f, y + 2.2f, x + 6.2f, y + 5.6f, 0.9f); d.drawLineAA(x + 6.2f, y + 5.6f, m, y + 7.6f, 0.9f);
  d.drawLineAA(x + 1.0f, y + 5.8f, x + 6.2f, y + 2.4f, 0.9f); d.drawLineAA(x + 6.2f, y + 2.4f, m, y + 0.4f, 0.9f);
}
// The status icons, right to left, the rightmost ending at `right`, as many as fit down to min_x;
// returns where they end. Order: USB, low power, night (these three come before the battery's %
// when space is short), then mail and Bluetooth (after it).
static const int STATUS_STEP = 11;
static const int SOS_CHIP_W = 24;
static int themeStatusWidth(uint8_t bits) { int n = 0; for (int b = 0; b < 5; b++) if (bits & (1 << b)) n++; return n * STATUS_STEP + ((bits & ST_SOS) ? SOS_CHIP_W : 0); }
static int themeStatus(DisplayDriver& d, int right, int y, int min_x, uint8_t bits) {
  int x = right;
  if (bits & ST_SOS) {  // an SOS going on (it runs in the background): tap it to see it
    d.setColor(THEME_RED);
    d.fillRect(x - SOS_CHIP_W + 2, y - 1, SOS_CHIP_W - 3, 11);
    d.setTextSize(1);
    d.setColor(0xFFFF);
    d.drawTextCentered(x - SOS_CHIP_W / 2 + 1, y, "SOS");
    x -= SOS_CHIP_W;
  }
  for (int b = 0; b < 5; b++) {
    if (!(bits & (1 << b))) continue;
    if (x - (STATUS_STEP - 3) < min_x) break;  // no room: the rest (less important) are left out
    int ix = x - 8;
    x -= STATUS_STEP;
    if (b == 0) { d.setColor(UIColor::warning_txt); themeBolt(d, ix, y); }
    else if (b == 1) themeLeaf(d, ix, y);
    else if (b == 2) themeMoon(d, ix, y);
    else if (b == 3) themeMail(d, ix, y);
    else themeBluetooth(d, ix, y);
  }
  return x;
}
static void themeEnvelope(DisplayDriver& display, int x, int y, const uint8_t* envelope) {
  SF32Display& d = (SF32Display&)display;
  if (!d.glassOn()) { display.drawXbm(x, y, envelope, 16, 12); return; }
  float x0 = x + 1, x1 = x + 15, y0 = y + 1, y1 = y + 11;
  d.drawLineAA(x0, y0, x1, y0, 1.3f); d.drawLineAA(x1, y0, x1, y1, 1.3f);
  d.drawLineAA(x1, y1, x0, y1, 1.3f); d.drawLineAA(x0, y1, x0, y0, 1.3f);
  d.drawLineAA(x0, y0, x + 8, y + 7, 1.3f); d.drawLineAA(x + 8, y + 7, x1, y0, 1.3f);
}

static void themeCard(DisplayDriver& d, int x, int y, int w, int h) {
  d.setColor(THEME_CARD);
  d.fillRect(x, y, w, h);
}

// small grey label above a value
static void themeLabel(DisplayDriver& d, int x, int y, const char* text) {
  d.setTextSize(1);
  d.setColor(UIColor::secondary_txt);
  d.setCursor(x, y);
  d.print(text);
}

static void themeHeader(DisplayDriver& d, const char* title, uint16_t mv) {
  d.setTextSize(2);
  d.setColor(UIColor::primary_txt);
  d.setCursor(4, 3);
  d.print(title);
  d.setColor(THEME_ACCENT);
  d.fillRect(4, 21, 12, 2);

  // compact battery: [icon] with % to its left, bolt on USB power
  int bx = g_batt_icon ? UI_W - 4 - 2 - 14 : UI_W - 4 + 3, by = 7;  // no icon: the % ends at the right margin
  char tmp[8];
  int pct = mv > 0 ? sf32ShownPercent(mv) : -1;
  ColorVal outline = pct >= 0 && g_batt_colors ? battColor(pct, UIColor::secondary_txt) : UIColor::secondary_txt;
  d.setColor(outline);
  if (g_batt_icon) {
    d.drawRect(bx, by, 14, 8);
    d.fillRect(bx + 14, by + 2, 2, 4);
  }
  if (pct >= 0) {
    d.setColor(battColor(pct, UIColor::secondary_txt));
    if (g_batt_icon) d.fillRect(bx + 2, by + 2, (10 * pct + 50) / 100, 4);
    sprintf(tmp, "%d%%", pct);
  } else {
    strcpy(tmp, "--");
  }
  // the % and the status icons, as far as they fit next to the title: the % goes first
  d.setTextSize(2);
  int title_end = 4 + d.getTextWidth(title) + 4;
  d.setTextSize(1);
  int tw = d.getTextWidth(tmp), right = bx - 3;
  if (right - tw - themeStatusWidth(g_status & (ST_USB | ST_LOWPOWER | ST_DND)) >= title_end) {  // mail, Bluetooth: if room
    d.setColor(pct >= 0 ? battColor(pct, UIColor::secondary_txt) : outline);  // low shows even without the icon
    d.setCursor(right - tw, by);
    d.print(tmp);
    right -= tw + 3;
  }
  themeStatus(d, right, by, title_end, g_status);
}

// a size-2 value with a small unit after it, on the same baseline
static void themeValue(DisplayDriver& d, int x, int y, const char* value, const char* unit = nullptr) {
  d.setTextSize(2);
  d.setColor(UIColor::primary_txt);
  d.setCursor(x, y);
  d.print(value);
  if (unit) {
    int w = d.getTextWidth(value);
    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    d.setCursor(x + w + 2, y + 6);
    d.print(unit);
  }
}

// Text in a box of width w: as is if it fits, otherwise scrolling left and back inside the
// box (clipped): 1.2 s pause, ~22 px/s, 1.2 s pause, jump back. Uses the current text
// size and colour. Returns true while it needs redrawing to animate.
// A label in a box w wide: if it's too long, it scrolls sideways (pause, scroll, pause).
// Returns 0 if it fits, else the ms until it next moves (redraw then).
static int drawMarquee(DisplayDriver& d, int x, int y, int w, const char* text) {
  int tw = d.getTextWidth(text);
  if (tw <= w) {
    d.setCursor(x, y);
    d.print(text);
    return 0;
  }
  const unsigned long PAUSE = 1200;
  const float SPEED = 22.0f / 1000;  // logical px per ms
  float overflow = tw - w + 2;
  unsigned long scroll_ms = (unsigned long)(overflow / SPEED);
  unsigned long t = millis() % (2 * PAUSE + scroll_ms);
  float off = t < PAUSE ? 0 : (t < PAUSE + scroll_ms ? (t - PAUSE) * SPEED : overflow);
  SF32Display& sd = (SF32Display&)d;
  sd.pushClip(x, y - 3, w, 8 * 3 + 6);
  sd.setCursorFine(x - off, y);
  d.print(text);
  sd.popClip();
  if (t < PAUSE) return PAUSE - t;                                   // holding at the start
  if (t >= PAUSE + scroll_ms) return 2 * PAUSE + scroll_ms - t;      // holding at the end
  return 30;                                                         // scrolling
}

static void themeDots(DisplayDriver& d, int page, int count, int y = 121) {
  // 10 apart, closer with many pages: the row stays within ~92 of the 128 (clear of the edges)
  int step = count > 1 && 92 / (count - 1) < 10 ? 92 / (count - 1) : 10;
  int x = d.width() / 2 - step * (count - 1) / 2;
  for (int i = 0; i < count; i++, x += step) {
    if (i == page) {
      d.setColor(THEME_ACCENT);
      d.fillRect(x - 1, y - 1, 4, 4);
    } else {
      d.setColor(THEME_DIM);
      d.fillRect(x, y, 2, 2);
    }
  }
}

static void formatAge(char* buf, int secs) {
  if (secs < 60) sprintf(buf, "%ds", secs < 0 ? 0 : secs);
  else if (secs < 60 * 60) sprintf(buf, "%dm", secs / 60);
  else if (secs < 48 * 60 * 60) sprintf(buf, "%dh", secs / (60 * 60));
  else sprintf(buf, "%dd", secs / (24 * 60 * 60));
}
