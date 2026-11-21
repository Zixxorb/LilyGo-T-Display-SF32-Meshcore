// The RT-Thread devices the shared SF32 code opens (compat/rtthread.h), on the T-Deck:
// "lcd" (TDeckPanel.cpp) and "rtc" (TDeckDevices.cpp).
#pragma once
#include "compat/rtdevice.h"

struct rt_device {
  rt_err_t (*open)(rt_device_t dev);
  rt_err_t (*control)(rt_device_t dev, int cmd, void* arg);
  bool is_open;
  rt_size_t (*read)(rt_device_t dev, void* buffer, rt_size_t size);
  rt_err_t (*rx_indicate)(rt_device_t dev, rt_size_t size);  // (set by its user)
};
extern struct rt_device tdeck_lcd_device, tdeck_touch_device;
void tdeck_touch_poll();  // TDeckTouch.cpp: the main loop asks the touch chip (TDeckBoard::loop)
