// The T-Deck's screen as the "lcd" device SF32Display drives (compat/rtthread.h, rtdevice.h):
// an ST7789 IPS panel, 320x240 landscape, on the SPI bus it shares with the radio and microSD.
// Every send is synchronous, from the main loop only, so it never collides with the radio
// (RadioLib uses the same bus from the same loop). Backlight: an AW9364 on GPIO42, 16 levels
// set by counting pulses.
#include <Arduino.h>
#include <SPI.h>
#include "compat/rtdevice.h"
#include "compat/bf0_hal.h"
#include "target.h"
#include "TDeckDevices.h"

#define TFT_DC     11
#define TFT_BL     42
#define TFT_W      LCD_HOR_RES_MAX
#define TFT_H      LCD_VER_RES_MAX
#define SPI_HZ     40000000
#define BL_LEVELS  16

static LCDC_HandleTypeDef s_lcdc = {{{TFT_W}}};
static rt_err_t (*s_done)(rt_device_t, void*) = nullptr;
static int s_x0, s_y0, s_x1, s_y1;  // the window (set_window)
static uint8_t s_level = 0;         // backlight level now, 0 = off

static void begin_tx() {
  tdeck_spi.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TFT_CS, LOW);
}
static void end_tx() {
  digitalWrite(PIN_TFT_CS, HIGH);
  tdeck_spi.endTransaction();
}
static void command(uint8_t c, const uint8_t* data = nullptr, int n = 0) {
  begin_tx();
  digitalWrite(TFT_DC, LOW);
  tdeck_spi.write(c);
  if (n) {
    digitalWrite(TFT_DC, HIGH);
    tdeck_spi.writeBytes(data, n);
  }
  end_tx();
}

// AW9364: high = on at full brightness; each low-high pulse steps one level down, wrapping
static void backlight(uint8_t level) {
  if (level > BL_LEVELS) level = BL_LEVELS;
  if (level == s_level) return;
  if (level == 0) {
    digitalWrite(TFT_BL, LOW);
    delay(3);  // (held low > 2.5 ms: off, and the count starts again)
    s_level = 0;
    return;
  }
  if (s_level == 0) {
    digitalWrite(TFT_BL, HIGH);
    delayMicroseconds(30);
    s_level = BL_LEVELS;
  }
  int pulses = (BL_LEVELS + (BL_LEVELS - level) - (BL_LEVELS - s_level)) % BL_LEVELS;
  for (int i = 0; i < pulses; i++) {
    digitalWrite(TFT_BL, LOW);
    delayMicroseconds(1);
    digitalWrite(TFT_BL, HIGH);
    delayMicroseconds(1);
  }
  s_level = level;
}

static void set_window(rt_uint16_t x1, rt_uint16_t y1, rt_uint16_t x2, rt_uint16_t y2) {
  s_x0 = x1; s_y0 = y1; s_x1 = x2; s_y1 = y2;
}

// the panel wants each pixel high byte first; the frame buffers hold them little endian
static void draw_rect(const char* pixels, int x1, int y1, int x2, int y2) {
  const uint8_t ca[] = {(uint8_t)(s_x0 >> 8), (uint8_t)s_x0, (uint8_t)(s_x1 >> 8), (uint8_t)s_x1};
  const uint8_t ra[] = {(uint8_t)(s_y0 >> 8), (uint8_t)s_y0, (uint8_t)(s_y1 >> 8), (uint8_t)s_y1};
  command(0x2A, ca, 4);
  command(0x2B, ra, 4);
  const int w = s_x1 - s_x0 + 1, h = s_y1 - s_y0 + 1, stride = s_lcdc.Layer[0].total_width;
  static uint16_t chunk[TFT_W * 8];  // (internal RAM: PSRAM rows are copied through it)
  begin_tx();
  digitalWrite(TFT_DC, LOW);
  tdeck_spi.write(0x2C);
  digitalWrite(TFT_DC, HIGH);
  const uint16_t* src = (const uint16_t*)pixels;
  int n = 0;
  for (int r = 0; r < h; r++) {
    const uint16_t* row = src + r * stride;
    for (int i = 0; i < w; i++) chunk[n++] = __builtin_bswap16(row[i]);
    if (n + w > TFT_W * 8 || r == h - 1) {
      tdeck_spi.writeBytes((const uint8_t*)chunk, n * 2);
      n = 0;
    }
  }
  end_tx();
  if (s_done) s_done(&tdeck_lcd_device, (void*)pixels);
}

static struct rt_device_graphic_ops s_ops = {set_window, draw_rect};

struct rt_device_graphic_ops* rt_graphix_ops(rt_device_t) { return &s_ops; }
extern "C" LCDC_HandleTypeDef* get_drv_lcd_handler(void) { return &s_lcdc; }
rt_err_t rt_device_set_tx_complete(rt_device_t, rt_err_t (*done)(rt_device_t, void*)) {
  s_done = done;
  return RT_EOK;
}

void HAL_LCDC_WriteU32Reg(LCDC_HandleTypeDef*, uint32_t cmd, uint8_t* data, int n) {
  uint8_t reg = (cmd >> 8) & 0xFF;
  if (reg == 0x36 && n == 1) {  // MADCTL: the SF32's rotations are from its panel's portrait;
    // the T-Deck's own landscape is 0x60. Its quarter turns: (not used yet: rotation 0 only)
    uint8_t m = 0x60;
    command(0x36, &m, 1);
    return;
  }
  command(reg, data, n);
}

static rt_err_t lcd_open(rt_device_t dev) {
  // the shared bus: started here (the screen comes up before the radio), the radio's
  // begin() then finds it running
  tdeck_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  command(0x01); delay(150);                         // software reset
  command(0x11); delay(120);                         // out of sleep
  const uint8_t colmod = 0x55;  command(0x3A, &colmod, 1);  // 16 bits per pixel
  const uint8_t madctl = 0x60;  command(0x36, &madctl, 1);  // landscape
  command(0x21);                                     // inversion on (this IPS panel needs it)
  command(0x13);                                     // normal mode
  return RT_EOK;
}

static rt_err_t lcd_control(rt_device_t dev, int cmd, void* arg) {
  switch (cmd) {
    case RTGRAPHIC_CTRL_POWERON:
      command(0x11);  // out of sleep
      delay(5);
      command(0x29);  // display on (the backlight follows with SET_BRIGHTNESS)
      break;
    case RTGRAPHIC_CTRL_POWEROFF:
      backlight(0);
      command(0x28);  // display off
      command(0x10);  // sleep
      break;
    case RTGRAPHIC_CTRL_SET_BRIGHTNESS: {
      int pct = *(uint8_t*)arg;
      backlight(pct <= 0 ? 0 : (uint8_t)((pct * BL_LEVELS + 99) / 100));
      break;
    }
    default:
      break;
  }
  return RT_EOK;
}

struct rt_device tdeck_lcd_device = {lcd_open, lcd_control, false, nullptr, nullptr};
