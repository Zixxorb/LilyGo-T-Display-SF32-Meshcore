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
  d.setColor(UIColor::secondary_txt);
  d.drawRect(bx, by, 14, 8);
  d.fillRect(bx + 14, by + 2, 2, 4);
  if (mv > 0) {
    int pct = sf32BatteryPercent(mv);
    d.setColor(pct <= 15 ? UIColor::warning_txt : UIColor::secondary_txt);
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
    d.setColor(UIColor::secondary_txt);
    d.setCursor(bx - 3 - tw, by);
    d.print(tmp);
  }
  if (usb) {
    d.setColor(UIColor::warning_txt);
    d.drawXbm(x, by, charging_icon, 8, 8);
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
