// The T-Deck's touchscreen (GT911, I2C 0x5D or 0x14, shared with the keyboard) as the "touch"
// device SF32TouchInput reads (compat/drv_touch.h): its gestures, taps and drags are the
// SF32's, unchanged. The main loop asks the chip for news every 15 ms (TDeckBoard::loop) and
// queues down / move / up messages in panel pixels, announcing each like the SF32's touch
// interrupt does.
#include <Arduino.h>
#include <Wire.h>
#include "TDeckDevices.h"
#include "compat/drv_touch.h"

#define GT911_STATUS   0x814E  // bit 7: new data; low 4 bits: how many points
#define GT911_POINT1   0x8150  // x lo, x hi, y lo, y hi, size lo, size hi
#define GT911_PRODUCT  0x8140  // "911"
#define POLL_MS        15
#define QUEUE_N        16

static uint8_t s_addr = 0;
static bool s_down = false;
static uint16_t s_x = 0, s_y = 0;
static touch_message s_queue[QUEUE_N];
static int s_head = 0, s_count = 0;

static bool gt_read(uint16_t reg, uint8_t* buf, int n) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)s_addr, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static void gt_write(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(v);
  Wire.endTransmission();
}

static void push(uint8_t event, uint16_t x, uint16_t y) {
  if (s_count == QUEUE_N) return;  // (full: the UI isn't reading; drop)
  s_queue[(s_head + s_count) % QUEUE_N] = {x, y, event};
  s_count++;
  if (tdeck_touch_device.rx_indicate) tdeck_touch_device.rx_indicate(&tdeck_touch_device, 1);
}

static rt_err_t touch_open(rt_device_t) {
  for (uint8_t a : {0x5D, 0x14}) {
    s_addr = a;
    uint8_t id[3];
    if (gt_read(GT911_PRODUCT, id, 3) && id[0] == '9') {
#ifdef TDECK_TOUCH_LOG
      Serial.printf("touch: GT911 at 0x%02x\n", a);
#endif
      return RT_EOK;
    }
  }
#ifdef TDECK_TOUCH_LOG
  Serial.println("touch: no GT911");
#endif
  s_addr = 0;
  return RT_ERROR;
}

static rt_size_t touch_read(rt_device_t, void* buffer, rt_size_t size) {
  if (!s_count || size < 1) return 0;
  *(touch_message*)buffer = s_queue[s_head];
  s_head = (s_head + 1) % QUEUE_N;
  s_count--;
  return 1;
}

struct rt_device tdeck_touch_device = {touch_open, nullptr, false, touch_read, nullptr};

void tdeck_touch_poll() {
  static unsigned long next = 0;
  if (!s_addr || !tdeck_touch_device.is_open || (long)(millis() - next) < 0) return;
  next = millis() + POLL_MS;
  uint8_t st;
  if (!gt_read(GT911_STATUS, &st, 1) || !(st & 0x80)) return;  // nothing new
  int points = st & 0x0F;
  if (points > 0) {
    uint8_t p[4];
    if (gt_read(GT911_POINT1, p, 4)) {
      int rx = p[0] | p[1] << 8, ry = p[2] | p[3] << 8;
      // the chip's axes are the panel's portrait ones: swapped, and y mirrored
      int x = ry, y = LCD_VER_RES_MAX - 1 - rx;
      x = x < 0 ? 0 : x >= LCD_HOR_RES_MAX ? LCD_HOR_RES_MAX - 1 : x;
      y = y < 0 ? 0 : y >= LCD_VER_RES_MAX ? LCD_VER_RES_MAX - 1 : y;
#ifdef TDECK_TOUCH_LOG
      Serial.printf("touch raw %d,%d -> %d,%d\n", rx, ry, x, y);
#endif
      if (!s_down || x != s_x || y != s_y) push(s_down ? TOUCH_EVENT_MOVE : TOUCH_EVENT_DOWN, x, y);
      s_down = true;
      s_x = x;
      s_y = y;
    }
  } else if (s_down) {
    s_down = false;
    push(TOUCH_EVENT_UP, s_x, s_y);
  }
  gt_write(GT911_STATUS, 0);  // taken
}

// SF32TouchInput's sleep commands are for the SF32's chip: "sleep" fails (it keeps scanning,
// for now: the GT911's own sleep is power work for later), "normal" is a no-op.
#define CST92XX_REG_DEEP_SLEEP 0xD105
extern "C" uint32_t cst922_i2c_write(uint16_t reg, uint8_t*, uint16_t) {
  return reg == CST92XX_REG_DEEP_SLEEP ? RT_ERROR : RT_EOK;
}
extern "C" void BSP_TP_Reset(uint8_t) {}
