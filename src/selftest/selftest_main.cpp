// Self-test firmware (SF32_SELFTEST=1 ./build.sh) for the MeshCore port to the LilyGo T-Display-SF32.
//
// Step 1: C++ runtime and global constructors.
// Step 2: Arduino replacement layer + littlefs (selftest_arduino.cpp).
// Step 3: crypto, identities, CayenneLPP (selftest_crypto.cpp).
// Step 4: mesh::Radio driver + TRNG (selftest_radio.cpp), then listen for packets.

#include <rtthread.h>
#include <rthw.h>
#include <Arduino.h>
#include "selftest.h"
#include "target.h"

#define LOG_TAG "mc.boot"
#include <ulog.h>

namespace {

struct CtorProbe {
  volatile uint32_t magic;
  CtorProbe() : magic(0xC0FFEE42u) {}
};
CtorProbe g_ctor_probe;

}  // namespace

int main(void) {
  LOG_I("MeshCore SF32 bring-up");
  const bool ctors_ok = g_ctor_probe.magic == 0xC0FFEE42u;
  LOG_I("C++ global ctors: %s", ctors_ok ? "OK" : "NOT RUN");

  SelfTestResult ard, cry, rad;
  selftest_arduino(ard);
  LOG_I("Arduino layer self-test: %d passed, %d failed", ard.pass, ard.fail);
  selftest_crypto(cry);
  LOG_I("Crypto self-test: %d passed, %d failed", cry.pass, cry.fail);
  selftest_radio(rad, radio_driver);
  LOG_I("Radio self-test: %d passed, %d failed", rad.pass, rad.fail);
  LOG_I("Listening on 910.525 MHz / BW62.5 / SF7 / CR4/5 and logging MeshCore packets...");

  // Repeat the results in a heartbeat: there's no reliable way to reset the board
  // from the host, so a serial monitor can miss the first lines.
  uint32_t rx_count = 0;
  unsigned long next_beat = millis() + 30000;
  while (1) {
    radio_listen_step(radio_driver, rx_count);
    if ((long)(millis() - next_beat) >= 0) {
      next_beat += 30000;
      LOG_I("alive: ctors=%s arduino=%d/%d crypto=%d/%d radio=%d/%d | rx=%lu err=%lu noise=%d",
            ctors_ok ? "OK" : "FAIL", ard.pass, ard.pass + ard.fail, cry.pass, cry.pass + cry.fail,
            rad.pass, rad.pass + rad.fail, (unsigned long)rx_count,
            (unsigned long)radio_driver.getPacketsRecvErrors(), radio_driver.getNoiseFloor());
    }
    rt_thread_mdelay(5);
  }
  return 0;
}
