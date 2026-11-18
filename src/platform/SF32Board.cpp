#include "SF32Board.h"
#include "SF32RTCClock.h"
#include "SF32Display.h"

extern SF32Display display;  // target.cpp
#include <helpers/ui/DisplayDriver.h>

#include <string.h>

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <stdio.h>
#include "sgm41562b_rt_device.h"
#include "bf0_hal.h"
#include <Arduino.h>

extern SF32Board board;  // target.cpp
#define LOG_TAG "board"
#include <ulog.h>

// VBAT is on GPADC channel 7 of the "bat1" ADC device; the driver returns 0.1 mV units
// (see T-Display-SF32-master/examples/adc_battery).
#define BATT_ADC_DEVICE   "bat1"
#define BATT_ADC_CHANNEL  7
// Each rt_adc_read() takes ~25 ms (22 conversions). Rather than block MeshCore's loop
// on every getBattMilliVolts(), sample once every 2 s in loop() and keep a rolling
// average of the last 16 readings. The average also smooths the charger's switching
// ripple on VBAT (single readings swing ~3.1-4.3 V while charging).
#define BATT_SAMPLE_MS    2000
#define BATT_SAMPLE_DARK_MS 6000  // screen off: each reading busy-waits ~25 ms at full speed

// Low-battery protection, as in LilyGo's factory firmware: on battery (no USB) below
// 3550 mV for 30 s, power off. The SF32LB52 needs > 3.5 V to start.
#define LOW_BATT_MV           3550
#define BATT_CHECK_MS         10000
#define LOW_BATT_CHECKS       3

#define WDT_FEED_MS       1000
#define CHG_POLL_MS       1000
#define CHG_WINDOW_MS     30000
#define CHG_CHANGES_NO_BATT 3   // state changes per window that mean "no battery"
#define MAIN_3V3_EN_PIN   41   // PA41, board 3.3 V rail
#define WAKE_PIN          34   // PA34, button "A"

extern "C" void rt_hw_watchdog_hook(int enable);

// Without RT-Thread's power manager (RT_USING_PM is off) the idle thread just spins.
// Sleep the core until the next interrupt or event instead (1 ms tick, UART, radio
// DIO1, LCD DMA, the BLE controller). WFE, as the SDK's own power manager uses
// (rt_hw_cpu_idle): the LCPU (BLE controller) signals the HCPU with events, which WFI
// doesn't wake for, so WFI broke Bluetooth.
static void idle_sleep() { __WFE(); }

#ifdef RT_USING_PM
// ---- RT-Thread / SiFli power manager (stage 1: light sleep only) ----
// Replaces the SDK's weak policy table (52x default: deep sleep after 30 ms) from the very
// start: light sleep whenever the next timer is at least 10 ms away, so the 25 ms
// screen-off main loop qualifies; deep sleep is never chosen. The low-power timer and
// GPIO (radio, touch, button, motion), Bluetooth (the LCPU) all wake it.
extern "C" const pm_policy_t pm_policy[] = {
  {10, PM_SLEEP_MODE_LIGHT},
};

// LilyGo's board support switches the LoRa radio's power off (PA26) and PA11 every time
// the chip sleeps, and back on when it wakes: the SX1262 then comes back unconfigured and
// deaf (RSSI -127). A mesh node must keep listening while asleep: leave both alone.
extern "C" void BSP_PowerDownCustom(int coreid, bool is_deep_sleep) { (void)coreid; (void)is_deep_sleep; }
extern "C" void BSP_PowerUpCustom(bool is_deep_sleep) { (void)is_deep_sleep; }

static uint8_t s_awake_mask = 0;  // AwakeReason bits; non-zero: one IDLE request held


// Held from early boot (right after the power manager starts): no sleep until the UI is up
// and the first minute is over (also the window to flash a board that won't answer while
// asleep).
static int sf32_pm_boot_hold(void) {
  s_awake_mask = SF32Board::AWAKE_BOOT;
  rt_pm_request(PM_SLEEP_MODE_IDLE);
  return 0;
}
INIT_ENV_EXPORT(sf32_pm_boot_hold);

// Sleep counters for battery tests, in every build (a few words of RAM): from the moment
// USB is unplugged until it's plugged back in (then they freeze, to be read over the debug
// channel: sf32_pm_stats). How much of the time the chip slept, how often it woke and what
// woke it (the wake source register's bits), how long something held it awake, and how
// many times the main loop ran.
extern "C" {
struct SF32PmStats {
  uint32_t magic;          // 'SFPM' once counting has started
  uint32_t batt_ms;        // time on battery (since the unplug)
  uint32_t slept_ms;       // of that, asleep
  uint32_t sleeps;         // sleeps (= wakes)
  uint32_t held_ms;        // time an awake reason was held (screen, audio, ...)
  uint32_t loops;          // main loop passes
  uint32_t held_mask_seen; // every awake reason that was held at some point (OR)
  uint32_t wsr[17];        // wakes per wake source bit 0..15 (RTC, GPIO1, LPTIM1, PMUC, -, -, LP2HP req, LP2HP irq, PIN0..7), [16] other
  uint32_t ev[4];          // SF32_PM_EV_*: radio DIO1 interrupts, touch interrupts, audio powered down, powered up
  uint32_t screen_ms;      // of the time on battery, the screen on
  uint32_t screen_ons;     // times it turned on
  uint32_t tx, rx;         // packets the radio sent / received (sending costs far more than listening)
};
volatile SF32PmStats sf32_pm_stats;
volatile uint32_t sf32_radio_tx = 0, sf32_radio_rx = 0;  // the radio's totals (the companion UI keeps them current)
}
static volatile bool s_pm_counting = false;
// on battery: count an event (interrupt-safe; see SF32_PM_EV_* in SF32Board.h)
extern "C" void sf32_pm_event(int k) {
  if (s_pm_counting && k >= 0 && k < 4) sf32_pm_stats.ev[k]++;
}
static volatile uint32_t s_pm_sleep_start = 0;
extern "C" uint32_t pm_get_wakeup_src(void);

