// Minimal Arduino Print/Stream for MeshCore on RT-Thread.
// Covers only what MeshCore uses: write/print/println/printf, read/available/peek, readBytes.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
public:
  virtual ~Print() {}

  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buf, size_t len);
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t write(const char* buf, size_t len) { return write((const uint8_t*)buf, len); }
  virtual void flush() {}

  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned char n, int base = DEC) { return print((unsigned long)n, base); }
  size_t print(int n, int base = DEC) { return print((long)n, base); }
  size_t print(unsigned int n, int base = DEC) { return print((unsigned long)n, base); }
  size_t print(long n, int base = DEC);
  size_t print(unsigned long n, int base = DEC);
  size_t print(double n, int digits = 2);

  size_t println() { return write("\r\n"); }
  template <typename T> size_t println(T v) { size_t n = print(v); return n + println(); }
  template <typename T> size_t println(T v, int fmt) { size_t n = print(v, fmt); return n + println(); }

  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};

class Stream : public Print {
protected:
  unsigned long _timeout = 1000;  // ms, used by readBytes()

public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;

  void setTimeout(unsigned long ms) { _timeout = ms; }

  // Reads up to len bytes, waiting up to the timeout for each byte. Returns bytes read.
  virtual size_t readBytes(uint8_t* buf, size_t len);
  size_t readBytes(char* buf, size_t len) { return readBytes((uint8_t*)buf, len); }
};
