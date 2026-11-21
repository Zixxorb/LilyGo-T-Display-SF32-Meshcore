#include "TDeckColor.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>

struct Preset { float sat, gamma, warm; };
// sat: saturation (1 = as is); gamma: > 1 darker darks and mid tones, so the LCD's grey-ish
// black and pale tones look deeper (black and white stay); warm: < 0 cooler (the panel leans
// orange), > 0 warmer
static const Preset PRESETS[TDECK_COLOR_PRESETS] = {
  {1.00f, 1.00f, 0.00f},   // 0: as drawn
  {1.25f, 1.15f, 0.00f},   // 1: mild
  {1.45f, 1.30f, 0.00f},   // 2: medium
  {1.70f, 1.50f, 0.00f},   // 3: strong
  {1.45f, 1.30f, -0.06f},  // 4: medium, cooler
};
#ifndef TDECK_COLOR_DEFAULT
#define TDECK_COLOR_DEFAULT 2
#endif

const uint16_t* tdeck_color_lut = nullptr;
static uint16_t* s_lut = nullptr;
static int s_preset = -1;

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

void tdeckColorSet(int preset) {
  if (preset < 0 || preset >= TDECK_COLOR_PRESETS) preset = TDECK_COLOR_DEFAULT;
  if (preset == s_preset && s_lut) return;
  if (!s_lut) s_lut = (uint16_t*)heap_caps_malloc(65536 * 2, MALLOC_CAP_SPIRAM);
  if (!s_lut) return;
  const Preset& p = PRESETS[preset];
  for (uint32_t v = 0; v < 65536; v++) {
    float r = ((v >> 11) & 31) / 31.0f, g = ((v >> 5) & 63) / 63.0f, b = (v & 31) / 31.0f;
    float y = 0.299f * r + 0.587f * g + 0.114f * b;
    r = clamp01(y + (r - y) * p.sat);
    g = clamp01(y + (g - y) * p.sat);
    b = clamp01(y + (b - y) * p.sat);
    r = clamp01(powf(r, p.gamma) * (1 + p.warm));
    g = powf(g, p.gamma);
    b = clamp01(powf(b, p.gamma) * (1 - p.warm));
    uint16_t o = ((uint16_t)lroundf(r * 31) << 11) | ((uint16_t)lroundf(g * 63) << 5) | (uint16_t)lroundf(b * 31);
    s_lut[v] = __builtin_bswap16(o);
  }
  s_preset = preset;
  tdeck_color_lut = s_lut;
}

int tdeckColorPreset() { return s_preset; }
