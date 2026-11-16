#include "SF32Display.h"

#include <math.h>
#include <string.h>
#include "mem_section.h"
#include "bf0_hal.h"  // system_bf0_ap.h: mpu_dcache_clean
#include "SF32Font.h"
// The chip's graphics hardware: much faster on PSRAM than the CPU going through its cache
// (measured, one 480x480 frame: copy 15.5 ms by CPU, 5.2 by DMA; a blend 38 by CPU, 8 by EPIC).
// On the SF32LB52 the panel controller has one layer only, and EPIC 2 layers plus a mask at a time.
#if defined(BSP_USING_EPIC) && defined(BSP_USING_EXT_DMA)
#define SF32_GFX_HW 1
extern "C" {
#include "drv_epic.h"
#include "drv_ext_dma.h"
#include "drv_psram.h"
}
#endif

extern "C" void sf32_cpu_full_speed();  // SF32Board.cpp
#define LOG_TAG "display"
#include <ulog.h>

extern "C" LCDC_HandleTypeDef* get_drv_lcd_handler(void);  // drv_lcd.c
static void panel_rotate(int rot);  // setRotation()

#define RGB565(r, g, b) (ColorVal)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// "MeshCore" theme: MeshCore's slate (#1F2937, its docs/brand colour) for bars, cards
// and popups, a MeshCore blue accent, on black (kinder to an AMOLED: power, burn-in).
ColorVal UIColor::window_bkg = 0x0000;
ColorVal UIColor::title_bkg = RGB565(0x1F, 0x29, 0x37);      // slate
ColorVal UIColor::title_txt = RGB565(0xF3, 0xF4, 0xF6);
ColorVal UIColor::primary_txt = RGB565(0xF3, 0xF4, 0xF6);
ColorVal UIColor::secondary_txt = RGB565(0x9C, 0xA3, 0xAF);  // gray
ColorVal UIColor::warning_txt = RGB565(0xF5, 0xA5, 0x24);    // amber
ColorVal UIColor::popup_bkg = RGB565(0x37, 0x41, 0x51);      // lighter slate: stands out from cards
ColorVal UIColor::popup_txt = RGB565(0xF3, 0xF4, 0xF6);
ColorVal UIColor::corp_blue = RGB565(0x4C, 0x8D, 0xFF);      // MeshCore blue accent

#define PANEL_W  LCD_HOR_RES_MAX
#define PANEL_H  LCD_VER_RES_MAX

// Logical (128x64) -> panel mapping, and the clip area (the logical screen).
#define AREA_W   ((int)(SF32Display::W * SF32Display::SCALE + 0.5f))
#define WIDE_H   ((int)(SF32Display::H * SF32Display::SCALE + 0.5f))
// Burn-in care: the whole layout can move a few pixels (setPixelShift).
static int s_shift_x = 0, s_shift_y = 0;
#define OFFSET_X ((PANEL_W - AREA_W) / 2 + s_shift_x)
// Normal layout: 128x64, centred. Square layout (setSquareLayout, the clock page):
// 128x128 over almost the whole panel. Reset to normal by startFrame().
static int s_area_h = WIDE_H;
static int s_offset_y = (PANEL_H - WIDE_H) / 2;
#define AREA_H   s_area_h
#define OFFSET_Y (s_offset_y + s_shift_y)
static inline int px_x(int x) { return OFFSET_X + (int)lroundf(x * SF32Display::SCALE); }
static inline int px_y(int y) { return OFFSET_Y + (int)lroundf(y * SF32Display::SCALE); }

#define BLEED         10  // full-width bars extend this far past the text margin
#define CLIP_X0       (OFFSET_X - BLEED)
#define CLIP_X1       (OFFSET_X + AREA_W + BLEED)
#define LINE_PX       2   // drawRect line width
#define PANEL_RADIUS  9   // corner radius for large filled rects (title bar, popups)

// Full-panel frame buffers in PSRAM (460 KB each, of 8 MB), as in LilyGo's lcd example:
// two that alternate between "being drawn" and "last shown" (transitions slide from
// one to the other).
#define FB_PIXELS (PANEL_W * PANEL_H)
L2_NON_RET_BSS_SECT_BEGIN(mc_fb)
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint16_t s_fb_a[FB_PIXELS]);
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint16_t s_fb_b[FB_PIXELS]);
// Off-screen layer for scrolling lists: drawn once, then slices are copied into frames.
#define LAYER_ROWS 2600  // panel px (~742 logical px of list: the 20 log entries are 680)
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint16_t s_layer[PANEL_W * LAYER_ROWS]);
// The Glass style (setGlass): the wallpaper, and the same picture frosted (what cards show).
#define GLASS_GRID 121  // the wallpaper is averaged down to this grid (4 px cells), blurred, smoothed back
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint16_t s_wall[FB_PIXELS]);
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint16_t s_frost[FB_PIXELS]);
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint8_t s_glass_grid[GLASS_GRID * GLASS_GRID * 3]);
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint8_t s_glass_tmp[GLASS_GRID * GLASS_GRID * 3]);
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static float s_glass_curve[4][PANEL_W]);
#ifdef SF32_GFX_HW
// A menu's rows in the Glass style, for the graphics engine (layerRowsDone): what's on the panes
// with its alpha, and where the panes are.
#define OVER_W    420   // x = 4..124 logical: the menu rows' width, panel px
#define OVER_ROWS 1680  // 20 rows
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint8_t s_over[OVER_W * OVER_ROWS * 3]);  // ARGB8565
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint8_t s_mask[OVER_W * OVER_ROWS]);      // A8
#endif
L2_NON_RET_BSS_SECT_END
static uint16_t* s_fb = s_fb_a;     // frame being drawn
static uint16_t* s_shown = s_fb_b;  // frame on the panel
// For tools/sf32_debug_uart.py shot (@address of this pointer): the frame on the panel.
extern "C" { uint16_t* volatile sf32_shown_fb = s_fb_b; }

#define FADE_STEPS   8   // screen wake and sleep
#define FADE_STEP_MS 20
#define SLIDE_H_MS   240  // how long a page slides
#define SLIDE_V_MS   190  // how long a menu or screen slides up or down
#define SLIDE_LEAD_MS 30  // where in the curve the first frame lands: it's on screen a frame later
#ifdef SF32_REMOTE_TOUCH
extern "C" { volatile uint32_t sf32_anim_ms = 0, sf32_anim_frames = 0, sf32_drag_sends = 0; }  // debug builds
#endif

static struct rt_semaphore s_flush_done;

static rt_err_t on_flush_done(rt_device_t, void*) {
  rt_sem_release(&s_flush_done);
  return RT_EOK;
}

static void glass_check();  // the Glass pictures are intact? (setGlass)

bool SF32Display::begin() {
  _lcd = rt_device_find("lcd");
  if (!_lcd || rt_device_open(_lcd, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
    LOG_E("lcd device not available");
    _lcd = nullptr;
    return false;
  }
  rt_sem_init(&s_flush_done, "mcfb", 0, RT_IPC_FLAG_FIFO);
  uint16_t fmt = RTGRAPHIC_PIXEL_FORMAT_RGB565;
  rt_device_control(_lcd, RTGRAPHIC_CTRL_SET_BUF_FORMAT, &fmt);
  memset(s_fb_a, 0, sizeof(s_fb_a));
  memset(s_fb_b, 0, sizeof(s_fb_b));
  turnOn();
  return true;
}


void SF32Display::turnOn() {
  if (!_lcd) return;
  sf32_cpu_full_speed();  // the display controller is timed from the CPU clock
  if (!_on) {
    // Power up dark and show nothing old: the UI renders the current screen right away,
    // and endFrame() turns the brightness up once that frame is on the panel.
    waitFlush();
    rt_device_control(_lcd, RTGRAPHIC_CTRL_POWERON, RT_NULL);
    uint8_t dark = 0;
    rt_device_control(_lcd, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &dark);
    _wake_pending = true;
    glass_check();
  }
  _on = true;
}

void SF32Display::setBrightness(uint8_t percent) {
  _brightness = percent;
  if (_lcd && _on && !_wake_pending) {
    waitFlush();
    rt_device_control(_lcd, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &_brightness);
  }
}

void SF32Display::setClip(int x, int y, int w, int h) {
  _clip_x0 = px_x(x);
  _clip_y0 = px_y(y);
  _clip_x1 = px_x(x + w);
  _clip_y1 = px_y(y + h);
  _clip_on = true;
}

void SF32Display::pushClip(int x, int y, int w, int h) {
  _saved_on = _clip_on;
  _saved_x0 = _clip_x0; _saved_y0 = _clip_y0; _saved_x1 = _clip_x1; _saved_y1 = _clip_y1;
  int x0 = px_x(x), y0 = px_y(y), x1 = px_x(x + w), y1 = px_y(y + h);
  if (_clip_on) {
    if (x0 < _clip_x0) x0 = _clip_x0;
    if (y0 < _clip_y0) y0 = _clip_y0;
    if (x1 > _clip_x1) x1 = _clip_x1;
    if (y1 > _clip_y1) y1 = _clip_y1;
  }
  _clip_x0 = x0; _clip_y0 = y0; _clip_x1 = x1; _clip_y1 = y1;
  _clip_on = true;
}

void SF32Display::popClip() {
  _clip_on = _saved_on;
  _clip_x0 = _saved_x0; _clip_y0 = _saved_y0; _clip_x1 = _saved_x1; _clip_y1 = _saved_y1;
}

void SF32Display::setCursorFine(float x, int y) {
  _cx = (int)x;
  _cy = y;
  _pen16 = (int)((OFFSET_X + x * SCALE) * 16);
  _pen_valid = true;
}

void SF32Display::setPixelShift(int dx, int dy) {
  s_shift_x = dx;
  s_shift_y = dy;
}

void SF32Display::panelToLogical(int px, int py, int& x, int& y) {
  int off = (PANEL_H - AREA_W) / 2 + s_shift_y;  // square layout: AREA_W x AREA_W, centred
  x = (int)floorf((px - OFFSET_X) / SCALE);
  y = (int)floorf((py - off) / SCALE);
}

void SF32Display::turnOff() {
  if (!_lcd) return;
  waitFlush();
  if (_on) {
    for (int i = FADE_STEPS - 1; i >= 1 && !_wake_pending; i--) {  // fade down
      uint8_t b = (uint8_t)(_brightness * i * i / (FADE_STEPS * FADE_STEPS));
      if (b < 1) b = 1;
      rt_device_control(_lcd, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &b);
      rt_thread_mdelay(FADE_STEP_MS);
    }
    rt_device_control(_lcd, RTGRAPHIC_CTRL_POWEROFF, RT_NULL);
  }
  _on = false;
  _transition = TRANSITION_NONE;
}

// ---- the Glass style ----
#define GLASS_KEY     0x0841  // in a layer: "a pane here", frosted when the layer is composed
#define GLASS_RADIUS  13
static bool s_glass_on = false, s_glass_paused = false, s_glass_multi = false;
static uint16_t s_glass_accent = 0;
static uint8_t s_glass_bg = 0;
static uint32_t s_glass_sum = 0;
static inline bool glassActive() { return s_glass_on && !s_glass_paused; }

static inline uint16_t add565(uint16_t a, uint16_t b) {
  int r = (a >> 11) + (b >> 11), g = ((a >> 5) & 0x3F) + ((b >> 5) & 0x3F), bl = (a & 0x1F) + (b & 0x1F);
  return (uint16_t)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (bl > 31 ? 31 : bl));
}

static void glass_hsv(float h, float s, float v, float* rgb) {  // 0..255
  h = fmodf(h, 360.0f);
  if (h < 0) h += 360.0f;
  float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
  rgb[0] = (r + m) * 255; rgb[1] = (g + m) * 255; rgb[2] = (b + m) * 255;
}

