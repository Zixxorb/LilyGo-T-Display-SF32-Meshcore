#include "SF32Motion.h"

#include <rtthread.h>
#include <math.h>
#include <Arduino.h>

#ifdef PKG_USING_BHI260AP
extern "C" {
#include "bhi260ap.h"
}
#endif

#define LOG_TAG "motion"
#include <ulog.h>
extern "C" void sf32_wake_loop(void);

SF32Motion motion;

#define SAMPLE_MS        100
// auto-rotate: the accelerometer axis along the screen's up-down direction (0 x, 1 y) and
// its sign when held the right way up (measured on the board 2026-11-02: held upright
// normally, x = -1.0 g, y and z ~0)
#define ROT_AXIS 0
#define ROT_SIGN (-1.0f)
#define ROT_SIDE_POS 3  // turned so gravity runs along the other axis (+): this quarter turn (left side up: y = -0.83 g, wants 3; checked on the board)
#define STILL_TICKS      15      // 1.5 s of stillness before a pick-up can count
#define STILL_DEV        0.06f   // max change between samples (g) that still counts as still
#define PICKUP_COS       0.94f   // tilt > ~20 deg from the resting direction
#define FACE_DOWN_Z     -0.80f   // z (normalised) below this = face down
#define FACE_UP_Z       -0.40f

static void init_thread(void*) {
#ifdef PKG_USING_BHI260AP
  if (rt_bhi260ap_init() != RT_EOK) {
    LOG_E("BHI260AP init failed");
    return;
  }
  bhi260ap_thread_resume();
  bool configured = motion._active;  // (what the hub is set to)
  if (!bhi260ap_configure(BHY2_SENSOR_ID_ACC, configured ? 10 : 0, 0)) {
    LOG_E("BHI260AP: accelerometer config failed");
    return;
  }
  motion._ready = true;
  // setActive() during the set-up above was skipped (not ready yet): apply the latest wish
  if (motion._active != configured) bhi260ap_configure(BHY2_SENSOR_ID_ACC, motion._active ? 10 : 0, 0);
  LOG_I("BHI260AP ready");
#ifdef SF32_SENSOR_PROBE
  // diagnostics: which virtual sensors the hub's firmware offers (mag = a compass is fitted)
  rt_kprintf("bhi260ap sensors:");
  for (int i = 1; i < BHY2_SENSOR_ID_MAX; i++)
    if (bhi260ap_is_sensor_available(i)) rt_kprintf(" %d(%s)", i, bhi260ap_get_sensor_name(i));
  rt_kprintf("\nbhi260ap magnetometer: %s\n", bhi260ap_is_sensor_available(BHY2_SENSOR_ID_MAG) ? "YES" : "no");
#endif
#endif
}

void SF32Motion::begin() {
#ifdef PKG_USING_BHI260AP
  rt_thread_t t = rt_thread_create("motinit", init_thread, RT_NULL, 3072, 20, 10);
  if (t) rt_thread_startup(t);
#endif
}

void SF32Motion::setActive(bool on) {
  _active = on;
  _face_down = false;
  _pickup = false;
#ifdef PKG_USING_BHI260AP
  if (_ready) bhi260ap_configure(BHY2_SENSOR_ID_ACC, on ? 10 : 0, 0);  // 0 Hz = off
#endif
}

bool SF32Motion::takePickup() {
  bool p = _pickup;
  _pickup = false;
  return p;
}

void SF32Motion::poll() {
#ifdef PKG_USING_BHI260AP
  if (!_ready || !_active) return;
  unsigned long now = millis();
  if ((long)(now - _next) < 0) return;
  _next = now + SAMPLE_MS;

  struct bhy2_data_xyz_float a = bhi260ap_get_acc_sensor_data();
  float m = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  if (m < 1e-3f) return;
  ax = a.x; ay = a.y; az = a.z;
  float x = a.x / m, y = a.y / m, z = a.z / m;  // direction of gravity

#ifdef SF32_MOTION_LOG
  static int n = 0;
  if (++n % 10 == 0)
    rt_kprintf("acc x %d y %d z %d |%d| (milli) face_down %d still %d\n", (int)(a.x * 1000), (int)(a.y * 1000),
               (int)(a.z * 1000), (int)(m * 1000), _face_down, _still_ticks);
#endif

  // face down, with a little hysteresis (1 s to enter, 0.5 s to leave)
  if (z < FACE_DOWN_Z) { _up_count = 0; if (++_down_count >= 10) _face_down = true; }
  else if (z > FACE_UP_Z) { _down_count = 0; if (++_up_count >= 5) _face_down = false; }

  // which way up (auto-rotate): gravity along the screen's up-down axis (ROT_AXIS, ROT_SIGN)
  // or across it; 0.55 g or more for 0.8 s
  {
    float up = ROT_SIGN * (ROT_AXIS == 0 ? x : y), side = ROT_SIGN * (ROT_AXIS == 0 ? y : x);
    int want = up > 0.55f ? 0 : up < -0.55f ? 2 : side > 0.55f ? ROT_SIDE_POS : side < -0.55f ? (ROT_SIDE_POS + 2) % 4 : -1;
    if (want >= 0 && want == _side_want) {
      if (++_side_count >= 8) _side = want;
    } else {
      _side_want = want;
      _side_count = 0;
    }
  }

  // still: barely any change since the last sample
  float d = fabsf(x - _px) + fabsf(y - _py) + fabsf(z - _pz);
  _px = x; _py = y; _pz = z;
  if (d < STILL_DEV) {
    if (_still_ticks < STILL_TICKS) {
      _still_ticks++;
    }
    if (_still_ticks >= STILL_TICKS) { _rx = x; _ry = y; _rz = z; }  // follow the resting direction
    return;
  }
  // moving: a pick-up if it had been lying still and now points well away from that
  if (_still_ticks >= STILL_TICKS) {
    float c = x * _rx + y * _ry + z * _rz;  // cos(angle) to the resting direction
    if (c < PICKUP_COS) {
      _pickup = true;
      sf32_wake_loop();  // (lift to wake: now, not at the next loop pass)
      _still_ticks = 0;
    }
  } else {
    _still_ticks = 0;
  }
#endif
}
