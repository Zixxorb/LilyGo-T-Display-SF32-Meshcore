// Short notification chimes on the T-Display-SF32's speaker: generated tones go
// AUDPRC -> AUDCODEC DAC -> NS4150B amplifier (enabled by PA20 only while playing).
// Playback runs in its own thread, so chime() returns at once.
#pragma once

#include <stdint.h>

class SF32Sound {
public:
  enum Chime : uint8_t { CHIME_DIRECT, CHIME_CHANNEL, CHIME_TEST, CHIME_CUSTOM, CHIME_CLICK, CHIME_POP, CHIME_TICK,
                         CHIME_SWISH, CHIME_STARTUP };
  enum Volume : uint8_t { VOL_OFF, VOL_LOW, VOL_HIGH, VOL_MEDIUM };

  void chime(Chime kind, Volume vol);  // ignored while one is playing, or if VOL_OFF
  // one tone (signal tester, SOS); up to `count` beeps of `ms` with equal gaps
  void tone(uint16_t hz, uint16_t ms, Volume vol, uint8_t count = 1);
  // Keep the audio path open (silent) so short sounds play at once at full level: the
  // path takes 100+ ms to come up when it starts. Costs a few mA while on.
  void keepOpen(bool on, Volume vol);
  // Screen off: the codec's PLL, bandgap and the crystal's audio buffer go off whenever the
  // path is closed (back on for the next sound).
  void powerSave(bool on);
  bool ready();
  bool running();  // the audio path is open  // the path is open and up to full level (a short sound plays properly)
  // touch feedback (skipped while something else plays): style 1 click, 2 pop, 3 tick
  void tap(uint8_t style, Volume vol) {
    if (style >= 1 && style <= 3) chime((Chime)(CHIME_CLICK + style - 1), vol);
  }
};

extern SF32Sound sound;
