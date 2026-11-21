// Debug builds only (env tdeck_debug: TDECK_SHOT, SF32_REMOTE_TOUCH): looking at and driving
// the UI from a PC over USB serial (tdeck/remote.py), to check screens without handling the
// T-Deck. Normal builds don't have it (the user's rule: remote control and screenshots stay in
// debug firmware; flash.sh refuses a normal build with it). Lines:
//   shot            -> "SHOT 320 240\n" and the shown frame, RGB565 little endian
//   rt HEX          a remote touch for SF32TouchInput (its sf32_remote_touch codes), queued
//   fakemsg         a pretend direct message (sf32_fake_msg)
//   fakemeet HEX    a pretend nearby Starlit's hello (sf32_fake_meet)
//   tune N          the panel set up again with tuning groups N (TDeckPanel.cpp), to compare
//   cat PATH        a data file, in hex: "CAT n" then the bytes (UI settings and the like; not
//                   the identity: /identity is refused)
#ifdef TDECK_SHOT
#include <Arduino.h>
#include "compat/rtthread.h"
#include <SPIFFS.h>
#include "TDeckColor.h"

extern "C" uint16_t* volatile sf32_shown_fb;  // SF32Display.cpp: the frame on the panel
void tdeck_panel_retune(uint8_t groups);  // TDeckPanel.cpp
#ifdef SF32_REMOTE_TOUCH
extern "C" volatile uint32_t sf32_remote_touch;  // SF32TouchInput.cpp: taken (zeroed) by the UI
extern volatile uint32_t sf32_fake_msg, sf32_fake_meet;  // ui_meet.inc
static uint32_t s_rt[32];
static int s_rt_head = 0, s_rt_n = 0;
#endif

static void shot() {
  if (!sf32_shown_fb) return;
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

void tdeck_shot_poll() {
#ifdef SF32_REMOTE_TOUCH
  if (s_rt_n && sf32_remote_touch == 0) {  // the last one is taken: the next
    sf32_remote_touch = s_rt[s_rt_head];
    s_rt_head = (s_rt_head + 1) % 32;
    s_rt_n--;
  }
#endif
  static char line[48];
  static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n') { if (n < (int)sizeof(line) - 1) line[n++] = c; continue; }
    line[n] = 0;
    n = 0;
    if (strcmp(line, "shot") == 0) shot();
    else if (strncmp(line, "color", 5) == 0) {  // "color" or "color N": the colour preset
      extern void tdeck_panel_resend();
      if (line[5] == ' ') { tdeckColorSet(atoi(line + 6)); tdeck_panel_resend(); }
      Serial.printf("OK color %d lut %p\n", tdeckColorPreset(), tdeck_color_lut);
    }
    else if (strncmp(line, "tune ", 5) == 0) { tdeck_panel_retune(atoi(line + 5)); Serial.println("OK"); }
    else if (strncmp(line, "cat /", 5) == 0 && strncmp(line + 4, "/identity", 9) != 0) {
      File f = SPIFFS.open(line + 4, "r");
      Serial.printf("CAT %d\n", f ? (int)f.size() : -1);
      while (f && f.available()) Serial.printf("%02x", f.read());
      Serial.println();
    }
#ifdef SF32_REMOTE_TOUCH
    else if (strncmp(line, "rt ", 3) == 0 && s_rt_n < 32) {
      s_rt[(s_rt_head + s_rt_n) % 32] = strtoul(line + 3, NULL, 16);
      s_rt_n++;
      Serial.println("OK");
    } else if (strcmp(line, "fakemsg") == 0) { sf32_fake_msg = 1; Serial.println("OK"); }
    else if (strncmp(line, "fakemeet ", 9) == 0) { sf32_fake_meet = strtoul(line + 9, NULL, 16); Serial.println("OK"); }
#endif
  }
}
#endif
