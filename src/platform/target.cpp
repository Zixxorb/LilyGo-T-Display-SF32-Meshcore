#include "target.h"
#include "SF32HardwareRNG.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

SF32Board board;
SF32SX1262Radio radio_driver(board);
SF32RTCClock rtc_clock;
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, false, true);  // active high, pull-down
  #ifdef PIN_USER_BTN_ANA
    MomentaryButton analog_btn(PIN_USER_BTN_ANA, 1000, 2000);  // "pressed" when analogRead < 2000
  #endif
#endif

// ---- RF noise diagnostics ("noise", "noise test"), USB serial only ----

static int noise_now(int* lo = nullptr, int* hi = nullptr) { return radio_driver.sampleNoise(60, lo, hi); }

static bool platform_command(const char* command, uint32_t sender_timestamp, char* reply) {
  if (!sf32IsLocalConsole(sender_timestamp)) return false;  // USB console only (not the app or remote CLI)
  if (strcmp(command, "noise") == 0) {
    int lo, hi, avg = noise_now(&lo, &hi);
    sprintf(reply, "%d dBm (min %d, max %d) at %lu kHz", avg, lo, hi,
            (unsigned long)(radio_driver.frequencyHz() / 1000));
    return true;
  }
  // "noise scan FROM_KHZ TO_KHZ STEP_KHZ": average noise per frequency, screen off
  unsigned long a, b, st;
  if (sscanf(command, "noise scan %lu %lu %lu", &a, &b, &st) == 3 && st > 0 && b >= a && (b - a) / st <= 400) {
    uint32_t freq = radio_driver.frequencyHz();
#ifdef DISPLAY_CLASS
    bool was_on = display.isOn();
    display.turnOff();
    delay(200);
#endif
    Serial.print("scan (kHz:dBm):");
    static rt_device_t wdt = rt_device_find("wdt");
    for (unsigned long k = a; k <= b; k += st) {
      // a long scan (up to ~50 s) mustn't trip the 30 s watchdog: feed it as we go
      if (wdt) rt_device_control(wdt, RT_DEVICE_CTRL_WDT_KEEPALIVE, RT_NULL);
      radio_driver.setFrequencyHz(k * 1000UL);
      Serial.printf(" %lu:%d", k, radio_driver.sampleNoise(60));
    }
    Serial.print("\r\n");
    radio_driver.setFrequencyHz(freq);
    radio_driver.sampleNoise(1);
#ifdef DISPLAY_CLASS
    if (was_on) display.turnOn();
#endif
    strcpy(reply, "done");
    return true;
  }
  if (strcmp(command, "noise test") != 0) return false;

  // Each line: condition, average instant RSSI in dBm (60 samples).
  uint32_t freq = radio_driver.frequencyHz();
  int lo, hi;
  Serial.printf("baseline: %d (min %d max %d)\r\n", noise_now(&lo, &hi), lo, hi);

  // (No charger-input-off step: with a weak or missing battery it cuts the board's
  // power, and the SGM41562B stays in HIZ until USB is replugged.)

  radio_driver.setRegulatorLDO(true);
  Serial.printf("SX1262 LDO regulator: %d\r\n", noise_now());
  radio_driver.setRegulatorLDO(false);
  Serial.printf("SX1262 DC-DC regulator: %d\r\n", noise_now());

  // receive gain: boosted (MeshCore's default) vs power saving
  bool boost = radio_driver.getRxBoostedGainMode();
  radio_driver.setRxBoostedGainMode(false);
  Serial.printf("RX gain power-saving: %d\r\n", noise_now());
  radio_driver.setRxBoostedGainMode(true);
  Serial.printf("RX gain boosted: %d\r\n", noise_now());
  radio_driver.setRxBoostedGainMode(boost);

  // CPU busy (no sleep between samples) vs mostly asleep (the normal case)
  {
    radio_driver.sampleNoise(1);
    int sum = 0;
    for (int i = 0; i < 60; i++) {
      unsigned long t = millis() + 2;
      volatile uint32_t spin = 0;
      while ((long)(millis() - t) < 0) spin++;  // keep the CPU running flat out
      sum += (int)radio_driver.getCurrentRSSI();
    }
    Serial.printf("CPU busy: %d\r\n", sum / 60);
  }

#ifdef DISPLAY_CLASS
  bool was_on = display.isOn();
  display.turnOff();
  delay(200);
  Serial.printf("display off: %d\r\n", noise_now());
  display.turnOn();
  display.startFrame(0x0000);
  display.endFrame();
  delay(200);
  Serial.printf("display on (black): %d\r\n", noise_now());
  display.startFrame(0xFFFF);
  display.endFrame();
  delay(200);
  Serial.printf("display on (white block): %d\r\n", noise_now());
  display.startFrame(0x0000);
  display.endFrame();
  if (!was_on) display.turnOff();
#endif

  Serial.print("sweep (MHz:dBm):");
  for (uint32_t mhz = 902; mhz <= 928; mhz += 2) {
    radio_driver.setFrequencyHz(mhz * 1000000UL);
    Serial.printf(" %lu:%d", (unsigned long)mhz, radio_driver.sampleNoise(30));
  }
  Serial.print("\r\n");
  // fine sweep around the channel and the 912 MHz spur (19 x 48 MHz), 250 kHz steps
  Serial.print("fine (kHz:dBm):");
  for (uint32_t khz = 909500; khz <= 913000; khz += 250) {
    radio_driver.setFrequencyHz(khz * 1000UL);
    Serial.printf(" %lu:%d", (unsigned long)khz, radio_driver.sampleNoise(20));
  }
  Serial.print("\r\n");
  radio_driver.setFrequencyHz(freq);
  radio_driver.sampleNoise(1);  // back in RX at the original frequency

  sprintf(reply, "done (radio back on %lu kHz)", (unsigned long)(freq / 1000));
  return true;
}

// Initial modulation; MyMesh::begin() applies the saved prefs right after.
bool radio_init() {
  rtc_clock.begin();
  board.setRTC(&rtc_clock);
  board.setCommandHook(platform_command);
#ifdef DISPLAY_CLASS
  board.setDisplay(&display);
#endif
  return radio_driver.init(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, LORA_TX_POWER) == RADIOLIB_ERR_NONE;
}

// Hardware TRNG mixed with SX1262 wideband noise.
class MixedRNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override {
    SF32HardwareRNG trng;
    trng.random(dest, sz);
    uint8_t noise[64];
    for (size_t off = 0; off < sz; off += sizeof(noise)) {
      size_t n = sz - off < sizeof(noise) ? sz - off : sizeof(noise);
      radio_driver.randomBytes(noise, n);
      for (size_t i = 0; i < n; i++) dest[off + i] ^= noise[i];
    }
  }
};

mesh::LocalIdentity radio_new_identity() {
  MixedRNG rng;
  return mesh::LocalIdentity(&rng);
}
