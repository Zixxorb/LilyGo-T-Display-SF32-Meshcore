// Step 4 self-test: SF32SX1262Radio (mesh::Radio on LilyGo's C RadioLib) and the TRNG.
//
// Uses MeshCore's US preset. The one transmission is a direct-route, zero-hop
// RAW_CUSTOM packet, which MeshCore nodes never forward (Mesh.cpp).

#include "selftest.h"
#include <Arduino.h>
#include <Packet.h>
#include "SF32SX1262Radio.h"
#include "SF32HardwareRNG.h"

#define LOG_TAG "test.rad"
#include <ulog.h>

// US preset
#define TEST_FREQ   910.525f
#define TEST_BW     62.5f
#define TEST_SF     7
#define TEST_CR     5
#define TEST_POWER  20

static bool all_same(const uint8_t* b, size_t n) {
  for (size_t i = 1; i < n; i++)
    if (b[i] != b[0]) return false;
  return true;
}

// Reference airtime from Semtech AN1200.13, in floating point, written independently
// of the integer implementation under test.
static double reference_toa_ms(int sf, double bw_hz, int cr, int preamble, int payload_len) {
  double tsym_ms = (double)(1 << sf) / bw_hz * 1000.0;
  int de = tsym_ms >= 16.0 ? 1 : 0;
  double num = 8.0 * payload_len - 4.0 * sf + 28 + 16 /* CRC */ - 0 /* explicit header */;
  double n = ceil(num / (4.0 * (sf - 2 * de)));
  if (n < 0) n = 0;
  double n_payload = 8 + n * cr;
  return (preamble + 4.25 + n_payload) * tsym_ms;
}

static void test_airtime(SelfTestResult& r, SF32SX1262Radio& radio) {
  struct Case { float bw; int sf; int cr; int len; };
  const Case cases[] = {
      {62.5f, 7, 5, 10}, {62.5f, 7, 5, 100}, {62.5f, 7, 5, 255},  // US preset
      {250.0f, 10, 5, 50}, {125.0f, 12, 8, 50}, {62.5f, 12, 5, 255},  // incl. LDRO cases
  };
  bool ok = true;
  for (const Case& c : cases) {
    radio.setParams(TEST_FREQ, c.bw, c.sf, c.cr);
    int preamble = c.sf <= 8 ? 32 : 16;
    double ref_ms = reference_toa_ms(c.sf, c.bw * 1000.0, c.cr, preamble, c.len);
    double got_ms = radio.getTimeOnAirMicros(c.len) / 1000.0;
    if (fabs(got_ms - ref_ms) > 1.0) {
      ok = false;
      LOG_E("airtime SF%d BW%d CR%d len%d: got %d us, expected %d us", c.sf, (int)c.bw, c.cr, c.len,
            (int)(got_ms * 1000), (int)(ref_ms * 1000));
    }
  }
  selftest_check(r, ok, "time-on-air matches Semtech formula (6 configs)");
  radio.setParams(TEST_FREQ, TEST_BW, TEST_SF, TEST_CR);
}

static void test_rng(SelfTestResult& r, SF32SX1262Radio& radio) {
  SF32HardwareRNG trng;
  selftest_check(r, trng.begin(), "TRNG init");
  uint8_t a[32], b[32];
  trng.random(a, sizeof(a));
  trng.random(b, sizeof(b));
  selftest_check(r, !all_same(a, sizeof(a)) && memcmp(a, b, sizeof(a)) != 0, "TRNG output varies");

  uint8_t c[16], d[16];
  radio.randomBytes(c, sizeof(c));
  radio.randomBytes(d, sizeof(d));
  selftest_check(r, !all_same(c, sizeof(c)) && memcmp(c, d, sizeof(c)) != 0, "SX1262 RNG output varies");
}

static void test_rx_idle(SelfTestResult& r, SF32SX1262Radio& radio) {
  uint8_t buf[256];
  radio.recvRaw(buf, sizeof(buf));  // enters RX
  selftest_check(r, radio.isInRecvMode(), "enters receive mode");

  float rssi = radio.getCurrentRSSI();
  selftest_check(r, rssi > -140 && rssi < -20, "instant RSSI in plausible range");

  // Diagnostic: spread of instant RSSI over ~1 s.
  int lo = 0, hi = -200, sum = 0;
  for (int i = 0; i < 50; i++) {
    int v = (int)radio.getCurrentRSSI();
    lo = v < lo ? v : lo;
    hi = v > hi ? v : hi;
    sum += v;
    delay(20);
  }
  LOG_I("instant RSSI over 1 s: min %d, avg %d, max %d dBm", lo, sum / 50, hi);

  // Same measurement with boosted RX gain, to compare.
  selftest_check(r, radio.setRxBoostedGainMode(true) && radio.getRxBoostedGainMode(), "boosted RX gain on");
  delay(50);
  lo = 0; hi = -200; sum = 0;
  for (int i = 0; i < 50; i++) {
    int v = (int)radio.getCurrentRSSI();
    lo = v < lo ? v : lo;
    hi = v > hi ? v : hi;
    sum += v;
    delay(20);
  }
  LOG_I("instant RSSI (boosted gain) over 1 s: min %d, avg %d, max %d dBm", lo, sum / 50, hi);
  radio.setRxBoostedGainMode(false);

  // Noise floor needs 64 quiet samples from loop().
  unsigned long t0 = millis();
  while (radio.getNoiseFloor() == 0 && millis() - t0 < 5000) {
    radio.recvRaw(buf, sizeof(buf));
    radio.loop();
    delay(10);
  }
  int nf = radio.getNoiseFloor();
  LOG_I("instant RSSI %d dBm, noise floor %d dBm", (int)rssi, nf);
  selftest_check(r, nf <= -60 && nf >= -125, "noise floor calibrates");
}

