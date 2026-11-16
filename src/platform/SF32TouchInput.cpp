#include "SF32TouchInput.h"
#include "SF32Board.h"

#include <Arduino.h>
#include <rtthread.h>
#include <rthw.h>  // rt_hw_interrupt_disable/enable
#include <rtdevice.h>
#include <stdlib.h>
#include "drv_touch.h"
#include "SF32Display.h"

extern SF32Display display;  // target.cpp
#ifdef SF32_UI_DEMO_TIME
#include "SF32RTCClock.h"
extern SF32RTCClock rtc_clock;
#endif

#define SWIPE_MIN_PX  60    // horizontal travel that counts as a swipe (480 px panel)
#define TAP_MAX_PX    25    // more travel than this is neither a tap nor a swipe

SF32TouchInput touch_input;

static volatile int s_pending_reads = 0;
static rt_err_t on_touch(rt_device_t, rt_size_t) {
  s_pending_reads++;
  sf32_pm_event(SF32_PM_EV_TOUCH_IRQ);
  return RT_EOK;
}

bool SF32TouchInput::begin() {
  _tried = true;
  rt_device_t dev = rt_device_find("touch");
  if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK) return false;
  rt_device_set_rx_indicate(dev, on_touch);
  _dev = dev;
  return true;
}

// Remote touch, for checking the UI from a PC: tools/sf32_debug_uart.py writes a command
// here over the debug channel (needs the USB cable and a halt-capable debug link, so only
// whoever holds the board can use it). Top byte: 1 tap at (x, y) panel px in the low
// 24 bits (x << 12 | y), 2 swipe up, 3 swipe down, 4 swipe left, 5 swipe right, 6 button
// press (as a tap with the screen off), 7 finger down and held at (x, y), 8 it lifts.
// Cleared once taken.
// Debug builds only (SF32_REMOTE_TOUCH=1 ./build.sh companion): not in normal firmware.
#ifdef SF32_REMOTE_TOUCH
extern "C" volatile uint32_t sf32_remote_touch = 0;
#endif


