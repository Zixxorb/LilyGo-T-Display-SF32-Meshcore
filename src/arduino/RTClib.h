// Stand-in for Adafruit RTClib. MeshCore only uses DateTime to format UTC
// timestamps (CommonCLI "clock", repeater log lines); no RTC chip drivers.
#pragma once

#include <stdint.h>
#include <time.h>

class DateTime {
  struct tm _tm;

public:
  explicit DateTime(uint32_t unix_time = 0) {
    time_t t = (time_t)unix_time;
    gmtime_r(&t, &_tm);
  }
  uint16_t year() const { return _tm.tm_year + 1900; }
  uint8_t month() const { return _tm.tm_mon + 1; }
  uint8_t day() const { return _tm.tm_mday; }
  uint8_t hour() const { return _tm.tm_hour; }
  uint8_t minute() const { return _tm.tm_min; }
  uint8_t second() const { return _tm.tm_sec; }
};
