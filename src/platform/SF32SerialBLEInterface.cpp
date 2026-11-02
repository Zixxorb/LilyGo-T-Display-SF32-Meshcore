#include "SF32SerialBLEInterface.h"

#include <Arduino.h>
#include <FS.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "bf0_sibles_advertising.h"
#include "ble_connection_manager.h"
}

#define LOG_TAG "ble"
#include <ulog.h>

#define BLE_WRITE_MIN_INTERVAL 60  // ms between notifications, as in MeshCore's ESP32 interface
#define BOND_RESET_MARKER "/ble_bonds_v2"

// Nordic UART service, little-endian byte order as the stack expects.
// 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (service), ...02 (RX, phone writes), ...03 (TX, notify)
#define NUS_UUID(b) { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, b, 0x00, 0x40, 0x6E }
static uint8_t s_nus_svc_uuid[ATT_UUID_128_LEN] = NUS_UUID(0x01);

enum {
  NUS_SVC,
  NUS_RX_CHAR,
  NUS_RX_VALUE,
  NUS_TX_CHAR,
  NUS_TX_VALUE,
  NUS_TX_CCCD,
  NUS_ATT_NB
};

#define SERIAL_UUID_16(x) {((uint8_t)((x) & 0xff)), ((uint8_t)((x) >> 8))}

