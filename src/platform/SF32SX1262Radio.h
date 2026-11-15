// mesh::Radio for the T-Display-SF32's SX1262, built on LilyGo's C RadioLib port.
//
// Behaviour mirrors MeshCore's RadioLibWrapper + CustomSX1262Wrapper
// (src/helpers/radiolib/), and the public API matches them so MeshCore code that
// talks to `radio_driver` works unchanged. The C driver lacks a few things MeshCore
// needs (preamble/header IRQ latching, instant RSSI, time-on-air, RNG, boosted
// gain, AGC reset); those are implemented here with raw SX126x commands.
#pragma once

#include <Mesh.h>
#include "RadioLib.h"

class SF32SX1262Radio : public mesh::Radio {
public:
  explicit SF32SX1262Radio(mesh::MainBoard& board) : _board(&board) {}

  // Powers up the module and applies LoRa settings (sync word 0x12, CRC on).
  // Returns a RADIOLIB_ERR_* code.
  int16_t init(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr, int8_t tx_power_dbm);

  // ---- mesh::Radio ----
  void begin() override;
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  float packetScore(float snr, int packet_len) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  void loop() override;
  int getNoiseFloor() const override { return _noise_floor; }
  void triggerNoiseFloorCalibrate(int threshold) override;
  void setCADEnabled(bool enable) override { _cad_enabled = enable; }
  void resetAGC() override;
  bool isInRecvMode() const override;
  bool isReceiving() override { return isReceivingPacket() || isChannelActive(); }
  float getLastRSSI() const override { return _last_rssi; }
  float getLastSNR() const override { return _last_snr; }

  // ---- RadioLibWrapper-compatible API (used by MeshCore examples/helpers) ----
  void setParams(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr);
  void setTxPower(int8_t dbm);
  uint32_t getRngSeed();
  float getCurrentRSSI();
  uint8_t getSpreadingFactor() const { return _sf; }
  void powerOff();
  bool setRxBoostedGainMode(bool enable);
  bool getRxBoostedGainMode() const { return _rx_boost; }
  // Low power mode: power-saving RX gain whatever the app chose (its choice comes back after)
  void setLowPowerGain(bool on) { if (on != _lp_gain) { _lp_gain = on; setRxBoostedGainMode(_rx_boost); } }
  bool configSideDetectors(const uint8_t[], uint8_t, float) { return false; }  // LR2021 only
  bool isChannelActive();
  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return n_recv_errors; }
  uint32_t getPacketsSent() const { return n_sent; }
  void resetStats() { n_recv = n_sent = n_recv_errors = 0; }

  // ---- extras ----
  void randomBytes(uint8_t* dest, size_t len);   // SX1262 noise RNG (slow)
  uint32_t getTimeOnAirMicros(int len_bytes) const;
  int16_t lastError() const { return _last_error; }

  // ---- RF diagnostics (CLI "noise"); leave the radio ready to resume RX ----
  int sampleNoise(int samples, int* lo = nullptr, int* hi = nullptr);  // avg instant RSSI in RX
  uint32_t frequencyHz() const { return _radio.config.frequency_hz; }
  void setFrequencyHz(uint32_t hz);
  void setRegulatorLDO(bool ldo);

private:
  static void onDio1(void* ctx);
  int16_t startReceive();
  void startRecv();
  void idle();
  bool isReceivingPacket();
  void doResetAGC();
  int16_t wakeFromSleep();
  uint16_t readIrqStatus();
  void clearIrqStatus(uint16_t mask);
  void readPacketStatus();
  void updateTiming();
  float packetScoreInt(float snr, int sf, int packet_len) const;

  Module_t _module;
  SX1262_t _radio;
  mesh::MainBoard* _board;

  volatile uint8_t _state = 0;
  uint32_t n_recv = 0, n_sent = 0, n_recv_errors = 0;
  int16_t _noise_floor = 0, _threshold = 0;
  bool _cad_enabled = false;
  uint16_t _num_floor_samples = 0;
  int32_t _floor_sample_sum = 0;

  float _bw_khz = 125;
  uint8_t _sf = 9, _cr = 5;
  bool _rx_boost = false;
  bool _lp_gain = false;
  float _last_rssi = 0, _last_snr = 0;
  int16_t _last_error = 0;

  // "Receiving now" detection (from CustomSX1262::isReceiving)
  uint32_t _preamble_millis = 66, _max_payload_millis = 3934;
  uint32_t _activity_at = 0;
  bool _header_seen = false;
};
