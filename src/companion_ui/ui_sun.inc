// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Sunrise and sunset from the node's location (as set in the app) and the date: the usual
// sunrise equation (NOAA's simplified one, within a minute or two away from the poles).
// No network needed. Shown on the Sun clock face and the Info face.

struct SunTimes {
  enum Kind : uint8_t { NONE, NORMAL, ALL_DAY, ALL_NIGHT } kind = NONE;  // NONE: no location
  uint32_t rise = 0, set = 0;  // UTC
};

// for the local date `local_day` (days since 1970 in local time) at lat/lon (degrees, east +)
static SunTimes sunTimesFor(long local_day, double lat, double lon) {
  SunTimes st;
  if (lat == 0 && lon == 0) return st;  // no location set
  const double D2R = 3.14159265358979 / 180;
  double n = (double)local_day + 2440588.0 - 2451545.0 + 0.0008;  // days since J2000 (noon)
  double jstar = n - lon / 360.0;
  double M = fmod(357.5291 + 0.98560028 * jstar, 360.0);
  double C = 1.9148 * sin(M * D2R) + 0.02 * sin(2 * M * D2R) + 0.0003 * sin(3 * M * D2R);
  double lam = fmod(M + C + 180 + 102.9372, 360.0);
  double jtransit = 2451545.0 + jstar + 0.0053 * sin(M * D2R) - 0.0069 * sin(2 * lam * D2R);
  double sind = sin(lam * D2R) * sin(23.4397 * D2R), cosd = cos(asin(sind));
  double cosw = (sin(-0.833 * D2R) - sin(lat * D2R) * sind) / (cos(lat * D2R) * cosd);
  if (cosw < -1) { st.kind = SunTimes::ALL_DAY; return st; }
  if (cosw > 1) { st.kind = SunTimes::ALL_NIGHT; return st; }
  double w = acos(cosw) / D2R;
  st.rise = (uint32_t)((jtransit - w / 360.0 - 2440587.5) * 86400.0);
  st.set = (uint32_t)((jtransit + w / 360.0 - 2440587.5) * 86400.0);
  st.kind = SunTimes::NORMAL;
  return st;
}

// today's (local) times, worked out once per day / location
static SunTimes sunToday(UITask* task, NodePrefs* prefs, uint32_t now) {
#ifdef SF32_SUN_TEST  // test builds (SF32_SUN_TEST=1): New York, without touching the node's location
  static NodePrefs test_prefs;
  test_prefs.node_lat = 40.7128;
  test_prefs.node_lon = -74.006;
  prefs = &test_prefs;
#endif
  static long cached_day = -1;
  static double cached_lat = 0, cached_lon = 0;
  static SunTimes cached;
  long day = (long)((now + task->localOffsetSecs(now)) / 86400);
  if (day != cached_day || prefs->node_lat != cached_lat || prefs->node_lon != cached_lon) {
    cached_day = day;
    cached_lat = prefs->node_lat;
    cached_lon = prefs->node_lon;
    cached = sunTimesFor(day, prefs->node_lat, prefs->node_lon);
  }
  return cached;
}

// ---- the moon: where it is and how full (a low-precision lunar formula with its main terms,
// good to a fraction of a degree: moonrise/moonset to within a few minutes). For the Sun face's arc.
struct MoonNow {
  bool known = false, up = false;  // up: above the horizon now
  float f = 0;                     // up: how far from its rise (0) to its set (1)
  float lit = 0;                   // how much of it is lit, 0 new .. 1 full
  bool waxing = true;              // growing (lit on the right, seen from the north)
};

static double moonAltDeg(double unix_s, double lat, double lon, double* elong) {
  const double D2R = 3.14159265358979 / 180;
  double d = unix_s / 86400.0 - 10957.5;  // days since J2000 (12:00 UTC, 2000-01-01)
  double L = fmod(218.316 + 13.176396 * d, 360.0), M = fmod(134.963 + 13.064993 * d, 360.0), F = fmod(93.272 + 13.229350 * d, 360.0);
  double Dm = fmod(297.850 + 12.190749 * d, 360.0), Ms = fmod(357.529 + 0.98560028 * d, 360.0);  // its elongation, the sun's anomaly
  // its main wobbles (equation of centre, evection, variation, annual): good to a few minutes at rise and set
  double lam = (L + 6.289 * sin(M * D2R) + 1.274 * sin((2 * Dm - M) * D2R) + 0.658 * sin(2 * Dm * D2R) + 0.214 * sin(2 * M * D2R)
                - 0.186 * sin(Ms * D2R) - 0.114 * sin(2 * F * D2R)) * D2R;
  double beta = (5.128 * sin(F * D2R) + 0.281 * sin((M + F) * D2R) + 0.278 * sin((M - F) * D2R) + 0.173 * sin((2 * Dm - F) * D2R)) * D2R;
  double eps = 23.4397 * D2R;
  double ra = atan2(sin(lam) * cos(eps) - tan(beta) * sin(eps), cos(lam));
  double dec = asin(sin(beta) * cos(eps) + cos(beta) * sin(eps) * sin(lam));
  double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
  double ha = (gmst + lon) * D2R - ra;
  if (elong) {  // the sun's longitude, for the phase
    double g = fmod(357.528 + 0.9856003 * d, 360.0) * D2R;
    double ls = fmod(280.460 + 0.9856474 * d, 360.0) + 1.915 * sin(g) + 0.020 * sin(2 * g);
    *elong = fmod(lam / D2R - ls + 720.0, 360.0);
  }
  return asin(sin(lat * D2R) * sin(dec) + cos(lat * D2R) * cos(dec) * cos(ha)) / D2R;
}

static MoonNow moonNow(NodePrefs* prefs, uint32_t now) {
  static MoonNow cached;
  static uint32_t at = 0, rise = 0, set = 0;
  static double clat = 0, clon = 0;
  double lat = prefs->node_lat, lon = prefs->node_lon;
#ifdef SF32_SUN_TEST
  lat = 40.7128; lon = -74.006;
#endif
  if (lat == 0 && lon == 0) return MoonNow();
  const double H0 = 0.13;  // its rise and set: the centre this high (parallax and refraction)
  if (!at || now - at > 600 || lat != clat || lon != clon) {  // every 10 min: where it rises and sets
    at = now; clat = lat; clon = lon;
    double el;
    double alt = moonAltDeg(now, lat, lon, &el);
    cached = MoonNow();
    cached.known = true;
    cached.lit = (1 - cos(el * 3.14159265358979 / 180)) / 2;
    cached.waxing = el < 180;
    cached.up = alt > H0;
    rise = set = 0;
    if (cached.up) {  // when it rose (back up to 26 h) and when it sets (ahead), 10 min steps
      double prev = alt;
      for (int k = 1; k <= 156 && !rise; k++) {
        double a = moonAltDeg(now - k * 600.0, lat, lon, NULL);
        if (a <= H0) rise = now - k * 600 + (uint32_t)(600 * (H0 - a) / (prev - a));
        prev = a;
      }
      prev = alt;
      for (int k = 1; k <= 156 && !set; k++) {
        double a = moonAltDeg(now + k * 600.0, lat, lon, NULL);
        if (a <= H0) set = now + (k - 1) * 600 + (uint32_t)(600 * (prev - H0) / (prev - a));
        prev = a;
      }
    }
  }
  if (cached.up) {
    if (rise && set && set > rise) cached.f = (float)((double)(now - rise) / (set - rise));
    else cached.f = 0.5f;  // up all day (far north/south): the top of the arc
    if (cached.f < 0) cached.f = 0;
    if (cached.f > 1) cached.f = 1;
  }
  return cached;
}
