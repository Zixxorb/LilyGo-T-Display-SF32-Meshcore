// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// The "MeshCore" theme: colours, cards, header, values, scrolling labels, page dots.

// ---- SF32 "MeshCore" theme (all screens use the square 128x128 layout) ----
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

static const ColorVal THEME_GOOD = ((0x34 & 0xF8) << 8) | ((0xD3 & 0xFC) << 3) | (0x99 >> 3);  // charging, strong signal
static const ColorVal THEME_RED = ((0xEF & 0xF8) << 8) | ((0x44 & 0xFC) << 3) | (0x44 >> 3);   // SOS
static const ColorVal THEME_YELLOW = ((0xFA & 0xF8) << 8) | ((0xCC & 0xFC) << 3) | (0x15 >> 3);
static const ColorVal THEME_GREEN = ((0x30 & 0xF8) << 8) | ((0xD1 & 0xFC) << 3) | (0x58 >> 3);  // true green (THEME_GOOD is teal)

// Settings > Theme > Battery colors: the battery icon and % coloured by the charge left
static bool g_batt_colors = false;
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
    float floor = sf32BatteryPercentF(mv > CHG_LIFT_MV ? mv - CHG_LIFT_MV : 0);
    if (est < 0) est = floor;  // started on USB: nothing better to go on
    uint8_t st = board.chargeState();
    if (st == 3) est = 100;    // charge done
    else if (st == 1 || st == 2) {
      // after a full charge the charger tops up now and then: stay at 100 through it
      float cap = est >= 100 ? 100 : 99;
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
    float volt = sf32BatteryPercentF(mv);
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
    d.drawXbmScaled(40, 30, bits, 32, 32, 1.5f);
    return;
  }
  const float cx = 64, cy = 54;
  d.glassBadge(38, 28, 52);
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
  int bx = 128 - 4 - 2 - 14, by = 7;
  char tmp[8];
  int pct = mv > 0 ? sf32ShownPercent(mv) : -1;
  ColorVal outline = pct >= 0 && g_batt_colors ? battColor(pct, UIColor::secondary_txt) : UIColor::secondary_txt;
  d.setColor(outline);
  d.drawRect(bx, by, 14, 8);
  d.fillRect(bx + 14, by + 2, 2, 4);
  if (pct >= 0) {
    d.setColor(battColor(pct, UIColor::secondary_txt));
    d.fillRect(bx + 2, by + 2, (10 * pct + 50) / 100, 4);
    sprintf(tmp, "%d%%", pct);
  } else {
    strcpy(tmp, "--");
  }
  // the % (and bolt) only if they fit next to the title
  d.setTextSize(2);
  int title_end = 4 + d.getTextWidth(title) + 4;
  d.setTextSize(1);
  int tw = d.getTextWidth(tmp);
  bool usb = board.isExternalPowered();
  int x = bx - 3 - tw - (usb ? 10 : 0);
  if (x < title_end) {
    tw = 0;
    x = bx - (usb ? 10 : 0);
    if (x < title_end) return;  // icon only
  } else {
    d.setColor(outline);
    d.setCursor(bx - 3 - tw, by);
    d.print(tmp);
  }
  if (usb) {
    d.setColor(UIColor::warning_txt);
    themeBolt(d, x, by);
  }
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
  int x = d.width() / 2 - 5 * (count - 1);
  for (int i = 0; i < count; i++, x += 10) {
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