static void test_tx(SelfTestResult& r, SF32SX1262Radio& radio) {
  mesh::Packet pkt;
  pkt.header = (PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
  pkt.path_len = 0;
  const char msg[] = "T-Display-SF32 radio test";
  memcpy(pkt.payload, msg, sizeof(msg));
  pkt.payload_len = sizeof(msg);
  uint8_t raw[MAX_TRANS_UNIT];
  int len = pkt.writeTo(raw);

  uint32_t expected_ms = radio.getEstAirtimeFor(len);
  unsigned long t0 = millis();
  bool started = radio.startSendRaw(raw, len);
  selftest_check(r, started, "startSendRaw");
  bool done = false;
  while (started && millis() - t0 < expected_ms * 2 + 500) {
    if (radio.isSendComplete()) {
      done = true;
      break;
    }
    rt_thread_mdelay(1);
  }
  unsigned long took = millis() - t0;
  radio.onSendFinished();
  selftest_check(r, done, "TX-done interrupt (DIO1) fires");
  LOG_I("TX %d bytes: %lu ms on air, predicted %lu ms", len, took, (unsigned long)expected_ms);
  selftest_check(r, done && took + 5 >= expected_ms && took <= expected_ms + 15,
                 "measured airtime matches prediction");
  selftest_check(r, radio.getPacketsSent() == 1, "sent counter");

  uint8_t buf[256];
  radio.recvRaw(buf, sizeof(buf));
  selftest_check(r, radio.isInRecvMode(), "returns to receive after TX");
}

static void test_agc_and_cad(SelfTestResult& r, SF32SX1262Radio& radio) {
  uint8_t buf[256];
  unsigned long t0 = millis();
  radio.resetAGC();  // warm sleep + wake (exercises the BUSY workaround) + recalibrate
  unsigned long took = millis() - t0;
  radio.recvRaw(buf, sizeof(buf));
  float rssi = radio.getCurrentRSSI();
  LOG_I("AGC reset took %lu ms; RSSI after %d dBm", took, (int)rssi);
  selftest_check(r, took < 200, "AGC reset completes quickly (radio woke from sleep)");
  selftest_check(r, radio.isInRecvMode() && rssi > -140 && rssi < -20, "receiving after AGC reset");

  radio.setCADEnabled(true);
  t0 = millis();
  bool busy = radio.isChannelActive();
  took = millis() - t0;
  radio.setCADEnabled(false);
  LOG_I("CAD: channel %s (%lu ms)", busy ? "busy" : "free", took);
  selftest_check(r, took < 100, "CAD completes");
  selftest_check(r, radio.isInRecvMode(), "receiving after CAD");
}

void selftest_radio(SelfTestResult& r, SF32SX1262Radio& radio) {
  int16_t err = radio.init(TEST_FREQ, TEST_BW, TEST_SF, TEST_CR, TEST_POWER);
  selftest_check(r, err == RADIOLIB_ERR_NONE, "radio init");
  if (err != RADIOLIB_ERR_NONE) {
    LOG_E("radio init error %d; skipping radio tests", err);
    return;
  }
  radio.begin();
  test_airtime(r, radio);
  test_rng(r, radio);
  test_rx_idle(r, radio);
  test_tx(r, radio);
  test_agc_and_cad(r, radio);
}

static const char* route_name(uint8_t rt) {
  static const char* names[] = {"T_FLOOD", "FLOOD", "DIRECT", "T_DIRECT"};
  return names[rt & 3];
}

static const char* type_name(uint8_t t) {
  static const char* names[] = {"REQ", "RESPONSE", "TXT_MSG", "ACK", "ADVERT", "GRP_TXT", "GRP_DATA", "ANON_REQ",
                                "PATH", "TRACE", "MULTIPART", "CONTROL", "?12", "?13", "?14", "RAW_CUSTOM"};
  return names[t & 15];
}

void radio_listen_step(SF32SX1262Radio& radio, uint32_t& rx_count) {
  uint8_t buf[256];
  int len = radio.recvRaw(buf, sizeof(buf));
  radio.loop();
  if (len <= 0) return;

  rx_count++;
  mesh::Packet pkt;
  // ulog has no %f: print SNR in quarter-dB units
  if (pkt.readFrom(buf, len)) {
    LOG_I("RX #%lu: %d bytes, %s %s, %d hops, RSSI %d, SNRx4 %d", (unsigned long)rx_count, len,
          route_name(pkt.getRouteType()), type_name(pkt.getPayloadType()), pkt.getPathHashCount(),
          (int)radio.getLastRSSI(), (int)(radio.getLastSNR() * 4));
  } else {
    LOG_W("RX #%lu: %d bytes (not a valid MeshCore packet), RSSI %d", (unsigned long)rx_count, len,
          (int)radio.getLastRSSI());
  }
}