#ifdef SF32_PM_LOG
static volatile uint32_t s_sleep_ms = 0, s_sleeps = 0;
#endif
static void pm_notify(uint8_t event, uint8_t mode, void*) {
  (void)mode;
  if (event == RT_PM_ENTER_SLEEP) { s_pm_sleep_start = HAL_GTIMER_READ(); return; }
  if (event != RT_PM_EXIT_SLEEP || !s_pm_sleep_start) return;
  uint32_t ms = (uint32_t)((uint64_t)(HAL_GTIMER_READ() - s_pm_sleep_start) * 1000 / HAL_LPTIM_GetFreq());
  s_pm_sleep_start = 0;
#ifdef SF32_PM_LOG
  s_sleep_ms += ms;
  s_sleeps++;
#endif
  if (!s_pm_counting) return;
  sf32_pm_stats.slept_ms += ms;
  sf32_pm_stats.sleeps++;
  uint32_t w = pm_get_wakeup_src();
  bool any = false;
  for (int b = 0; b < 16; b++) if (w & (1UL << b)) { sf32_pm_stats.wsr[b]++; any = true; }
  if (!any || (w >> 16)) sf32_pm_stats.wsr[16]++;
}

// board.loop(): start counting at the unplug (afresh), freeze at the replug
#define PM_STATS_MIN_MS (5UL * 60 * 1000)

#ifdef SF32_LINK_AB
// Battery test build (SF32_LINK_AB=1): on battery, 45 min with the normal Bluetooth link
// rhythm, then 45 min with low power mode's slow one, and so on (low power mode must be on,
// a phone connected). The battery voltage every 15 min, and the minutes a phone was
// connected in each kind of stretch, go to sf32_ab_log for tools/abstats.sh.
extern "C" {
struct SF32AbLog {
  uint32_t magic, n;
  uint16_t mv[64];                              // mv[i]: i * 15 min after the unplug
  uint32_t a_conn_ms, a_n, b_conn_ms, b_n;      // connected time in each kind (n: loops seen)
};
volatile SF32AbLog sf32_ab_log;
bool sf32_ble_link_connected(void);  // SF32SerialBLEInterface.cpp
}
#define AB_PHASE_MS  (45UL * 60 * 1000)
#define AB_SAMPLE_MS (15UL * 60 * 1000)
static bool ab_second_stretch() {
  return s_pm_counting && sf32_pm_stats.batt_ms >= AB_PHASE_MS && (sf32_pm_stats.batt_ms / AB_PHASE_MS) % 2 == 1;
}
extern "C" bool sf32_link_ab_slow(void) { return ab_second_stretch(); }
static void ab_log_loop(uint16_t mv) {
  static bool started = false;
  static unsigned long last = 0;
  unsigned long now = millis();
  if (!s_pm_counting) { started = false; return; }
  if (sf32_pm_stats.batt_ms < PM_STATS_MIN_MS) return;
  if (!started) {
    started = true;
    last = now;
    sf32_ab_log.magic = 0x53464142;
    sf32_ab_log.n = 0;
    sf32_ab_log.a_conn_ms = sf32_ab_log.a_n = sf32_ab_log.b_conn_ms = sf32_ab_log.b_n = 0;
  }
  uint32_t d = (uint32_t)(now - last);
  last = now;
  if (sf32_ble_link_connected()) {
    if (ab_second_stretch()) sf32_ab_log.b_conn_ms += d; else sf32_ab_log.a_conn_ms += d;
  }
  if (ab_second_stretch()) sf32_ab_log.b_n++; else sf32_ab_log.a_n++;
  uint32_t n = sf32_ab_log.n;
  if (n < 64 && sf32_pm_stats.batt_ms >= n * AB_SAMPLE_MS) {
    sf32_ab_log.mv[n] = mv;
    sf32_ab_log.n = n + 1;
  }
}
#endif

