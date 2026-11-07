/* Host tests for finding a resource name in a PUI .rco (no PS5/Mono). */
#include "test_harness.h"

#include "aniketos_shell/core/rco_names.hpp"

#include <cstring>
#include <vector>

namespace {

/* A header and a table of names laid out as Sce.PlayStation.PUI_UI3.rco
   has them: a 4-byte node offset in front of each NUL-ended name. */
std::vector<unsigned char> fake_rco() {
  const char table[] = "\x18\x82\x01\x00iconid_trophy_rarity3_rare\0\0"
                       "\x64\x82\x01\x00iconid_troubleshoot\0"
                       "\xb0\x82\x01\x00iconid_troubleshoot_game\0";
  std::vector<unsigned char> rco(0x40, 0);
  std::memcpy(rco.data(), "RCOF\x10\x01\x00\x00", 8);
  const unsigned offset = 0x40, size = sizeof(table);
  for (int i = 0; i < 4; ++i) {
    rco[0x10 + i] = static_cast<unsigned char>(offset >> (8 * i));
    rco[0x14 + i] = static_cast<unsigned char>(size >> (8 * i));
  }
  rco.insert(rco.end(), table, table + sizeof(table));
  rco.push_back('x'); /* past the table */
  return rco;
}

int test_finds_a_whole_name() {
  const std::vector<unsigned char> rco = fake_rco();
  TEST_ASSERT_EQ_INT((int)rco.size() - 1,
                    (int)RcoNames::TableEnd(rco.data(), rco.size()));
  TEST_ASSERT_TRUE(RcoNames::Has(rco.data(), rco.size(), "iconid_troubleshoot"));
  TEST_ASSERT_TRUE(
      RcoNames::Has(rco.data(), rco.size(), "iconid_troubleshoot_game"));
  TEST_ASSERT_TRUE(!RcoNames::Has(rco.data(), rco.size(), "iconid_trouble"));
  TEST_ASSERT_TRUE(!RcoNames::Has(rco.data(), rco.size(), "iconid_build"));
  return 0;
}

int test_refuses_what_is_no_table() {
  std::vector<unsigned char> rco = fake_rco();
  /* cut short of the table's end */
  TEST_ASSERT_TRUE(!RcoNames::Has(rco.data(), rco.size() - 10,
                                  "iconid_troubleshoot_game"));
  TEST_ASSERT_EQ_INT(0, (int)RcoNames::TableEnd(rco.data(), 8));
  rco[0] = 'X';
  TEST_ASSERT_EQ_INT(0, (int)RcoNames::TableEnd(rco.data(), rco.size()));
  TEST_ASSERT_TRUE(!RcoNames::Has(rco.data(), rco.size(), "iconid_troubleshoot"));
  return 0;
}

} // namespace

extern "C" int test_rco_names_suite(void) {
  int failures = 0;
  failures += onion_test_run("rco_names.finds_a_whole_name",
                             test_finds_a_whole_name);
  failures += onion_test_run("rco_names.refuses_what_is_no_table",
                             test_refuses_what_is_no_table);
  return failures;
}