// Ordered dither, so the slow gradients don't show bands in 16-bit colour.
static const uint8_t BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static inline uint16_t glass_pixel(int x, int y, int r, int g, int b) {
  int d = BAYER4[(y & 3) * 4 + (x & 3)];
  r += d / 2; g += d / 4; b += d / 2;
  r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint32_t glass_sum() {  // a quick sample of both pictures (setGlass, glass_check)
  uint32_t sum = 0;
  for (int i = 0; i < 64; i++) {
    int p = (i * 3607 + 11) % FB_PIXELS;
    sum = sum * 31 + s_wall[p] + ((uint32_t)s_frost[p] << 16);
  }
  return sum;
}

// The light on the glass (setGlassLight): a soft glint in the frost, centred at a panel point
// that follows how the board is tilted. Baked into the frost, so every pane shows it in the
// right place whatever draws it (direct, list layers, the hardware compose).
#define GLINT_R 175
static bool s_light_on = false;
static int s_light_x = 150, s_light_y = 70;
static inline int glint_add(int x, int y) {  // 0..GLINT_MAX added to each channel (0..255)
  if (!s_light_on) return 0;
  int dx = x - s_light_x, dy = y - s_light_y, d2 = dx * dx + dy * dy;
  const int R2 = GLINT_R * GLINT_R;
  if (d2 >= R2) return 0;
  int q = (R2 - d2) / (R2 / 64);  // 0..64
  return q * q * 24 / (64 * 64);   // a soft peak, ~24 at the centre
}

static inline void glass_screen(float* out, const float* c, float a) {  // lighter where they overlap, never clips
  for (int k = 0; k < 3; k++) {
    float v = c[k] * a;
    out[k] = out[k] + v - out[k] * v / 255;
  }
}

// The three colours each background is made from: the theme's hue and two neighbours.
#define GLASS_HUES 3
static const float GLASS_HUE_OFFSET[GLASS_HUES] = {0, 48, -42};
static const float GLASS_MULTI_HUE[GLASS_HUES] = {300, 190, 40};  // Rainbow: its own

// Aurora: glowing ribbons, a sharp edge above, fading out below.
static const float AURORA_Y[3] = {140, 255, 365}, AURORA_AMP[3] = {55, 45, 60}, AURORA_FREQ[3] = {0.011f, 0.015f, 0.009f},
                   AURORA_PHASE[3] = {0.6f, 2.4f, 4.1f}, AURORA_AMP2[3] = {18, 16, 20}, AURORA_FREQ2[3] = {0.031f, 0.027f, 0.037f},
                   AURORA_V[3] = {0.58f, 0.50f, 0.44f};
// Dunes: layers of hills, back to front, each lightest along its crest.
static const float DUNE_Y[4] = {165, 245, 325, 410}, DUNE_AMP[4] = {42, 36, 30, 24}, DUNE_FREQ[4] = {0.010f, 0.013f, 0.008f, 0.016f},
                   DUNE_PHASE[4] = {1.1f, 3.6f, 5.2f, 0.4f}, DUNE_AMP2[4] = {12, 14, 10, 9}, DUNE_FREQ2[4] = {0.029f, 0.024f, 0.035f, 0.041f},
                   DUNE_V[4] = {0.50f, 0.42f, 0.34f, 0.24f};
static const uint8_t DUNE_COLOR[4] = {1, 0, 2, 0};

static int s_frost_tint[3] = {0, 0, 0};
// The frost's pixels in a rectangle, from the blurred grid: the veil, a fine grain (from the
// position, so a region can be redone exactly) and the light's glint.
static void frost_region(int x0, int y0, int x1, int y1) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > PANEL_W) x1 = PANEL_W;
  if (y1 > PANEL_H) y1 = PANEL_H;
  for (int y = y0; y < y1; y++) {
    int gy = y / 4, ty = y & 3;
    const uint8_t* row0 = s_glass_grid + gy * GLASS_GRID * 3;
    const uint8_t* row1 = row0 + GLASS_GRID * 3;
    uint16_t* f = s_frost + y * PANEL_W + x0;
    for (int x = x0; x < x1; x++) {
      int gx = x / 4, tx = x & 3;
      const uint8_t* a = row0 + gx * 3, *b = row1 + gx * 3;
      uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663);
      h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
      int n = (int)(h & 7) - 3 + glint_add(x, y);  // a fine grain, and the light
      int c[3];
      for (int k = 0; k < 3; k++) {
        int top = a[k] * (4 - tx) + a[k + 3] * tx, bot = b[k] * (4 - tx) + b[k + 3] * tx;
        c[k] = (top * (4 - ty) + bot * ty) / 16;
        c[k] = c[k] * 12 / 16 + 24 + s_frost_tint[k] + n;  // the veil
      }
      *f++ = glass_pixel(x, y, c[0], c[1], c[2]);
    }
  }
}

static void glass_build() {
  float r0 = ((s_glass_accent >> 11) & 0x1F) / 31.0f, g0 = ((s_glass_accent >> 5) & 0x3F) / 63.0f, b0 = (s_glass_accent & 0x1F) / 31.0f;
  float mx = fmaxf(r0, fmaxf(g0, b0)), mn = fminf(r0, fminf(g0, b0)), dl = mx - mn, hue = 0;
  if (dl > 0.001f) {
    if (mx == r0) hue = 60 * fmodf((g0 - b0) / dl + 6, 6);
    else if (mx == g0) hue = 60 * ((b0 - r0) / dl + 2);
    else hue = 60 * ((r0 - g0) / dl + 4);
  }
  float sat = (mx > 0 && dl / mx < 0.15f) ? 0.0f : 0.78f;  // the Mono theme stays grey
  float base[3], unit[GLASS_HUES][3];  // base: the darkest part; unit: at full value
  glass_hsv(hue, sat * 0.7f, 0.08f, base);
  for (int i = 0; i < GLASS_HUES; i++)
    glass_hsv(s_glass_multi ? GLASS_MULTI_HUE[i] : hue + GLASS_HUE_OFFSET[i], sat, 1.0f, unit[i]);

  if (s_glass_bg < 2) {  // the curves, a column at a time (sines are slow)
    for (int x = 0; x < PANEL_W; x++) {
      for (int i = 0; i < 4; i++) {
        if (s_glass_bg == 0 && i == 3) { s_glass_curve[i][x] = 0; continue; }
        s_glass_curve[i][x] = s_glass_bg == 0
          ? AURORA_Y[i] + AURORA_AMP[i] * sinf(x * AURORA_FREQ[i] + AURORA_PHASE[i]) + AURORA_AMP2[i] * sinf(x * AURORA_FREQ2[i] + AURORA_PHASE[i] * 2)
          : DUNE_Y[i] + DUNE_AMP[i] * sinf(x * DUNE_FREQ[i] + DUNE_PHASE[i]) + DUNE_AMP2[i] * sinf(x * DUNE_FREQ2[i] + DUNE_PHASE[i] * 2);
      }
    }
  }

  uint16_t* w = s_wall;
  float px[3];
  for (int y = 0; y < PANEL_H; y++) {
    float fade = 1.0f - y / 480.0f * 0.5f;
    for (int x = 0; x < PANEL_W; x++) {
      if (s_glass_bg == 3) {  // None: black
        *w++ = 0;
        continue;
      }
      if (s_glass_bg == 2) {  // Gradient: corner to corner, through the three colours, darkening
        float t = (x + y) / 958.0f;
        const float* a = t < 0.5f ? unit[0] : unit[1], *b = t < 0.5f ? unit[1] : unit[2];
        float u = t < 0.5f ? t * 2 : t * 2 - 1, v = 0.52f - 0.40f * t;
        for (int k = 0; k < 3; k++) px[k] = (a[k] + (b[k] - a[k]) * u) * v + base[k] * 0.5f;
      } else {
        for (int k = 0; k < 3; k++) px[k] = base[k] * fade;
        if (s_glass_bg == 0) {
          for (int i = 0; i < 3; i++) {
            float d = y - s_glass_curve[i][x], t = d / (d < 0 ? 24.0f : 115.0f), f = 1 / (1 + t * t);
            glass_screen(px, unit[i], AURORA_V[i] * f * f);
          }
        } else {
          for (int i = 0; i < 4; i++) {
            float d = y - s_glass_curve[i][x];
            if (d <= -0.5f) continue;
            float cov = d >= 0.5f ? 1 : d + 0.5f, shade = 1 - d / 300;
            if (shade < 0.35f) shade = 0.35f;
            float v = DUNE_V[i] * shade;
            for (int k = 0; k < 3; k++) {
              float c = unit[DUNE_COLOR[i]][k] * v + base[k] * 0.5f;
              px[k] += (c - px[k]) * cov;
            }
          }
        }
      }
      *w++ = glass_pixel(x, y, (int)px[0], (int)px[1], (int)px[2]);
    }
  }

  // Frost: the wallpaper itself, blurred, with a veil of light over it. Averaged down to the
  // grid, blurred there (about 20 px across), then smoothed back up.
  uint8_t* g = s_glass_grid;
  for (int gy = 0; gy < GLASS_GRID; gy++) {
    for (int gx = 0; gx < GLASS_GRID; gx++) {
      int x0 = gx * 4 < PANEL_W - 4 ? gx * 4 : PANEL_W - 4, y0 = gy * 4 < PANEL_H - 4 ? gy * 4 : PANEL_H - 4;
      int sr = 0, sg = 0, sb = 0;
      for (int j = 0; j < 4; j++) {
        const uint16_t* row = s_wall + (y0 + j) * PANEL_W + x0;
        for (int i = 0; i < 4; i++) {
          uint16_t p = row[i];
          sr += (p >> 11) << 3; sg += ((p >> 5) & 0x3F) << 2; sb += (p & 0x1F) << 3;
        }
      }
      *g++ = sr / 16; *g++ = sg / 16; *g++ = sb / 16;
    }
  }
  for (int pass = 0; pass < 5; pass++) {
    memcpy(s_glass_tmp, s_glass_grid, sizeof(s_glass_tmp));
    for (int gy = 0; gy < GLASS_GRID; gy++) {
      for (int gx = 0; gx < GLASS_GRID; gx++) {
        for (int k = 0; k < 3; k++) {
          int sum = 0;
          for (int dy = -1; dy <= 1; dy++) {
            int yy = gy + dy < 0 ? 0 : (gy + dy >= GLASS_GRID ? GLASS_GRID - 1 : gy + dy);
            for (int dx = -1; dx <= 1; dx++) {
              int xx = gx + dx < 0 ? 0 : (gx + dx >= GLASS_GRID ? GLASS_GRID - 1 : gx + dx);
              sum += s_glass_tmp[(yy * GLASS_GRID + xx) * 3 + k];
            }
          }
          s_glass_grid[(gy * GLASS_GRID + gx) * 3 + k] = sum / 9;
        }
      }
    }
  }
  // With no backdrop there's nothing to frost: the panes are just the veil, tinted by the theme.
  int tint[3] = {0, 0, 0};
  // (Rainbow gives each page its own colour, so one tint would be wrong on all of them: none.)
  if (s_glass_bg == 3 && !s_glass_multi) for (int k = 0; k < 3; k++) tint[k] = (int)(unit[0][k] * 0.07f);
  for (int k = 0; k < 3; k++) s_frost_tint[k] = tint[k];
  frost_region(0, 0, PANEL_W, PANEL_H);
  s_glass_sum = glass_sum();
  mpu_dcache_clean((void*)s_wall, FB_PIXELS * 2);  // the hardware reads these directly
  mpu_dcache_clean((void*)s_frost, FB_PIXELS * 2);
}

