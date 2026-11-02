// BLE stack event registration for SF32SerialBLEInterface. Lives in C because the
// SDK's BLE_EVENT_REGISTER macro (also used by SIBLES_ADVERTISING_CONTEXT_DECLAR)
// builds a section name from adjacent string literals ("..."n), which C++11 rejects
// as a user-defined literal.
#include <rtthread.h>
#include "bf0_ble_common.h"
#include "bf0_sibles_advertising.h"

int sf32_ble_on_event(uint16_t event_id, uint8_t* data, uint16_t len, uint32_t context);  // C++ side

static int sf32_ble_event_handler(uint16_t event_id, uint8_t* data, uint16_t len, uint32_t context) {
  return sf32_ble_on_event(event_id, data, len, context);
}
BLE_EVENT_REGISTER(sf32_ble_event_handler, NULL);

// Advertising context (defines the global pointer sf32_adv_context).
SIBLES_ADVERTISING_CONTEXT_DECLAR(sf32_adv_context);