static uint32_t s_tx0 = 0, s_rx0 = 0;  // the radio's totals when the battery stretch began
static bool s_screen_was = false;
static void pm_stats_loop(bool external) {
  static unsigned long last = 0;
  unsigned long now = millis();
  // A blip of a few seconds off USB (seen while charging) would wipe a whole run's counts:
  // a battery stretch shorter than PM_STATS_MIN_MS gives the previous counts back.
  static SF32PmStats prev;
  if (external) {
    if (s_pm_counting && sf32_pm_stats.batt_ms < PM_STATS_MIN_MS && prev.magic) {
      rt_base_t level = rt_hw_interrupt_disable();
      s_pm_counting = false;
      memcpy((void*)&sf32_pm_stats, &prev, sizeof(prev));
      rt_hw_interrupt_enable(level);
    }
    s_pm_counting = false;
    return;
  }
  if (!s_pm_counting) {
    rt_base_t level = rt_hw_interrupt_disable();
    memcpy(&prev, (const void*)&sf32_pm_stats, sizeof(prev));
    memset((void*)&sf32_pm_stats, 0, sizeof(sf32_pm_stats));
    sf32_pm_stats.magic = 0x5346504D;
    s_pm_counting = true;
    rt_hw_interrupt_enable(level);
    last = now;
    s_tx0 = sf32_radio_tx;
    s_rx0 = sf32_radio_rx;
    s_screen_was = false;
    return;
  }
  uint32_t d = (uint32_t)(now - last);
  last = now;
  sf32_pm_stats.batt_ms += d;
  sf32_pm_stats.loops++;
  if (s_awake_mask) { sf32_pm_stats.held_ms += d; sf32_pm_stats.held_mask_seen |= s_awake_mask; }
  bool screen = s_awake_mask & SF32Board::AWAKE_SCREEN;  // (held exactly while the screen is on)
  if (screen) sf32_pm_stats.screen_ms += d;
  if (screen && !s_screen_was) sf32_pm_stats.screen_ons++;
  s_screen_was = screen;
  sf32_pm_stats.tx = sf32_radio_tx - s_tx0;
  sf32_pm_stats.rx = sf32_radio_rx - s_rx0;
}
#endif  // RT_USING_PM

void SF32Board::holdAwake(uint8_t reason, bool hold) {
#ifdef RT_USING_PM
  rt_base_t level = rt_hw_interrupt_disable();
  uint8_t before = s_awake_mask;
  s_awake_mask = hold ? (s_awake_mask | reason) : (s_awake_mask & ~reason);
  bool was = before != 0, now = s_awake_mask != 0;
  rt_hw_interrupt_enable(level);
  if (now && !was) rt_pm_request(PM_SLEEP_MODE_IDLE);       // one request while any reason
  else if (!now && was) rt_pm_release(PM_SLEEP_MODE_IDLE);
#else
  (void)reason; (void)hold;
#endif
}

bool SF32Board::sleepSupported() const {
#ifdef RT_USING_PM
  return true;
#else
  return false;
#endif
}

uint32_t SF32Board::takeSleepStats(uint32_t* sleeps) {
#if defined(RT_USING_PM) && defined(SF32_PM_LOG)
  rt_base_t level = rt_hw_interrupt_disable();
  uint32_t ms = s_sleep_ms, n = s_sleeps;
  s_sleep_ms = 0;
  s_sleeps = 0;
  rt_hw_interrupt_enable(level);
  if (sleeps) *sleeps = n;
  return ms;
#else
  if (sleeps) *sleeps = 0;
  return 0;
#endif
}

int sf32_i2c_scan(char* out, int out_size);  // below

// Weak default; the companion UI shows a message (src/companion_ui/UITask.cpp).
extern "C" __attribute__((weak)) void sf32_before_poweroff(int reason) { (void)reason; }

#ifndef SF32_COMPANION
// Repeater and room server: nothing reads the touchscreen, but the CST9220 scans from power-on
// (most of the screen-off drain in the 2026-11-08 test). Reset it and send deep sleep straight
// over I2C (as SF32TouchInput does with the screen dark): the SDK's touch device can't be used
// for this, its open probes the bus later from a thread. It answers nothing until reset, so
// it stays asleep until the next boot.
extern "C" void BSP_TP_PowerUp(void);
extern "C" void BSP_TP_Reset(uint8_t high1_low0);
static void touchSleep() {
  struct rt_i2c_bus_device* bus = (struct rt_i2c_bus_device*)rt_device_find("i2c1");
  if (!bus) return;
  BSP_TP_PowerUp();  // its pins, out of reset
  BSP_TP_Reset(0);
  rt_thread_mdelay(3);
  BSP_TP_Reset(1);
  rt_thread_mdelay(50);
  uint8_t cmd[2] = {0xD1, 0x05};  // CST92XX deep sleep (SF32TouchInput.cpp)
  struct rt_i2c_msg msg;
  msg.addr = 0x5A;  // the CST9220
  msg.flags = RT_I2C_WR;
  msg.buf = cmd;
  msg.len = 2;
  bool ok = rt_i2c_transfer(bus, &msg, 1) == 1;
  rt_kprintf("touch: %s\n", ok ? "asleep (not used)" : "sleep FAILED");
}
#endif

void SF32Board::begin() {
  // The SDK feeds the watchdog from the idle thread, which keeps it fed even if
  // MeshCore's loop is stuck. Feed it from loop() instead.
  if (rt_device_find("wdt")) rt_hw_watchdog_hook(0);
#ifndef SF32_COMPANION
  touchSleep();
#endif
#ifdef RT_USING_PM
  (void)idle_sleep;  // the power manager owns the idle thread (and sleeps the chip)
  rt_pm_notify_set(pm_notify, RT_NULL);
#else
  rt_thread_idle_sethook(idle_sleep);
#endif
}

