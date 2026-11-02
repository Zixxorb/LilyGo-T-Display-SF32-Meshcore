// mesh::RNG backed by the SF32LB52's hardware TRNG.
#pragma once

#include <Mesh.h>

class SF32HardwareRNG : public mesh::RNG {
public:
  bool begin();  // safe to call more than once
  void random(uint8_t* dest, size_t sz) override;
};
