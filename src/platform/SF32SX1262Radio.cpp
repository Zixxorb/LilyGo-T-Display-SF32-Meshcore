#include "SF32SX1262Radio.h"
#include "SF32Board.h"
#include "SX126x_commands.h"

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <Arduino.h>

#define LOG_TAG "radio"
#include <ulog.h>

// Wrapper state machine, same values as RadioLibWrappers.cpp
#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD       14

#define SYNC_WORD_PRIVATE  0x12  // RADIOLIB_SX126X_SYNC_WORD_PRIVATE, used by all MeshCore nodes

// SX126x registers not defined by the C driver
#define REG_RX_GAIN                0x08AC
#define RX_GAIN_BOOSTED            0x96
#define RX_GAIN_POWER_SAVING       0x94
#define REG_RX_GAIN_RETENTION_0    0x029F  // retain RX gain setting across warm sleep
#define REG_RANDOM_NUMBER_0        0x0819

static const float kBandwidthsKhz[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f};
static const uint32_t kBandwidthsHz[] = {7810, 10420, 15630, 20830, 31250, 41670, 62500, 125000, 250000, 500000};

static bool bandwidth_from_khz(float khz, SX126xBandwidth_t* out) {
  for (unsigned i = 0; i < sizeof(kBandwidthsKhz) / sizeof(kBandwidthsKhz[0]); i++) {
    float d = khz - kBandwidthsKhz[i];
    if (d > -0.05f && d < 0.05f) {
      *out = (SX126xBandwidth_t)i;
      return true;
    }
  }
  return false;
}

static uint16_t preamble_length_for_sf(uint8_t sf) { return sf <= 8 ? 32 : 16; }

// ---- setup ----

int16_t SF32SX1262Radio::init(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr, int8_t tx_power_dbm) {
  ModuleConfig_t module_config;
  Module_configDefault(&module_config);  // pins/SPI from Kconfig (PA26/29/30/31/32, spi1)
  module_config.board_init = RadioLib_boardInitTDisplaySF32;
  int16_t state = Module_init(&_module, &module_config);
  if (state != RADIOLIB_ERR_NONE) return _last_error = state;

  SX126xBandwidth_t bw;
  if (!bandwidth_from_khz(bw_khz, &bw)) return _last_error = RADIOLIB_ERR_INVALID_BANDWIDTH;

  SX1262Config_t config;
  SX1262_configDefault(&config);  // TCXO 3.0 V, DC-DC, DIO2 RF switch (from Kconfig)
  config.frequency_hz = (uint32_t)(freq_mhz * 1000000.0 + 0.5);
  config.bandwidth = bw;
  config.spreading_factor = sf;
  config.coding_rate = cr;
  config.sync_word = SYNC_WORD_PRIVATE;
  config.output_power_dbm = tx_power_dbm;
  config.preamble_length = preamble_length_for_sf(sf);
  config.crc_enabled = true;
  state = SX1262_beginConfig(&_radio, &_module, &config);
  if (state != RADIOLIB_ERR_NONE) {
    Module_deinit(&_module);
    return _last_error = state;
  }

  _bw_khz = bw_khz;
  _sf = sf;
  _cr = cr;
  updateTiming();

  state = SX126x_setDio1Action(&_radio, onDio1, this);
  if (state != RADIOLIB_ERR_NONE) return _last_error = state;

  // ulog has no %f support
  LOG_I("SX1262 up: %lu kHz, BW %d Hz, SF%d, CR4/%d, %d dBm", (unsigned long)(config.frequency_hz / 1000),
        (int)kBandwidthsHz[bw], sf, cr, tx_power_dbm);
  return RADIOLIB_ERR_NONE;
}

void SF32SX1262Radio::onDio1(void* ctx) {
  // Interrupt context: just flag it. All SPI work happens in thread context.
  static_cast<SF32SX1262Radio*>(ctx)->_state |= STATE_INT_READY;
  sf32_pm_event(SF32_PM_EV_RADIO_IRQ);
}

void SF32SX1262Radio::begin() {
  _state = STATE_IDLE;
  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) _state |= STATE_INT_READY;
  _noise_floor = 0;
  _threshold = 0;
  _cad_enabled = false;
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