// Raw SGM41562B register read over I2C (diagnostics only; read-only).
static bool charger_read_reg(uint8_t reg, uint8_t* val) {
  static struct rt_i2c_bus_device* bus = (struct rt_i2c_bus_device*)rt_device_find("i2c1");
  if (!bus) return false;
  struct rt_i2c_msg msgs[2];
  msgs[0].addr = 0x03;  // SGM41562B_I2C_ADDR
  msgs[0].flags = RT_I2C_WR;
  msgs[0].buf = &reg;
  msgs[0].len = 1;
  msgs[1].addr = 0x03;
  msgs[1].flags = RT_I2C_RD;
  msgs[1].buf = val;
  msgs[1].len = 1;
  return rt_i2c_transfer(bus, msgs, 2) == 2;
}

static const int kButtonPins[3] = {33, 34, 35};  // C, A, B

void SF32Board::loop() {
  unsigned long now = millis();
  if (!_boot_checked) {  // the filesystem is mounted by now (MeshCore's setup())
    _boot_checked = true;
    checkBootLog();
    if (_rtc) _rtc->afterSetup();
  }
#ifdef SF32_I2C_SCAN_AT_BOOT  // debug builds: log an I2C scan once, 5 s after boot
  static bool scanned = false;
  if (!scanned && now > 5000) {
    scanned = true;
    char buf[200];
    int n = sf32_i2c_scan(buf, sizeof(buf));
    rt_kprintf("I2C scan (%d found): %s\n", n, buf);
  }
#endif
  // Diagnostics: count level changes on the three button pins ("buttons" command).
  for (int i = 0; i < 3; i++) {
    uint8_t lvl = rt_pin_read(kButtonPins[i]) == PIN_HIGH;
    if (lvl != _btn_level[i]) {
      _btn_level[i] = lvl;
      _btn_edges[i]++;
    }
  }
#ifdef RT_USING_PM
  pm_stats_loop(isExternalPowered());
#ifdef SF32_LINK_AB
  ab_log_loop(getBattMilliVolts());
#endif
#endif
  if ((long)(now - _next_wdt_feed) >= 0) {
    _next_wdt_feed = now + WDT_FEED_MS;
    static rt_device_t wdt = rt_device_find("wdt");
    if (wdt) rt_device_control(wdt, RT_DEVICE_CTRL_WDT_KEEPALIVE, RT_NULL);
  }
  if ((long)(now - _next_batt_sample) >= 0) {
    // screen off (and on battery): a third as often; the 16-reading average then spans ~1.5 min,
    // still quick for the low-battery check against a charge that falls over hours
#ifdef RT_USING_PM
    bool dark = !(s_awake_mask & AWAKE_SCREEN) && !isExternalPowered();
#else
    bool dark = !isExternalPowered();  // (no power manager: the repeater / room server, no screen use)
#endif
    _next_batt_sample = now + (dark ? BATT_SAMPLE_DARK_MS : BATT_SAMPLE_MS);
    sampleBattery();
  }
  if ((long)(now - _next_chg_poll) >= 0) {
    _next_chg_poll = now + CHG_POLL_MS;
    trackChargerState();
  }
  if ((long)(now - _next_batt_check) >= 0) {
    _next_batt_check = now + BATT_CHECK_MS;
    checkBattery();
  }
}

void SF32Board::trackChargerState() {
  if (!readCharger()) return;
  // the charge limit: (re)written at start, when changed, and on each plug-in
  if (!_chg_limit_ok || (_chg_power_good && !_chg_pg_before)) {
    static rt_device_t chg = rt_device_find(SGM41562B_RT_DEVICE_NAME);
    rt_uint16_t mv = _chg_limit_mv;
    _chg_limit_ok = chg && rt_device_control(chg, SGM41562B_RT_CTRL_SET_CHARGE_VOLTAGE, &mv) == RT_EOK;
  }
  _chg_pg_before = _chg_power_good;
  unsigned long now = millis();
  if (!_chg_power_good) {
    // Running without USB: there must be a battery.
    _batt_absent = false;
    _chg_changes = 0;
    _last_chg_state = 0xFF;
    _chg_window_end = now + CHG_WINDOW_MS;
    return;
  }
  if (_last_chg_state != 0xFF && _chg_state != _last_chg_state) _chg_changes++;
  _last_chg_state = _chg_state;
  if ((long)(now - _chg_window_end) >= 0) {
    bool absent = _chg_changes >= CHG_CHANGES_NO_BATT;
    if (absent != _batt_absent) LOG_W("battery %s", absent ? "not detected" : "detected");
    _batt_absent = absent;
    _chg_changes = 0;
    _chg_window_end = now + CHG_WINDOW_MS;
  }
}

void SF32Board::checkBattery() {
  bool on_usb = readCharger() && _chg_power_good;
  uint16_t mv = getBattMilliVolts();  // 0 if no battery (only possible on USB)
  if (on_usb || mv == 0 || mv >= LOW_BATT_MV) {
    if (_low_batt_count) LOG_W("battery OK again (%d mV, USB %d)", mv, on_usb);
    _low_batt_count = 0;
    return;
  }
  _low_batt_count++;
  LOG_W("low battery: %d mV (%d/%d)", mv, _low_batt_count, LOW_BATT_CHECKS);
  if (_low_batt_count >= LOW_BATT_CHECKS) {
    LOG_E("battery below %d mV for 30 s: powering off", LOW_BATT_MV);
    sf32_before_poweroff(1);  // 1 = battery empty
    setPowerOffReason("battery empty");
    powerOff();
  }
}

