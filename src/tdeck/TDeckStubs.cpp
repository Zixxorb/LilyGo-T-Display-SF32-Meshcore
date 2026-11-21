// The SF32 parts the companion UI uses that the T-Deck doesn't have yet, as silent stand-ins,
// so the UI runs while they're ported one by one (the order in tdeck-port-research.md):
//   SF32TouchInput: the GT911 touchscreen (step 3: no touches yet)
//   SF32Sound: the I2S speaker (step 6: silent, the same theme and chime names)
//   SF32Motion: the T-Deck has no motion sensor (never ready: lift to wake etc. stay off)
//   SF32UsbInterface: the USB app link (later: off)
// and the SF32 main loop's pacing variables (TDeckBoard::loop uses the sleep).
#include <Arduino.h>
#include "SF32Sound.h"
#include "SF32Motion.h"
#include "SF32TouchInput.h"
#include "SF32UsbInterface.h"

// ---- sound: silent ----
SF32Sound sound;
static const char* const THEME_NAMES[SF32Sound::THEMES] = {"Classic", "Soft", "Retro", "Nature"};
static const uint8_t THEME_TAPS[SF32Sound::THEMES] = {1, 4, 3, 4};
static const char* const CHIME_NAMES[SF32Sound::CHIMES + 1] = {"Theme", "Chime", "Note", "Marimba", "Ding-dong", "Ping", "Retro", "Bird", "Drop", "Bubbles", "Blip"};
const char* SF32Sound::themeName(int t) { return THEME_NAMES[(t < 0 || t >= THEMES) ? 0 : t]; }
int SF32Sound::themeTap(int t) { return THEME_TAPS[(t < 0 || t >= THEMES) ? 0 : t]; }
const char* SF32Sound::chimeName(int c) { return CHIME_NAMES[(c < 0 || c > CHIMES) ? 0 : c]; }
void SF32Sound::setTheme(int, int, int) {}
void SF32Sound::previewChime(int, Volume) {}
void SF32Sound::chime(Chime, Volume) {}
void SF32Sound::tone(uint16_t, uint16_t, Volume, uint8_t) {}
void SF32Sound::keepOpen(bool, Volume) {}
void SF32Sound::powerSave(bool) {}
bool SF32Sound::ready() { return true; }
bool SF32Sound::playing() { return false; }
bool SF32Sound::running() { return false; }

// ---- motion: none on the T-Deck ----
SF32Motion motion;
void SF32Motion::begin() {}
void SF32Motion::poll() {}
bool SF32Motion::takePickup() { return false; }
void SF32Motion::setActive(bool on) { _active = on; }

// ---- touch: not yet ----
SF32TouchInput touch_input;
bool SF32TouchInput::begin() { return false; }
RotaryInputEvent SF32TouchInput::poll() { return RotaryInputEvent::None; }
int SF32TouchInput::analogRead() { return 4095; }
SF32TouchInput::Gesture SF32TouchInput::takeGesture() { return Gesture::None; }
bool SF32TouchInput::takeTap(int&, int&) { return false; }
void SF32TouchInput::setLowPower(bool low) { _low_power = low; }
unsigned long SF32TouchInput::stillHeldMs() { return 0; }
void SF32TouchInput::update() {}

// ---- the USB app link: off ----
SF32UsbInterface usb_app_interface;
void SF32UsbInterface::enable() {}
void SF32UsbInterface::disable() {}
void SF32UsbInterface::loop() {}
size_t SF32UsbInterface::writeFrame(const uint8_t[], size_t) { return 0; }
size_t SF32UsbInterface::checkRecvFrame(uint8_t[]) { return 0; }
void SF32UsbInterface::setConnected(bool c) { _connected = c; }

// ---- the SF32 main loop's pacing (src/main.cpp there) ----
volatile bool sf32_ui_animating = false;
volatile uint8_t sf32_ui_wait_ms = 0;
volatile uint8_t sf32_loop_sleep_ms = 0;
volatile uint16_t sf32_loop_event_ms = 0;
extern "C" volatile uint32_t sf32_radio_tx = 0, sf32_radio_rx = 0;
extern "C" void sf32_button_wake_arm(void) {}

// The UI's tap "button" reads TOUCH_VIRTUAL_PIN with analogRead(), as on the SF32 (its
// Arduino.cpp hooks it). Arduino-ESP32's analogRead is weak: this one answers that pin.
extern "C" uint16_t __analogRead(uint8_t pin);
extern "C" uint16_t analogRead(uint8_t pin) {
  if (pin == TOUCH_VIRTUAL_PIN) return touch_input.analogRead();
  return __analogRead(pin);
}
