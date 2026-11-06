#include "test_harness.h"

#include <onion/payload_catalog_policy.hpp>

using namespace onion::payload_catalog;

static int test_version_order(void) {
  TEST_ASSERT_TRUE(version_compare("v1.0.0", "1.0.0") == 0);
  TEST_ASSERT_TRUE(version_compare("1.2.3", "1.2.4") < 0);
  TEST_ASSERT_TRUE(version_compare("1.10", "1.9") > 0);
  TEST_ASSERT_TRUE(version_compare("2.0.0", "2.0") > 0);
  TEST_ASSERT_TRUE(version_compare("1.0.0-beta", "1.0.0") < 0);
  TEST_ASSERT_TRUE(version_compare("1.0.0-rc2", "1.0.0-rc10") < 0);
  TEST_ASSERT_TRUE(version_compare("v1.0", "V1.0") == 0);
  TEST_ASSERT_TRUE(version_compare("2026-08-20", "2026-08-19") > 0);
  TEST_ASSERT_TRUE(version_compare("", "") == 0);
  TEST_ASSERT_TRUE(version_compare("1..2", "1-2") == 0);
  TEST_ASSERT_TRUE(compare_versions("1.0", "2026-01-01", "1.0", "2026-02-01") <
                   0);
  return 0;
}

static int test_slug_and_hash(void) {
  const std::string ftp = slug("FTPSrv");
  const std::string loader = slug("PS5 Linux Loader");
  const std::string version = slug("v1.2.3");
  const std::string empty = slug("***");
  const std::string icon = icon_cache_name("icon");
  TEST_ASSERT_STREQ("ftpsrv", ftp.c_str());
  TEST_ASSERT_STREQ("ps5-linux-loader", loader.c_str());
  TEST_ASSERT_STREQ("v1-2-3", version.c_str());
  TEST_ASSERT_STREQ("payload", empty.c_str());
  TEST_ASSERT_STREQ("dbff4cc56c2092f0", icon.c_str());
  TEST_ASSERT_EQ_INT(8, static_cast<int>(source_hash8("https://x/y").size()));
  TEST_ASSERT_TRUE(fnv1a32("abc") == 0x1a47e90bu);
  return 0;
}

static int test_validation(void) {
  TEST_ASSERT_TRUE(valid_url("https://example.com/a.elf"));
  TEST_ASSERT_TRUE(valid_url("http://example.com/a.elf"));
  TEST_ASSERT_TRUE(!valid_url("ftp://example.com/a.elf"));
  TEST_ASSERT_TRUE(!valid_url("https://"));
  TEST_ASSERT_TRUE(!valid_filename("../a.elf"));
  TEST_ASSERT_TRUE(!valid_filename("a/b.elf"));
  TEST_ASSERT_TRUE(valid_filename("a b.elf"));
  TEST_ASSERT_TRUE(is_binary_filename("x.BIN"));
  TEST_ASSERT_TRUE(valid_checksum(
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  TEST_ASSERT_TRUE(!valid_checksum("abc"));
  TEST_ASSERT_TRUE(is_system_or_conflicting_name("kstuff-lite"));
  TEST_ASSERT_TRUE(is_system_or_conflicting_name("etaHEN"));
  TEST_ASSERT_TRUE(!is_system_or_conflicting_name("ftpsrv"));
  TEST_ASSERT_TRUE(!autostart_allowed("elfldr.elf"));
  TEST_ASSERT_TRUE(autostart_allowed("ftpsrv.elf"));
  return 0;
}

extern "C" int test_payload_version_suite(void) {
  int failures = 0;
  failures += onion_test_run("payload.version_order", test_version_order);
  failures += onion_test_run("payload.slug_hash", test_slug_and_hash);
  failures += onion_test_run("payload.validation", test_validation);
  return failures;
}
