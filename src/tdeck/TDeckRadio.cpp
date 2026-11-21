#include "TDeckRadio.h"

int TDeckRadio::sampleNoise(int samples, int* lo, int* hi) {
  if (!_probing) startRecv();
  else _sx->startReceive();
  delay(5);
  int mn = 0, mx = -200, sum = 0;
  for (int i = 0; i < samples; i++) {
    int v = (int)_sx->getRSSI(false);
    mn = v < mn ? v : mn;
    mx = v > mx ? v : mx;
    sum += v;
    delay(2);
  }
  if (lo) *lo = mn;
  if (hi) *hi = mx;
  return samples ? sum / samples : 0;
}

// The band scan: off our channel, and back (our own frequency: then sampleNoise() listens
// for packets again, as before)
void TDeckRadio::setFrequencyHz(uint32_t hz) {
  idle();
  uint32_t home = (uint32_t)(_freq * 1000000.0f + 0.5f);
  _probing = hz + 1000 < home || hz > home + 1000;
  _probe_hz = hz;
  _sx->setFrequency(hz / 1000000.0f);
}

int TDeckRadio::cadProbe(uint32_t hz, float bw_khz, uint8_t sf, bool invert_iq, int* rssi) {
  if (sf < 5 || sf > 12) return -1;
  idle();
  _probing = true;
  _probe_hz = hz;
  if (_sx->setFrequency(hz / 1000000.0f) != RADIOLIB_ERR_NONE ||
      _sx->setBandwidth(bw_khz) != RADIOLIB_ERR_NONE ||
      _sx->setSpreadingFactor(sf) != RADIOLIB_ERR_NONE) return -1;
  _sx->invertIQ(invert_iq);
  int16_t r = _sx->scanChannel();
  if (r == RADIOLIB_LORA_DETECTED) r = _sx->scanChannel();  // again: still there
  if (r == RADIOLIB_LORA_DETECTED) {
    if (rssi) {
      _sx->startReceive();
      delay(3);
      *rssi = (int)_sx->getRSSI(false);
      _sx->standby();
    }
    return 1;
  }
  return r == RADIOLIB_CHANNEL_FREE ? 0 : -1;
}

void TDeckRadio::restoreParams() {
  if (!_probing) return;
  _probing = false;
  idle();
  _sx->invertIQ(false);
  setParams(_freq, _bw, _sf, _cr);
  startRecv();
}
