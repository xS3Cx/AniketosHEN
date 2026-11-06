#include "test_harness.h"

#include "payload_center/payload_center_install.hpp"

using onion::payload_center::InstallRequest;
using onion::payload_center::validate_install_request;

static int test_request_validation(void) {
  InstallRequest request;
  request.url = "https://example.com/file.elf";
  request.name = "file.elf";
  request.filename = "file.elf";
  TEST_ASSERT_TRUE(validate_install_request(request).empty());
  request.url = "http://example.com/file.elf";
  TEST_ASSERT_STREQ("rejected", validate_install_request(request).c_str());
  request.url = "https://example.com/file.elf";
  request.name.clear();
  TEST_ASSERT_STREQ("rejected", validate_install_request(request).c_str());
  request.name = "file.elf";
  request.has_checksum = true;
  request.checksum = "bad";
  TEST_ASSERT_STREQ("hash", validate_install_request(request).c_str());
  request.checksum.assign(64, '0');
  TEST_ASSERT_TRUE(validate_install_request(request).empty());
  request.filename = "../file.elf";
  TEST_ASSERT_STREQ("rejected", validate_install_request(request).c_str());
  return 0;
}

extern "C" int test_payload_install_suite(void) {
  int failures = 0;
  failures +=
      onion_test_run("payload.install_validation", test_request_validation);
  return failures;
}
