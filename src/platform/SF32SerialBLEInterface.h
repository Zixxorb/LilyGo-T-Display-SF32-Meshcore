// MeshCore companion transport over BLE on the SF32 (SiFli "sibles" stack).
//
// Same behaviour as MeshCore's ESP32 SerialBLEInterface: Nordic UART service,
// one companion frame per write/notification, LE Secure Connections pairing with
// MITM (passkey = the companion's BLE PIN) and bonding, encrypted characteristics.
#pragma once

#include <helpers/BaseSerialInterface.h>
#include <rtthread.h>

class SF32SerialBLEInterface : public BaseSerialInterface {
public:
  SF32SerialBLEInterface();

  /**
   * @param prefix    device name prefix (MeshCore's BLE_NAME_PREFIX)
   * @param name      IN/OUT node name; "@@MAC" is replaced by the BLE address
   * @param pin_code  6-digit passkey the phone must enter
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  bool forgetBonds();  // drop every paired phone (they must pair again with the PIN); false: Bluetooth not up
  // Low power mode: advertise every 2 s instead of 500 ms while no phone is connected (the
  // first 30 s after a disconnect stay fast). A connected phone isn't affected.
  void setSlowAdvertising(bool slow);
  // Low power mode: once a connected phone has sent nothing for 30 s (and not in the first 45 s
  // of a connection), ask it for a slow connection rhythm (320-400 ms); fast again as soon as
  // it sends something (the board's own notifications just wait for a slow slot). Messages from the phone then wait up to ~0.4 s.
  void setSlowLink(bool on) { _slow_link = on; }
  uint16_t connIntervalUnits() const { return _conn_intv; }  // last granted (1.25 ms units), 0 unknown

  // A new phone can pair only while pairing is open: Bluetooth page > Pair phone (2 min), or
  // the first 5 min after start while no phone is paired. One failed pairing closes it, so
  // the PIN can't be worked out by trying again and again. Paired phones reconnect anytime.
  void openPairing(unsigned long ms) { _pair_until = (millis() + ms) | 1; }
  bool pairingOpen() const { return _pair_until && (long)(_pair_until - millis()) > 0; }
  unsigned long pairingLeftMs() const { return pairingOpen() ? _pair_until - millis() : 0; }

  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _enabled; }
  // Like MeshCore's nRF52 interface: "connected" only once the link is encrypted with a
  // PIN-paired bond, so the UI keeps showing the PIN until then.
  bool isConnected() const override { return _connected; }
  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  // Called from the BLE stack thread (see .cpp).
  void onStackEvent(uint16_t event_id, uint8_t* data, uint16_t len);
  uint8_t onAttributeWrite(uint8_t conn_idx, uint8_t idx, const uint8_t* value, uint16_t len);

private:
  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };
  static const int QUEUE_SIZE = 4;

  void setupOnPowerOn();  // main-thread part of BLE power-on
  void startAdvertising();

  char _dev_name[48];
  uint32_t _pin_code = 0;
  bool _enabled = false;

  // shared with the BLE thread
  volatile bool _power_on_pending = false, _ready = false;
  volatile bool _link_up = false;    // raw BLE connection
  volatile bool _connected = false;  // link encrypted with a PIN-paired bond
  volatile bool _notify_on = false;
  volatile bool _security_pending = false;  // send a security request (main thread)
  volatile rt_tick_t _security_due = 0;     // ...from this tick on
  volatile uint8_t _conn_idx = 0;
  volatile unsigned long _pair_until = 0;  // pairing open until (millis; 0 closed)
  volatile uint16_t _mtu = 23;

  void* _srv_handle = nullptr;  // sibles_hdl
  struct rt_messagequeue _rx_mq;
  uint8_t _rx_pool[QUEUE_SIZE * (sizeof(Frame) + 8)];

  Frame _send_queue[QUEUE_SIZE];
  int _send_len = 0;
  unsigned long _last_write = 0;
  void linkPowerLoop();
  bool _slow_link = false;
  uint8_t _link_req = 0;                     // 0 the phone's choice, 1 fast asked, 2 slow asked
  unsigned long _link_req_at = 0;
  volatile unsigned long _connected_at = 0;  // when the link became encrypted (millis)
  volatile unsigned long _last_activity = 0; // last frame from the phone (millis)
  volatile uint16_t _conn_intv = 0;
};
