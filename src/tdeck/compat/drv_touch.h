// T-Deck: the SF32 SDK's touch messages (drv_touch.h there), as the "touch" device returns
// them (TDeckTouch.cpp: the GT911), in panel pixels.
#pragma once
#include <stdint.h>
#include "rtthread.h"

#define TOUCH_EVENT_UP    0
#define TOUCH_EVENT_DOWN  1
#define TOUCH_EVENT_MOVE  2
struct touch_message {
  uint16_t x;
  uint16_t y;
  uint8_t event;
};

// The SF32's touch chip (CST9220) commands SF32TouchInput sends for its sleep mode: the GT911
// has its own (not done yet), so "sleep" fails and it keeps scanning (see TDeckTouch.cpp)
extern "C" uint32_t cst922_i2c_write(uint16_t reg, uint8_t* p_data, uint16_t len);
extern "C" void BSP_TP_Reset(uint8_t high1_low0);