// Screen back on: if a sleep cost us the pictures (they live in PSRAM), make them again.
static void glass_check() {
  if (s_glass_on && glass_sum() != s_glass_sum) glass_build();
}

void SF32Display::setGlass(bool on, uint16_t accent, bool multi, uint8_t background) {
  bool same = s_glass_sum && accent == s_glass_accent && multi == s_glass_multi && background == s_glass_bg;
  s_glass_accent = accent;
  s_glass_multi = multi;
  s_glass_bg = background;
  s_glass_on = on;
  if (on && !same) glass_build();
}

bool SF32Display::glassOn() const { return glassActive(); }
void SF32Display::setGlassPaused(bool paused) { s_glass_paused = paused; }

// Fill n pixels with one colour, two at a time (much faster than 16-bit stores).
// Fill n pixels with one colour. Big fills (a frame, a layer, strips during slides) go by DMA,
// so a theme's background (black or ChattaMesh's navy alike) costs the CPU almost nothing.
static void fill16(uint16_t* p, ColorVal c, int n) {
#ifdef SF32_GFX_HW
  if (n >= 4096 && ((uintptr_t)p & 3) == 0 && (n & 1) == 0) {
    static uint32_t word = 0;  // in RAM: not const (flash is very slow for DMA)
    word = ((uint32_t)c << 16) | c;
    mpu_dcache_clean(&word, 4);
    mpu_dcache_clean((void*)p, n * 2);
    if (EXT_DMA_Config(0, 1) == RT_EOK && EXT_DMA_TRANS_SYNC((uint32_t)&word, (uint32_t)p, n / 2, 200) == RT_EOK) {
      mpu_dcache_invalidate((void*)p, n * 2);
      return;
    }
  }
#endif
  if (c == 0) { memset(p, 0, n * 2); return; }
  uint32_t two = ((uint32_t)c << 16) | c;
  uint32_t* q = (uint32_t*)p;  // frame buffers are 64-byte aligned, n even
  for (int i = 0; i < n / 2; i++) q[i] = two;
}

void SF32Display::clear() {
  fill16(s_fb, UIColor::window_bkg, PANEL_W * PANEL_H);
}

// Draw into the layer instead of the frame: logical y 0 = layer row 0, same x mapping.
static uint16_t* s_saved_fb;
static int s_saved_area_h, s_saved_offset_y;
static int s_layer_h = 0;  // rows drawn in the last beginLayer()
// each layer row's content, [x0, x1) panel px (x0 >= x1: none), when s_spans_ok (layerSpans)
static uint16_t s_span0[LAYER_ROWS], s_span1[LAYER_ROWS];
static bool s_spans_ok = false;

static uint32_t s_layer_gen = 0;  // bumped each time the layer is cleared for a new list
uint32_t SF32Display::layerGen() const { return s_layer_gen; }

int SF32Display::beginLayer(int h, bool clear) {
  s_layer_gen++;
  s_spans_ok = false;
  int rows = (int)lroundf(h * SCALE) + 4;
  if (rows > LAYER_ROWS) rows = LAYER_ROWS;
  s_layer_h = rows;
  s_saved_fb = s_fb;
  s_saved_area_h = s_area_h;
  s_saved_offset_y = s_offset_y;
  s_fb = s_layer;
  s_offset_y = -s_shift_y;  // OFFSET_Y (which adds the burn-in shift) = 0
  s_area_h = rows;
  if (clear) fill16(s_layer, UIColor::window_bkg, rows * PANEL_W);
  else {  // the caller clears what it draws: the few spare rows past it are still ours to clear
    int content = (int)lroundf(h * SCALE);
    if (content < rows) fill16(s_layer + content * PANEL_W, UIColor::window_bkg, (rows - content) * PANEL_W);
  }
  return rows;
}

void SF32Display::clearRows(int y, int h) {
  int y0 = px_y(y), y1 = px_y(y + h);
  if (y0 < OFFSET_Y) y0 = OFFSET_Y;
  if (y1 > OFFSET_Y + AREA_H) y1 = OFFSET_Y + AREA_H;
  if (y1 > y0) fill16(s_fb + y0 * PANEL_W, UIColor::window_bkg, (y1 - y0) * PANEL_W);
}

void SF32Display::resumeLayer() {
  s_spans_ok = false;
  s_saved_fb = s_fb;
  s_saved_area_h = s_area_h;
  s_saved_offset_y = s_offset_y;
  s_fb = s_layer;
  s_offset_y = -s_shift_y;
  s_area_h = s_layer_h;
}

void SF32Display::layerSpans() {
  const uint16_t bg = UIColor::window_bkg;
  const uint32_t bg2 = ((uint32_t)bg << 16) | bg;
  for (int r = 0; r < s_layer_h; r++) {
    const uint32_t* row = (const uint32_t*)(s_layer + r * PANEL_W);
    int a = 0, b = PANEL_W / 2;
    while (a < b && (row[a] == bg2 || row[a] == 0)) a++;
    while (b > a && (row[b - 1] == bg2 || row[b - 1] == 0)) b--;
    s_span0[r] = a * 2; s_span1[r] = b * 2;
  }
  s_spans_ok = true;
}

void SF32Display::endLayer() {
  s_fb = s_saved_fb;
  s_area_h = s_saved_area_h;
  s_offset_y = s_saved_offset_y;
}

// Copy layer rows [src_px, src_px + rect height) into the frame rectangle (logical).
void SF32Display::composeLayer(int x, int y, int w, int h, int src_px) {
  int x0 = px_x(x), x1 = px_x(x + w), y0 = px_y(y), y1 = px_y(y + h);
  if (x0 < 0) x0 = 0;
  if (x1 > PANEL_W) x1 = PANEL_W;
  const bool glass = glassActive();
  if (_list_frame) {  // nothing was drawn behind it: what's around the list, band margin included
    for (int py = y0 - 6; py < y1 + 6; py++) {
      if (py < 0 || py >= PANEL_H) continue;
      uint16_t* dst = s_fb + py * PANEL_W;
      const uint16_t* wall = s_wall + py * PANEL_W;
      if (py < y0 || py >= y1) {
        if (glass) memcpy(dst, wall, PANEL_W * 2); else fill16(dst, UIColor::window_bkg, PANEL_W);
        continue;
      }
      for (int side = 0; side < 2; side++) {
        int a = side ? x1 : 0, b = side ? PANEL_W : x0;
        for (int i = a; i < b; i++) dst[i] = glass ? wall[i] : UIColor::window_bkg;
      }
    }
  }
  if (glass) {  // the wallpaper stays fixed behind the list, panes frost what's under them
    const bool bare = _list_frame;  // also fill in the wallpaper
    for (int py = y0; py < y1; py++) {
      if (py < 0 || py >= PANEL_H) continue;
      int src = src_px + (py - y0);
      uint16_t* dst = s_fb + py * PANEL_W;
      const uint16_t* wall = s_wall + py * PANEL_W;
      if (src < 0 || src >= s_layer_h) {
        if (bare) memcpy(dst + x0, wall + x0, (x1 - x0) * 2);
        continue;
      }
      const uint16_t* from = s_layer + src * PANEL_W;
      const uint16_t* frost = s_frost + py * PANEL_W;
      int rx0 = x0, rx1 = x1;
      if (s_spans_ok && !bare) {  // only where the row has something (the frame has the wallpaper)
        if (s_span0[src] > rx0) rx0 = s_span0[src];
        if (s_span1[src] < rx1) rx1 = s_span1[src];
      }
      // the layer's background (black, or a theme's colour such as ChattaMesh's navy) is bare
      // wallpaper, already in the frame: skipped two pixels at a time (it was blended, ~47 ms)
      const uint16_t bg = UIColor::window_bkg;
      const uint32_t bg2 = ((uint32_t)bg << 16) | bg;
      const uint32_t key2 = ((uint32_t)GLASS_KEY << 16) | GLASS_KEY;
      for (int i = rx0; i < rx1; i++) {
        if (!(i & 1) && i + 1 < rx1) {  // two at a time where they're plain pane or background
          uint32_t v = *(const uint32_t*)(from + i);
          if (v == key2) { *(uint32_t*)(dst + i) = *(const uint32_t*)(frost + i); i++; continue; }
          if (v == bg2 || v == 0) { if (bare) *(uint32_t*)(dst + i) = *(const uint32_t*)(wall + i); i++; continue; }
        }
        uint16_t p = from[i];
        if (p == GLASS_KEY) dst[i] = frost[i];
        else if (p == 0 || p == bg) { if (bare) dst[i] = wall[i]; }
        else dst[i] = add565(frost[i], p);
      }
    }
    return;
  }
  for (int py = y0; py < y1; py++) {
    if (py < 0 || py >= PANEL_H) continue;
    int src = src_px + (py - y0);
    uint16_t* dst = s_fb + py * PANEL_W + x0;
    if (src >= 0 && src < s_layer_h) memcpy(dst, s_layer + src * PANEL_W + x0, (x1 - x0) * 2);
    else for (int i = 0; i < x1 - x0; i++) dst[i] = UIColor::window_bkg;
  }
}

void SF32Display::composeSprites(int x, int y, int w, int h, int src_px) {
  if (!glassActive()) { composeLayer(x, y, w, h, src_px); return; }  // a plain colour: a straight copy
  int x0 = px_x(x), x1 = px_x(x + w), y0 = px_y(y), y1 = px_y(y + h);
  if (x0 < 0) x0 = 0;
  if (x1 > PANEL_W) x1 = PANEL_W;
  x0 &= ~1; x1 &= ~1;  // pairs of pixels at a time
  const uint16_t bg = UIColor::window_bkg;
  const uint32_t bg2 = ((uint32_t)bg << 16) | bg;
  for (int py = y0; py < y1; py++) {  // the frame already has the wallpaper: copy only what's drawn
    int src = src_px + (py - y0);
    if (py < 0 || py >= PANEL_H || src < 0 || src >= s_layer_h) continue;
    const uint32_t* from = (const uint32_t*)(s_layer + src * PANEL_W + x0);
    uint16_t* dst = s_fb + py * PANEL_W + x0;
    for (int i = 0, n = (x1 - x0) / 2; i < n; i++) {
      uint32_t v = from[i];
      if (v == bg2) continue;  // the commonest by far
      uint16_t a = v & 0xFFFF, b = v >> 16;
      if (a != bg) dst[i * 2] = a;
      if (b != bg) dst[i * 2 + 1] = b;
    }
  }
}

void SF32Display::setSquareLayout(bool square) {
  s_area_h = square ? AREA_W : WIDE_H;
  s_offset_y = (PANEL_H - s_area_h) / 2;
}

// ---- the frame's background, by DMA ----
static void frame_from(const uint16_t* src) {
#ifdef SF32_GFX_HW
  mpu_dcache_clean((void*)s_fb, FB_PIXELS * 2);  // anything the CPU left there mustn't land on top later
  if (EXT_DMA_Config(1, 1) == RT_EOK && EXT_DMA_TRANS_SYNC((uint32_t)src, (uint32_t)s_fb, FB_PIXELS / 2, 200) == RT_EOK) {
    mpu_dcache_invalidate((void*)s_fb, FB_PIXELS * 2);
    return;
  }
#endif
  memcpy(s_fb, src, FB_PIXELS * 2);
}
// The frame cleared to one colour by DMA (black, or a theme's background such as ChattaMesh's
// navy): the CPU fill took several ms a frame, enough to starve the sound during page slides.
static void frame_fill(ColorVal c) {
#ifdef SF32_GFX_HW
  static uint32_t word = 0;  // in RAM: not const, which would put it in flash, very slow for DMA
  word = ((uint32_t)c << 16) | c;
  mpu_dcache_clean(&word, 4);
  mpu_dcache_clean((void*)s_fb, FB_PIXELS * 2);
  if (EXT_DMA_Config(0, 1) == RT_EOK && EXT_DMA_TRANS_SYNC((uint32_t)&word, (uint32_t)s_fb, FB_PIXELS / 2, 200) == RT_EOK) {
    mpu_dcache_invalidate((void*)s_fb, FB_PIXELS * 2);
    return;
  }
#endif
  fill16(s_fb, c, FB_PIXELS);
}

