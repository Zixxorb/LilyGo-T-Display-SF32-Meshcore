#include "SF32RTCClock.h"

#include <rtthread.h>
#include <rtdevice.h>
#include <time.h>

// An unset RTC starts near 2000. Like MeshCore's VolatileRTCClock, start from
// 15 May 2024 instead, so timestamps are at least plausible until the clock is synced.
#define MIN_VALID_TIME 1715770351UL
// A clock at or after this (2025-01-01) has been set from a real source (phone/CLI).
#define SYNCED_TIME    1735689600UL

static rt_device_t rtc() {
  static rt_device_t dev = rt_device_find("rtc");
  return dev;
}

static void rtc_write(uint32_t time) {
  time_t t = (time_t)time;
  if (rtc()) rt_device_control(rtc(), RT_DEVICE_CTRL_RTC_SET_TIME, &t);
}

#define TIME_OK_FILE "/sf32_timeok"

void SF32RTCClock::begin() {
  _lost_at_boot = getCurrentTime() < SYNCED_TIME;
  _source = _lost_at_boot ? SRC_NONE : SRC_PHONE;  // confirmed in afterSetup()
  if (getCurrentTime() < MIN_VALID_TIME) rtc_write(MIN_VALID_TIME);
}

void SF32RTCClock::afterSetup() {
  _setup_done = true;
  if (_lost_at_boot) {
    InternalFS.remove(TIME_OK_FILE);  // anything set during setup is a guess
    _source = SRC_NONE;
  } else if (!InternalFS.exists(TIME_OK_FILE)) {
    _source = SRC_NONE;  // never synced since the last loss
  } else {
    File f = InternalFS.open(TIME_OK_FILE, "r");
    uint8_t c = 0;
    if (f) f.read(&c, 1);
    _source = c == 'm' ? SRC_MESH : SRC_PHONE;
  }
}

void SF32RTCClock::setSource(Source s) {
  _source = s;
  if (File f = InternalFS.open(TIME_OK_FILE, "w", true)) {
    if (s == SRC_MESH) f.write((const uint8_t*)"m", 1);
    f.close();
  }
}

void SF32RTCClock::setFromMesh(uint32_t time) {
  if (_source != SRC_NONE || !_setup_done || time < SYNCED_TIME) return;
  rtc_write(time);
  setSource(SRC_MESH);
}

void SF32RTCClock::reset() {
  rtc_write(MIN_VALID_TIME);
  if (_setup_done) {  // unknown again: the phone (or the mesh) sets it next
    _source = SRC_NONE;
    InternalFS.remove(TIME_OK_FILE);
  }
}

uint32_t SF32RTCClock::getCurrentTime() {
  time_t t = 0;
  if (rtc()) rt_device_control(rtc(), RT_DEVICE_CTRL_RTC_GET_TIME, &t);
  return (uint32_t)t;
}

// The companion calls bootstrapRTCfromContacts() at every boot, which sets the clock to
// the newest contact's timestamp: right for boards without an RTC, but here it would
// throw a synced clock back to whenever the contacts were last changed (May 2024, if
// that was before the first sync). Like MeshCore's own time commands, a synced clock
// only moves forward; `clock reset` (reset()) is the explicit way back.
void SF32RTCClock::setCurrentTime(uint32_t time) {
  uint32_t now = getCurrentTime();
  if (now >= SYNCED_TIME && time < now) return;
  rtc_write(time);
  // After setup, a time comes from the phone / CLI: it's real. (During setup it's
  // MeshCore's bootstrapRTCfromContacts() guess.)
  if (_setup_done && time >= SYNCED_TIME && _source != SRC_PHONE) setSource(SRC_PHONE);
}
