// Motion from the T-Display-SF32's BHI260AP (Bosch sensor hub on i2c1, LilyGo's driver):
// the accelerometer at 10 Hz gives "picked up" (it was lying still, then tilted) and
// "face down" (gravity through the back of the screen).
#pragma once

#include <stdint.h>

class SF32Motion {
public:
  void begin();           // starts the sensor (firmware upload, ~2 s) in a background thread
  void poll();            // call often; samples every 100 ms
  bool ready() const { return _ready; }
  bool faceDown() const { return _face_down; }
  bool takePickup();      // true once per pick-up
  // the accelerometer runs only while a motion feature needs it (saves power)
  void setActive(bool on);

  // last sample in g (for diagnostics)
  float ax = 0, ay = 0, az = 0;

  volatile bool _ready = false;  // set by the init thread
  volatile bool _active = true;
private:
  unsigned long _next = 0;
  bool _face_down = false, _pickup = false;
  int _down_count = 0, _up_count = 0;
  // stillness: the reference direction while lying still
  float _rx = 0, _ry = 0, _rz = 0;
  int _still_ticks = 0;
  float _px = 0, _py = 0, _pz = 0;
};

extern SF32Motion motion;
