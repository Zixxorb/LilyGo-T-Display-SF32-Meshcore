// Minimal Arduino API for building MeshCore on the SiFli SDK (RT-Thread).
// Only what MeshCore and its helpers actually use. Add to it as needed, not speculatively.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "Stream.h"

#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))

#ifdef __cplusplus
#include <algorithm>
// Same approach as the ESP32 Arduino core, which MeshCore already builds against.
using std::min;
using std::max;

// Arduino cores define abs() as a macro, so abs() of an unsigned value compiles there.
// Exact-match overloads give the same (identity) result without a macro.
inline unsigned int abs(unsigned int x) { return x; }
inline unsigned long abs(unsigned long x) { return x; }

unsigned long millis();
void delay(unsigned long ms);

// Arduino-style random(); newlib's `long random(void)` remains available.
long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);

// GPIO, numbered as RT-Thread pins (PA34 = 34)
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
void pinMode(int pin, int mode);
int digitalRead(int pin);
void digitalWrite(int pin, int value);
int analogRead(int pin);  // no real ADC pins; boards can add virtual ones (see Arduino.cpp)

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// newlib provides itoa/utoa but not these.
char* ltoa(long value, char* str, int base);
char* ultoa(unsigned long value, char* str, int base);

// Console UART, which is also the CH343P USB serial port.
//
// begin() takes over console input (the msh shell is disabled in proj.conf) and
// switches output to raw bytes, as on Arduino. Before begin(), output still goes
// through RT-Thread's console with \n -> \r\n conversion.
class ConsoleSerial : public Stream {
public:
  void begin(unsigned long baud);  // baud is fixed by Kconfig (RT_SERIAL_DEFAULT_BAUDRATE)
  using Print::write;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t len) override;
  int available() override;
  int read() override;
  int peek() override;
  operator bool() const { return true; }

private:
  bool _rx_open = false;
  int _peeked = -1;
};

extern ConsoleSerial Serial;
#endif