#ifdef SF32_GFX_HW
// ---- a menu's list, by the graphics engine ----
static void epic_input(EPIC_LayerConfigTypeDef* l, const void* data, uint32_t mode, int x, int y, int w, int h, int total) {
  HAL_EPIC_LayerConfigInit(l);
  l->data = (uint8_t*)data;
  l->color_mode = mode;
  l->x_offset = x; l->y_offset = y;
  l->width = w; l->height = h;
  l->total_width = total;
  l->alpha = 255;
  l->ax_mode = ALPHA_BLEND_RGBCOLOR;
}

// Fill bytes by DMA (the whole words of it; the ends by CPU), leaving the cache out of it.
static void dma_fill(uint8_t* dst, uint8_t value, int len) {
  static uint32_t word;
  word = value * 0x01010101UL;
  uint8_t* first = (uint8_t*)(((uintptr_t)dst + 3) & ~(uintptr_t)3);
  int head = first - dst, words = (len - head) / 4;
  mpu_dcache_clean(dst, len);
  mpu_dcache_clean(&word, 4);
  if (words <= 0 || EXT_DMA_Config(0, 1) != RT_EOK || EXT_DMA_TRANS_SYNC((uint32_t)&word, (uint32_t)first, words, 100) != RT_EOK) {
    memset(dst, value, len);
    return;
  }
  mpu_dcache_invalidate(dst, len);
  memset(dst, value, head);
  memset(first + words * 4, value, len - head - words * 4);
}

// Rows of the layer were just drawn: get them ready for composeList. What's light added to a pane
// (text, rims, sheen) becomes a colour with an alpha; anything drawn at all marks a pane.
void SF32Display::layerRowsDone(int y, int h) {
  int y0 = (int)lroundf(y * SCALE), y1 = (int)lroundf((y + h) * SCALE);
  if (y0 < 0) y0 = 0;
  if (y1 > s_layer_h) y1 = s_layer_h;
  const int x0 = px_x(4);
  if (glassActive()) {
    if (y1 > OVER_ROWS) y1 = OVER_ROWS;
    if (y1 <= y0) return;
    // Most of it is bare pane: fill it all by DMA, then the CPU only touches what differs.
    dma_fill(s_over + y0 * OVER_W * 3, 0, (y1 - y0) * OVER_W * 3);
    dma_fill(s_mask + y0 * OVER_W, 0xFF, (y1 - y0) * OVER_W);
    for (int py = y0; py < y1; py++) {
      const uint16_t* from = s_layer + py * PANEL_W + x0;
      uint8_t* o = s_over + py * OVER_W * 3;
      uint8_t* m = s_mask + py * OVER_W;
      for (int i = 0; i < OVER_W; i++) {
        uint16_t p = from[i];
        if (p == GLASS_KEY) continue;
        if (p == 0) { m[i] = 0; continue; }
        int r = (p >> 11) << 3, g = ((p >> 5) & 0x3F) << 2, b = (p & 0x1F) << 3;
        int a = r > g ? r : g;
        if (b > a) a = b;
        r = r * 255 / a; g = g * 255 / a; b = b * 255 / a;
        uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        o[i * 3] = c & 0xFF; o[i * 3 + 1] = c >> 8; o[i * 3 + 2] = a;
      }
    }
    mpu_dcache_clean(s_over + y0 * OVER_W * 3, (y1 - y0) * OVER_W * 3);
    mpu_dcache_clean(s_mask + y0 * OVER_W, (y1 - y0) * OVER_W);
  } else if (y1 > y0) {
    mpu_dcache_clean(s_layer + y0 * PANEL_W, (y1 - y0) * PANEL_W * 2);
  }
}

// A slice of a menu's rows into the frame: panes frosting what's behind, then what's on them. On
// a list frame (nothing drawn behind it) the wallpaper goes in first, band margins included.
int SF32Display::listRows() { return glassActive() ? OVER_ROWS : s_layer_h; }
bool SF32Display::composeList(int y, int h, int src_px) {
  const int x0 = px_x(4), x1 = x0 + OVER_W;
  int y0 = px_y(y), y1 = px_y(y + h);
  if (y0 < 0) y0 = 0;
  if (y1 > PANEL_H) y1 = PANEL_H;
  if (src_px < 0 || y1 <= y0) return true;
  const int rows = glassActive() ? OVER_ROWS : s_layer_h;
  if (src_px + (y1 - y0) > rows) return false;  // past what the hardware's copy holds: composeLayer does it
  if (y1 <= y0) return true;
  const int by0 = _list_frame ? (y0 - 6 < 0 ? 0 : y0 - 6) : y0;
  const int by1 = _list_frame ? (y1 + 6 > PANEL_H ? PANEL_H : y1 + 6) : y1;
  uint16_t* dst = s_fb + by0 * PANEL_W;
  mpu_dcache_clean(dst, (by1 - by0) * PANEL_W * 2);
  EPIC_HandleTypeDef* epic = drv_get_epic_handle();
  if (!epic) return false;
  EPIC_LayerConfigTypeDef in[3], out;
  bool ok = true;
  drv_epic_take(RT_WAITING_FOREVER);
  if (glassActive()) {
    const int bx0 = _list_frame ? 0 : x0, bw = _list_frame ? PANEL_W : OVER_W;
    epic_input(&in[0], s_wall + by0 * PANEL_W + bx0, EPIC_COLOR_RGB565, bx0, by0, bw, by1 - by0, PANEL_W);
    epic_input(&in[1], s_frost + y0 * PANEL_W + x0, EPIC_COLOR_RGB565, x0, y0, OVER_W, y1 - y0, PANEL_W);
    epic_input(&in[2], s_mask + src_px * OVER_W, EPIC_COLOR_A8, x0, y0, OVER_W, y1 - y0, OVER_W);
    in[2].ax_mode = ALPHA_BLEND_MASK;
    epic_input(&out, dst + bx0, EPIC_COLOR_RGB565, bx0, by0, bw, by1 - by0, PANEL_W);
    ok = HAL_EPIC_BlendStartEx(epic, in, 3, &out) == HAL_OK;
    if (ok) {
      epic_input(&in[0], s_fb + y0 * PANEL_W + x0, EPIC_COLOR_RGB565, x0, y0, OVER_W, y1 - y0, PANEL_W);
      epic_input(&in[1], s_over + src_px * OVER_W * 3, EPIC_COLOR_ARGB8565, x0, y0, OVER_W, y1 - y0, OVER_W);
      epic_input(&out, s_fb + y0 * PANEL_W + x0, EPIC_COLOR_RGB565, x0, y0, OVER_W, y1 - y0, PANEL_W);
      ok = HAL_EPIC_BlendStartEx(epic, in, 2, &out) == HAL_OK;
    }
  } else {
    epic_input(&in[0], s_layer + src_px * PANEL_W + x0, EPIC_COLOR_RGB565, x0, y0, OVER_W, y1 - y0, PANEL_W);
    epic_input(&out, s_fb + y0 * PANEL_W + x0, EPIC_COLOR_RGB565, x0, y0, OVER_W, y1 - y0, PANEL_W);
    ok = HAL_EPIC_BlendStartEx(epic, in, 1, &out) == HAL_OK;
  }
  drv_epic_release();
  mpu_dcache_invalidate(dst, (by1 - by0) * PANEL_W * 2);
  return ok;
}
#else
void SF32Display::layerRowsDone(int, int) {}
bool SF32Display::composeList(int, int, int) { return false; }
int SF32Display::listRows() { return 0; }
#endif


#ifdef SF32_REMOTE_TOUCH
// Debug builds: recent frames, for tools/frames.sh. Gap since the last frame, and time to draw.
extern "C" { volatile uint32_t sf32_gaps[32] = {0}, sf32_draws[32] = {0}, sf32_frame_n = 0; }
static rt_tick_t s_last_end = 0, s_frame_t0 = 0;
#endif

void SF32Display::startFrame(ColorVal bkg) {
#ifdef SF32_REMOTE_TOUCH
  s_frame_t0 = rt_tick_get();
#endif
  setSquareLayout(false);
  _clip_on = false;
  _list_frame = _list_next && _transition == TRANSITION_NONE && !_wake_pending;
  _list_next = false;
  if (_list_frame) {}  // composeLayer builds the list; the rest isn't sent this frame
  else if (glassActive() && bkg == UIColor::window_bkg && s_glass_bg != 3) frame_from(s_wall);
  else frame_fill(bkg);
  _color = UIColor::primary_txt;
  _text_size = 1;
  _cx = _cy = 0;
}

// ---- pixels ----

// fg over bg at a/15 (RGB565): blendPixel, and the shaded shapes' anti-aliased rims
static inline uint16_t blend565(uint16_t fg, uint16_t bg, int a) {
  int r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (15 - a)) / 15;
  int g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * (15 - a)) / 15;
  int b = ((fg & 0x1F) * a + (bg & 0x1F) * (15 - a)) / 15;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

bool SF32Display::clipBounds(int& x0, int& y0, int& x1, int& y1) const {
  int cx0 = CLIP_X0, cx1 = CLIP_X1 - 1, cy0 = OFFSET_Y, cy1 = OFFSET_Y + AREA_H - 1;
  if (_clip_on) {
    if (_clip_x0 > cx0) cx0 = _clip_x0;
    if (_clip_x1 - 1 < cx1) cx1 = _clip_x1 - 1;
    if (_clip_y0 > cy0) cy0 = _clip_y0;
    if (_clip_y1 - 1 < cy1) cy1 = _clip_y1 - 1;
  }
  if (x0 < cx0) x0 = cx0;
  if (x1 > cx1) x1 = cx1;
  if (y0 < cy0) y0 = cy0;
  if (y1 > cy1) y1 = cy1;
  return x0 <= x1 && y0 <= y1;
}

void SF32Display::blendPixel(int px, int py, int a) {
  if (a <= 0 || px < CLIP_X0 || px >= CLIP_X1 || py < OFFSET_Y || py >= OFFSET_Y + AREA_H) return;
  if (_clip_on && (px < _clip_x0 || px >= _clip_x1 || py < _clip_y0 || py >= _clip_y1)) return;
  uint16_t* d = &s_fb[py * PANEL_W + px];
  if (a >= 15) {
    *d = _color;
    return;
  }
  *d = blend565(_color, *d, a);
}

// One opaque span [x0,x1) of row py in _color (clipped): plain stores, 32 bits at a time.
void SF32Display::fillSpan(int x0, int x1, int py) {
  if (py < OFFSET_Y || py >= OFFSET_Y + AREA_H) return;
  if (x0 < CLIP_X0) x0 = CLIP_X0;
  if (x1 > CLIP_X1) x1 = CLIP_X1;
  if (_clip_on) {
    if (py < _clip_y0 || py >= _clip_y1) return;
    if (x0 < _clip_x0) x0 = _clip_x0;
    if (x1 > _clip_x1) x1 = _clip_x1;
  }
  if (x1 <= x0) return;
  uint16_t* d = &s_fb[py * PANEL_W + x0];
  int n = x1 - x0;
  uint16_t c = _color;
  if (((uintptr_t)d & 2) && n > 0) { *d++ = c; n--; }  // to a 32-bit boundary
  uint32_t two = ((uint32_t)c << 16) | c;
  uint32_t* q = (uint32_t*)d;
  for (int i = 0; i < n / 2; i++) q[i] = two;
  if (n & 1) d[n - 1] = c;
}