void SF32SX1262Radio::setParams(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr) {
  SX126xBandwidth_t bw;
  if (!bandwidth_from_khz(bw_khz, &bw)) {
    LOG_E("unsupported bandwidth %d Hz", (int)(bw_khz * 1000));
    return;
  }
  idle();
  SX126x_setFrequency(&_radio, (uint32_t)(freq_mhz * 1000000.0 + 0.5));
  SX126x_setSpreadingFactor(&_radio, sf);
  SX126x_setBandwidth(&_radio, bw);
  SX126x_setCodingRate(&_radio, cr);
  SX126x_setPreambleLength(&_radio, preamble_length_for_sf(sf));
  _bw_khz = bw_khz;
  _sf = sf;
  _cr = cr;
  updateTiming();
}

// Recomputes timing that depends on the modulation settings.
void SF32SX1262Radio::updateTiming() {
  // Same as RadioLibWrapper::calcMaxPacketMillis(): how long a preamble or a
  // header may stay "in progress" before we decide it was noise.
  uint32_t tsym_us = (uint32_t)(((uint64_t)1000000 << _sf) / kBandwidthsHz[_radio.config.bandwidth]);
  uint32_t sf_coeff1_x4 = (_sf == 5 || _sf == 6) ? 25 : 17;
  uint32_t preamble_us = (((_radio.config.preamble_length + 8) * 4 + sf_coeff1_x4) * tsym_us) / 4;
  uint32_t total_us = getTimeOnAirMicros(MAX_TRANS_UNIT);
  uint32_t payload_us = total_us > preamble_us ? total_us - preamble_us : 4000 - preamble_us;
  if (_cr >= 5 && _cr < 8) payload_us = (payload_us * 8) / _cr;
  _preamble_millis = (preamble_us + 999) / 1000;
  _max_payload_millis = (payload_us + 999) / 1000;

  // The driver's TX watchdog defaults to 5 s, too short for slow settings (SF12/BW62.5).
  _radio.config.tx_timeout_ms = total_us / 1000 * 2 + 1000;
}

// LoRa time on air, per the SX126x datasheet (6.1.4). Same arithmetic as
// RadioLib's calculateTimeOnAir(), using the LDRO rule the C driver applies.
uint32_t SF32SX1262Radio::getTimeOnAirMicros(int len_bytes) const {
  uint32_t bw_hz = kBandwidthsHz[_radio.config.bandwidth];
  uint32_t tsym_us = (uint32_t)(((uint64_t)1000000 << _sf) / bw_hz);
  bool ldro = tsym_us >= 16000;
  int sf_coeff1_x4 = (_sf == 5 || _sf == 6) ? 25 : 17;
  int sf_coeff2 = (_sf == 5 || _sf == 6) ? 0 : 8;
  int sf_divisor = 4 * (ldro ? _sf - 2 : _sf);
  int header_bits = _radio.implicit_header ? 0 : 20;
  int bits = 8 * len_bytes + (_radio.config.crc_enabled ? 16 : 0) - 4 * _sf + sf_coeff2 + header_bits;
  if (bits < 0) bits = 0;
  int coded_blocks = (bits + sf_divisor - 1) / sf_divisor;
  uint32_t symbols_x4 = (_radio.config.preamble_length + 8) * 4 + sf_coeff1_x4 + coded_blocks * _cr * 4;
  return (uint32_t)(((uint64_t)tsym_us * symbols_x4) / 4);
}

void SF32SX1262Radio::setTxPower(int8_t dbm) {
  int16_t err = SX126x_setOutputPower(&_radio, dbm);
  if (err != RADIOLIB_ERR_NONE) LOG_E("setOutputPower(%d) failed: %d", dbm, err);
}

bool SF32SX1262Radio::setRxBoostedGainMode(bool enable) {
  uint8_t gain = enable ? RX_GAIN_BOOSTED : RX_GAIN_POWER_SAVING;
  if (Module_writeRegister(&_module, REG_RX_GAIN, &gain, 1) != RADIOLIB_ERR_NONE) return false;
  // Keep the gain setting through warm sleep (as RadioLib does).
  const uint8_t retention[] = {0x01, 0x08, 0xAC};
  Module_writeRegister(&_module, REG_RX_GAIN_RETENTION_0, retention, sizeof(retention));
  _rx_boost = enable;
  return true;
}

void SF32SX1262Radio::powerOff() {
  SX126x_sleep(&_radio, false);
}

// ---- raw SX126x helpers ----

uint16_t SF32SX1262Radio::readIrqStatus() {
  uint8_t d[2] = {0, 0};
  Module_readCommand(&_module, SX126X_CMD_GET_IRQ_STATUS, RT_NULL, d, sizeof(d));
  return (uint16_t)((d[0] << 8) | d[1]);
}