// RX/TX values require an encrypted, authenticated (secure connections) link, so the
// phone has to pair with the PIN before any companion traffic. The CCCD stays open (as
// on ESP32): Android enables notifications right after connecting, and a refused write
// there isn't retried after pairing, so no replies would ever be sent.
BLE_GATT_SERVICE_DEFINE_128(s_nus_att_db) {
  BLE_GATT_SERVICE_DECLARE(NUS_SVC, SERIAL_UUID_16_PRI_SERVICE, BLE_GATT_PERM_READ_ENABLE),
  BLE_GATT_CHAR_DECLARE(NUS_RX_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
  BLE_GATT_CHAR_VALUE_DECLARE(NUS_RX_VALUE, NUS_UUID(0x02),
                              BLE_GATT_PERM_WRITE_REQ_ENABLE | BLE_GATT_PERM_WRITE_COMMAND_ENABLE |
                                  BLE_GATT_PERM_WRITE_PERMISSION_SEC_CON,
                              BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE, MAX_FRAME_SIZE),
  BLE_GATT_CHAR_DECLARE(NUS_TX_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
  BLE_GATT_CHAR_VALUE_DECLARE(NUS_TX_VALUE, NUS_UUID(0x03),
                              BLE_GATT_PERM_NOTIFY_ENABLE | BLE_GATT_PERM_NOTIFY_PERMISSION_SEC_CON,
                              BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE, MAX_FRAME_SIZE),
  BLE_GATT_DESCRIPTOR_DECLARE(NUS_TX_CCCD, SERIAL_UUID_16_CLIENT_CHAR_CFG,
                              BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_WRITE_REQ_ENABLE,
                              BLE_GATT_VALUE_PERM_RI_ENABLE, 2),
};

extern "C" sibles_advertising_context_t* sf32_adv_context;  // SF32SerialBLEInterface_events.c
#define s_adv_context sf32_adv_context

static SF32SerialBLEInterface* s_instance = nullptr;

// ---- C callbacks into the instance ----

static uint8_t* gatt_get_cbk(uint8_t conn_idx, uint8_t idx, uint16_t* len) {
  *len = 0;  // nothing readable
  return NULL;
}

static uint8_t gatt_set_cbk(uint8_t conn_idx, sibles_set_cbk_t* para) {
  return s_instance ? s_instance->onAttributeWrite(conn_idx, para->idx, para->value, para->len) : 0;
}

// Registered with the stack in SF32SerialBLEInterface_events.c.
extern "C" int sf32_ble_on_event(uint16_t event_id, uint8_t* data, uint16_t len, uint32_t context) {
  if (s_instance) s_instance->onStackEvent(event_id, data, len);
  return 0;
}

static uint8_t adv_event(uint8_t event, void* context, void* data) { return 0; }

// Stable public address derived from the chip's UID (as in the SDK's BLE examples).
extern "C" ble_common_update_type_t ble_request_public_address(bd_addr_t* addr) {
  return bt_mac_addr_generate_via_uid_v2(addr) == 0 ? BLE_UPDATE_ONCE : BLE_UPDATE_NO_UPDATE;
}

// ---- SF32SerialBLEInterface ----


SF32SerialBLEInterface::SF32SerialBLEInterface() {
  _dev_name[0] = 0;
  rt_mq_init(&_rx_mq, "blerx", _rx_pool, sizeof(Frame), sizeof(_rx_pool), RT_IPC_FLAG_FIFO);
}

void SF32SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  s_instance = this;
  _pin_code = pin_code;
  if (strcmp(name, "@@MAC") == 0) {
    uint8_t mac[6] = {0};
    bd_addr_t addr;
    if (bt_mac_addr_generate_via_uid_v2(&addr) == 0) memcpy(mac, addr.addr, 6);
    sprintf(name, "%02X%02X%02X%02X%02X%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  }
  snprintf(_dev_name, sizeof(_dev_name), "%s%s", prefix, name);
  LOG_I("enabling BLE as '%s', PIN %06lu", _dev_name, (unsigned long)_pin_code);  // INFO: debug builds only
  sifli_ble_enable();  // BLE_POWER_ON_IND follows; setup continues in setupOnPowerOn()
}

void SF32SerialBLEInterface::setupOnPowerOn() {
  _power_on_pending = false;

  BLE_GATT_SERVICE_INIT_128(svc, s_nus_att_db, NUS_ATT_NB,
                            BLE_GATT_SERVICE_PERM_NOAUTH | BLE_GATT_SERVICE_PERM_UUID_128, s_nus_svc_uuid);
  _srv_handle = sibles_register_svc_128(&svc);
  LOG_I("power on: NUS service handle %p, name '%s'", _srv_handle, _dev_name);
  if (!_srv_handle) {
    LOG_E("NUS service registration failed");
    return;
  }
  sibles_register_cbk(_srv_handle, gatt_get_cbk, gatt_set_cbk);

  // Passkey pairing: we display (the PIN is shown on screen / fixed), the phone enters it.
  // Level 3 = display-only IO, MITM + bonding + secure connections, and it *requires*
  // authenticated pairing (no "Just Works" bonds).
  connection_manager_set_security_level(3);
  connection_manager_set_bond_ack(BOND_PENDING);
  connection_manager_bond_pin_code_set(_pin_code);

  // Bonds made by earlier test firmware may be unauthenticated: clear them once.
  if (!InternalFS.exists(BOND_RESET_MARKER)) {
    connection_manager_delete_all_bond();
    if (File f = InternalFS.open(BOND_RESET_MARKER, "w")) f.close();
    LOG_I("cleared stored BLE bonds (one-time)");
  }

  ble_gap_dev_name_t* dev_name = (ble_gap_dev_name_t*)rt_malloc(sizeof(ble_gap_dev_name_t) + strlen(_dev_name));
  dev_name->len = strlen(_dev_name);
  memcpy(dev_name->name, _dev_name, dev_name->len);
  ble_gap_set_dev_name(dev_name);
  rt_free(dev_name);

  _ready = true;
  if (_enabled) startAdvertising();
}

void SF32SerialBLEInterface::startAdvertising() {
  static bool initialised = false;
  if (!initialised) {
    sibles_advertising_para_t para = {0};
    para.own_addr_type = GAPM_STATIC_ADDR;
    para.config.adv_mode = SIBLES_ADV_CONNECT_MODE;
    // Fast (30 ms) for 30 s after start / a disconnect so phones find it quickly, then
    // every 500 ms until connected: about 16x less radio time while waiting.
    para.config.mode_config.conn_config.duration = 3000;   // 30 s (10 ms units)
    para.config.mode_config.conn_config.interval = 0x30;   // 30 ms (0.625 ms units)
    para.config.mode_config.conn_config.backgroud_mode_enabled = 1;
    para.config.mode_config.conn_config.backgroud_duration = 0;       // until connected
    para.config.mode_config.conn_config.backgroud_interval = 0x320;   // 500 ms
    para.config.max_tx_pwr = 0x7F;
    para.config.is_auto_restart = 1;  // re-advertise after a disconnect

    // adv data: NUS service UUID; scan response: the device name
    para.adv_data.completed_uuid =
        (sibles_adv_type_srv_uuid_t*)rt_malloc(sizeof(sibles_adv_type_srv_uuid_t) + sizeof(sibles_adv_uuid_t));
    para.adv_data.completed_uuid->count = 1;
    para.adv_data.completed_uuid->uuid_list[0].uuid_len = ATT_UUID_128_LEN;
    memcpy(para.adv_data.completed_uuid->uuid_list[0].uuid.uuid_128, s_nus_svc_uuid, ATT_UUID_128_LEN);

    size_t name_len = strlen(_dev_name);
    para.rsp_data.completed_name = (sibles_adv_type_name_t*)rt_malloc(sizeof(sibles_adv_type_name_t) + name_len);
    para.rsp_data.completed_name->name_len = name_len;
    memcpy(para.rsp_data.completed_name->name, _dev_name, name_len);

    para.evt_handler = adv_event;
    uint8_t ret = sibles_advertising_init(s_adv_context, &para);
    rt_free(para.adv_data.completed_uuid);
    rt_free(para.rsp_data.completed_name);
    if (ret != SIBLES_ADV_NO_ERR) {
      LOG_E("advertising init failed: %d", ret);
      return;
    }
    initialised = true;
  }
  uint8_t r = sibles_advertising_start(s_adv_context);
  LOG_I("advertising start: %d", r);
}

void SF32SerialBLEInterface::forgetBonds() {
  if (_link_up) {
    ble_gap_disconnect_t dis = {0};
    dis.conn_idx = _conn_idx;
    dis.reason = 0x13;  // remote user terminated connection
    ble_gap_disconnect(&dis);
  }
  if (_ready) connection_manager_delete_all_bond();
}

void SF32SerialBLEInterface::enable() {
  if (_enabled) return;
  _enabled = true;
  _send_len = 0;
  if (_ready) startAdvertising();
}

void SF32SerialBLEInterface::disable() {
  _enabled = false;
  if (_ready) sibles_advertising_stop(s_adv_context);
  if (_link_up) {
    ble_gap_disconnect_t dis = {0};
    dis.conn_idx = _conn_idx;
    dis.reason = 0x13;  // remote user terminated connection
    ble_gap_disconnect(&dis);
  }
  _send_len = 0;
}

// ---- BLE thread side ----

void SF32SerialBLEInterface::onStackEvent(uint16_t event_id, uint8_t* data, uint16_t len) {
  switch (event_id) {
    case BLE_POWER_ON_IND:
      LOG_I("BLE_POWER_ON_IND");
      _power_on_pending = true;  // finish setup in the main thread
      break;
    case BLE_GAP_CONNECTED_IND: {
      ble_gap_connect_ind_t* ind = (ble_gap_connect_ind_t*)data;
      _conn_idx = ind->conn_idx;
      _mtu = 23;
      _notify_on = false;
      _connected = false;
      _link_up = true;
      // Ask the phone to pair / re-encrypt, unless it starts that by itself within
      // 1.5 s: a security request sent while the phone's own pairing is starting
      // collides with it and the connection drops (seen with a 4 ms main loop).
      _security_pending = true;
      _security_due = rt_tick_get() + rt_tick_from_millisecond(1500);
      LOG_I("link up (conn %d)", ind->conn_idx);
      sibles_exchange_mtu(ind->conn_idx);  // frames are up to MAX_FRAME_SIZE (176) bytes
      break;
    }
    case BLE_GAP_ENCRYPT_IND: {
      // Security level 3 (setupOnPowerOn) only allows authenticated (passkey) pairing,
      // and older bonds were cleared, so any LE Secure Connections encryption here comes
      // from a PIN-paired bond. (This stack reports bonded re-encryption as 0x09,
      // without the MITM bit, even for passkey-paired keys.)
      _security_pending = false;  // already encrypted
      ble_gap_encrypt_ind_t* ind = (ble_gap_encrypt_ind_t*)data;
      bool sc = (ind->auth & GAP_AUTH_SEC_CON) != 0;
      LOG_I("encrypted (auth 0x%02X)", ind->auth);
      if (ind->conn_idx == _conn_idx && _link_up && sc) _connected = true;
      break;
    }
    case BLE_GAP_DISCONNECTED_IND:
      LOG_I("disconnected (reason 0x%02X)", ((ble_gap_disconnected_ind_t*)data)->reason);
      _link_up = false;
      _connected = false;
      _notify_on = false;
      _security_pending = false;
      break;
    case SIBLES_MTU_EXCHANGE_IND:
      _mtu = ((sibles_mtu_exchange_ind_t*)data)->mtu;
      LOG_I("MTU %d", _mtu);
      break;
    case CONNECTION_MANAGER_BOND_AUTH_INFOR: {
      connection_manager_bond_ack_infor_t* info = (connection_manager_bond_ack_infor_t*)data;
      _security_pending = false;  // pairing under way
      LOG_I("pairing step %d", info->request);
      if (info->request == GAPC_TK_EXCH) connection_manager_bond_pin_code_set(_pin_code);
      connection_manager_bond_ack_reply(info->conn_idx, info->request, true);
      break;
    }
    default:
      break;
  }
}

uint8_t SF32SerialBLEInterface::onAttributeWrite(uint8_t conn_idx, uint8_t idx, const uint8_t* value, uint16_t len) {
  if (idx == NUS_RX_VALUE) {
    if (len == 0 || len > MAX_FRAME_SIZE) return 0;
    // The characteristic already requires an encrypted, PIN-paired link (stack-enforced);
    // check it here too, so a stack quirk can never let an unpaired device send commands.
    if (!_connected || conn_idx != _conn_idx) return 0;
    Frame f;
    f.len = (uint8_t)len;
    memcpy(f.buf, value, len);
    rt_err_t r = rt_mq_send(&_rx_mq, &f, sizeof(f));  // dropped if the queue is full, as on ESP32
    LOG_D("rx frame %d bytes (cmd %d), queued %d", len, value[0], r);
  } else if (idx == NUS_TX_CCCD && len >= 1) {
    _notify_on = (value[0] & 0x01) != 0;
    LOG_I("notifications %s", _notify_on ? "on" : "off");
  }
  return 0;
}

// ---- main thread side ----

bool SF32SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;
}

size_t SF32SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  LOG_D("writeFrame %d (connected %d, notify %d, queued %d)", (int)len, _connected, _notify_on, _send_len);
  if (len == 0 || len > MAX_FRAME_SIZE || !_connected || _send_len >= QUEUE_SIZE) return 0;
  _send_queue[_send_len].len = (uint8_t)len;
  memcpy(_send_queue[_send_len].buf, src, len);
  _send_len++;
  return len;
}

size_t SF32SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  if (_power_on_pending) setupOnPowerOn();
  if (_security_pending && _link_up && (rt_int32_t)(rt_tick_get() - _security_due) >= 0) {
    // SMP security request: the phone then pairs (passkey = PIN) or, if already
    // bonded, re-encrypts with the stored keys.
    _security_pending = false;
    connection_manager_set_link_security(_conn_idx, LE_SECURITY_LEVEL_SEC_CON_MITM_BOND);
  }

  if (!_connected) _send_len = 0;

  if (_send_len > 0 && _notify_on && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL) {
    sibles_value_t value;
    value.hdl = _srv_handle;
    value.idx = NUS_TX_VALUE;
    value.len = _send_queue[0].len;
    value.value = _send_queue[0].buf;
    int w = sibles_write_value(_conn_idx, &value);
    LOG_D("notify %d bytes -> %d", value.len, w);
    if (w == value.len) {  // 0 = stack busy, retry later
      _last_write = millis();
      _send_len--;
      for (int i = 0; i < _send_len; i++) _send_queue[i] = _send_queue[i + 1];
    }
  }

  Frame f;
  if (rt_mq_recv(&_rx_mq, &f, sizeof(f), 0) == RT_EOK) {
    if (!_connected) return 0;  // queued before the link dropped: discard
    memcpy(dest, f.buf, f.len);
    return f.len;
  }
  return 0;
}