// Filled physical rectangle [x0,x1) x [y0,y1), optionally with anti-aliased round corners.
// Only the corner pixels are blended; everything else is filled as spans.
void SF32Display::fillPhys(int x0, int y0, int x1, int y1, int radius) {
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return;
  if (radius * 2 > w) radius = w / 2;
  if (radius * 2 > h) radius = h / 2;
  for (int y = y0; y < y1; y++) {
    bool corner_row = radius > 0 && (y < y0 + radius || y >= y1 - radius);
    if (!corner_row) {
      fillSpan(x0, x1, y);
      continue;
    }
    fillSpan(x0 + radius, x1 - radius, y);
    for (int x = x0; x < x1; x++) {
      if (x == x0 + radius) x = x1 - radius;  // the middle was a span
      if (x >= x1) break;
      int a = 15;
      if (radius > 0) {
        // distance from the nearest corner centre, if in a corner square
        float cx = x < x0 + radius ? x0 + radius - 0.5f : (x >= x1 - radius ? x1 - radius - 0.5f : x);
        float cy = y < y0 + radius ? y0 + radius - 0.5f : (y >= y1 - radius ? y1 - radius - 0.5f : y);
        if (cx != x && cy != y) {
          float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
          float cov = radius - d + 0.5f;
          a = cov >= 1 ? 15 : (cov <= 0 ? 0 : (int)(cov * 15 + 0.5f));
        }
      }
      blendPixel(x, y, a);
    }
  }
}

void SF32Display::drawLineAA(float x0, float y0, float x1, float y1, float width) {
  // to panel pixels
  float ax = OFFSET_X + x0 * SCALE, ay = OFFSET_Y + y0 * SCALE;
  float bx = OFFSET_X + x1 * SCALE, by = OFFSET_Y + y1 * SCALE;
  float r = width * SCALE / 2;
  int minx = (int)floorf(fminf(ax, bx) - r - 1), maxx = (int)ceilf(fmaxf(ax, bx) + r + 1);
  int miny = (int)floorf(fminf(ay, by) - r - 1), maxy = (int)ceilf(fmaxf(ay, by) + r + 1);
  float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
  for (int py = miny; py <= maxy; py++) {
    for (int px = minx; px <= maxx; px++) {
      float qx = px + 0.5f - ax, qy = py + 0.5f - ay;
      float t = len2 > 0 ? (qx * dx + qy * dy) / len2 : 0;  // closest point on the segment
      t = t < 0 ? 0 : (t > 1 ? 1 : t);
      float ex = qx - t * dx, ey = qy - t * dy;
      float cov = r - sqrtf(ex * ex + ey * ey) + 0.5f;     // distance -> coverage, ~1 px edge
      if (cov <= 0) continue;
      blendPixel(px, py, cov >= 1 ? 15 : (int)(cov * 15 + 0.5f));
    }
  }
}

// A shaded ellipse (logical units, rotated by rot radians): lit as a rounded 3D surface, from the
// upper left and towards the viewer, with a glossy highlight (gloss 0..1) and an anti-aliased
// edge. Used for soft figures (the pet).
void SF32Display::fillShaded(float cx, float cy, float rx, float ry, uint16_t base, float gloss, float rot) {
  float acx = OFFSET_X + cx * SCALE, acy = OFFSET_Y + cy * SCALE, arx = rx * SCALE, ary = ry * SCALE;
  if (arx < 0.5f || ary < 0.5f) return;
  const float cr = cosf(rot), sr = sinf(rot), mr = fminf(arx, ary), iax = 1 / arx, iay = 1 / ary;
  const float LIM = 1.06f;  // drawn out to here (the anti-aliased rim)
  // the rotated ellipse's exact extent: rows from its bounding box, each row's stretch from
  // the quadratic in x (so nothing outside it is visited)
  const float A = cr * cr * iax * iax + sr * sr * iay * iay;
  const float Bk = 2 * cr * sr * (iax * iax - iay * iay);
  const float Ck = sr * sr * iax * iax + cr * cr * iay * iay;
  const float hh = sqrtf(LIM * (arx * arx * sr * sr + ary * ary * cr * cr));
  int y0 = (int)floorf(acy - hh), y1 = (int)ceilf(acy + hh), cx0 = 0, cx1 = PANEL_W - 1;
  if (!clipBounds(cx0, y0, cx1, y1)) return;
  const float inv = 1 / (2 * A);
  const int br = (base >> 11) << 3, bgc = ((base >> 5) & 0x3F) << 2, bb = (base & 0x1F) << 3;
  const float lx = -0.45f, ly = -0.62f, lz = 0.64f;            // the light
  const float hx = -0.2f, hy = -0.276f, hz = 0.94f;            // half-way to the viewer (normalised)
  const float solid = mr > 1 ? (1 - 0.5f / mr) * (1 - 0.5f / mr) : 0;  // d2 below this: fully covered
  const float dux = cr * iax, duy = -sr * iay;                  // one pixel right, in the ellipse's units
  for (int py = y0; py <= y1; py++) {
    float qy = py + 0.5f - acy;
    float b = Bk * qy, c = Ck * qy * qy - LIM, disc = b * b - 4 * A * c;
    if (disc <= 0) continue;
    float sq = sqrtf(disc);
    int x0 = (int)floorf(acx + (-b - sq) * inv), x1 = (int)ceilf(acx + (-b + sq) * inv);
    if (x0 < cx0) x0 = cx0;
    if (x1 > cx1) x1 = cx1;
    uint16_t* row = s_fb + py * PANEL_W;
    float qx = x0 + 0.5f - acx;
    float ux = (qx * cr + qy * sr) * iax, uy = (-qx * sr + qy * cr) * iay;
    for (int px = x0; px <= x1; px++, ux += dux, uy += duy) {
      float d2 = ux * ux + uy * uy;
      if (d2 >= LIM) continue;
      int a = 15;
      if (d2 > solid) {  // the rim: coverage from the distance
        float cov = (1 - sqrtf(d2)) * mr + 0.5f;
        if (cov <= 0) continue;
        if (cov < 1) a = (int)(cov * 15 + 0.5f);
        if (a <= 0) continue;
      }
      float nz = d2 < 1 ? sqrtf(1 - d2) : 0;
      float nx = ux * cr - uy * sr, ny = ux * sr + uy * cr;
      float lam = nx * lx + ny * ly + nz * lz;
      if (lam < 0) lam = 0;
      float shade = 0.38f + 0.72f * lam + 0.10f * (ny > 0 ? ny * (1 - nz) : 0);  // ambient, light, a little bounce below
      float sp = nx * hx + ny * hy + nz * hz;
      sp = sp > 0 ? sp : 0;
      sp *= sp; sp *= sp; sp *= sp; sp *= sp; sp *= sp;  // ^32: a tight highlight
      int add = (int)(sp * gloss * 230);
      int r = (int)(br * shade) + add, g = (int)(bgc * shade) + add, bl = (int)(bb * shade) + add;
      r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; bl = bl > 255 ? 255 : bl;
      uint16_t col = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (bl >> 3));
      row[px] = a >= 15 ? col : blend565(col, row[px], a);
    }
  }
}

