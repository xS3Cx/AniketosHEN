/* Host tests for the PIN lock's record and unlock rules (no PS5/Mono). */
#include "test_harness.h"

#include "aniketos_shell/system_ui/pin_lock_core.hpp"

using namespace PinLockCore;

namespace {

int test_pin_shape() {
  TEST_ASSERT_TRUE(ValidPin("1234"));
  TEST_ASSERT_TRUE(ValidPin("12345678"));
  TEST_ASSERT_TRUE(!ValidPin("123"));
  TEST_ASSERT_TRUE(!ValidPin("123456789"));
  TEST_ASSERT_TRUE(!ValidPin("12a4"));
  TEST_ASSERT_TRUE(!ValidPin(" 1234"));
  return 0;
}

int test_sha256_known_vector() {
  const std::string hash = Sha256Hex("abc");
  TEST_ASSERT_STREQ(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      hash.c_str());
  return 0;
}

int test_record_matches_only_its_pin() {
  const std::string record = MakeRecord("00112233445566778899aabbccddeeff", "4321");
  TEST_ASSERT_TRUE(record.find("4321") == std::string::npos);
  TEST_ASSERT_TRUE(Matches(record, "4321"));
  TEST_ASSERT_TRUE(!Matches(record, "1234"));
  TEST_ASSERT_TRUE(!Matches(record, ""));
  /* Another salt, another hash for the same PIN. */
  TEST_ASSERT_TRUE(MakeRecord("aa", "4321") != MakeRecord("bb", "4321"));
  TEST_ASSERT_TRUE(!Matches("garbage", "4321"));
  TEST_ASSERT_TRUE(!Matches("", "4321"));
  return 0;
}

int test_unlock_expiry() {
  Session s{true, 1000, 7};
  TEST_ASSERT_TRUE(StillOpen(s, 1000, 7));
  TEST_ASSERT_TRUE(StillOpen(s, 1000 + kIdleSeconds, 7));
  TEST_ASSERT_TRUE(!StillOpen(s, 1001 + kIdleSeconds, 7));
  TEST_ASSERT_TRUE(!StillOpen(s, 1000, 8)); /* another user */
  TEST_ASSERT_TRUE(!StillOpen(s, 999, 7));  /* clock went back */
  TEST_ASSERT_TRUE(!StillOpen(Session{}, 1000, -1));
  return 0;
}

} // namespace

extern "C" int test_pin_lock_suite(void) {
  int fails = 0;
  fails += onion_test_run("pin_lock.pin_shape", test_pin_shape);
  fails += onion_test_run("pin_lock.sha256", test_sha256_known_vector);
  fails += onion_test_run("pin_lock.record", test_record_matches_only_its_pin);
  fails += onion_test_run("pin_lock.expiry", test_unlock_expiry);
  return fails;
}