void SF32SX1262Radio::clearIrqStatus(uint16_t mask) {
  uint8_t d[2] = {(uint8_t)(mask >> 8), (uint8_t)mask};
  Module_writeCommand(&_module, SX126X_CMD_CLEAR_IRQ_STATUS, d, sizeof(d), true);
}

void SF32SX1262Radio::readPacketStatus() {
  // The C driver keeps SNR in whole dB; MeshCore wants 0.25 dB resolution.
  uint8_t d[3];
  if (Module_readCommand(&_module, SX126X_CMD_GET_PACKET_STATUS, RT_NULL, d, sizeof(d)) == RADIOLIB_ERR_NONE) {
    _last_rssi = -(float)d[0] / 2.0f;
    _last_snr = (float)(int8_t)d[1] / 4.0f;
  }
}

// Continuous RX that also latches PREAMBLE_DETECTED and HEADER_VALID (not routed
// to DIO1), so isReceivingPacket() can see a packet in flight. The C driver's
// SX126x_startReceive() doesn't enable those two flags.
int16_t SF32SX1262Radio::startReceive() {
  int16_t state = SX126x_standby(&_radio);
  if (state != RADIOLIB_ERR_NONE) return state;
  state = SX126x_explicitHeader(&_radio);  // restores packet params with max payload length
  if (state != RADIOLIB_ERR_NONE) return state;
  clearIrqStatus(SX126X_IRQ_ALL);

  const uint16_t dio1 = SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT | SX126X_IRQ_CRC_ERROR | SX126X_IRQ_HEADER_ERROR;
  const uint16_t irq = dio1 | SX126X_IRQ_PREAMBLE_DETECTED | SX126X_IRQ_HEADER_VALID;
  uint8_t params[8] = {(uint8_t)(irq >> 8), (uint8_t)irq, (uint8_t)(dio1 >> 8), (uint8_t)dio1, 0, 0, 0, 0};
  state = Module_writeCommand(&_module, SX126X_CMD_SET_DIO_IRQ_PARAMS, params, sizeof(params), true);
  if (state != RADIOLIB_ERR_NONE) return state;

  const uint8_t rx_continuous[3] = {0xFF, 0xFF, 0xFF};
  return Module_writeCommand(&_module, SX126X_CMD_SET_RX, rx_continuous, sizeof(rx_continuous), true);
}

// Wakes the SX1262 from sleep. A sleeping SX1262 holds BUSY high until NSS goes
// low, but the C driver waits for BUSY low *before* asserting NSS, so its own
// commands can never wake the chip. Pulse NSS ourselves.
int16_t SF32SX1262Radio::wakeFromSleep() {
  rt_mutex_take(_module.lock, RT_WAITING_FOREVER);
  rt_spi_take_bus(_module.spi);
  rt_pin_write(_module.config.cs, PIN_LOW);
  rt_hw_us_delay(100);
  rt_pin_write(_module.config.cs, PIN_HIGH);
  rt_spi_release_bus(_module.spi);
  rt_mutex_release(_module.lock);
  return Module_waitForBusy(&_module);
}

// ---- RadioLibWrapper logic ----

// Battery test builds (SF32_RADIO_AB): the radio sleeps (warm, settings kept) for every
// other 2 h on battery; checked from recvRaw(), which the mesh calls every loop. Nothing
// touches the SPI bus meanwhile (that would wake the chip).
#ifdef SF32_RADIO_AB
extern "C" bool sf32_radio_ab_off(void);
bool SF32SX1262Radio::abAsleep() {
  bool off = sf32_radio_ab_off();
  if (off && !_ab_asleep && _state != STATE_TX_WAIT) {  // (a send finishes first)
    SX126x_sleep(&_radio, true);
    _state = STATE_IDLE;
    _ab_asleep = true;
    LOG_I("AB test: radio asleep");
  } else if (!off && _ab_asleep) {
    _ab_asleep = false;
    int16_t err = wakeFromSleep();
    if (err != RADIOLIB_ERR_NONE) LOG_E("AB test: wake failed: %d", err);
    doResetAGC();
    _state = STATE_IDLE;
    _noise_floor = 0;
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
    LOG_I("AB test: radio listening");
  }
  return _ab_asleep;
}
#else
bool SF32SX1262Radio::abAsleep() { return false; }
#endif


void SF32SX1262Radio::idle() {
  SX126x_standby(&_radio);
  _state = STATE_IDLE;
}

void SF32SX1262Radio::startRecv() {
  int16_t err = startReceive();
  if (err == RADIOLIB_ERR_NONE) {
    _state = STATE_RX;
  } else {
    LOG_E("startReceive failed: %d", err);
  }
}

