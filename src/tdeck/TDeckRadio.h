// The T-Deck's SX1262: MeshCore's CustomSX1262Wrapper (RadioLib) plus what the companion UI
// uses from the SF32's radio (SF32SX1262Radio.h): the heard-repeats tap, noise sampling, the
// band scan and the LoRa network finder, and the low power RX gain.
#pragma once
#include <helpers/radiolib/CustomSX1262Wrapper.h>

class TDeckRadio : public CustomSX1262Wrapper {
public:
  TDeckRadio(CustomSX1262& radio, mesh::MainBoard& board) : CustomSX1262Wrapper(radio, board), _sx(&radio) {}

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    _freq = freq; _bw = bw; _sf = sf; _cr = cr;
    CustomSX1262Wrapper::setParams(freq, bw, sf, cr);
  }
  float getBandwidth() const { return _bw; }

  // every packet sent (tx) or received, as raw bytes (the companion's heard-repeats tracker)
  void (*raw_tap)(bool tx, const uint8_t* bytes, int len) = nullptr;
  int recvRaw(uint8_t* bytes, int sz) override {
    int n = CustomSX1262Wrapper::recvRaw(bytes, sz);
    if (n > 0 && raw_tap) raw_tap(false, bytes, n);
    return n;
  }
  bool startSendRaw(const uint8_t* bytes, int len) override {
    if (raw_tap) raw_tap(true, bytes, len);
    return CustomSX1262Wrapper::startSendRaw(bytes, len);
  }

  // Low power mode: power-saving RX gain whatever the app chose (its choice comes back after)
  bool setRxBoostedGainMode(bool en) override {
    _boost = en;
    return CustomSX1262Wrapper::setRxBoostedGainMode(en && !_lp_gain);
  }
  bool getRxBoostedGainMode() const override { return _boost; }
  void setLowPowerGain(bool on) {
    if (on == _lp_gain) return;
    _lp_gain = on;
    setRxBoostedGainMode(_boost);
  }

  // ---- RF diagnostics; leave the radio ready to resume RX ----
  int sampleNoise(int samples, int* lo = nullptr, int* hi = nullptr);  // avg instant RSSI in RX
  uint32_t frequencyHz() const { return _probing ? _probe_hz : (uint32_t)(_freq * 1000000.0f + 0.5f); }
  void setFrequencyHz(uint32_t hz);
  // LoRa network finder: CAD on other LoRa settings. 1 if LoRa with this spreading factor and
  // bandwidth is on the air there (two CADs in a row), 0 if not, < 0 on an error; rssi gets the
  // level then. restoreParams() puts MeshCore's settings back and listens again.
  int cadProbe(uint32_t hz, float bw_khz, uint8_t sf, bool invert_iq, int* rssi);
  void restoreParams();

private:
  CustomSX1262* _sx;
  float _freq = 0, _bw = 0;
  uint8_t _sf = 0, _cr = 0;
  bool _boost = false, _lp_gain = false;
  bool _probing = false;
  uint32_t _probe_hz = 0;
};
