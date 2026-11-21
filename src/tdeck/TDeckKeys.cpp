#include "TDeckKeys.h"
#include <Arduino.h>
#include <Wire.h>
#include <helpers/ui/UIScreen.h>  // KEY_*

// Trackball: four Hall sensors, each output toggles as the ball rolls past (interrupts count the
// edges). Enough edges make one key, so a light touch doesn't move things; after a key the ball
// rests a moment (one roll, one step: a page, a menu row).
#define BALL_UP     3
#define BALL_RIGHT  2
#define BALL_DOWN   15
#define BALL_LEFT   1
// sideways turns a page: it takes more rolling, and rests longer, than up/down in a menu
#define EDGES_SIDEWAYS 7
#define EDGES_UPDOWN   4
#define REST_SIDEWAYS_MS 400
#define REST_UPDOWN_MS   180
#define BALL_FORGET_MS 300  // no edges for this long: a part roll is forgotten
// Keyboard: an ESP32-C3 on I2C 0x55; reading a byte gives the last key's ASCII (0: none)
#define KB_ADDR     0x55
#define KB_POLL_MS  30

static volatile int16_t s_edges[4];  // up, right, down, left
static volatile unsigned long s_edge_ms = 0;  // the last edge
static void IRAM_ATTR on_up() { s_edges[0]++; s_edge_ms = millis(); }
static void IRAM_ATTR on_right() { s_edges[1]++; s_edge_ms = millis(); }
static void IRAM_ATTR on_down() { s_edges[2]++; s_edge_ms = millis(); }
static void IRAM_ATTR on_left() { s_edges[3]++; s_edge_ms = millis(); }
static bool s_kb = false;
static bool s_active = true;
static bool s_flush = false;
void tdeckKeysActive(bool on) {
  if (on && !s_active) s_flush = true;  // keys typed while it was off: not acted on
  s_active = on;
}
bool tdeckKeysPending() { return s_edges[0] || s_edges[1] || s_edges[2] || s_edges[3]; }

void tdeckKeysBegin() {
  const uint8_t pins[4] = {BALL_UP, BALL_RIGHT, BALL_DOWN, BALL_LEFT};
  void (*isr[4])() = {on_up, on_right, on_down, on_left};
  for (int i = 0; i < 4; i++) {
    pinMode(pins[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pins[i]), isr[i], CHANGE);
  }
}

// the keyboard's own chip takes ~0.5 s to start after power-on: look for it for 10 s
static bool keyboardThere() {
  static unsigned long next = 0;
  if (s_kb || millis() > 10000 || (long)(millis() - next) < 0) return s_kb;
  next = millis() + 500;
  Wire.beginTransmission(KB_ADDR);
  s_kb = Wire.endTransmission() == 0;
  return s_kb;
}

static char ballKey() {
  static const char keys[4] = {KEY_UP, KEY_NEXT, KEY_DOWN, KEY_PREV};  // (right = next, as a swipe left)
  static unsigned long rest_until = 0;
  if (rest_until && (long)(millis() - rest_until) < 0) {
    for (int i = 0; i < 4; i++) s_edges[i] = 0;  // (resting: what it rolls now doesn't count)
    return 0;
  }
  unsigned long last = s_edge_ms;  // (read first: an edge may come in meanwhile)
  if ((long)(millis() - last) > BALL_FORGET_MS) {  // stopped part way: start again
    for (int i = 0; i < 4; i++) s_edges[i] = 0;
    return 0;
  }
  int best = -1, n = 0;
  for (int i = 0; i < 4; i++) if (s_edges[i] > n) { n = s_edges[i]; best = i; }
  bool sideways = best == 1 || best == 3;
  if (best < 0 || n < (sideways ? EDGES_SIDEWAYS : EDGES_UPDOWN)) return 0;
  noInterrupts();
  for (int i = 0; i < 4; i++) s_edges[i] = 0;
  interrupts();
  rest_until = millis() + (sideways ? REST_SIDEWAYS_MS : REST_UPDOWN_MS);
  return keys[best];
}

static char keyboardKey() {
  static unsigned long next = 0;
  if (!s_active || !keyboardThere() || (long)(millis() - next) < 0) return 0;
  next = millis() + KB_POLL_MS;
  if (Wire.requestFrom(KB_ADDR, 1) != 1) return 0;
  char k = Wire.read();
  if (s_flush) { s_flush = false; return 0; }
  switch (k) {
    case '\r': case '\n': return KEY_ENTER;
    case 0x08: return KEY_CANCEL;              // Backspace: back
    case ' ': return KEY_NEXT;
    default: return 0;  // (typing: not for now)
  }
}

char tdeckTakeKey() {
  char k = ballKey();
  if (!k) k = keyboardKey();
#ifdef TDECK_TOUCH_LOG
  if (k) Serial.printf("key 0x%02x\n", (uint8_t)k);
#endif
  return k;
}
