#include "SF32Display.h"

#include <math.h>
#include <string.h>
#include "mem_section.h"
#include "bf0_hal.h"  // system_bf0_ap.h: mpu_dcache_clean
#include "SF32Font.h"

extern "C" void sf32_cpu_full_speed();  // SF32Board.cpp
#define LOG_TAG "display"
#include <ulog.h>

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
// one to the other), and one to compose the in-between animation frames.
#define FB_PIXELS (PANEL_W * PANEL_H)
L2_NON_RET_BSS_SECT_BEGIN(mc_fb)
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint16_t s_fb_a[FB_PIXELS]);
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint16_t s_fb_b[FB_PIXELS]);
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint16_t s_fb_anim[FB_PIXELS]);
// Off-screen layer for scrolling lists: drawn once, then slices are copied into frames.
#define LAYER_ROWS 2600  // panel px (~742 logical px of list: the 20 log entries are 680)
L2_NON_RET_BSS_SECT(mc_fb, ALIGN(64) static uint16_t s_layer[PANEL_W * LAYER_ROWS]);
L2_NON_RET_BSS_SECT_END
static uint16_t* s_fb = s_fb_a;     // frame being drawn
static uint16_t* s_shown = s_fb_b;  // frame on the panel
// For tools/sf32_debug_uart.py shot (@address of this pointer): the frame on the panel.
extern "C" { uint16_t* volatile sf32_shown_fb = s_fb_b; }

#define ANIM_STEPS 6  // frames per transition, ~41 ms each (a full-panel flush at the panel's 50 MHz)

static struct rt_semaphore s_flush_done;

static rt_err_t on_flush_done(rt_device_t, void*) {
  rt_sem_release(&s_flush_done);
  return RT_EOK;
}

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
  if (_on) rt_device_control(_lcd, RTGRAPHIC_CTRL_POWEROFF, RT_NULL);
  _on = false;
  _transition = TRANSITION_NONE;
}

// Fill n pixels with one colour, two at a time (much faster than 16-bit stores).
static void fill16(uint16_t* p, ColorVal c, int n) {
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

static uint32_t s_layer_gen = 0;  // bumped each time the layer is cleared for a new list
uint32_t SF32Display::layerGen() const { return s_layer_gen; }

int SF32Display::beginLayer(int h, bool clear) {
  s_layer_gen++;
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
  s_saved_fb = s_fb;
  s_saved_area_h = s_area_h;
  s_saved_offset_y = s_offset_y;
  s_fb = s_layer;
  s_offset_y = -s_shift_y;
  s_area_h = s_layer_h;
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
  for (int py = y0; py < y1; py++) {
    if (py < 0 || py >= PANEL_H) continue;
    int src = src_px + (py - y0);
    uint16_t* dst = s_fb + py * PANEL_W + x0;
    if (src >= 0 && src < s_layer_h) memcpy(dst, s_layer + src * PANEL_W + x0, (x1 - x0) * 2);
    else for (int i = 0; i < x1 - x0; i++) dst[i] = UIColor::window_bkg;
  }
}

void SF32Display::setSquareLayout(bool square) {
  s_area_h = square ? AREA_W : WIDE_H;
  s_offset_y = (PANEL_H - s_area_h) / 2;
}

void SF32Display::startFrame(ColorVal bkg) {
  setSquareLayout(false);
  _clip_on = false;
  fill16(s_fb, bkg, PANEL_W * PANEL_H);
  _color = UIColor::primary_txt;
  _text_size = 1;
  _cx = _cy = 0;
}

// ---- pixels ----

void SF32Display::blendPixel(int px, int py, int a) {
  if (a <= 0 || px < CLIP_X0 || px >= CLIP_X1 || py < OFFSET_Y || py >= OFFSET_Y + AREA_H) return;
  if (_clip_on && (px < _clip_x0 || px >= _clip_x1 || py < _clip_y0 || py >= _clip_y1)) return;
  uint16_t* d = &s_fb[py * PANEL_W + px];
  if (a >= 15) {
    *d = _color;
    return;
  }
  uint16_t bg = *d, fg = _color;
  int r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (15 - a)) / 15;
  int g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * (15 - a)) / 15;
  int b = ((fg & 0x1F) * a + (bg & 0x1F) * (15 - a)) / 15;
  *d = (uint16_t)((r << 11) | (g << 5) | b);
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
  fillPhys(x0, y0, x1, y1, radius);
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
  if (_wake_pending) _transition = TRANSITION_NONE;  // first frame after waking: no slide
  if (_transition != TRANSITION_NONE) {
    rt_tick_t t0 = rt_tick_get();
    animate(_transition);
#ifdef SF32_FRAME_LOG
    rt_kprintf("transition %d: %d ms\n", _transition, (int)(rt_tick_get() - t0) * 1000 / RT_TICK_PER_SECOND);
#else
    LOG_D("transition %d: %d ms", _transition, (int)(rt_tick_get() - t0) * 1000 / RT_TICK_PER_SECOND);
#endif
  }
  bool band = _band_y0 >= 0 && _transition == TRANSITION_NONE && !_wake_pending;
  _transition = TRANSITION_NONE;
  if (band) flush(s_fb, _band_y0, _band_y1);
  else flush(s_fb);
  _band_y0 = _band_y1 = -1;
  if (_wake_pending) {
    _wake_pending = false;
    waitFlush();
    rt_device_control(_lcd, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &_brightness);
  }
  uint16_t* t = s_shown;  // the new frame is now the shown one
  s_shown = s_fb;
  s_fb = t;
  sf32_shown_fb = s_shown;
}