static void wake_irq(void*) {}

// Same sequence as LilyGo's factory firmware (platform_power_shutdown): arm button A
// as a wake source, put the SGM41562B into shipping mode (disconnects the battery),
// and fall back to PMU shutdown if shipping mode can't cut power (USB connected).
// From shipping mode only the charger's own "D" key or plugging in USB powers the
// board back on. Button A can only wake it from the PMU-shutdown fallback.
// ---- why the board last stopped (/sf32_boot) ----
// "running" while up; a deliberate power-off / reboot writes its reason first. Finding
// "running" at the next start means power was lost or the chip reset unexpectedly.
#define BOOT_FILE "/sf32_boot"

static void boot_write(const char* state) {
  File f = InternalFS.open(BOOT_FILE, "w", true);
  if (f) f.write((const uint8_t*)state, strlen(state));
}

// ---- event log (/sf32_log): the last LOG_MAX events, "epoch|text" per line (epoch 0 =
// time not known), shown in the companion's Settings > Logs. Since 2026-11-02 each line
// also carries a snapshot of the board when it was written, after a \x1f:
//   uptime_s,batt_mV,usb,charge_state,chip_C,heap_free_B,heap_min_free_B,cpu_MHz[,extra]
// (tap an entry in Logs to see it). Older lines have none. ----
#define LOG_FILE "/sf32_log"
#define LOG_MAX  20
#define LOG_LINE 160
#define LOG_SEP  '\x1f'

