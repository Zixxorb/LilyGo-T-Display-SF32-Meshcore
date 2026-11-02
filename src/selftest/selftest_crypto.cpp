// Step 3 self-test: MeshCore's crypto on this target, checked against published test
// vectors (FIPS 180-2, FIPS 197, RFC 8032), plus MeshCore-level round trips
// (ECDH, encrypt-then-MAC, IdentityStore on littlefs) and CayenneLPP encoding.

#include "selftest.h"
#include <Arduino.h>
#include <FS.h>
#include <Utils.h>
#include <Identity.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/IdentityStore.h>
#include <AES.h>
#include <ed_25519.h>
#include <CayenneLPP.h>

#define LOG_TAG "test.cry"
#include <ulog.h>

static bool hex_eq(const uint8_t* bytes, const char* hex, size_t len) {
  uint8_t expected[64];
  if (len > sizeof(expected) || !mesh::Utils::fromHex(expected, len, hex)) return false;
  return memcmp(bytes, expected, len) == 0;
}

static void test_sha256(SelfTestResult& r) {
  // FIPS 180-2 Appendix B.1
  uint8_t hash[32];
  mesh::Utils::sha256(hash, sizeof(hash), (const uint8_t*)"abc", 3);
  selftest_check(r, hex_eq(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32),
                 "SHA-256(\"abc\") matches FIPS 180-2");
}

static void test_aes(SelfTestResult& r) {
  // FIPS 197 Appendix C.1
  uint8_t key[16], pt[16], ct[16];
  mesh::Utils::fromHex(key, 16, "000102030405060708090a0b0c0d0e0f");
  mesh::Utils::fromHex(pt, 16, "00112233445566778899aabbccddeeff");
  AES128 aes;
  aes.setKey(key, sizeof(key));
  aes.encryptBlock(ct, pt);
  selftest_check(r, hex_eq(ct, "69c4e0d86a7b0430d8cdb78070b4c55a", 16), "AES-128 matches FIPS 197");
}

struct Rfc8032Vector {
  const char* seed;
  const char* pub;
  const char* msg;  // hex, may be empty
  const char* sig;
};

