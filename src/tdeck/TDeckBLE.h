// What the companion UI needs from Bluetooth beyond MeshCore's ESP32 interface (see
// compat/helpers/rp2040/SerialBLEInterface.h).
#pragma once
#include <Arduino.h>

class TDeckBLE {
public:
  bool forgetBonds();  // drop every paired phone; false: Bluetooth isn't on
  // The pairing window (Settings > Bluetooth > Pair a phone): the PIN is shown while it's open.
  // (Not enforced yet: MeshCore's ESP32 interface accepts a pairing any time.)
  void openPairing(unsigned long ms) { _pair_until = (millis() + ms) | 1; }
  bool pairingOpen() const { return _pair_until && (long)(_pair_until - millis()) > 0; }
  unsigned long pairingLeftMs() const { return pairingOpen() ? _pair_until - millis() : 0; }
  void setSlowAdvertising(bool slow) { _slow_adv = slow; }  // (power work: later)
  void setSlowLink(bool slow) { _slow_link = slow; }
private:
  unsigned long _pair_until = 0;
  bool _slow_adv = false, _slow_link = false;
};
extern TDeckBLE tdeck_ble;
bool bluetooth_connected();  // a phone is connected (MeshCore's interface)
