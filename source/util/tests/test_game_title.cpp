/* Host tests for telling games from apps and homebrew (no PS5). */
#include "test_harness.h"

#include "aniketos_shell/games/game_title.hpp"

#include <cstdint>
#include <string>

using namespace AniketosShell::GameTitle;

namespace {

/* A param.sfo with one string entry. */
std::string sfo(const std::string &key, const std::string &value) {
  std::string d("\0PSF\x01\x01\0\0", 8);
  const auto u32 = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i)
      d += static_cast<char>((v >> (8 * i)) & 0xff);
  };
  const uint32_t keys = 20 + 16;
  const uint32_t data = keys + static_cast<uint32_t>(key.size()) + 1;
  const uint32_t len = static_cast<uint32_t>(value.size()) + 1;
  u32(keys);
  u32(data);
  u32(1);
  u32(0x02040000); /* key offset 0, utf-8 */
  u32(len);
  u32(len);
  u32(0);
  d += key + std::string(1, '\0') + value + std::string(1, '\0');
  return d;
}

int test_ps5_titles() {
  TEST_ASSERT_TRUE(IsGame("PPSA02100", "{\"applicationCategoryType\": 0}", ""));
  TEST_ASSERT_TRUE(!IsGame("PPSA01650", "{\"applicationCategoryType\":65536}", ""));
  TEST_ASSERT_TRUE(IsGame("PPSA02100", "", ""));
  TEST_ASSERT_TRUE(!IsGame("NPXS40041", "{\"applicationCategoryType\": 0}", ""));
  TEST_ASSERT_TRUE(!IsGame("MOUU12023", "{\"applicationCategoryType\": 0}", ""));
  return 0;
}

int test_ps4_titles() {
  TEST_ASSERT_TRUE(SfoString(sfo("TITLE", "Outlast"), "TITLE") == "Outlast");
  TEST_ASSERT_TRUE(SfoString(sfo("TITLE", "Outlast"), "TITLE_ID").empty());
  TEST_ASSERT_TRUE(IsGame("CUSA00409", "", sfo("CATEGORY", "gd")));
  TEST_ASSERT_TRUE(IsGame("CUSA02624", "", sfo("CATEGORY", "gp")));
  TEST_ASSERT_TRUE(IsGame("CUSA03509", "", sfo("CATEGORY", "gpo")));
  TEST_ASSERT_TRUE(!IsGame("CUSA00127", "", sfo("CATEGORY", "gde")));
  TEST_ASSERT_TRUE(IsGame("CUSA00409", "", ""));
  TEST_ASSERT_TRUE(!IsGame("ITEM00001", "", sfo("CATEGORY", "gd")));
  return 0;
}

} // namespace

extern "C" int test_game_title_suite(void) {
  int failures = 0;
  failures += onion_test_run("game_title.ps5_titles", test_ps5_titles);
  failures += onion_test_run("game_title.ps4_titles", test_ps4_titles);
  return failures;
}
