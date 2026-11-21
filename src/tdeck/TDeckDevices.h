// The RT-Thread devices the shared SF32 code opens (compat/rtthread.h), on the T-Deck:
// "lcd" (TDeckPanel.cpp) and "rtc" (TDeckDevices.cpp).
#pragma once
#include "compat/rtdevice.h"

struct rt_device {
  rt_err_t (*open)(rt_device_t dev);
  rt_err_t (*control)(rt_device_t dev, int cmd, void* arg);
  bool is_open;
};
extern struct rt_device tdeck_lcd_device;
