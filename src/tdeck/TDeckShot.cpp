// Debug builds only (env tdeck_debug, TDECK_SHOT): screenshots over USB serial, for working on
// the UI without looking at the T-Deck. Send "shot\n"; the reply is "SHOT 320 240\n" and the
// shown frame, RGB565 little endian (tdeck/shot.py saves it as a PNG). Normal builds don't
// have it (the user's rule: remote control and screenshots stay in debug firmware).
#ifdef TDECK_SHOT
#include <Arduino.h>
#include "compat/rtthread.h"

extern "C" uint16_t* volatile sf32_shown_fb;  // SF32Display.cpp: the frame on the panel

void tdeck_shot_poll() {
  static char line[16];
  static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n') { if (n < (int)sizeof(line) - 1) line[n++] = c; continue; }
    line[n] = 0;
    n = 0;
    if (strcmp(line, "shot") != 0 || !sf32_shown_fb) continue;
    Serial.printf("SHOT %d %d\n", LCD_HOR_RES_MAX, LCD_VER_RES_MAX);
    const uint8_t* p = (const uint8_t*)sf32_shown_fb;
    size_t left = LCD_HOR_RES_MAX * LCD_VER_RES_MAX * 2;
    while (left) {
      size_t k = Serial.write(p, left > 4096 ? 4096 : left);
      p += k;
      left -= k;
      if (!k) delay(1);
    }
    Serial.flush();
  }
}
#endif
