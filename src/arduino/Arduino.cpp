#include "Arduino.h"

#include <rtthread.h>
#include <rtdevice.h>
#include <stdarg.h>

ConsoleSerial Serial;

unsigned long millis() {
  return (unsigned long)((uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}

void delay(unsigned long ms) {
  rt_thread_mdelay(ms);
}

long random(long max) {
  return max <= 0 ? 0 : ::random() % max;
}

long random(long min, long max) {
  return min >= max ? min : random(max - min) + min;
}

void pinMode(int pin, int mode) {
  static const rt_base_t modes[] = {PIN_MODE_INPUT, PIN_MODE_OUTPUT, PIN_MODE_INPUT_PULLUP,
                                    PIN_MODE_INPUT_PULLDOWN};
  if (mode >= 0 && mode <= 3) rt_pin_mode(pin, modes[mode]);
}

int digitalRead(int pin) { return rt_pin_read(pin) == PIN_HIGH ? HIGH : LOW; }
void digitalWrite(int pin, int value) { rt_pin_write(pin, value ? PIN_HIGH : PIN_LOW); }
// Boards can provide virtual analog inputs (e.g. the touchscreen "button").
__attribute__((weak)) int sf32_analog_read_hook(int pin, bool* handled) { return 0; }
int analogRead(int pin) {
  bool handled = false;
  int v = sf32_analog_read_hook(pin, &handled);
  return handled ? v : 0;
}

void randomSeed(unsigned long seed) {
  if (seed != 0) srandom(seed);
}

char* ultoa(unsigned long value, char* str, int base) {
  char tmp[33];
  int i = 0;
  if (base < 2 || base > 36) { *str = 0; return str; }
  do {
    int d = value % base;
    tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10;
    value /= base;
  } while (value);
  char* p = str;
  while (i) *p++ = tmp[--i];
  *p = 0;
  return str;
}

char* ltoa(long value, char* str, int base) {
  if (value < 0 && base == 10) {
    *str = '-';
    ultoa(-(unsigned long)value, str + 1, base);
    return str;
  }
  return ultoa((unsigned long)value, str, base);
}

// ---- Print / Stream ----

size_t Print::write(const uint8_t* buf, size_t len) {
  size_t n = 0;
  while (n < len && write(buf[n])) n++;
  return n;
}

size_t Print::print(long n, int base) {
  char buf[34];
  return write(ltoa(n, buf, base));
}

size_t Print::print(unsigned long n, int base) {
  char buf[34];
  return write(ultoa(n, buf, base));
}

size_t Print::print(double n, int digits) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%.*f", digits, n);
  return write(buf);
}

size_t Print::printf(const char* fmt, ...) {
  char stack_buf[160];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
  va_end(args);
  if (len < 0) return 0;
  if ((size_t)len < sizeof(stack_buf)) return write((const uint8_t*)stack_buf, len);

  char* heap_buf = (char*)rt_malloc(len + 1);
  if (!heap_buf) return 0;
  va_start(args, fmt);
  vsnprintf(heap_buf, len + 1, fmt, args);
  va_end(args);
  size_t n = write((const uint8_t*)heap_buf, len);
  rt_free(heap_buf);
  return n;
}

size_t Stream::readBytes(uint8_t* buf, size_t len) {
  size_t n = 0;
  while (n < len) {
    unsigned long start = millis();
    int c;
    while ((c = read()) < 0) {
      if (millis() - start >= _timeout) return n;
      rt_thread_mdelay(1);
    }
    buf[n++] = (uint8_t)c;
  }
  return n;
}

// ---- Console ----

void ConsoleSerial::begin(unsigned long baud) {
  (void)baud;
  if (_rx_open) return;
  rt_device_t dev = rt_console_get_device();
  if (!dev) return;
  // Same flags the msh shell would use.
  rt_uint16_t oflag = RT_DEVICE_OFLAG_RDWR;
  oflag |= (dev->flag & RT_DEVICE_FLAG_DMA_RX) ? RT_DEVICE_FLAG_DMA_RX : RT_DEVICE_FLAG_INT_RX;
  _rx_open = rt_device_open(dev, oflag) == RT_EOK;
  // Raw output like Arduino: drop the console's \n -> \r\n conversion (which the
  // serial driver keeps across reopens), or MeshCore's println() sends \r\r\n.
  dev->open_flag &= ~RT_DEVICE_FLAG_STREAM;
}

size_t ConsoleSerial::write(const uint8_t* buf, size_t len) {
  rt_device_t dev = rt_console_get_device();
  if (!dev) return 0;
  return rt_device_write(dev, 0, buf, len);
}

int ConsoleSerial::peek() {
  if (_peeked < 0 && _rx_open) {
    uint8_t c;
    if (rt_device_read(rt_console_get_device(), 0, &c, 1) == 1) _peeked = c;
  }
  return _peeked;
}

int ConsoleSerial::available() {
  return peek() >= 0 ? 1 : 0;
}

int ConsoleSerial::read() {
  int c = peek();
  _peeked = -1;
  return c;
}
