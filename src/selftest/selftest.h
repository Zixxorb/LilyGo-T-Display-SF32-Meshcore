// On-device self-tests run at boot during bring-up.
#pragma once

struct SelfTestResult {
  int pass = 0;
  int fail = 0;
};

// Records a result; logs failures.
void selftest_check(SelfTestResult& r, bool ok, const char* what);

void selftest_arduino(SelfTestResult& r);  // step 2: Arduino layer + littlefs
void selftest_crypto(SelfTestResult& r);   // step 3: crypto, identities, CayenneLPP

class SF32SX1262Radio;
void selftest_radio(SelfTestResult& r, SF32SX1262Radio& radio);  // step 4: radio + TRNG
// After the self-test: receive and log any packets heard. Call repeatedly.
void radio_listen_step(SF32SX1262Radio& radio, uint32_t& rx_count);
