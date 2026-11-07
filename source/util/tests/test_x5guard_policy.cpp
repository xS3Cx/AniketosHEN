/* Host tests for x5Guard's labels, banner text and banner rate limit. */
#include "test_harness.h"

#include "x5guard/guard_policy.hpp"

#include <cstdio>
#include <string>

using namespace X5Guard::Policy;

namespace {

int Expect(const std::string &want, const std::string &got) {
  if (want == got)
    return 0;
  fprintf(stderr, "    want '%s'\n    got  '%s'\n", want.c_str(), got.c_str());
  return 1;
}

int test_labels() {
  int bad = 0;
  bad += Expect("PSN sign-in request [SigninUtility.Open]",
                Label("signin.utility", "Open"));
  bad += Expect("PSN auth deeplink [psaccounts:signin]",
                Label("signin.link", "psaccounts:signin?x=1"));
  bad += Expect("Firmware update dialog [SystemUpdaterDialog]",
                Label("update.dialog", "SystemUpdaterDialogA"));
  bad += Expect("Firmware updater call [StartDownload]",
                Label("update.process", "StartDownload"));
  bad += Expect("Auto-update registry write [0x7820100]",
                Label("update.auto", "0x7820100"));
  bad += Expect("Feature update fetch [CheckRnpsApps]",
                Label("update.feature", "CheckRnpsApps"));
  bad += Expect("PSN notification [NUC123]", Label("notify.drop", "NUC123"));
  bad += Expect("PSN connection test [NetTest]",
                Label("native.nettest", "NetTest"));
  bad += Expect("PSN system call [foo]", Label("native.foo", ""));
  bad += Expect("Firmware updater call", Label("update.process", ""));
  bad += Expect("something odd", Label("new.code", "something odd"));
  bad += Expect("new.code", Label("new.code", ""));
  /* control bytes from JS never reach the banner */
  bad += Expect("PSN notification [ab]", Label("notify.drop", "a\nb\x01"));
  return bad;
}

int test_long_detail_is_cut() {
  const std::string label =
      Label("update.process", std::string(200, 'x'));
  TEST_ASSERT_TRUE(label.size() <= kMaxLabel);
  TEST_ASSERT_TRUE(label.rfind("Firmware updater call [", 0) == 0);
  TEST_ASSERT_TRUE(label.substr(label.size() - 3) == "..]");
  /* a cut never splits a UTF-8 character */
  const std::string utf8 = Label("notify.drop", std::string(40, 'a') + "\xC3\xA9\xC3\xA9");
  for (size_t i = 0; i + 1 < utf8.size(); ++i)
    if ((unsigned char)utf8[i] == 0xC3)
      TEST_ASSERT_TRUE(((unsigned char)utf8[i + 1] & 0xC0) == 0x80);
  return 0;
}

int test_banner_text() {
  int bad = 0;
  bad += Expect("! x5Guard PSN sign-in request blocked",
                BannerText("! x5Guard", "%s blocked", "PSN sign-in request"));
  bad += Expect("! x5Guard bloqueado: X",
                BannerText("! x5Guard", "bloqueado: %s", "X"));
  /* missing key, bad or extra placeholders: safe English fallback */
  bad += Expect("P X blocked", BannerText("P", "x5guard.banner_fmt", "X"));
  bad += Expect("P X blocked", BannerText("P", "%s %d", "X"));
  bad += Expect("P X blocked", BannerText("P", "%n%s", "X"));
  bad += Expect("X blocked", BannerText("", nullptr, "X"));
  return bad;
}

int test_rate_limit() {
  RateLimit limit;
  TEST_ASSERT_TRUE(limit.Allow("a|1", 0));
  TEST_ASSERT_TRUE(!limit.Allow("b|1", 300));   /* any banner: 0.4 s */
  TEST_ASSERT_TRUE(limit.Allow("b|1", 400));
  TEST_ASSERT_TRUE(!limit.Allow("a|1", 1000));  /* same one: 2.5 s */
  TEST_ASSERT_TRUE(limit.Allow("a|2", 1000));   /* other detail is new */
  TEST_ASSERT_TRUE(!limit.Allow("a|1", 2499));
  TEST_ASSERT_TRUE(limit.Allow("a|1", 2500));
  /* a dropped try does not restart the 2.5 s */
  TEST_ASSERT_TRUE(!limit.Allow("c|1", 2600));
  TEST_ASSERT_TRUE(limit.Allow("c|1", 2900));
  TEST_ASSERT_TRUE(BannerName("signin.utility", "SigninUtility.Open") ==
                   "SigninUtility.Open()");
  TEST_ASSERT_TRUE(BannerName("signin.link", "psaccounts:signin") ==
                   "psaccounts:signin");
  TEST_ASSERT_TRUE(BannerName("update.auto", "isSystemUpdateAutoDownload") ==
                   "isSystemUpdateAutoDownload");
  return 0;
}

int test_update_check_is_silent() {
  TEST_ASSERT_TRUE(Silent("update.check"));
  TEST_ASSERT_TRUE(!Silent("update.dialog"));
  TEST_ASSERT_TRUE(!Silent(nullptr));
  return 0;
}

} // namespace

extern "C" int test_x5guard_policy_suite(void) {
  int failures = 0;
  failures += onion_test_run("x5guard.labels", test_labels);
  failures += onion_test_run("x5guard.long_detail", test_long_detail_is_cut);
  failures += onion_test_run("x5guard.banner_text", test_banner_text);
  failures += onion_test_run("x5guard.rate_limit", test_rate_limit);
  failures += onion_test_run("x5guard.update_check_silent",
                             test_update_check_is_silent);
  return failures;
}
