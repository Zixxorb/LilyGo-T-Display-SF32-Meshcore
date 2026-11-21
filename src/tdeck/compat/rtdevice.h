// T-Deck: the graphics device API SF32Display uses (see rtthread.h).
#pragma once
#include "rtthread.h"

#define RTGRAPHIC_CTRL_POWERON         1
#define RTGRAPHIC_CTRL_POWEROFF        2
#define RTGRAPHIC_CTRL_SET_BRIGHTNESS  3
#define RTGRAPHIC_CTRL_SET_BUF_FORMAT  4
#define RTGRAPHIC_PIXEL_FORMAT_RGB565  1

struct rt_device_graphic_ops {
  // the window the next draw_rect_async fills (panel pixels, inclusive)
  void (*set_window)(rt_uint16_t x1, rt_uint16_t y1, rt_uint16_t x2, rt_uint16_t y2);
  // sends the rectangle; rows of the source are LCDC Layer total_width pixels apart. On the
  // T-Deck it's synchronous: the tx_complete callback runs before it returns.
  void (*draw_rect_async)(const char* pixels, int x1, int y1, int x2, int y2);
};
struct rt_device_graphic_ops* rt_graphix_ops(rt_device_t dev);
