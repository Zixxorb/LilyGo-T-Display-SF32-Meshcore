#pragma once
// The MeshCore app protocol over the USB serial port (the board's CH343, the same port as
// the console and flashing), next to Bluetooth: a PC app (meshcore-cli, the Python library,
// an observer / MQTT bridge) and the phone can be connected at the same time (MeshCore's
// MultiSerialInterface sends every frame to both). Companion only; Settings > USB app.
//
// Unlike MeshCore's ArduinoSerialInterface it only counts as connected once an app has sent
// it a frame (until USB power goes away): until then nothing is written, so the port is
// just the console. While connected the console logs are paused, so they can't land in the
// middle of the app's frames.
#include <helpers/BaseSerialInterface.h>

class SF32UsbInterface : public BaseSerialInterface {
  bool _enabled = false, _connected = false;
  uint8_t _state = 0;
  uint16_t _frame_len = 0, _rx_len = 0;
  uint8_t _rx_buf[MAX_FRAME_SIZE];
  void setConnected(bool c);

public:
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _enabled; }
  bool isConnected() const override { return _enabled && _connected; }
  void loop() override;
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

extern SF32UsbInterface usb_app_interface;
