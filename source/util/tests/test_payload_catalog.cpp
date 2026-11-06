#include "test_harness.h"

#include <onion/payload_catalog.hpp>
#include <onion/payload_sources_json.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>

using namespace onion::payload_catalog;

static std::string fixture() {
  FILE *file = std::fopen(
      ONION_TEST_DIR "/fixtures/payload_catalog/payloads_v2.json", "rb");
  if (!file)
    return {};
  std::string body;
  char buffer[4096];
  std::size_t n = 0;
  while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    body.append(buffer, n);
  std::fclose(file);
  return body;
}

static int test_wrapper_catalog(void) {
  const ParseResult result = parse_catalog(fixture());
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_STREQ("PS5 Payloads", result.catalog.name.c_str());
  TEST_ASSERT_TRUE(result.catalog.items.size() >= 3);
  const Item &first = result.catalog.items.front();
  TEST_ASSERT_STREQ("ftpsrv", first.id.c_str());
  TEST_ASSERT_STREQ("ftpsrv", first.name.c_str());
  TEST_ASSERT_TRUE(first.has_checksum);
  TEST_ASSERT_TRUE(!first.insecure);
  TEST_ASSERT_STREQ("Networking & Servers", first.category.c_str());
  bool saw_insecure = false;
  bool saw_bad_checksum_skipped = false;
  for (const Item &item : result.catalog.items) {
    if (item.id == "insecure")
      saw_insecure = item.insecure;
    if (item.id == "badsum")
      saw_bad_checksum_skipped = true;
  }
  TEST_ASSERT_TRUE(saw_insecure);
  TEST_ASSERT_TRUE(!saw_bad_checksum_skipped);
  TEST_ASSERT_TRUE(result.skipped >= 1);
  return 0;
}

static int test_bare_array_and_limits(void) {
  const ParseResult bare = parse_catalog(
      R"([{"name":"A","filename":"a.elf","url":"https://x/a.elf"}])");
  TEST_ASSERT_TRUE(bare.ok);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(bare.catalog.items.size()));
  TEST_ASSERT_STREQ("a", bare.catalog.items.front().id.c_str());
  TEST_ASSERT_TRUE(!parse_catalog("").ok);
  TEST_ASSERT_TRUE(!parse_catalog("{}").ok);
  TEST_ASSERT_TRUE(!parse_catalog("[] trailing").ok);
  const ParseResult insecure =
      parse_catalog(R"([{"name":"x","url":"http://x/a.elf"}])");
  TEST_ASSERT_TRUE(insecure.ok);
  TEST_ASSERT_TRUE(insecure.catalog.items.front().insecure);
  TEST_ASSERT_TRUE(truncate_utf8("abcdef", 3) == "abc");
  TEST_ASSERT_TRUE(truncate_utf8("\xE2\x82\xAC\xE2\x82\xAC", 4) == "\xE2\x82\xAC");
  return 0;
}

static int test_sources_roundtrip(void) {
  char path[] = "/tmp/onion-sources-XXXXXX";
  const int fd = mkstemp(path);
  TEST_ASSERT_TRUE(fd >= 0);
  close(fd);
  SourcesFile sources = default_sources();
  TEST_ASSERT_EQ_INT(1, static_cast<int>(sources.sources.size()));
  TEST_ASSERT_TRUE(add_source(&sources, "Mine", "https://example.com/c.json"));
  TEST_ASSERT_TRUE(!add_source(&sources, "Bad", "http://example.com/c.json"));
  TEST_ASSERT_TRUE(!add_source(&sources, "Mine", "https://example.com/c.json"));
  TEST_ASSERT_TRUE(save_sources_file(path, sources));
  SourcesFile loaded;
  TEST_ASSERT_TRUE(load_sources_file(path, &loaded));
  TEST_ASSERT_EQ_INT(2, static_cast<int>(loaded.sources.size()));
  TEST_ASSERT_TRUE(loaded.sources.front().builtin);
  TEST_ASSERT_STREQ("https://example.com/c.json",
                    loaded.sources.back().url.c_str());
  TEST_ASSERT_TRUE(remove_source(&loaded, "https://example.com/c.json"));
  TEST_ASSERT_TRUE(!remove_source(&loaded, loaded.sources.front().url));
  unlink(path);
  return 0;
}

extern "C" int test_payload_catalog_suite(void) {
  int failures = 0;
  failures += onion_test_run("payload.catalog_wrapper", test_wrapper_catalog);
  failures += onion_test_run("payload.catalog_bare", test_bare_array_and_limits);
  failures += onion_test_run("payload.sources_roundtrip", test_sources_roundtrip);
  return failures;
}
