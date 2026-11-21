#include "TDeckBLE.h"
#include <helpers/esp32/SerialBLEInterface.h>
#include <esp_gap_ble_api.h>

extern SerialBLEInterface bluetooth_interface;  // MeshCore's (main.cpp)
TDeckBLE tdeck_ble;

bool TDeckBLE::forgetBonds() {
  if (!bluetooth_interface.isEnabled()) return false;
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) return true;
  esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(n * sizeof(esp_ble_bond_dev_t));
  if (!list) return false;
  esp_ble_get_bond_device_list(&n, list);
  for (int i = 0; i < n; i++) esp_ble_remove_bond_device(list[i].bd_addr);
  free(list);
  return true;
}