// A soft spot: colour c fading out from the centre (alpha 0..15 at the middle): shadows, blush, glow.
void SF32Display::fillSoft(float cx, float cy, float rx, float ry, uint16_t c, int alpha) {
  float acx = OFFSET_X + cx * SCALE, acy = OFFSET_Y + cy * SCALE, arx = rx * SCALE, ary = ry * SCALE;
  if (arx < 0.5f || ary < 0.5f || alpha <= 0) return;
  // smooth: 256 levels (alpha is on the 0..15 scale) and an ordered dither into RGB565, so a big
  // faint glow has no rings. A pixel shows where k * (1 - d2)^2 >= 1: d2 < lim (the rest is skipped).
  const float k = alpha * 256.0f / 15;
  const float lim = 1 - sqrtf(1 / k);
  if (lim <= 0) return;
  const float sl = sqrtf(lim);
  int y0 = (int)floorf(acy - ary * sl), y1 = (int)ceilf(acy + ary * sl), cx0 = 0, cx1 = PANEL_W - 1;
  if (!clipBounds(cx0, y0, cx1, y1)) return;
  static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const float iarx = 1 / arx, iary = 1 / ary;
  const int fr = ((c >> 11) & 0x1F) << 3, fg = ((c >> 5) & 0x3F) << 2, fb = (c & 0x1F) << 3;
  for (int py = y0; py <= y1; py++) {
    float uy = (py + 0.5f - acy) * iary, rest = lim - uy * uy;
    if (rest <= 0) continue;
    float half = sqrtf(rest) * arx;  // this row's stretch, worked out once
    int x0 = (int)floorf(acx - half), x1 = (int)ceilf(acx + half);
    if (x0 < cx0) x0 = cx0;
    if (x1 > cx1) x1 = cx1;
    uint16_t* row = s_fb + py * PANEL_W;
    const uint8_t* bay = BAYER[py & 3];
    const float uy2 = uy * uy;
    float ux = (x0 + 0.5f - acx) * iarx;
    for (int px = x0; px <= x1; px++, ux += iarx) {
      float f = 1 - (ux * ux + uy2);
      if (f <= 0) continue;
      int a = (int)(k * f * f);
      if (a <= 0) continue;
      if (a >= 256) { row[px] = c; continue; }
      uint16_t bg = row[px];
      int d = bay[px & 3];  // 0..15
      int r = ((fr * a + (((bg >> 11) & 0x1F) << 3) * (256 - a)) >> 8) + (d >> 1);
      int g = ((fg * a + (((bg >> 5) & 0x3F) << 2) * (256 - a)) >> 8) + (d >> 2);
      int b = ((fb * a + ((bg & 0x1F) << 3) * (256 - a)) >> 8) + (d >> 1);
      r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
      row[px] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
  }
}

// Rotation by the panel itself (CO5300 MADCTL: bit 7 MY rows mirrored, bit 6 MX columns
// mirrored, bit 5 MV rows and columns swapped): free, partial updates included (they're
// just addresses). The LCD controller's own mirroring isn't used (the SF32LB52's can only
// mirror top to bottom). rot: quarter turns clockwise (0..3).
static const uint8_t ROT_MADCTL[4] = {0x00, 0x60, 0xC0, 0xA0};
static void panel_rotate(int rot) {
  LCDC_HandleTypeDef* h = get_drv_lcd_handler();
  uint8_t madctl = ROT_MADCTL[rot & 3];
  HAL_LCDC_Exit_LP(h);  // the driver puts the controller back into low power after its next frame
  HAL_LCDC_WriteU32Reg(h, (0x02 << 24) | (0x36 << 8), &madctl, 1);
}

void SF32Display::setRotation(int rot) {
  rot &= 3;
  if (rot == _rot || !_lcd) return;
  waitFlush();
  panel_rotate(rot);
  _rot = rot;
}

void SF32Display::fillWholePanel(uint16_t color) {  // edge to edge, margins too (flashlight)
  fill16(s_fb, color, PANEL_W * PANEL_H);
}

void SF32Display::fillPanelRect(int px, int py, int w, int h) {
  fillPhys(px + s_shift_x, py + s_shift_y, px + s_shift_x + w, py + s_shift_y + h, 0);
}

void SF32Display::fillRect(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  int x0 = px_x(x), y0 = px_y(y), x1 = px_x(x + w), y1 = px_y(y + h);
  if (x <= 0 && x + w >= W) {  // full-width bar: leave the text inside a margin
    x0 = CLIP_X0;
    x1 = CLIP_X1;
  }
  int radius = 0;
  if (w == h && w <= 4) {
    radius = (x1 - x0) / 2;  // page dots and the like: round
  } else if (w >= 24 && h >= 8) {
    radius = PANEL_RADIUS;   // title bar, popups
  }
  if (glassActive() && w >= 6 && h >= 3 && (_color == UIColor::title_bkg || _color == UIColor::popup_bkg)) {
    if (radius) radius = GLASS_RADIUS;
    bool popup = _color == UIColor::popup_bkg;
    if (popup && s_fb != s_layer) {
      glassPopup(x0, y0, x1, y1, radius);
    } else {
      glassFill(x0, y0, x1, y1, radius);
      if (radius) glassRim(x0, y0, x1, y1, radius, false);
    }
    return;
  }
  fillPhys(x0, y0, x1, y1, radius);
}

// The soft light inside a pane, strongest at the top edge (added to the frost), dithered so
// it fades smoothly instead of in visible steps. 0 below it.
#define GLASS_SHEEN_ROWS 36
static inline uint16_t glass_sheen(int dy, int x, int y) {
  if (dy >= GLASS_SHEEN_ROWS) return 0;
  int q = (GLASS_SHEEN_ROWS - dy) * 64 / GLASS_SHEEN_ROWS;  // 0..64: four levels, in sixteenths
  int a = (q + BAYER4[(y & 3) * 4 + (x & 3)]) / 16;
  return a > 0 ? (uint16_t)((a << 11) | ((a * 2) << 5) | a) : 0;
}

// A pane: the frosted picture through a rounded rectangle (in a layer: marked, frosted when
// composed, so it stays true to what's behind it while the list scrolls).
void SF32Display::glassFill(int x0, int y0, int x1, int y1, int radius) {
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return;
  if (radius * 2 > w) radius = w / 2;
  if (radius * 2 > h) radius = h / 2;
  bool layer = s_fb == s_layer;
  uint16_t saved = _color;
  for (int py = y0; py < y1; py++) {
    if (py < OFFSET_Y || py >= OFFSET_Y + AREA_H) continue;
    if (_clip_on && (py < _clip_y0 || py >= _clip_y1)) continue;
    int cy_d = py < y0 + radius ? y0 + radius - py : (py >= y1 - radius ? py - (y1 - radius) + 1 : 0);
    const bool shine = h >= 16 && py - y0 < GLASS_SHEEN_ROWS;
    int xa = x0, xb = x1;
    if (cy_d) {  // a corner row: the corners blended, one pixel at a time, the middle below
      for (int side = 0; side < 2; side++) {
        for (int i = 0; i < radius; i++) {
          int px = side ? x1 - 1 - i : x0 + i;
          float dx = radius - i - 0.5f, dy = cy_d - 0.5f;
          float cov = radius - sqrtf(dx * dx + dy * dy) + 0.5f;
          if (cov <= 0 || px < 0 || px >= PANEL_W) continue;
          if (layer) {
            if (cov >= 0.5f && (!_clip_on || (px >= _clip_x0 && px < _clip_x1))) {
              uint16_t v = shine ? glass_sheen(py - y0, px, py) : 0;
              s_fb[py * PANEL_W + px] = v ? v : GLASS_KEY;
            }
            continue;
          }
          uint16_t f = py >= 0 && py < PANEL_H ? s_frost[py * PANEL_W + px] : 0;
          _color = shine ? add565(f, glass_sheen(py - y0, px, py)) : f;
          blendPixel(px, py, cov >= 1 ? 15 : (int)(cov * 15 + 0.5f));
        }
      }
      xa = x0 + radius;
      xb = x1 - radius;
    }
    if (xa < CLIP_X0) xa = CLIP_X0;
    if (xb > CLIP_X1) xb = CLIP_X1;
    if (_clip_on) { if (xa < _clip_x0) xa = _clip_x0; if (xb > _clip_x1) xb = _clip_x1; }
    if (xb <= xa) continue;
    uint16_t* d = s_fb + py * PANEL_W + xa;
    if (layer) {
      for (int i = 0; i < xb - xa; i++) {
        uint16_t v = shine ? glass_sheen(py - y0, xa + i, py) : 0;
        d[i] = v ? v : GLASS_KEY;
      }
    } else if (shine) {
      const uint16_t* f = s_frost + py * PANEL_W + xa;
      for (int i = 0; i < xb - xa; i++) d[i] = add565(f[i], glass_sheen(py - y0, xa + i, py));
    } else {
      memcpy(d, s_frost + py * PANEL_W + xa, (xb - xa) * 2);
    }
  }
  _color = saved;
}

// A popup over a page: real glass. Blurs what's actually behind it (the page too), with a soft
// shadow under it so it floats. s_popup_fade: how far it has faded in.
static float s_popup_fade = 1;
void SF32Display::setPopupFade(float fade) { s_popup_fade = fade < 0 ? 0 : (fade > 1 ? 1 : fade); }

void SF32Display::glassPopup(int x0, int y0, int x1, int y1, int radius) {
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0 || s_popup_fade <= 0) return;
  if (radius * 2 > w) radius = w / 2;
  if (radius * 2 > h) radius = h / 2;
  const int fade = (int)(s_popup_fade * 15 + 0.5f);

  // the frame behind it, a little beyond its edges, averaged down and blurred
  const int M = 16;
  int bx = x0 - M < 0 ? 0 : (x0 - M) & ~3, by = y0 - M < 0 ? 0 : (y0 - M) & ~3;
  int gw = (x1 + M - bx) / 4 + 2, gh = (y1 + M - by) / 4 + 2;
  if (gw > GLASS_GRID) gw = GLASS_GRID;
  if (gh > GLASS_GRID) gh = GLASS_GRID;
  uint8_t* g = s_glass_grid;
  for (int gy = 0; gy < gh; gy++) {
    int sy = by + gy * 4 > PANEL_H - 4 ? PANEL_H - 4 : by + gy * 4;
    for (int gx = 0; gx < gw; gx++) {
      int sx = bx + gx * 4 > PANEL_W - 4 ? PANEL_W - 4 : bx + gx * 4;
      int sr = 0, sg = 0, sb = 0;
      for (int j = 0; j < 4; j++) {
        const uint16_t* row = s_fb + (sy + j) * PANEL_W + sx;
        for (int i = 0; i < 4; i++) {
          uint16_t p = row[i];
          sr += (p >> 11) << 3; sg += ((p >> 5) & 0x3F) << 2; sb += (p & 0x1F) << 3;
        }
      }
      *g++ = sr / 16; *g++ = sg / 16; *g++ = sb / 16;
    }
  }
  for (int pass = 0; pass < 4; pass++) {
    memcpy(s_glass_tmp, s_glass_grid, gw * gh * 3);
    for (int gy = 0; gy < gh; gy++) {
      for (int gx = 0; gx < gw; gx++) {
        for (int k = 0; k < 3; k++) {
          int sum = 0;
          for (int dy = -1; dy <= 1; dy++) {
            int yy = gy + dy < 0 ? 0 : (gy + dy >= gh ? gh - 1 : gy + dy);
            for (int dx = -1; dx <= 1; dx++) {
              int xx = gx + dx < 0 ? 0 : (gx + dx >= gw ? gw - 1 : gx + dx);
              sum += s_glass_tmp[(yy * gw + xx) * 3 + k];
            }
          }
          s_glass_grid[(gy * gw + gx) * 3 + k] = sum / 9;
        }
      }
    }
  }

  // the shadow, drawn now that the blur has its copy
  uint16_t saved = _color;
  _color = 0;
  const int S = 18, DROP = 6;
  for (int py = y0 - S + DROP; py < y1 + S + DROP; py++) {
    int dy = py - DROP < y0 + radius ? y0 + radius - (py - DROP) : (py - DROP >= y1 - radius ? py - DROP - (y1 - radius) + 1 : 0);
    for (int px = x0 - S; px < x1 + S; px++) {
      if (py >= y0 && py < y1 && px >= x0 && px < x1) { px = x1 - 1; continue; }  // under the pane
      int dx = px < x0 + radius ? x0 + radius - px : (px >= x1 - radius ? px - (x1 - radius) + 1 : 0);
      float d = ((dx && dy) ? sqrtf((float)(dx * dx + dy * dy)) : (float)(dx + dy)) - radius;
      if (d >= S) continue;
      float t = d <= 0 ? 1 : 1 - d / S;
      blendPixel(px, py, (int)(t * t * 8 * s_popup_fade + 0.5f));
    }
  }

  // the pane
  uint32_t grain = 0x9E3779B9;
  for (int py = y0; py < y1; py++) {
    int cy_d = py < y0 + radius ? y0 + radius - py : (py >= y1 - radius ? py - (y1 - radius) + 1 : 0);
    int ty = py - by, gy = ty / 4, fy = ty & 3;
    if (gy < 0) { gy = 0; fy = 0; }
    if (gy >= gh - 1) { gy = gh - 2; fy = 4; }
    int a_sheen = py - y0 < GLASS_SHEEN_ROWS ? (GLASS_SHEEN_ROWS - (py - y0)) * 28 / GLASS_SHEEN_ROWS : 0;  // the sheen
    for (int px = x0; px < x1; px++) {
      int tx = px - bx, gx = tx / 4, fx = tx & 3;
      if (gx < 0) { gx = 0; fx = 0; }
      if (gx >= gw - 1) { gx = gw - 2; fx = 4; }
      const uint8_t* a = s_glass_grid + (gy * gw + gx) * 3, *b = a + gw * 3;
      grain ^= grain << 13; grain ^= grain >> 17; grain ^= grain << 5;
      int n = (int)(grain & 7) - 3;
      int c[3];
      for (int k = 0; k < 3; k++) {
        int top = a[k] * (4 - fx) + a[k + 3] * fx, bot = b[k] * (4 - fx) + b[k + 3] * fx;
        c[k] = ((top * (4 - fy) + bot * fy) / 16) * 11 / 16 + 44 + n + a_sheen;  // a thicker veil than the cards
      }
      int cov = fade;
      if (cy_d) {
        int dx = px < x0 + radius ? x0 + radius - px : (px >= x1 - radius ? px - (x1 - radius) + 1 : 0);
        if (dx) {
          float ddx = dx - 0.5f, ddy = cy_d - 0.5f, e = radius - sqrtf(ddx * ddx + ddy * ddy) + 0.5f;
          if (e <= 0) continue;
          if (e < 1) cov = (int)(e * fade + 0.5f);
        }
      }
      _color = glass_pixel(px, py, c[0], c[1], c[2]);
      blendPixel(px, py, cov);
    }
  }
  _color = saved;
  if (radius) glassRim(x0, y0, x1, y1, radius, true);
}

// Move (or turn on/off) the light on the glass: only the frost around the old and the new
// glint is redone (a few ms), then the next frame shows it.
void SF32Display::setGlassLight(bool on, int x, int y) {
  if (on == s_light_on && (!on || (x == s_light_x && y == s_light_y))) return;
  bool was = s_light_on;
  int ox = s_light_x, oy = s_light_y;
  s_light_on = on;
  s_light_x = x;
  s_light_y = y;
  if (!s_glass_on) return;  // (glass_build uses it when Glass comes on)
  int r0 = PANEL_H, r1 = 0;
  if (was) { frost_region(ox - GLINT_R, oy - GLINT_R, ox + GLINT_R, oy + GLINT_R); r0 = oy - GLINT_R; r1 = oy + GLINT_R; }
  if (on) {
    frost_region(x - GLINT_R, y - GLINT_R, x + GLINT_R, y + GLINT_R);
    if (y - GLINT_R < r0) r0 = y - GLINT_R;
    if (y + GLINT_R > r1) r1 = y + GLINT_R;
  }
  if (r0 < 0) r0 = 0;
  if (r1 > PANEL_H) r1 = PANEL_H;
  if (r1 > r0) mpu_dcache_clean((void*)(s_frost + r0 * PANEL_W), (r1 - r0) * PANEL_W * 2);  // the hardware reads it
  s_glass_sum = glass_sum();
}