void SF32Board::logEvent(const char* text, const char* extra) {
  static char buf[LOG_MAX * LOG_LINE];
  int n = 0;
  if (File f = InternalFS.open(LOG_FILE, "r")) {
    n = f.read((uint8_t*)buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
  }
  buf[n] = 0;
  // drop the oldest lines so that LOG_MAX - 1 remain
  int lines = 0;
  for (int i = 0; i < n; i++) if (buf[i] == '\n') lines++;
  char* start = buf;
  while (lines >= LOG_MAX) {
    char* nl = strchr(start, '\n');
    if (!nl) break;
    start = nl + 1;
    lines--;
  }
  uint32_t t = (_rtc && _rtc->timeKnown()) ? _rtc->getCurrentTime() : 0;
  // the snapshot
  bool chg_ok = readCharger();
  rt_uint32_t total = 0, used = 0, max_used = 0;
#ifdef RT_USING_HEAP
  rt_memory_info(&total, &used, &max_used);
#endif
  char clean[48];  // one line, no separator inside
  snprintf(clean, sizeof(clean), "%s", text);
  for (char* p = clean; *p; p++) if (*p == '\n' || *p == '\r' || *p == LOG_SEP) *p = ' ';
  char line[LOG_LINE];
  snprintf(line, sizeof(line), "%lu|%s%c%lu,%u,%d,%d,%d,%lu,%lu,%lu%s%s\n", (unsigned long)t, clean, LOG_SEP,
           (unsigned long)(millis() / 1000), (unsigned)getBattMilliVolts(), chg_ok ? (int)_chg_power_good : -1,
           chg_ok ? (int)_chg_state : -1, chipTempC(), (unsigned long)(total - used),
           (unsigned long)(total - max_used), (unsigned long)cpuMHz(), extra ? "," : "", extra ? extra : "");
  if (File f = InternalFS.open(LOG_FILE, "w", true)) {
    f.write((const uint8_t*)start, strlen(start));
    f.write((const uint8_t*)line, strlen(line));
  }
}

int SF32Board::readLog(uint32_t* times, char (*texts)[48], int max, char (*snaps)[LOG_SNAP_LEN]) {
  static char buf[LOG_MAX * LOG_LINE];
  int n = 0;
  if (File f = InternalFS.open(LOG_FILE, "r")) {
    n = f.read((uint8_t*)buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
  }
  buf[n] = 0;
  // newest first: walk the lines backwards
  int count = 0;
  char* end = buf + n;
  while (end > buf && count < max) {
    if (end[-1] == '\n') end--;
    char* line = end;
    while (line > buf && line[-1] != '\n') line--;
    if (end > line) {
      char tmp[LOG_LINE];
      int len = end - line < LOG_LINE - 1 ? end - line : LOG_LINE - 1;
      memcpy(tmp, line, len);
      tmp[len] = 0;
      char* sep = strchr(tmp, LOG_SEP);
      if (sep) *sep = 0;
      char* bar = strchr(tmp, '|');
      times[count] = bar ? strtoul(tmp, NULL, 10) : 0;
      snprintf(texts[count], 48, "%s", bar ? bar + 1 : tmp);
      if (snaps) snprintf(snaps[count], LOG_SNAP_LEN, "%s", sep ? sep + 1 : "");
      count++;
    }
    end = line;
  }
  return count;
}

void SF32Board::checkBootLog() {
  char prev[32] = "";
  File f = InternalFS.open(BOOT_FILE, "r");
  if (f) {
    int n = f.read((uint8_t*)prev, sizeof(prev) - 1);
    prev[n > 0 ? n : 0] = 0;
    f.close();
  }
  pm_power_on_mode_t mode = SystemPowerOnModeGet();
  if (prev[0] == 0) strcpy(_last_stop, "none yet (first start)");
  else if (strncmp(prev, "off:", 4) == 0) snprintf(_last_stop, sizeof(_last_stop), "power off (%s)", prev + 4);
  else if (strcmp(prev, "reboot") == 0) strcpy(_last_stop, "normal reboot");
  else if (mode == PM_REBOOT_BOOT) strcpy(_last_stop, "unexpected reset");
  else strcpy(_last_stop, "power lost or crash");
  LOG_W("last stop: %s (prev '%s', power-on mode %d, WSR %08lx)", _last_stop, prev, (int)mode,
        (unsigned long)HAL_PMU_GET_WSR());
  boot_write("running");
  char ev[64];
  snprintf(ev, sizeof(ev), "Started. Before: %s", _last_stop);
  char extra[40];  // why the chip started: power-on mode, PMU wake status
  snprintf(extra, sizeof(extra), "boot=%d,wsr=%08lx", (int)mode, (unsigned long)HAL_PMU_GET_WSR());
  logEvent(ev, extra);
}

void SF32Board::powerOff() {
  display.waitFlush();  // let a last frame (e.g. "Battery empty") reach the panel
  char state[24];
  snprintf(state, sizeof(state), "off:%s", _poweroff_reason);
  boot_write(state);
  char ev[48];
  snprintf(ev, sizeof(ev), "Power off (%s)", _poweroff_reason);
  logEvent(ev);
  HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1);
  HAL_HPAON_EnableWakeupSrc(HPAON_WAKEUP_SRC_PIN10, AON_PIN_MODE_POS_EDGE);
  rt_pin_mode(WAKE_PIN, PIN_MODE_INPUT);
  rt_pin_irq_enable(WAKE_PIN, PIN_IRQ_DISABLE);
  rt_pin_detach_irq(WAKE_PIN);
  if (rt_pin_attach_irq(WAKE_PIN, PIN_IRQ_MODE_RISING, wake_irq, RT_NULL) == RT_EOK)
    rt_pin_irq_enable(WAKE_PIN, PIN_IRQ_ENABLE);
  int8_t wakeup_pin = HAL_HPAON_QueryWakeupPin(hwp_gpio1, WAKE_PIN);
  if (wakeup_pin >= 0) {
    HAL_PMU_SelectWakeupPin(0, (uint8_t)wakeup_pin);
    HAL_PMU_EnablePinWakeup(0, AON_PIN_MODE_HIGH);
  }

  // Shipping mode (battery disconnected, nothing drains it, the clock stops) only for an
  // empty battery. Otherwise PMU shutdown: the RTC keeps the time on a few uA; button A
  // turns it back on.
  if (strcmp(_poweroff_reason, "battery empty") == 0) {
    rt_device_t chg = rt_device_find(SGM41562B_RT_DEVICE_NAME);
    if (chg) rt_device_control(chg, SGM41562B_RT_CTRL_ENTER_SHIPPING, RT_NULL);
  }

  rt_pin_write(MAIN_3V3_EN_PIN, PIN_LOW);
  rt_hw_interrupt_disable();
  HAL_PMU_EnterShutdown();
  while (1) {}
}

uint16_t SF32Board::readBattRaw() {
  rt_adc_device_t adc = (rt_adc_device_t)rt_device_find(BATT_ADC_DEVICE);
  if (!adc) return 0;
  rt_adc_enable(adc, BATT_ADC_CHANNEL);
  rt_uint32_t tenth_mv = rt_adc_read(adc, BATT_ADC_CHANNEL);
  rt_adc_disable(adc, BATT_ADC_CHANNEL);
  return (uint16_t)(tenth_mv / 10);
}

uint16_t SF32Board::getBattMilliVolts() {
  return _batt_absent ? 0 : readBattAvg();
}

uint16_t SF32Board::readBattAvg() {
  if (_batt_count == 0) sampleBattery();  // first call before loop() has sampled
  uint32_t sum = 0;
  for (int i = 0; i < _batt_count; i++) sum += _batt_samples[i];
  return _batt_count ? (uint16_t)(sum / _batt_count * _adc_multiplier) : 0;
}

void SF32Board::sampleBattery() {
  _batt_samples[_batt_idx] = readBattRaw();
  _batt_idx = (_batt_idx + 1) % BATT_AVG_N;
  if (_batt_count < BATT_AVG_N) _batt_count++;
}

bool SF32Board::readCharger() {
  static rt_device_t dev = rt_device_find(SGM41562B_RT_DEVICE_NAME);
  if (!dev) return false;
  struct sgm41562b_rt_status st;
  if (rt_device_read(dev, 0, &st, 1) != 1) return false;
  _chg_state = st.charge_status & 0x03;  // already shifted by the driver
  _chg_power_good = st.power_good != 0;
  _chg_sys = st.system_status;
  _chg_fault = st.fault_status;
  return true;
}

bool SF32Board::setAdcMultiplier(float multiplier) {
  // 0 means "not set" in MeshCore's prefs. Anything far from 1 is refused: a remote admin
  // could otherwise make the low-battery cutoff fire at once (powering off into shipping
  // mode) or never (over-discharging the cell).
  if (multiplier <= 0.0f) { _adc_multiplier = 1.0f; return true; }
  if (multiplier < 0.8f || multiplier > 1.25f) return false;
  _adc_multiplier = multiplier;
  return true;
}

void SF32Board::reboot() {
  boot_write("reboot");
  logEvent("Reboot");
  rt_hw_cpu_reset();
}

// Board-specific CLI commands (MeshCore calls this before its own commands).
// Probe every 7-bit address on the board's I2C buses (i2c1, i2c2, i2c4) with a 1-byte
// read; lists the ones that answer, e.g. "i2c1: 03 15; i2c2: -; i2c4: 6b".
int sf32_i2c_scan(char* out, int out_size) {
  static const char* const buses[] = {"i2c1", "i2c2", "i2c4"};
  int n = 0, found = 0;
  out[0] = 0;
  // every absent address makes the I2C driver log an error (and reset the bus): mute it
  ulog_tag_lvl_filter_set("drv.i2c", LOG_LVL_ASSERT);
  for (unsigned b = 0; b < sizeof(buses) / sizeof(buses[0]); b++) {
    n += snprintf(out + n, out_size - n, "%s%s:", b ? "; " : "", buses[b]);
    struct rt_i2c_bus_device* bus = (struct rt_i2c_bus_device*)rt_device_find(buses[b]);
    if (!bus) { n += snprintf(out + n, out_size - n, " n/a"); continue; }
    rt_uint32_t saved_timeout = bus->timeout;  // the SDK's default is 5 s per transfer
    bus->timeout = 5;                          // ms: absent addresses NACK quickly anyway
    int on_bus = 0;
    for (uint16_t a = 0x03; a <= 0x77; a++) {
      uint8_t v;
      struct rt_i2c_msg msg;
      msg.addr = a;
      msg.flags = RT_I2C_RD;
      msg.len = 1;
      msg.buf = &v;
      if (rt_i2c_transfer(bus, &msg, 1) == 1) {
        n += snprintf(out + n, out_size - n, " %02x", a);
        on_bus++;
      }
      if (n >= out_size - 4) {
        bus->timeout = saved_timeout;
        ulog_tag_lvl_filter_set("drv.i2c", LOG_FILTER_LVL_ALL);
        return found + on_bus;
      }
    }
    bus->timeout = saved_timeout;
    if (!on_bus) n += snprintf(out + n, out_size - n, " -");
    found += on_bus;
  }
  ulog_tag_lvl_filter_set("drv.i2c", LOG_FILTER_LVL_ALL);  // back to the global level
  return found;
}

bool SF32Board::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  if (_command_hook && _command_hook(command, sender_timestamp, reply)) return true;
  // "clock reset": MeshCore refuses to move the clock backwards, so a clock set
  // into the future can't otherwise be corrected. Serial only (sender_timestamp 0),
  // never over the mesh.
  if (strcmp(command, "clock reset") == 0 && sf32IsLocalConsole(sender_timestamp) && _rtc) {
    _rtc->reset();
    strcpy(reply, "OK - clock reset; set the time again (e.g. reconnect the configurator)");
    return true;
  }
  // "i2c scan": which addresses answer on the I2C buses (serial only)
  if (strcmp(command, "i2c scan") == 0 && sf32IsLocalConsole(sender_timestamp)) {
    sf32_i2c_scan(reply, 150);
    return true;
  }
  // "wdt test": stall MeshCore's loop to check the watchdog reboots the board (~30 s).
  if (strcmp(command, "wdt test") == 0 && sf32IsLocalConsole(sender_timestamp)) {
    Serial.println("  -> stalling the main loop; the watchdog should reboot in ~30 s");
    while (1) rt_thread_mdelay(100);  // other threads (idle) keep running
  }
  // "display on" / "display off": exercise the panel power path.
  if ((strcmp(command, "display on") == 0 || strcmp(command, "display off") == 0) && _display) {
    bool on = command[9] == 'n';
    if (on) _display->turnOn(); else _display->turnOff();
    sprintf(reply, "OK - display %s (isOn=%d)", on ? "on" : "off", _display->isOn());
    return true;
  }
  // "charger": raw SGM41562B registers 00-0B plus the bits that can keep a battery
  // from charging or connecting: EN_HIZ, CEB (REG01), FET_DIS (REG06).
  if (strcmp(command, "charger") == 0) {
    uint8_t r[12] = {0};
    char* p = reply;
    for (int i = 0; i < 12; i++) {
      if (!charger_read_reg(i, &r[i])) {
        strcpy(reply, "I2C read failed");
        return true;
      }
      p += sprintf(p, "%02X ", r[i]);
    }
    sprintf(p, "| HIZ=%d CEB=%d FET_DIS=%d", (r[1] >> 4) & 1, (r[1] >> 3) & 1, (r[6] >> 5) & 1);
    return true;
  }
  // "charger test" (USB serial only): pause charging (CEB, not HIZ: the board stays on
  // USB power) for 3 s and sample VBAT. With a cell attached, VBAT settles at the cell
  // voltage with little spread; with none, it collapses or wanders.
  if (strcmp(command, "charger test") == 0 && sf32IsLocalConsole(sender_timestamp)) {
    rt_device_t chg = rt_device_find(SGM41562B_RT_DEVICE_NAME);
    if (!chg) { strcpy(reply, "charger n/a"); return true; }
    rt_bool_t off = RT_FALSE, on = RT_TRUE;
    rt_device_control(chg, SGM41562B_RT_CTRL_ENABLE_CHARGING, &off);
    rt_thread_mdelay(2000);
    int lo = 99999, hi = 0, sum = 0;
    for (int i = 0; i < 20; i++) {
      int v = readBattRaw();
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
      sum += v;
      rt_thread_mdelay(50);
    }
    rt_device_control(chg, SGM41562B_RT_CTRL_ENABLE_CHARGING, &on);
    sprintf(reply, "charging paused: VBAT %d mV (min %d, max %d); charging re-enabled", sum / 20, lo, hi);
    return true;
  }
  // "buttons": live pin levels and number of level changes seen (C=PA33, A=PA34, B=PA35).
  if (strcmp(command, "buttons") == 0) {
    sprintf(reply, "C(PA33)=%d/%u A(PA34)=%d/%u B(PA35)=%d/%u (level/changes)",
            rt_pin_read(33) == PIN_HIGH, _btn_edges[0], rt_pin_read(34) == PIN_HIGH, _btn_edges[1],
            rt_pin_read(35) == PIN_HIGH, _btn_edges[2]);
    return true;
  }
  // "battery": voltage spread over 20 samples plus charger state (diagnostics).
  if (strcmp(command, "battery") == 0) {
    int lo = 99999, hi = 0, sum = 0;
    for (int i = 0; i < 20; i++) {
      int v = readBattRaw();
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
      sum += v;
      rt_thread_mdelay(10);
    }
    static const char* states[] = {"not charging", "pre-charge", "fast charge", "charge done"};
    bool ok = readCharger();
    // REG09 faults: VIN, thermal shutdown, battery OVP, safety timer, NTC hot/cold
    char faults[64] = "";
    static const struct { uint8_t bit; const char* name; } kFaults[] = {
        {1 << 5, " VIN"}, {1 << 4, " THERM_SD"}, {1 << 3, " BAT"}, {1 << 2, " SAFETY_TIMER"},
        {1 << 1, " NTC_HOT"}, {1 << 0, " NTC_COLD"}};
    for (auto& f : kFaults)
      if (_chg_fault & f.bit) strcat(faults, f.name);
    sprintf(reply, "%s%d mV (min %d, max %d); USB %s; %s; sys=%02X fault=%02X%s%s",
            _batt_absent ? "NO BATTERY (reports 0); VBAT " : "", sum / 20, lo, hi,
            ok ? (_chg_power_good ? "yes" : "no") : "?", ok ? states[_chg_state] : "charger n/a",
            _chg_sys, _chg_fault, faults[0] ? " faults:" : "", faults);
    return true;
  }
  return false;
}

