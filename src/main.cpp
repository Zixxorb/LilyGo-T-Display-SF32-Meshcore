// MeshCore firmware for the LilyGo T-Display-SF32.
//
// setup() and loop() are MeshCore's own examples/simple_repeater/main.cpp (default)
// or examples/companion_radio/main.cpp (SF32_FIRMWARE=companion), compiled
// unmodified (see lib/SConscript). This just runs them as an RT-Thread main thread.

#include <rtthread.h>
#include <ulog.h>

void setup();
void loop();

// Set by the companion UI while it animates (scrolling, gliding): the loop then sleeps
// 1 ms instead of 4 so the next frame isn't held up.
volatile bool sf32_ui_animating = false;
volatile uint8_t sf32_ui_wait_ms = 1;  // while animating: how long until the next frame is due (1..4)
// The companion UI sets this: 4 ms with the screen in use, 25 ms otherwise (fewer wakeups;
// the radio holds a received packet, and packets take 30+ ms on air).
volatile uint8_t sf32_loop_sleep_ms = 4;

int main(void) {
#if defined(SF32_LOG_LEVEL)
  ulog_global_filter_lvl_set(SF32_LOG_LEVEL);  // debug builds, e.g. -DSF32_LOG_LEVEL=LOG_LVL_INFO
#elif defined(SF32_COMPANION)
  // The console carries the binary companion protocol over USB: no log output at all.
  ulog_global_filter_lvl_set(LOG_LVL_ASSERT);
#else
  // The console is also the CLI that config.meshcore.io talks to. Keep log
  // output off it except for real problems.
  ulog_global_filter_lvl_set(LOG_LVL_WARNING);
#endif

  setup();
  while (1) {
    loop();
    if (sf32_ui_animating) {  // the UI is mid-animation: back in time for its next frame
      rt_thread_mdelay(sf32_ui_wait_ms);  // (until it's due, not 1 ms polls: each pass runs the whole loop)
      continue;
    }
    // Let lower-priority threads (idle, timers) run; the idle thread sleeps the CPU.
    // 4 ms is plenty for MeshCore (the SX1262 holds a received packet; packets take
    // 30+ ms on air) and the UI, and wakes the CPU 250 rather than 1000 times a second.
    rt_thread_mdelay(sf32_loop_sleep_ms);
  }
  return 0;
}
