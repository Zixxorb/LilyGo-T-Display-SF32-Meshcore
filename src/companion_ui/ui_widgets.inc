// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// On-screen buttons and daylight-saving rules.

// ---- SF32: on-screen buttons (logical px, square layout) ----

struct Btn { int x, y, w, h; };
static bool inBtn(const Btn& b, int x, int y) {
  return x >= b.x - 2 && x < b.x + b.w + 2 && y >= b.y - 2 && y < b.y + b.h + 2;
}
static void drawBtn(DisplayDriver& d, const Btn& b, const char* label, int size, bool primary) {
  d.setColor(primary ? THEME_ACCENT : THEME_CARD);
  d.fillRect(b.x, b.y, b.w, b.h);
  d.setTextSize(size);
  d.setColor(UIColor::primary_txt);
  d.drawTextCentered(b.x + b.w / 2, b.y + (b.h - 8 * size) / 2, label);
}
static void drawMinusPlus(DisplayDriver& d, const Btn& minus, const Btn& plus) {
  drawBtn(d, minus, "", 1, false);
  drawBtn(d, plus, "", 1, false);
  d.setColor(UIColor::primary_txt);  // drawn: the font's '-' is too small
  int cy = minus.y + minus.h / 2;
  d.fillRect(minus.x + minus.w / 2 - 6, cy - 1, 12, 3);
  d.fillRect(plus.x + plus.w / 2 - 6, cy - 1, 12, 3);
  d.fillRect(plus.x + plus.w / 2 - 1, cy - 6, 3, 13);
}
// selection marker for button-A navigation
static void drawSelBar(DisplayDriver& d, const Btn& b) {
  d.setColor(THEME_ACCENT);
  d.fillRect(b.x, b.y + 2, 2, b.h - 4);
}

// ---- SF32: daylight saving (the companion protocol has no time zone, so it can't come
// from the phone; with a rule set, tz_offset is the standard-time offset) ----

enum DstRule : uint8_t { DST_OFF, DST_US, DST_EU, DST_RULES };
static const char* const DST_NAMES[DST_RULES] = {"Off", "US", "EU"};

static int64_t daysFromCivil(int y, int m, int d) {  // days since 1970-01-01
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + doe - 719468;
}
static int weekday(int y, int m, int d) { return (int)((daysFromCivil(y, m, d) + 4) % 7); }  // 0 = Sunday
static int nthSunday(int y, int m, int n) { return 1 + (7 - weekday(y, m, 1)) % 7 + 7 * (n - 1); }
static int lastSunday(int y, int m) {
  static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int last = mdays[m - 1];
  return last - weekday(y, m, last);
}
static int64_t utcAt(int y, int m, int d, int h) { return daysFromCivil(y, m, d) * 86400 + h * 3600; }

static bool dstActive(uint8_t rule, int std_offset_h, uint32_t utc) {
  if (rule == DST_OFF) return false;
  time_t lt = (time_t)utc + std_offset_h * 3600;
  struct tm tm;
  gmtime_r(&lt, &tm);
  int y = tm.tm_year + 1900;
  int64_t start, end;
  if (rule == DST_US) {  // 2nd Sunday of March 02:00 local -> 1st Sunday of November 02:00 local (DST)
    start = utcAt(y, 3, nthSunday(y, 3, 2), 2) - std_offset_h * 3600;
    end = utcAt(y, 11, nthSunday(y, 11, 1), 2) - (std_offset_h + 1) * 3600;
  } else {               // EU: last Sunday of March -> last Sunday of October, 01:00 UTC
    start = utcAt(y, 3, lastSunday(y, 3), 1);
    end = utcAt(y, 10, lastSunday(y, 10), 1);
  }
  return (int64_t)utc >= start && (int64_t)utc < end;
}