// A round pane behind a big icon (logical x, y, diameter).
void SF32Display::glassBadge(int x, int y, int d) {
  int x0 = px_x(x), y0 = px_y(y), x1 = px_x(x + d), y1 = px_y(y + d), r = (x1 - x0) / 2;
  glassFill(x0, y0, x1, y1, r);
  glassRim(x0, y0, x1, y1, r, false);
}

// The pane's edge: light catching it, brightest along the top, from the left.
void SF32Display::glassRim(int x0, int y0, int x1, int y1, int radius, bool popup) {
  int w = x1 - x0, h = y1 - y0;
  if (radius * 2 > w) radius = w / 2;
  if (radius * 2 > h) radius = h / 2;
  const float RIM = 2.0f;
  uint16_t saved = _color;
  _color = 0xFFFF;
  int boost = popup ? 3 : 0;
  const float rim_fade = popup ? s_popup_fade : 1;
  // the light on the glass: the edge brightens where it's near (not in a scrolling list's
  // layer, whose rows aren't where they'll be shown)
  const bool lit = s_light_on && s_fb != s_layer;
  auto lb = [&](int px, int py, int a) {
    if (lit) {
      int dx = px - s_light_x, dy = py - s_light_y, d2 = dx * dx + dy * dy;
      const int R2 = (GLINT_R * 2) * (GLINT_R * 2);
      if (d2 < R2) a += (R2 - d2) * 7 / R2;
    }
    return a > 15 ? 15 : a;
  };
  for (int py = y0; py < y1; py++) {
    int a = (int)((2 + boost + (9 * (y1 - py)) / (h * 2)) * rim_fade + 0.5f);  // the sides: fading downward
    if (s_light_on && !popup) a /= 2;  // with the light on, the edges glow near it rather than all round
    int cy_d = py < y0 + radius ? y0 + radius - py : (py >= y1 - radius ? py - (y1 - radius) + 1 : 0);
    if (cy_d) {
      for (int side = 0; side < 2; side++) {
        for (int i = 0; i < radius; i++) {
          float dx = radius - i - 0.5f, dy = cy_d - 0.5f, d = sqrtf(dx * dx + dy * dy);
          float out = radius - d + 0.5f, in = d - (radius - RIM) + 0.5f;
          float cov = (out > 1 ? 1 : out) * (in > 1 ? 1 : in);
          if (out <= 0 || in <= 0) continue;
          int px = side ? x1 - 1 - i : x0 + i;
          blendPixel(px, py, (int)(lb(px, py, a) * cov + 0.5f));
        }
      }
      int rows = py < y0 + radius ? py - y0 : y1 - 1 - py;
      if (rows < (int)RIM) {  // the straight top or bottom edge
        bool top = py < y0 + radius;
        for (int px = x0 + radius; px < x1 - radius; px++) {
          int along = (int)((top ? 11 + boost - (6 * (px - x0)) / w : 3 + boost) * rim_fade + 0.5f);
          if (s_light_on && !popup) along /= 2;
          int al = lb(px, py, along);
          blendPixel(px, py, rows == 0 ? al : al / 2);
        }
      }
    } else {
      for (int i = 0; i < (int)RIM; i++) {
        blendPixel(x0 + i, py, lb(x0 + i, py, a));
        blendPixel(x1 - 1 - i, py, lb(x1 - 1 - i, py, a));
      }
    }
  }
  _color = saved;
}

void SF32Display::drawRect(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  int x0 = px_x(x), y0 = px_y(y), x1 = px_x(x + w), y1 = px_y(y + h);
  // One-pixel lines (separators) become a thin centred line.
  if (h == 1) {
    if (x <= 0 && x + w >= W) {
      x0 = CLIP_X0;
      x1 = CLIP_X1;
    }
    int my = (y0 + y1) / 2;
    fillPhys(x0, my - LINE_PX / 2, x1, my - LINE_PX / 2 + LINE_PX, 0);
    return;
  }
  if (w == 1) {
    int mx = (x0 + x1) / 2;
    fillPhys(mx - LINE_PX / 2, y0, mx - LINE_PX / 2 + LINE_PX, y1, 0);
    return;
  }
  fillPhys(x0, y0, x1, y0 + LINE_PX, 0);
  fillPhys(x0, y1 - LINE_PX, x1, y1, 0);
  fillPhys(x0, y0, x0 + LINE_PX, y1, 0);
  fillPhys(x1 - LINE_PX, y0, x1, y1, 0);
}

// MSB-first rows, as Adafruit_GFX::drawBitmap (used by MeshCore's SSD1306 driver).
// The bitmap is scaled up smoothly: each panel pixel bilinearly interpolates the
// bitmap's bits, and the 0.5 contour of that field becomes the icon's edge, with
// about one panel pixel of anti-aliasing. Pixel staircases turn into straight
// diagonals and curves instead of 3.5-pixel blocks.
static inline int xbm_bit(const uint8_t* bits, int byte_w, int w, int h, int bx, int by) {
  if (bx < 0 || by < 0 || bx >= w || by >= h) return 0;
  return (bits[by * byte_w + bx / 8] >> (7 - (bx & 7))) & 1;
}

void SF32Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  drawXbmScaled(x, y, bits, w, h, 1.0f);
}

void SF32Display::drawXbmScaled(int x, int y, const uint8_t* bits, int w, int h, float scale) {
  int byte_w = (w + 7) / 8;
  int x0 = px_x(x), y0 = px_y(y);
  int x1 = x0 + (int)lroundf(w * scale * SCALE), y1 = y0 + (int)lroundf(h * scale * SCALE);
  float sx = (float)w / (x1 - x0), sy = (float)h / (y1 - y0);
  const float threshold = 0.45f;   // slightly below 0.5 keeps 1-px diagonal strokes joined
  const float sharpness = SCALE * scale;  // edge ramp about one panel pixel wide
  for (int py = y0; py < y1; py++) {
    float v = (py - y0 + 0.5f) * sy - 0.5f;
    int by = (int)floorf(v);
    float fy = v - by;
    for (int px = x0; px < x1; px++) {
      float u = (px - x0 + 0.5f) * sx - 0.5f;
      int bx = (int)floorf(u);
      float fx = u - bx;
      float f = (xbm_bit(bits, byte_w, w, h, bx, by) * (1 - fx) + xbm_bit(bits, byte_w, w, h, bx + 1, by) * fx) * (1 - fy) +
                (xbm_bit(bits, byte_w, w, h, bx, by + 1) * (1 - fx) + xbm_bit(bits, byte_w, w, h, bx + 1, by + 1) * fx) * fy;
      float cov = (f - threshold) * sharpness + 0.5f;
      if (cov <= 0) continue;
      blendPixel(px, py, cov >= 1 ? 15 : (int)(cov * 15 + 0.5f));
    }
  }
}

// ---- text ----

// Glyph set for the current text size: the largest generated size <= text size. Sets
// that only cover some characters (size 5: digits) fall back to smaller ones.
static const SF32Glyph* find_glyph(int text_size, unsigned char c, const uint8_t** bitmap) {
  if (c < SF32_FONT_FIRST || c > SF32_FONT_LAST) return nullptr;
  for (int i = SF32_FONT_SIZES - 1; i >= 0; i--) {
    if (sf32_font_text_size[i] > text_size) continue;
    const SF32Glyph* g = &sf32_glyphs[i][c - SF32_FONT_FIRST];
    if (g->adv == 0 && i > 0) continue;  // not in this set
    if (bitmap) *bitmap = sf32_bitmaps[i] + g->data;
    return g;
  }
  return nullptr;
}

int SF32Display::charAdvance(unsigned char c) const {
  if (c == 0xDB) return (int)(6 * _text_size * SCALE * 16);
  const SF32Glyph* g = find_glyph(_text_size, c, nullptr);
  return g ? g->adv : 0;
}

int SF32Display::drawChar(int pen16, int y, unsigned char c) {
  int ox = (pen16 + 8) >> 4, oy = px_y(y);
  if (c == 0xDB) {  // translateUTF8ToBlocks' placeholder for non-ASCII: a soft block
    int u = (int)(_text_size * SCALE);
    fillPhys(ox + u, oy + u, ox + 5 * u, oy + 7 * u, 3);
    return charAdvance(c);
  }
  const uint8_t* bm;
  const SF32Glyph* gp = find_glyph(_text_size, c, &bm);
  if (!gp) return 0;
  const SF32Glyph& g = *gp;
  int row_bytes = (g.w + 1) / 2;
  // clip once per glyph (not per pixel): the visible part of its box
  int gx = ox + g.x, gy = oy + g.y;
  int cx0 = CLIP_X0, cx1 = CLIP_X1, cy0 = OFFSET_Y, cy1 = OFFSET_Y + AREA_H;
  if (_clip_on) {
    if (_clip_x0 > cx0) cx0 = _clip_x0;
    if (_clip_x1 < cx1) cx1 = _clip_x1;
    if (_clip_y0 > cy0) cy0 = _clip_y0;
    if (_clip_y1 < cy1) cy1 = _clip_y1;
  }
  int i0 = cx0 - gx > 0 ? cx0 - gx : 0, i1 = cx1 - gx < g.w ? cx1 - gx : g.w;
  int j0 = cy0 - gy > 0 ? cy0 - gy : 0, j1 = cy1 - gy < g.h ? cy1 - gy : g.h;
  const uint16_t fg = _color;
  const int fr = (fg >> 11) & 0x1F, fgg = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  for (int j = j0; j < j1; j++) {
    const uint8_t* row = bm + j * row_bytes;
    uint16_t* d = &s_fb[(gy + j) * PANEL_W + gx];
    for (int i = i0; i < i1; i++) {
      int a = (i & 1) ? (row[i >> 1] & 0x0F) : (row[i >> 1] >> 4);
      if (a == 0) continue;
      if (a == 15) { d[i] = fg; continue; }
      uint16_t bg = d[i];
      int r = (fr * a + ((bg >> 11) & 0x1F) * (15 - a)) / 15;
      int gg = (fgg * a + ((bg >> 5) & 0x3F) * (15 - a)) / 15;
      int b = (fb * a + (bg & 0x1F) * (15 - a)) / 15;
      d[i] = (uint16_t)((r << 11) | (gg << 5) | b);
    }
  }
  return g.adv;
}

void SF32Display::print(const char* str) {
  char buf[160];
  translateUTF8ToBlocks(buf, str, sizeof(buf));
  int pen = _pen_valid ? _pen16 : px_x(_cx) * 16;
  for (const char* p = buf; *p; p++) {
    if (*p == '\n') {
      _cx = 0;
      _cy += 8 * _text_size;
      pen = px_x(0) * 16;
      continue;
    }
    pen += drawChar(pen, _cy, (unsigned char)*p);
  }
  _pen16 = pen;
  _pen_valid = true;
  _cx = (int)ceilf((pen / 16.0f - OFFSET_X) / SCALE);
}

