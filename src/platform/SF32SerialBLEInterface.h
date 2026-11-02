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

  void forgetBonds();  // drop every paired phone (they must pair again with the PIN)

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
  volatile uint16_t _mtu = 23;

  void* _srv_handle = nullptr;  // sibles_hdl
  struct rt_messagequeue _rx_mq;
  uint8_t _rx_pool[QUEUE_SIZE * (sizeof(Frame) + 8)];

  Frame _send_queue[QUEUE_SIZE];
  int _send_len = 0;
  unsigned long _last_write = 0;
};
