#include "SF32UsbInterface.h"
#include "SF32Board.h"
#include <rtthread.h>
extern "C" void log_pause(rt_bool_t pause);  // RT-Thread kservice.c: pause rt_kprintf + ulog

extern SF32Board board;
SF32UsbInterface usb_app_interface;

// frame format as MeshCore's ArduinoSerialInterface: to the board '<' len16 data,
// from the board '>' len16 data (little-endian length)
enum { RX_IDLE, RX_HDR, RX_LEN1, RX_DATA };

void SF32UsbInterface::setConnected(bool c) {
  if (c == _connected) return;
  if (c) {
    log_pause(RT_TRUE);
  } else {
    log_pause(RT_FALSE);
    rt_kprintf("usb app: disconnected\n");
  }
  _connected = c;
}

void SF32UsbInterface::enable() {
  _enabled = true;
  _state = RX_IDLE;
}

void SF32UsbInterface::disable() {
  _enabled = false;
  setConnected(false);
}

void SF32UsbInterface::loop() {
  // the cable is out: no app any more (the next frame connects again)
  if (_connected && !board.isExternalPowered()) setConnected(false);
}

size_t SF32UsbInterface::writeFrame(const uint8_t src[], size_t len) {
  if (!_enabled || len > MAX_FRAME_SIZE) return 0;
  if (!_connected) return len;  // nobody listening: dropped (the console stays clean)
  uint8_t hdr[3] = {'>', (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
  Serial.write(hdr, 3);
  return Serial.write(src, len);
}

size_t SF32UsbInterface::checkRecvFrame(uint8_t dest[]) {
  if (!_enabled) return 0;
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    switch (_state) {
      case RX_IDLE:
        if (c == '<') _state = RX_HDR;
        break;
      case RX_HDR:
        _frame_len = (uint8_t)c;
        _state = RX_LEN1;
        break;
      case RX_LEN1:
        _frame_len |= (uint16_t)c << 8;
        _rx_len = 0;
        // a sane length only (a stray '<' on the console must not swallow what follows)
        _state = (_frame_len > 0 && _frame_len <= MAX_FRAME_SIZE) ? RX_DATA : RX_IDLE;
        break;
      default:
        _rx_buf[_rx_len++] = (uint8_t)c;
        if (_rx_len >= _frame_len) {
          memcpy(dest, _rx_buf, _frame_len);
          _state = RX_IDLE;
          setConnected(true);  // an app is talking to us
          return _frame_len;
        }
    }
  }
  return 0;
}