// RFC 8032 section 7.1, TEST 1 and TEST 2
static const Rfc8032Vector kEd25519Vectors[] = {
    {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
     "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
    {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
     "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
};

static void test_ed25519(SelfTestResult& r) {
  for (size_t i = 0; i < sizeof(kEd25519Vectors) / sizeof(kEd25519Vectors[0]); i++) {
    const Rfc8032Vector& v = kEd25519Vectors[i];
    uint8_t seed[32], pub[32], prv[64], msg[8], sig[64];
    size_t msg_len = strlen(v.msg) / 2;
    mesh::Utils::fromHex(seed, 32, v.seed);
    if (msg_len) mesh::Utils::fromHex(msg, msg_len, v.msg);

    // Key generation (orlp/ed25519, used by MeshCore for keygen and signing).
    ed25519_create_keypair(pub, prv, seed);
    selftest_check(r, hex_eq(pub, v.pub, 32), "Ed25519 public key matches RFC 8032");

    // Signing through MeshCore's LocalIdentity.
    uint8_t keys[96];
    memcpy(keys, prv, 64);
    memcpy(keys + 64, pub, 32);
    mesh::LocalIdentity id;
    id.readFrom(keys, sizeof(keys));
    id.sign(sig, msg, msg_len);
    selftest_check(r, hex_eq(sig, v.sig, 64), "Ed25519 signature matches RFC 8032");

    // Verification through MeshCore's Identity (rweather/Crypto).
    selftest_check(r, id.verify(sig, msg, msg_len), "Ed25519 verify accepts valid signature");
    sig[10] ^= 0x01;
    selftest_check(r, !id.verify(sig, msg, msg_len), "Ed25519 verify rejects tampered signature");
  }
}

static void test_identities(SelfTestResult& r) {
  StdRNG rng;
  rng.begin(millis() ^ 0x5eed);

  unsigned long t0 = millis();
  mesh::LocalIdentity a(&rng);
  unsigned long t_keygen = millis() - t0;
  mesh::LocalIdentity b(&rng);

  uint8_t msg[] = "MeshCore on T-Display-SF32";
  uint8_t sig[SIGNATURE_SIZE];
  t0 = millis();
  a.sign(sig, msg, sizeof(msg));
  unsigned long t_sign = millis() - t0;
  t0 = millis();
  bool ok = a.verify(sig, msg, sizeof(msg));
  unsigned long t_verify = millis() - t0;
  selftest_check(r, ok, "sign/verify round trip with random identity");
  selftest_check(r, !b.verify(sig, msg, sizeof(msg)), "signature fails under a different key");

  uint8_t sa[PUB_KEY_SIZE], sb[PUB_KEY_SIZE];
  t0 = millis();
  a.calcSharedSecret(sa, b);
  unsigned long t_ecdh = millis() - t0;
  b.calcSharedSecret(sb, a);
  selftest_check(r, memcmp(sa, sb, sizeof(sa)) == 0, "ECDH shared secrets match");
  selftest_check(r, !mesh::Utils::isZeroes(sa, sizeof(sa)), "ECDH shared secret is non-zero");

  uint8_t keys[96];
  a.writeTo(keys, sizeof(keys));
  selftest_check(r, mesh::LocalIdentity::validatePrivateKey(keys), "validatePrivateKey accepts generated key");

  LOG_I("timing: keygen %lu ms, sign %lu ms, verify %lu ms, ECDH %lu ms", t_keygen, t_sign,
        t_verify, t_ecdh);

  // Encrypt-then-MAC (how MeshCore protects packet payloads).
  uint8_t plain[40], enc[64], dec[64];
  for (size_t i = 0; i < sizeof(plain); i++) plain[i] = (uint8_t)i;
  int enc_len = mesh::Utils::encryptThenMAC(sa, enc, plain, sizeof(plain));
  int dec_len = mesh::Utils::MACThenDecrypt(sb, dec, enc, enc_len);
  selftest_check(r, dec_len >= (int)sizeof(plain) && memcmp(dec, plain, sizeof(plain)) == 0,
                 "encryptThenMAC / MACThenDecrypt round trip");
  enc[enc_len - 1] ^= 0x01;
  selftest_check(r, mesh::Utils::MACThenDecrypt(sb, dec, enc, enc_len) == 0,
                 "MACThenDecrypt rejects tampered ciphertext");

  // IdentityStore on littlefs (how MeshCore persists the node's key).
  IdentityStore store(InternalFS, "/identity");
  store.begin();
  selftest_check(r, store.save("_selftest", a), "IdentityStore save");
  mesh::LocalIdentity loaded;
  selftest_check(r, store.load("_selftest", loaded), "IdentityStore load");
  uint8_t keys2[96];
  loaded.writeTo(keys2, sizeof(keys2));
  selftest_check(r, memcmp(keys, keys2, sizeof(keys)) == 0, "IdentityStore round trip preserves keys");
  InternalFS.remove("/identity/_selftest.id");
}

static void test_cayenne(SelfTestResult& r) {
  CayenneLPP lpp(16);
  lpp.addVoltage(1, 3.70f);  // channel 1, type 116, 370 = 0x0172
  const uint8_t expected[] = {0x01, 0x74, 0x01, 0x72};
  selftest_check(r, lpp.getSize() == sizeof(expected) && memcmp(lpp.getBuffer(), expected, sizeof(expected)) == 0,
                 "CayenneLPP addVoltage encoding");
}

void selftest_crypto(SelfTestResult& r) {
  test_sha256(r);
  test_aes(r);
  test_ed25519(r);
  test_identities(r);
  test_cayenne(r);
}