bool SF32SX1262Radio::isInRecvMode() const {
  return _ab_asleep || (_state & ~STATE_INT_READY) == STATE_RX;  // (asleep for a test: as planned)
}

int SF32SX1262Radio::recvRaw(uint8_t* bytes, int sz) {
  if (abAsleep()) return 0;
  int len = 0;
  if (_state & STATE_INT_READY) {
    size_t n = (size_t)sz;
    int16_t err = SX126x_readData(&_radio, bytes, &n);
    if (err == RADIOLIB_ERR_NONE) {
      len = (int)n;
      if (len > 0) {
        readPacketStatus();
        n_recv++;
      }
    } else {
      // CRC mismatch, damaged header, oversize: counted like RadioLibWrapper does.
      n_recv_errors++;
    }
    _state = STATE_IDLE;  // need another startReceive()
  }

  if (_state != STATE_RX) startRecv();
  return len;
}

uint32_t SF32SX1262Radio::getEstAirtimeFor(int len_bytes) {
  return getTimeOnAirMicros(len_bytes) / 1000;
}

bool SF32SX1262Radio::startSendRaw(const uint8_t* bytes, int len) {
  if (_ab_asleep) return false;
  _board->onBeforeTransmit();
  int16_t err = SX126x_startTransmit(&_radio, bytes, (size_t)len);
  if (err == RADIOLIB_ERR_NONE) {
    _state = STATE_TX_WAIT;
    return true;
  }
  LOG_E("startTransmit failed: %d", err);
  idle();
  _board->onAfterTransmit();
  return false;
}

bool SF32SX1262Radio::isSendComplete() {
  if (_state & STATE_INT_READY) {
    _state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void SF32SX1262Radio::onSendFinished() {
  int16_t err = SX126x_finishTransmit(&_radio);
  if (err != RADIOLIB_ERR_NONE) LOG_W("finishTransmit: %d", err);
  _board->onAfterTransmit();
  _state = STATE_IDLE;
}

// Port of CustomSX1262::isReceiving(): true while a preamble or header has been
// detected and the packet could still be arriving.
bool SF32SX1262Radio::isReceivingPacket() {
  if (_ab_asleep) return false;
  const uint16_t activity = SX126X_IRQ_PREAMBLE_DETECTED | SX126X_IRQ_HEADER_VALID | SX126X_IRQ_HEADER_ERROR;
  uint16_t irq = readIrqStatus();
  bool preamble = irq & SX126X_IRQ_PREAMBLE_DETECTED;
  bool header = irq & SX126X_IRQ_HEADER_VALID;
  bool hdr_err = irq & SX126X_IRQ_HEADER_ERROR;
  uint32_t now = millis();

  if (hdr_err) {
    clearIrqStatus(activity);
    _activity_at = 0;
    _header_seen = false;
    return false;
  }
  if (!header && _header_seen) {
    _activity_at = 0;
    _header_seen = false;
    return false;
  }
  if (header) {
    if (!_header_seen) {
      _header_seen = true;
      _activity_at = now;
    }
    if (now - _activity_at > _max_payload_millis) {
      clearIrqStatus(activity);
      _activity_at = 0;
      _header_seen = false;
      return false;
    }
    return true;
  }
  if (preamble) {
    if (_activity_at == 0) _activity_at = now;
    if (now - _activity_at > _preamble_millis) {
      clearIrqStatus(SX126X_IRQ_PREAMBLE_DETECTED);
      _activity_at = 0;
      return false;
    }
    return true;
  }
  _activity_at = 0;
  _header_seen = false;
  return false;
}

float SF32SX1262Radio::getCurrentRSSI() {
  if (_ab_asleep) return -127;
  uint8_t d = 0;
  Module_readCommand(&_module, SX126X_CMD_GET_RSSI_INST, RT_NULL, &d, 1);
  return -(float)d / 2.0f;
}

bool SF32SX1262Radio::isChannelActive() {
  if (_ab_asleep) return false;
  if (_threshold != 0 && getCurrentRSSI() > _noise_floor + _threshold) return true;

  if (_cad_enabled) {
    // SetCAD is only valid from standby, and the C driver doesn't switch to standby itself.
    SX126x_standby(&_radio);
    int16_t result = SX126x_scanChannel(&_radio);
    // CAD-done also raises DIO1; clear our flag so recvRaw() doesn't treat it as a packet.
    _state = STATE_IDLE;
    startRecv();
    if (result == RADIOLIB_LORA_DETECTED) return true;
  }
  return false;
}

void SF32SX1262Radio::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

void SF32SX1262Radio::loop() {
  if (_state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!isReceivingPacket()) {
      int rssi = (int)getCurrentRSSI();
      if (rssi < _noise_floor + SAMPLING_THRESHOLD) {
        _num_floor_samples++;
        _floor_sample_sum += rssi;
      }
    }
  } else if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && _floor_sample_sum != 0) {
    _noise_floor = _floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
    if (_noise_floor < -120) _noise_floor = -120;
    _floor_sample_sum = 0;
  }
}