// Slides from the shown frame to the new one (s_fb): the new frame pushes the old one
// out in the given direction, ease-out. The last step is the new frame itself (endFrame).
void SF32Display::animate(Transition dir) {
  const int W = PANEL_W, Hh = PANEL_H;
  for (int i = 1; i < ANIM_STEPS; i++) {
    float t = (float)i / ANIM_STEPS;
    float e = 1 - (1 - t) * (1 - t) * (1 - t);
    uint16_t* out = s_fb_anim;
    waitFlush();  // the previous step is still being sent from s_fb_anim (composing during
                  // a send also slows the send: they share the PSRAM)
    if (dir == TRANSITION_LEFT || dir == TRANSITION_RIGHT) {
      int off = (int)(e * W + 0.5f);
      for (int y = 0; y < Hh; y++) {
        uint16_t* o = out + y * W;
        const uint16_t* oldr = s_shown + y * W;
        const uint16_t* newr = s_fb + y * W;
        if (dir == TRANSITION_LEFT) {  // new page comes in from the right
          memcpy(o, oldr + off, (W - off) * 2);
          memcpy(o + W - off, newr, off * 2);
        } else {                       // new page comes in from the left
          memcpy(o, newr + W - off, off * 2);
          memcpy(o + off, oldr, (W - off) * 2);
        }
      }
    } else {
      int off = (int)(e * Hh + 0.5f);
      if (dir == TRANSITION_UP) {  // new screen comes up from the bottom
        memcpy(out, s_shown + off * W, (Hh - off) * W * 2);
        memcpy(out + (Hh - off) * W, s_fb, off * W * 2);
      } else {                     // new screen comes down from the top
        memcpy(out, s_fb + (Hh - off) * W, off * W * 2);
        memcpy(out + off * W, s_shown, (Hh - off) * W * 2);
      }
    }
    flush(out);
  }
}

// Transfers run in the background: flush() starts one and returns; the next flush (or
// anything else that needs the panel or the buffer being sent) waits for it first. The
// UI meanwhile draws the next frame into the other buffer (double buffering), so drawing
// and sending overlap.
static bool s_flushing = false;

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