// Breaks lines at spaces (or mid-word for words longer than a line); new lines start
// at the original cursor x, like OLEDDisplay::drawStringMaxWidth.
void SF32Display::printWordWrap(const char* str, int max_width) {
  char buf[256];
  translateUTF8ToBlocks(buf, str, sizeof(buf));
  int left = _pen_valid ? _pen16 : px_x(_cx) * 16;
  int right = px_x(_cx + max_width) * 16;
  if (right > (OFFSET_X + AREA_W) * 16) right = (OFFSET_X + AREA_W) * 16;
  const char* p = buf;
  while (*p) {
    // longest run that fits, preferring to break after a space
    int n = 0, w = 0, last_space = -1;
    while (p[n] && p[n] != '\n') {
      int a = charAdvance((unsigned char)p[n]);
      if (n > 0 && left + w + a > right) break;
      if (p[n] == ' ') last_space = n;
      w += a;
      n++;
    }
    int take = n;
    if (p[n] && p[n] != '\n' && p[n] != ' ' && last_space > 0) take = last_space;
    int pen = left;
    for (int i = 0; i < take; i++) pen += drawChar(pen, _cy, (unsigned char)p[i]);
    _pen16 = pen;
    p += take;
    while (*p == ' ') p++;
    if (*p == '\n') p++;
    if (*p) _cy += 8 * _text_size + 1;
  }
  _pen_valid = true;
  _cx = (int)ceilf((_pen16 / 16.0f - OFFSET_X) / SCALE);
}

uint16_t SF32Display::getTextWidth(const char* str) {
  char buf[160];
  translateUTF8ToBlocks(buf, str, sizeof(buf));
  int w16 = 0;
  for (const char* p = buf; *p; p++) w16 += charAdvance((unsigned char)*p);
  return (uint16_t)ceilf(w16 / 16.0f / SCALE);
}

// ---- output ----

void SF32Display::endFrame() {
  if (!_lcd || !_on) return;
#ifdef SF32_REMOTE_TOUCH
  { uint32_t k = sf32_frame_n++ & 31; rt_tick_t now = rt_tick_get();
    sf32_gaps[k] = now - s_last_end; sf32_draws[k] = now - s_frame_t0; s_last_end = now; }
#endif
  if (_wake_pending) _transition = TRANSITION_NONE;  // first frame after waking: no slide
  if (_transition != TRANSITION_NONE) {
    rt_tick_t t0 = rt_tick_get();
    animate(_transition);
#ifdef SF32_REMOTE_TOUCH
    sf32_anim_ms = (rt_tick_get() - t0) * 1000 / RT_TICK_PER_SECOND;
#endif
#ifdef SF32_FRAME_LOG
    rt_kprintf("transition %d: %d ms\n", _transition, (int)(rt_tick_get() - t0) * 1000 / RT_TICK_PER_SECOND);
#else
    LOG_D("transition %d: %d ms", _transition, (int)(rt_tick_get() - t0) * 1000 / RT_TICK_PER_SECOND);
#endif
  }
  bool band = _band_y0 >= 0 && _transition == TRANSITION_NONE && !_wake_pending;
  _transition = TRANSITION_NONE;
  // a scrolling list: straight away. Its frames take about a refresh to draw, so waiting for the
  // panel's sync makes some of them wait a whole extra refresh, and that shows as judder.
  if (band && _list_frame) { waitFlush(); HAL_LCDC_Next_Frame_TE(get_drv_lcd_handler(), 0); }
  if (band) flush(s_fb, _band_y0, _band_y1);
  else flush(s_fb);
  _band_y0 = _band_y1 = -1;
  if (_wake_pending) {
    _wake_pending = false;
    waitFlush();
    if (_rot) {  // powering on re-initialised the panel (rotation lost): set it, send again
      panel_rotate(_rot);
      flush(s_fb);
      waitFlush();
    }
    for (int i = 1; i <= FADE_STEPS; i++) {  // fade up (squared: brightness looks linear that way)
      uint8_t b = (uint8_t)(_brightness * i * i / (FADE_STEPS * FADE_STEPS));
      if (b < 1) b = 1;
      rt_device_control(_lcd, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &b);
      if (i < FADE_STEPS) rt_thread_mdelay(FADE_STEP_MS);
    }
  }
  uint16_t* t = s_shown;  // the new frame is now the shown one
  s_shown = s_fb;
  s_fb = t;
  sf32_shown_fb = s_shown;
}

// Slides from the shown frame to the new one (s_fb): the new frame pushes the old one
// out in the given direction, ease-out. The last step is the new frame itself (endFrame).
static bool s_flushing = false;  // a send to the panel is under way (waitFlush)

// A slide copies nothing: the panel is sent the part of the old frame still in view and the
// part of the new one coming in, straight from their buffers (copying them together took as long
// as sending them, PSRAM being the bottleneck). The time is on the clock, not a frame count, so
// a slide lasts the same however fast the frames go. Each frame's first part waits for the panel's
// sync; a frame fits inside its refresh, so that gives 60 fps with no tearing.
void SF32Display::sendPart(const uint16_t* buf, int sx, int sy, int dx, int dy, int w, int h, bool sync) {
  if (w <= 0 || h <= 0) return;
  waitFlush();
  rt_graphix_ops(_lcd)->set_window(dx, dy, dx + w - 1, dy + h - 1);
  rt_device_set_tx_complete(_lcd, on_flush_done);
  LCDC_HandleTypeDef* lcdc = get_drv_lcd_handler();
  lcdc->Layer[HAL_LCDC_LAYER_DEFAULT].total_width = PANEL_W;  // rows of the frame, however little is sent
  if (!sync) HAL_LCDC_Next_Frame_TE(lcdc, 0);
  s_flushing = true;
  rt_graphix_ops(_lcd)->draw_rect_async((const char*)(buf + sy * PANEL_W + sx), dx, dy, dx + w - 1, dy + h - 1);
}

void SF32Display::animate(Transition dir) {
  const int W = PANEL_W, Hh = PANEL_H;
  const uint32_t DURATION = (dir == TRANSITION_UP || dir == TRANSITION_DOWN) ? SLIDE_V_MS : SLIDE_H_MS;
  mpu_dcache_clean((void*)s_fb, FB_PIXELS * 2);  // the new frame, read directly (the shown one already is)
  const rt_tick_t t0 = rt_tick_get();
#ifdef SF32_REMOTE_TOUCH
  uint32_t frames = 0;
#endif
  for (;;) {
    waitFlush();
    uint32_t el = (rt_tick_get() - t0) * 1000 / RT_TICK_PER_SECOND + SLIDE_LEAD_MS;
    if (el >= DURATION) break;
    float t = (float)el / DURATION;
    // soft start, long settle: half ease-in-out, half ease-out
    float e = 0.5f * t * t * (3 - 2 * t) + 0.5f * (1 - (1 - t) * (1 - t) * (1 - t));
#ifdef SF32_REMOTE_TOUCH
    sf32_anim_frames = ++frames;
#endif
    slideTo(dir, (int)(e * ((dir == TRANSITION_LEFT || dir == TRANSITION_RIGHT) ? W : Hh) + 0.5f));
  }
}

// One frame of a slide: the new frame `off` panel pixels in. The first part waits for the panel's sync.
bool SF32Display::s_cap30 = false;
void SF32Display::slideTo(Transition dir, int off) {
  const int W = PANEL_W, Hh = PANEL_H;
  if (s_cap30) {  // low power mode: at most 30 frames a second (a slide is timed by the clock: same length)
    static rt_tick_t last = 0;
    int32_t wait = (int32_t)(last + rt_tick_from_millisecond(33) - rt_tick_get());
    if (wait > 0 && wait < 40) rt_thread_delay(wait);
    last = rt_tick_get();
  }
  off &= ~1;  // the panel's windows start on even pixels
  if (dir == TRANSITION_LEFT) {          // new page comes in from the right
    sendPart(s_shown, off, 0, 0, 0, W - off, Hh, true);
    sendPart(s_fb, 0, 0, W - off, 0, off, Hh, off >= W);
  } else if (dir == TRANSITION_RIGHT) {  // new page comes in from the left
    sendPart(s_fb, W - off, 0, 0, 0, off, Hh, true);
    sendPart(s_shown, 0, 0, off, 0, W - off, Hh, off == 0);
  } else if (dir == TRANSITION_UP) {     // new screen comes up from the bottom
    sendPart(s_shown, 0, off, 0, 0, W, Hh - off, true);
    sendPart(s_fb, 0, 0, 0, Hh - off, W, off, off >= Hh);
  } else {                               // new screen comes down from the top
    sendPart(s_fb, 0, Hh - off, 0, 0, W, off, true);
    sendPart(s_shown, 0, 0, 0, off, W, Hh - off, off == 0);
  }
}

// ---- a page following the finger: the page coming in is drawn once (dragFrameDone), then each
// move only sends the two frames at a new offset, as a slide does ----
void SF32Display::dragFrameDone() {
  waitFlush();
  mpu_dcache_clean((void*)s_fb, FB_PIXELS * 2);
  _band_y0 = _band_y1 = -1;
  _transition = TRANSITION_NONE;
}

bool SF32Display::sendBusy() {
  if (!s_flushing) return false;
  if (rt_sem_trytake(&s_flush_done) != RT_EOK) return true;
  s_flushing = false;
  return false;
}

void SF32Display::dragShow(int off, bool next) {
  slideTo(next ? TRANSITION_LEFT : TRANSITION_RIGHT, off);
#ifdef SF32_REMOTE_TOUCH
  sf32_drag_sends++;
#endif
}

// Let go: on from `off` to all the way (commit) or back to none, easing out. It keeps some of
// the finger's speed by covering what's left quickly; a short way takes a short time.
void SF32Display::dragSettle(int off, bool next, bool commit) {
  const Transition dir = next ? TRANSITION_LEFT : TRANSITION_RIGHT;
  const int to = commit ? PANEL_W : 0;
  const uint32_t ms = 50 + 130 * abs(to - off) / PANEL_W;
  const rt_tick_t t0 = rt_tick_get();
  for (;;) {
    waitFlush();
    uint32_t el = (rt_tick_get() - t0) * 1000 / RT_TICK_PER_SECOND + 8;
    if (el >= ms) break;
    float t = (float)el / ms, e = 1 - (1 - t) * (1 - t);
    slideTo(dir, off + (int)((to - off) * e));
  }
  // and exactly there
  flush(commit ? s_fb : s_shown);
  waitFlush();
  if (commit) {
    uint16_t* swap = s_shown;
    s_shown = s_fb;
    s_fb = swap;
    sf32_shown_fb = s_shown;
  }
}

// Transfers run in the background: flush() starts one and returns; the next flush (or
// anything else that needs the panel or the buffer being sent) waits for it first. The
// UI meanwhile draws the next frame into the other buffer (double buffering), so drawing
// and sending overlap.
void SF32Display::waitFlush() {
  if (!s_flushing) return;
  if (rt_sem_take(&s_flush_done, rt_tick_from_millisecond(500)) != RT_EOK) LOG_W("flush timeout");
  s_flushing = false;
}

void SF32Display::flush(const uint16_t* buf, int py0, int py1) {
  if (py1 < 0) py1 = PANEL_H - 1;
  waitFlush();
  // full-width rows are contiguous in the frame buffer: send just that band
  const uint16_t* band = buf + py0 * PANEL_W;
  mpu_dcache_clean((void*)band, (py1 - py0 + 1) * PANEL_W * 2);
  rt_graphix_ops(_lcd)->set_window(0, py0, PANEL_W - 1, py1);
  rt_device_set_tx_complete(_lcd, on_flush_done);
  s_flushing = true;
  rt_graphix_ops(_lcd)->draw_rect_async((const char*)band, 0, py0, PANEL_W - 1, py1);
}

void SF32Display::setFlushBand(int y, int h) {
  int y0 = px_y(y) - 2, y1 = px_y(y + h) + 2;  // a little margin (burn-in shift)
  _band_y0 = y0 < 0 ? 0 : y0;
  _band_y1 = y1 > PANEL_H - 1 ? PANEL_H - 1 : y1;
  // AMOLED controllers like the window on even rows: start even, end odd (even height)
  _band_y0 &= ~1;
  _band_y1 |= 1;
  if (_band_y1 > PANEL_H - 1) _band_y1 = PANEL_H - 1;  // PANEL_H is even: stays odd
}