// Same steps as MeshCore's sx126xResetAGC().
void SF32SX1262Radio::doResetAGC() {
  SX126x_sleep(&_radio, true);  // warm sleep resets the analog front end
  rt_thread_mdelay(1);
  int16_t err = wakeFromSleep();
  if (err != RADIOLIB_ERR_NONE) LOG_E("wake after AGC reset failed: %d", err);
  SX126x_standby(&_radio);

  uint8_t cal = 0x7F;  // calibrate all blocks
  Module_writeCommand(&_module, SX126X_CMD_CALIBRATE, &cal, 1, false);
  rt_thread_mdelay(5);
  Module_waitForBusy(&_module);
  SX126x_setFrequency(&_radio, _radio.config.frequency_hz);  // also re-runs image calibration

  uint8_t dio2 = _radio.config.use_dio2_rf_switch ? 1 : 0;
  Module_writeCommand(&_module, SX126X_CMD_SET_DIO2_RF_SWITCH, &dio2, 1, true);
  setRxBoostedGainMode(_rx_boost);
}

void SF32SX1262Radio::resetAGC() {
  if (_ab_asleep) return;
  if ((_state & STATE_INT_READY) != 0 || isReceivingPacket()) return;  // mid-packet

  doResetAGC();
  _state = STATE_IDLE;  // trigger a startReceive()
  _noise_floor = 0;
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

// Approximate SNR needed per SF for successful reception (Semtech datasheets).
static const float snr_threshold[] = {-7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f};

float SF32SX1262Radio::packetScoreInt(float snr, int sf, int packet_len) const {
  if (sf < 7) return 0.0f;
  if (snr < snr_threshold[sf - 7]) return 0.0f;
  float success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0f;
  float collision_penalty = 1.0f - (packet_len / 256.0f);
  float score = success_rate_based_on_snr * collision_penalty;
  return score < 0.0f ? 0.0f : (score > 1.0f ? 1.0f : score);
}

float SF32SX1262Radio::packetScore(float snr, int packet_len) {
  return packetScoreInt(snr, _sf, packet_len);
}

// ---- random numbers ----

// SX1262 RNG: sample the wideband noise register while in RX. Slow; for seeding only.
void SF32SX1262Radio::randomBytes(uint8_t* dest, size_t len) {
  bool was_rx = isInRecvMode();
  startReceive();
  for (size_t i = 0; i < len; i += 4) {
    rt_thread_mdelay(1);
    uint8_t r[4];
    Module_readRegister(&_module, REG_RANDOM_NUMBER_0, r, sizeof(r));
    for (size_t j = 0; j < 4 && i + j < len; j++) dest[i + j] = r[j];
  }
  idle();
  if (was_rx) startRecv();
}

uint32_t SF32SX1262Radio::getRngSeed() {
  uint32_t seed;
  randomBytes((uint8_t*)&seed, sizeof(seed));
  return seed & 0x7FFFFFFF;
}

// ---- RF diagnostics ----

int SF32SX1262Radio::sampleNoise(int samples, int* lo, int* hi) {
  if (_ab_asleep) return -127;
  startRecv();
  rt_thread_mdelay(5);
  int mn = 0, mx = -200, sum = 0;
  for (int i = 0; i < samples; i++) {
    int v = (int)getCurrentRSSI();
    mn = v < mn ? v : mn;
    mx = v > mx ? v : mx;
    sum += v;
    rt_thread_mdelay(2);
  }
  if (lo) *lo = mn;
  if (hi) *hi = mx;
  return samples ? sum / samples : 0;
}

void SF32SX1262Radio::setFrequencyHz(uint32_t hz) {
  idle();
  SX126x_setFrequency(&_radio, hz);
}

// SetRegulatorMode (only valid in STDBY_RC): 0 = LDO, 1 = DC-DC.
void SF32SX1262Radio::setRegulatorLDO(bool ldo) {
  idle();
  uint8_t mode = ldo ? 0 : 1;
  Module_writeCommand(&_module, 0x96, &mode, 1, true);
}
