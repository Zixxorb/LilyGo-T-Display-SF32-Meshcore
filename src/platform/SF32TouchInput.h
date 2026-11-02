// CST9220 touchscreen as input for MeshCore's companion UI (ui-new).
//
// MeshCore's UI is button-driven. Without modifying it, touch is fed in through two
// optional inputs the UI already supports:
//   - taps -> a virtual "analog button" (PIN_USER_BTN_ANA): each tap is replayed as a
//     press of the same length, so MeshCore's MomentaryButton turns them into click
//     (next), double-click (previous) and long-press (enter), same as button A.
//   - horizontal swipes -> RotaryInput Next/Prev (UI_HAS_ROTARY_INPUT).
// Our companion UI (src/companion_ui) also uses, directly:
//   - vertical swipes (takeGesture): down = power menu, up = page menu
//   - direct taps with coordinates (setDirectTaps/takeTap) while a menu is open
#pragma once

#include <helpers/ui/RotaryInput.h>

#define TOUCH_VIRTUAL_PIN 0x7F  // analogRead() pin for the virtual tap button

class SF32TouchInput : public RotaryInput {
public:
  bool begin() override;
  RotaryInputEvent poll() override;
  bool isReady() const override { return _dev != nullptr; }

  int analogRead();  // TOUCH_VIRTUAL_PIN: 0 while a replayed tap is "pressed", else 4095

  enum class Gesture { None, SwipeUp, SwipeDown };
  Gesture takeGesture();
  // While on, taps aren't replayed as button presses; takeTap() returns their panel
  // coordinates instead (menus hit-test them).
  void setDirectTaps(bool on) { _direct = on; }
  bool takeTap(int& x, int& y);
  void setRotation(int rot) { _rot = rot & 3; }  // the picture's quarter turns: touches follow
  // true once after each lift while the screen is on (touch feedback)
  uint8_t takeClick() { uint8_t c = _click; _click = 0; return c; }  // 0 none, 1 tap, 2 swipe

  // Touch controller (CST9220) scan mode: low power while the screen is off (a slower scan
  // that still reports a touch, which wakes the screen), normal while it's on.
  void setLowPower(bool low);

  // Live finger (drag scrolling): panel coordinates while touching.
  bool fingerDown() { update(); return _down; }
  int fingerY0() const { return _y0; }  // where this touch started
  int fingerY() const { return _y; }
  int fingerX0() const { return _x0; }
  // a press held in place (hold gestures): how long, 0 if not touching or it moved
  unsigned long stillHeldMs();
  // this touch is used up (a hold did its thing): its lift does nothing (no tap or swipe)
  void consumeTouch() { _consumed = true; }
  void unconsumeTouch() { _consumed = false; }  // changed our mind (the hold became a swipe)
  // Vertical speed (panel px per ms) when the finger last lifted, for flick scrolling.
  float releaseVelocity() const { return _release_vy; }

private:
  void update();

  void* _dev = nullptr;
  bool _tried = false;
  bool _down = false;
  // recent finger positions for the release velocity
  int _hist_y[4] = {0, 0, 0, 0};
  unsigned long _hist_t[4] = {0, 0, 0, 0};
  int _hist_n = 0;
  float _release_vy = 0;
  int _x0 = 0, _y0 = 0, _x = 0, _y = 0;
  unsigned long _down_at = 0;
  // tap replay
  unsigned long _press_from = 0, _press_until = 0;
  RotaryInputEvent _pending = RotaryInputEvent::None;
  Gesture _gesture = Gesture::None;
  bool _direct = false;
  bool _tap_pending = false;
  int _rot = 0;
  bool _consumed = false;
  uint8_t _click = 0;
  bool _low_power = false;
  int _tap_x = 0, _tap_y = 0;
};

extern SF32TouchInput touch_input;
