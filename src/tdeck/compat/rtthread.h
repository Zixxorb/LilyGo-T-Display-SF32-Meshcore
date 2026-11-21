// T-Deck: the few RT-Thread calls the shared SF32 code (SF32Display, the companion UI) makes,
// on Arduino-ESP32. Ticks are milliseconds. The "lcd" device is the T-Deck's ST7789
// (TDeckPanel.cpp). Not RT-Thread: only what that code uses.
#pragma once
#include <Arduino.h>
#include <stdint.h>

typedef int rt_err_t;
typedef uint32_t rt_tick_t;
typedef uint16_t rt_uint16_t;
typedef uint8_t rt_uint8_t;
typedef uint32_t rt_uint32_t;
typedef int32_t rt_int32_t;
typedef uint32_t rt_size_t;
typedef int32_t rt_base_t;
#define RT_EOK        0
#define RT_ERROR      1
#define RT_ETIMEOUT   2
#define RT_NULL       NULL
#define RT_WAITING_FOREVER  -1
#define RT_TICK_PER_SECOND  1000
#define RT_DEVICE_OFLAG_RDWR 0x003
#define RT_DEVICE_FLAG_RDONLY 0x001
#define RT_IPC_FLAG_FIFO     0x00
#define RT_IPC_CMD_RESET     0x01

// the panel (rtconfig.h on the SF32)
#define LCD_HOR_RES_MAX 320
#define LCD_VER_RES_MAX 240

static inline rt_tick_t rt_tick_get() { return millis(); }
static inline rt_tick_t rt_tick_from_millisecond(int32_t ms) { return (rt_tick_t)ms; }
static inline rt_err_t rt_thread_delay(rt_tick_t t) { delay(t); return RT_EOK; }
static inline rt_err_t rt_thread_mdelay(int32_t ms) { delay(ms); return RT_EOK; }
#define rt_kprintf(...) Serial.printf(__VA_ARGS__)

// A semaphore without threads: the panel sends synchronously, so a "send done" is always
// released before anyone waits for it.
struct rt_semaphore { volatile int value; };
static inline rt_err_t rt_sem_init(struct rt_semaphore* s, const char*, uint32_t v, uint8_t) { s->value = v; return RT_EOK; }
static inline rt_err_t rt_sem_release(struct rt_semaphore* s) { s->value++; return RT_EOK; }
static inline rt_err_t rt_sem_trytake(struct rt_semaphore* s) {
  if (s->value <= 0) return RT_ETIMEOUT;
  s->value--;
  return RT_EOK;
}
static inline rt_err_t rt_sem_take(struct rt_semaphore* s, int32_t) { return rt_sem_trytake(s); }
static inline rt_err_t rt_sem_control(struct rt_semaphore* s, int cmd, void*) {
  if (cmd == RT_IPC_CMD_RESET) s->value = 0;
  return RT_EOK;
}

// devices: "lcd", "rtc", "touch" (TDeckDevices.cpp)
struct rt_device;
typedef struct rt_device* rt_device_t;
rt_device_t rt_device_find(const char* name);
rt_err_t rt_device_open(rt_device_t dev, uint16_t flags);
rt_err_t rt_device_control(rt_device_t dev, int cmd, void* arg);
rt_err_t rt_device_set_tx_complete(rt_device_t dev, rt_err_t (*done)(rt_device_t dev, void* buffer));
rt_err_t rt_device_set_rx_indicate(rt_device_t dev, rt_err_t (*ind)(rt_device_t dev, rt_size_t size));
rt_size_t rt_device_read(rt_device_t dev, int pos, void* buffer, rt_size_t size);
