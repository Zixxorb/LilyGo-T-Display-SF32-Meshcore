// Step 2 self-test: Arduino layer (timing, random, Serial, Stream) and littlefs File/FS,
// including a boot counter to prove data persists.

#include "selftest.h"
#include "Arduino.h"
#include "FS.h"

#define LOG_TAG "test"
#include <ulog.h>

void selftest_check(SelfTestResult& r, bool ok, const char* what) {
  if (ok) {
    r.pass++;
  } else {
    r.fail++;
    LOG_E("FAIL: %s", what);
  }
}

static void test_core(SelfTestResult& r) {
  unsigned long t0 = millis();
  delay(100);
  unsigned long dt = millis() - t0;
  selftest_check(r, dt >= 100 && dt <= 110, "delay(100) takes ~100 ms");

  randomSeed(1234);
  bool in_range = true;
  for (int i = 0; i < 1000; i++) {
    long v = random(5, 10);
    if (v < 5 || v >= 10) in_range = false;
  }
  selftest_check(r, in_range, "random(5,10) stays in range");
  selftest_check(r, min(3, 7) == 3 && max(3, 7) == 7, "min/max");
  selftest_check(r, constrain(1.5f, 0, 1.0f) == 1.0f && constrain(5, 0, 20.0f) == 5, "constrain");

  char buf[16];
  selftest_check(r, strcmp(ltoa(-1234, buf, 10), "-1234") == 0, "ltoa negative");
  selftest_check(r, strcmp(ltoa(255, buf, 16), "ff") == 0, "ltoa hex");

  size_t n = Serial.printf("Serial.printf ok: %d %s\r\n", 42, "hello");
  selftest_check(r, n == strlen("Serial.printf ok: 42 hello\r\n"), "Serial.printf returns byte count");
  Serial.print("Serial.print ok: ");
  Serial.print(3.14159, 3);
  Serial.print(' ');
  Serial.println(0xBEEFUL, HEX);
}

static void test_fs(SelfTestResult& r) {
  selftest_check(r, InternalFS.begin(), "InternalFS.begin()");
  LOG_I("fs: %u KB used of %u KB", (unsigned)(InternalFS.usedBytes() / 1024),
        (unsigned)(InternalFS.totalBytes() / 1024));

  // Persistence: count boots.
  uint32_t boots = 0;
  if (File f = InternalFS.open("/bootcount")) {
    f.read((uint8_t*)&boots, sizeof(boots));
    f.close();
  }
  boots++;
  if (File f = InternalFS.open("/bootcount", "w")) {
    selftest_check(r, f.write((uint8_t*)&boots, sizeof(boots)) == sizeof(boots), "write bootcount");
    f.close();
  }
  LOG_I("boot count: %lu (should go up by one each reset)", (unsigned long)boots);

  // Write / read / append / seek / peek / remove, with parent-dir creation.
  const char* path = "/selftest/sub/hello.txt";
  InternalFS.remove(path);
  File w = InternalFS.open(path, "w", true);
  selftest_check(r, (bool)w, "open for write with create=true");
  selftest_check(r, w.print("hello ") == 6, "File.print");
  selftest_check(r, w.printf("%d", 123) == 3, "File.printf");
  w.close();
  selftest_check(r, InternalFS.exists(path), "exists after write");

  File a = InternalFS.open(path, "a");
  selftest_check(r, a.position() == 9, "append starts at end");
  a.write((const uint8_t*)"!", 1);
  a.close();

  File rd = InternalFS.open(path);
  selftest_check(r, rd.size() == 10, "size after append");
  selftest_check(r, rd.available() == 10, "available at start");
  selftest_check(r, rd.peek() == 'h' && rd.position() == 0, "peek does not advance");
  char buf[16] = {0};
  selftest_check(r, rd.readBytes(buf, 5) == 5 && memcmp(buf, "hello", 5) == 0, "readBytes");
  selftest_check(r, rd.seek(6) && rd.read() == '1', "seek + read");
  selftest_check(r, rd.available() == 3, "available after seek/read");
  rd.close();
  selftest_check(r, !rd, "closed File is false");

  // Copies share one handle; closing the last copy closes the file.
  File c1 = InternalFS.open(path);
  File c2 = c1;
  c1.close();
  selftest_check(r, (bool)c2 && c2.read() == 'h', "copy survives original close");
  c2.close();

  selftest_check(r, !InternalFS.open("/does/not/exist"), "missing file opens as false");
  selftest_check(r, InternalFS.remove(path) && !InternalFS.exists(path), "remove");
}

void selftest_arduino(SelfTestResult& r) {
  test_core(r);
  test_fs(r);
}