// ---- CPU speed (DVFS) ----
// 240 MHz (S1) <-> 96 MHz (S0, lower core voltage). The SDK switch keeps the flash/PSRAM
// clock (DLL2, 288 MHz: allowed in S0). The RT-Thread tick runs from the CPU clock here, so
// it is set up again for the new speed; HAL_Delay_us(0) recalibrates the us delay loop.
// I2C and SPI get slower with the bus clock (fine); the UART runs from the fixed 48 MHz.
// The display controller needs full speed: SF32Display::turnOn() calls sf32_cpu_full_speed().
void SF32Board::setCpuLow(bool low) {
#ifdef RT_USING_PM
  (void)low;  // stage 1 of sleep: one change at a time (no clock scaling with the power manager)
  return;
#endif
  if (low == _cpu_low) return;
  if (HAL_RCC_HCPU_ConfigHCLK(low ? 96 : 240) != HAL_OK) {
    LOG_W("CPU clock change to %d MHz failed", low ? 96 : 240);
    return;
  }
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq(CORE_ID_DEFAULT) / RT_TICK_PER_SECOND);
  HAL_Delay_us(0);
  _cpu_low = low;
}

uint32_t SF32Board::cpuMHz() const { return HAL_RCC_GetHCLKFreq(CORE_ID_DEFAULT) / 1000000; }

extern "C" void sf32_cpu_full_speed() { board.setCpuLow(false); }

// ---- chip temperature (the SoC's TSEN) ----
int SF32Board::chipTempC() {
  static TSEN_HandleTypeDef tsen;
  static bool inited = false;
  static int last = 0;
  static unsigned long next = 0;
  if (!inited) {
    memset(&tsen, 0, sizeof(tsen));
    tsen.Instance = hwp_tsen;
    inited = HAL_TSEN_Init(&tsen) == HAL_OK;
    if (!inited) return 0;
  }
  if (next && (long)(millis() - next) < 0) return last;
  next = millis() + 30000;
  int t = HAL_TSEN_Read(&tsen);
  if (t > -40 && t < 125) last = t;
  return last;
}

#ifndef RT_USING_PM
extern "C" void sf32_pm_event(int) {}  // no battery stats without the power manager
#endif