void SF32TouchInput::update() {
  if (!_dev && !_tried) begin();
  if (_dev) setLowPower(!display.isOn() && !_tap_wake);
#ifdef SF32_REMOTE_TOUCH
  uint32_t rc = sf32_remote_touch;
  if (rc) {
    sf32_remote_touch = 0;
    int kind = rc >> 24;
    if (kind == 1 && display.isOn() && _direct) {
      _tap_pending = true;
      _tap_x = (rc >> 12) & 0xFFF;
      _tap_y = rc & 0xFFF;
      _click = 1;
    } else if (kind == 2 || kind == 3) {
      if (display.isOn()) { _gesture = kind == 2 ? Gesture::SwipeUp : Gesture::SwipeDown; _click = 2; }
    } else if (kind == 4 || kind == 5) {
      _remote_swipe = true;
      if (display.isOn()) { _pending = kind == 4 ? RotaryInputEvent::Next : RotaryInputEvent::Prev; _click = 2; }
    } else if (kind == 7) {  // finger down and held at (x, y) (hold gestures), until kind 8
      _down = true;
      _consumed = false;
      _remote_swipe = false;  // (a drag: judged by where it starts, like a real one)
      _x0 = _x = (rc >> 12) & 0xFFF;
      _y0 = _y = rc & 0xFFF;
      _down_at = millis();
      _hist_n = 0;
    } else if (kind == 9) {  // it moves to (x, y)
      _x = (rc >> 12) & 0xFFF;
      _y = rc & 0xFFF;
    } else if (kind == 8) {  // that finger lifts
      _release_vx = 0;
      _touch_ms = millis() - _down_at;
      if (_down && !_consumed && display.isOn() && _direct && abs(_x - _x0) <= TAP_MAX_PX && abs(_y - _y0) <= TAP_MAX_PX) { _tap_pending = true; _tap_x = _x0; _tap_y = _y0; }  // (a drag isn't a tap)
      _down = false;
      _consumed = false;
    } else {  // a press (also a tap when taps aren't taken directly; wakes the screen)
      _press_from = millis();
      _press_until = _press_from + 60;
      if (display.isOn()) _click = 1;
    }
  }
#endif
  if (!_dev) return;
  // Take the count the touch interrupt has built up, atomically (a plain -- here could
  // race with its ++ and leave a message behind, so lifts would arrive one touch late).
  rt_base_t level = rt_hw_interrupt_disable();
  int pending = s_pending_reads;
  s_pending_reads = 0;
  rt_hw_interrupt_enable(level);
  while (pending-- > 0) {
    struct touch_message m;
    if (rt_device_read((rt_device_t)_dev, 0, &m, 1) != 1) continue;
    if (_rot) {  // the picture is turned (SF32Display::setRotation): back to its coordinates
      int px = m.x, py = m.y;
      if (_rot == 1) { m.x = py; m.y = 479 - px; }
      else if (_rot == 2) { m.x = 479 - px; m.y = 479 - py; }
      else { m.x = 479 - py; m.y = px; }
    }
    if (m.event == TOUCH_EVENT_DOWN || m.event == TOUCH_EVENT_MOVE) {
      if (!_down) {
        _down = true;
        _consumed = false;
        _remote_swipe = false;
        _x0 = m.x;
        _y0 = m.y;
        _down_at = millis();
        _hist_n = 0;
      }
      _x = m.x;
      _y = m.y;
      int k = _hist_n++ % 4;
      _hist_y[k] = m.y;
      _hist_x[k] = m.x;
      _hist_t[k] = millis();
    } else if (m.event == TOUCH_EVENT_UP && _down) {
      _down = false;
      _touch_ms = millis() - _down_at;
      _release_vx = 0;  // (before the rest: a page being dragged has taken the touch over)
      if (_hist_n >= 2) {
        int newest = (_hist_n - 1) % 4, oldest = (_hist_n >= 4 ? _hist_n - 4 : 0) % 4;
        long dt = (long)(_hist_t[newest] - _hist_t[oldest]);
        if (dt > 0 && (long)(millis() - _hist_t[newest]) < 150) _release_vx = (float)(_hist_x[newest] - _hist_x[oldest]) / dt;
      }
      if (_consumed) {  // a hold already did its thing: nothing on lift
        _consumed = false;
        continue;
      }
      // speed over the last few samples (up to ~4 moves)
      _release_vy = 0;
      if (_hist_n >= 2) {
        int newest = (_hist_n - 1) % 4, oldest = (_hist_n >= 4 ? _hist_n - 4 : 0) % 4;
        long dt = (long)(_hist_t[newest] - _hist_t[oldest]);
        // the last moves before lifting (up to 150 ms old still counts as a flick)
        if (dt > 0 && (long)(millis() - _hist_t[newest]) < 150) _release_vy = (float)(_hist_y[newest] - _hist_y[oldest]) / dt;
      }
      int dx = _x - _x0, dy = _y - _y0;
      unsigned long held = millis() - _down_at;
      // touch feedback: 1 a tap, 2 a swipe (not after slow drags)
      if (display.isOn() && held < 500) _click = (abs(dx) >= SWIPE_MIN_PX || abs(dy) >= SWIPE_MIN_PX) ? 2 : 1;
      if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > 2 * abs(dy) && display.isOn()) {
        _pending = dx < 0 ? RotaryInputEvent::Next : RotaryInputEvent::Prev;  // swipe left = next
      } else if (abs(dy) >= SWIPE_MIN_PX && abs(dy) > 2 * abs(dx) && display.isOn()) {
        _gesture = dy > 0 ? Gesture::SwipeDown : Gesture::SwipeUp;
      } else if (_direct && display.isOn() && abs(dx) <= TAP_MAX_PX && abs(dy) <= TAP_MAX_PX && held >= 800) {
        // held still, not on anything that takes a hold: not a tap either
      } else if (_direct && display.isOn() && abs(dx) <= TAP_MAX_PX && abs(dy) <= TAP_MAX_PX) {
        _tap_pending = true;
        _tap_x = _x0;
        _tap_y = _y0;
      } else if (!display.isOn() || (abs(dx) <= TAP_MAX_PX && abs(dy) <= TAP_MAX_PX)) {
        // MeshCore ignores swipes while the display is off, so any touch then is a tap
        // (wakes it). Replay the tap as a press of the same length (at least 30 ms)
        _press_from = millis();
        // (with the screen off the loop polls every 25 ms: make it long enough to be seen)
        unsigned long min_press = display.isOn() ? 30 : 60;
        _press_until = _press_from + (held < min_press ? min_press : held);
      }
    }
  }
}

#ifdef SF32_UI_DEMO_SEQ
static int s_drag_dir = 0;
static unsigned long s_drag_start = 0;
extern "C" void sf32_before_poweroff(int reason);
void sf32_before_poweroff_test() { sf32_before_poweroff(1); }
#endif

RotaryInputEvent SF32TouchInput::poll() {
  update();
  RotaryInputEvent e = _pending;
  _pending = RotaryInputEvent::None;
  return e;
}

unsigned long SF32TouchInput::stillHeldMs() {
  update();
  if (!_down || abs(_x - _x0) > TAP_MAX_PX || abs(_y - _y0) > TAP_MAX_PX) return 0;
  return millis() - _down_at;
}

SF32TouchInput::Gesture SF32TouchInput::takeGesture() {
  update();
  Gesture g = _gesture;
  _gesture = Gesture::None;
  return g;
}

bool SF32TouchInput::takeTap(int& x, int& y) {
  update();
  if (!_tap_pending) return false;
  _tap_pending = false;
  x = _tap_x;
  y = _tap_y;
  return true;
}

