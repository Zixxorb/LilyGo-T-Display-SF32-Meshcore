#include "SF32HardwareRNG.h"

#include "bf0_hal.h"

#define LOG_TAG "trng"
#include <ulog.h>

static RNG_HandleTypeDef s_trng;
static bool s_ready = false;

bool SF32HardwareRNG::begin() {
  if (s_ready) return true;
  s_trng.Instance = TRNG;
  if (HAL_RNG_Init(&s_trng) != HAL_OK) {
    LOG_E("TRNG init failed");
    return false;
  }
  s_ready = true;
  return true;
}

void SF32HardwareRNG::random(uint8_t* dest, size_t sz) {
  if (!s_ready && !begin()) {
    memset(dest, 0, sz);  // zeroes on failure; callers mix in SX1262 noise as well
    return;
  }
  // As in the SDK example: new seed, then a random number from it, per 32 bits.
  for (size_t i = 0; i < sz; i += 4) {
    uint32_t v = 0;
    HAL_RNG_Generate(&s_trng, &v, 1);
    HAL_RNG_Generate(&s_trng, &v, 0);
    for (size_t j = 0; j < 4 && i + j < sz; j++) dest[i + j] = (uint8_t)(v >> (8 * j));
  }
}
