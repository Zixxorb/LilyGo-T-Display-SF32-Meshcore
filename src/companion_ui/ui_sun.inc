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