int SF32TouchInput::analogRead() {
  update();
#ifdef SF32_UI_DEMO_SEQ
  if (s_drag_dir) {  // a scripted drag in progress
    unsigned long t = millis() - s_drag_start;
    float f = t >= 500 ? 1.0f : t / 500.0f;
    _y = _y0 + (int)(s_drag_dir * 250 * f);
    int k = _hist_n++ % 4;
    _hist_y[k] = _y;
    _hist_t[k] = millis();
    if (f >= 1.0f) {
      _down = false;
      _release_vy = s_drag_dir * 0.5f;  // a gentle flick
      s_drag_dir = 0;
    }
  }
  // Scripted touch input for UI tests (see SConscript): one step every 8 s.
  static unsigned long next_step = 10000;
  static const char* step = SF32_UI_DEMO_SEQ;
#ifdef SF32_UI_DEMO_TIME
  static bool time_set = false;
  if (!time_set) {  // once per boot, even backwards (reset() first)
    time_set = true;
    rtc_clock.reset();
    rtc_clock.setCurrentTime(SF32_UI_DEMO_TIME);
  }
#endif
  while (*step == ' ' || *step == '_') step++;
  if (*step && (long)(millis() - next_step) >= 0) {
    next_step = millis() + 8000;
    if (*step == '.') {  // wait one step
      step++;
    } else if (*step == 'S' || *step == 'W') {  // drag up / down 250 px over 0.5 s, then lift
      s_drag_dir = *step == 'S' ? -1 : 1;
      s_drag_start = millis();
      _down = true;
      _x0 = _x = 240;
      _y0 = _y = s_drag_dir < 0 ? 380 : 130;
      _hist_n = 0;
      step++;
    } else if (*step == 'B') {  // show the "battery empty" screen (no power-off)
      extern void sf32_before_poweroff_test();
      step++;
      sf32_before_poweroff_test();
    } else if (*step == 't' || *step == 'L') {
      if (display.isOn()) _click = 1;
      _press_from = millis();
      _press_until = _press_from + (*step == 'L' ? 1500 : 60);
      step++;
    } else if (*step == 'l' || *step == 'r') {  // horizontal swipe left / right
      _pending = *step == 'l' ? RotaryInputEvent::Next : RotaryInputEvent::Prev;
      step++;
    } else if (*step == 'U' || *step == 'D') {
      _gesture = *step == 'U' ? Gesture::SwipeUp : Gesture::SwipeDown;
      step++;
    } else {  // X,Y
      int x = atoi(step);
      while (*step && *step != ',') step++;
      if (*step) step++;
      int y = atoi(step);
      while (*step && *step != ' ' && *step != '_') step++;
      if (!display.isOn()) {  // like a real touch on a dark screen: wake it
        _press_from = millis();
        _press_until = _press_from + 60;
      } else if (_direct) {
        _tap_pending = true;
        _tap_x = x;
        _tap_y = y;
      }
    }
  }
#endif
  unsigned long now = millis();
  return (_press_until && (long)(now - _press_until) < 0) ? 0 : 4095;
}

// Arduino analogRead() hook (src/arduino/Arduino.cpp)
int sf32_analog_read_hook(int pin, bool* handled) {
  if (pin != TOUCH_VIRTUAL_PIN) return 0;
  *handled = true;
  return touch_input.analogRead();
}

// ---- CST9220 scan modes (registers from the SDK's cst922 driver) ----
extern "C" uint32_t cst922_i2c_write(uint16_t reg, uint8_t* p_data, uint16_t len);
extern "C" void BSP_TP_Reset(uint8_t high1_low0);
#define CST92XX_REG_MODE_NORMAL   0xD109
#define CST92XX_REG_DEEP_SLEEP    0xD105

void SF32TouchInput::setLowPower(bool low) {
  if (!_dev || low == _low_power) return;
  uint8_t dummy = 0;
  uint32_t r = RT_EOK;
  if (low) {
    // deep sleep: answers nothing until it's reset (sends no touches, so the button wakes it)
    r = cst922_i2c_write(CST92XX_REG_DEEP_SLEEP, &dummy, 0);
  } else {
    BSP_TP_Reset(0);
    rt_thread_mdelay(3);
    BSP_TP_Reset(1);
    rt_thread_mdelay(50);
    r = cst922_i2c_write(CST92XX_REG_MODE_NORMAL, &dummy, 0);
    // a touch cut off by the sleep never lifts: start clean
    rt_base_t level = rt_hw_interrupt_disable();
    s_pending_reads = 0;
    rt_hw_interrupt_enable(level);
    _down = false;
    _consumed = false;
  }
#ifdef SF32_SOUND_LOG
  rt_kprintf("touch: %s, write %s\n", low ? "deep sleep" : "normal", r == RT_EOK ? "ok" : "FAILED");
#endif
  _low_power = low;
}
