// Short notification chimes on the T-Display-SF32's speaker: generated tones go
// AUDPRC -> AUDCODEC DAC -> NS4150B amplifier (enabled by PA20 only while playing).
// Playback runs in its own thread, so chime() returns at once.
//
// Every sound is synthesised from a small recipe (up to 4 tones: a pitch that may glide, a
// length, a waveform and an envelope), so a sound theme is just another set of recipes.
#pragma once

#include <stdint.h>

class SF32Sound {
public:
  enum Chime : uint8_t { CHIME_DIRECT, CHIME_CHANNEL, CHIME_TEST, CHIME_CUSTOM, CHIME_CLICK, CHIME_POP, CHIME_TICK,
                         CHIME_SWISH, CHIME_STARTUP,
                         // more tap styles (Settings > Sound > Taps)
                         CHIME_THUMP, CHIME_BUBBLE, CHIME_CHIRP, CHIME_TYPE,
                         // events (Settings > Sound > Events), each in the theme's voice
                         CHIME_ADVERT, CHIME_CONNECT, CHIME_DISCONNECT, CHIME_PLUG, CHIME_UNPLUG, CHIME_LOWBATT,
                         CHIME_BOT, CHIME_VISIT, CHIME_MENU_OPEN, CHIME_MENU_CLOSE,
                         CHIME_N };
  enum Volume : uint8_t { VOL_OFF, VOL_LOW, VOL_HIGH, VOL_MEDIUM };

  // tap styles (touch_snd): 0 off, 1 click, 2 pop, 3 tick, 4 thump, 5 bubble, 6 chirp, 7 type
  static const int TAP_STYLES = 8;
  // sound themes: Classic, Soft, Retro, Nature
  static const int THEMES = 4;
  static const char* themeName(int t);
  static int themeTap(int t);  // the theme's own tap style
  // message chimes to choose from (Settings > Sound > Direct / Channel); 0 means "the theme's"
  static const int CHIMES = 10;
  static const char* chimeName(int c);  // c 1..CHIMES
  // the theme for the startup, test, event sounds and (unless chosen) the message chimes
  void setTheme(int theme, int direct_chime, int channel_chime);
  void previewChime(int c, Volume vol);  // play message chime c (1..CHIMES) once

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
  bool playing();  // a sound is playing right now
  bool running();  // the audio path is open  // the path is open and up to full level (a short sound plays properly)
  // touch feedback (skipped while something else plays): style 1..7 (see TAP_STYLES)
  void tap(uint8_t style, Volume vol) {
    if (style >= 1 && style <= 3) chime((Chime)(CHIME_CLICK + style - 1), vol);
    else if (style >= 4 && style <= 7) chime((Chime)(CHIME_THUMP + style - 4), vol);
  }
  // a tap-like sound (skippable, replaced by any chime): taps, swish, menu blips
  static bool isTapKind(int k) {
    return (k >= CHIME_CLICK && k <= CHIME_SWISH) || (k >= CHIME_THUMP && k <= CHIME_TYPE) || k == CHIME_MENU_OPEN || k == CHIME_MENU_CLOSE;
  }
};

extern SF32Sound sound;
