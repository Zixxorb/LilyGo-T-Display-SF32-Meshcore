#include "TDeckDevices.h"
#include <string.h>
#include <sys/time.h>

// "rtc": the ESP32's own clock (it keeps going through a restart, not through a power cut)
static rt_err_t rtc_control(rt_device_t, int cmd, void* arg) {
  if (cmd == RT_DEVICE_CTRL_RTC_GET_TIME) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *(time_t*)arg = tv.tv_sec;
  } else if (cmd == RT_DEVICE_CTRL_RTC_SET_TIME) {
    struct timeval tv = {*(time_t*)arg, 0};
    settimeofday(&tv, NULL);
  }
  return RT_EOK;
}
static struct rt_device s_rtc = {nullptr, rtc_control, false, nullptr, nullptr};

rt_device_t rt_device_find(const char* name) {
  if (strcmp(name, "lcd") == 0) return &tdeck_lcd_device;
  if (strcmp(name, "rtc") == 0) return &s_rtc;
  if (strcmp(name, "touch") == 0) return &tdeck_touch_device;
  return nullptr;
}
rt_err_t rt_device_open(rt_device_t dev, uint16_t) {
  if (dev->is_open) return RT_EOK;
  rt_err_t r = dev->open ? dev->open(dev) : RT_EOK;
  if (r == RT_EOK) dev->is_open = true;
  return r;
}
rt_err_t rt_device_control(rt_device_t dev, int cmd, void* arg) { return dev->control ? dev->control(dev, cmd, arg) : RT_EOK; }
rt_err_t rt_device_set_rx_indicate(rt_device_t dev, rt_err_t (*ind)(rt_device_t, rt_size_t)) {
  dev->rx_indicate = ind;
  return RT_EOK;
}
rt_size_t rt_device_read(rt_device_t dev, int, void* buffer, rt_size_t size) {
  return dev->read ? dev->read(dev, buffer, size) : 0;
}
