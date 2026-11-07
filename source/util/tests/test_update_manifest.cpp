#include "test_harness.h"

#include <onion/update_manifest.hpp>
#include <onion/update_store.hpp>

#include <string>

namespace {

constexpr const char *kHash =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

std::string manifest_json(const std::string &version = "0.05a",
                          const std::string &tag = "v0.05a",
                          const std::string &channel = "stable",
                          const std::string &url =
                              "https://github.com/xS3Cx/AniketosHEN/releases/"
                              "download/v0.05a/AniketosHEN.elf",
                          const std::string &hash = kHash,
                          int size = 1024,
                          const std::string &summary = "A new build") {
  return std::string("{\"version\":\"") + version + "\",\"tag\":\"" + tag +
         "\",\"channel\":\"" + channel +
         "\",\"published_at\":\"2026-11-07T12:00:00Z\",\"url\":\"" + url +
         "\",\"sha256\":\"" + hash + "\",\"size\":" + std::to_string(size) +
         ",\"summary\":\"" + summary + "\"}";
}

int test_accepts_valid_manifest(void) {
  const auto parsed =
      onion::update::parse_manifest(manifest_json());
  TEST_ASSERT_TRUE(parsed.ok);
  TEST_ASSERT_STREQ("0.05a", parsed.manifest.version.c_str());
  TEST_ASSERT_STREQ("v0.05a", parsed.manifest.tag.c_str());
  TEST_ASSERT_STREQ("stable", parsed.manifest.channel.c_str());
  TEST_ASSERT_STREQ(kHash, parsed.manifest.sha256.c_str());
  TEST_ASSERT_EQ_U64(1024, parsed.manifest.size);
  return 0;
}

int test_rejects_bad_manifest(void) {
  TEST_ASSERT_TRUE(!onion::update::parse_manifest("").ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest("[]").ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest(
                        manifest_json("0.05a", "v0.05b"))
                        .ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest(
                        manifest_json("0.05a", "v0.05a", "nightly"))
                        .ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest(
                        manifest_json("0.05a", "v0.05a", "stable",
                                      "http://github.com/x/AniketosHEN.elf"))
                        .ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest(
                        manifest_json("0.05a", "v0.05a", "stable",
                                      "https://evil.example/AniketosHEN.elf"))
                        .ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest(
                        manifest_json("0.05a", "v0.05a", "stable",
                                      "https://github.com/x/AniketosHEN.bin"))
                        .ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest(
                        manifest_json("0.05a", "v0.05a", "stable",
                                      "https://github.com/x/AniketosHEN.elf",
                                      "abc"))
                        .ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest(
                        manifest_json("0.05a", "v0.05a", "stable",
                                      "https://github.com/x/AniketosHEN.elf",
                                      kHash, 0))
                        .ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest(
                        manifest_json("0.05a", "v0.05a", "stable",
                                      "https://github.com/x/AniketosHEN.elf",
                                      kHash, 1,
                                      std::string(301, 'a')))
                        .ok);
  TEST_ASSERT_TRUE(!onion::update::parse_manifest(
                        std::string(onion::update::kMaxManifestBytes + 1, ' '))
                        .ok);
  return 0;
}

int test_version_helpers(void) {
  TEST_ASSERT_STREQ("0.05a",
                    onion::update::normalize_version(" v0.05a ").c_str());
  TEST_ASSERT_TRUE(onion::update::valid_version("0.05a"));
  TEST_ASSERT_TRUE(!onion::update::valid_version("bad version"));
  TEST_ASSERT_TRUE(onion::update::development_version("dirty-unknown"));
  TEST_ASSERT_TRUE(onion::update::development_version("0.04n-dirty"));
  TEST_ASSERT_TRUE(!onion::update::development_version("0.04n"));
  onion::update::Manifest manifest;
  manifest.version = "0.05a";
  TEST_ASSERT_TRUE(onion::update::manifest_newer("0.04n", manifest));
  TEST_ASSERT_TRUE(!onion::update::manifest_newer("0.05a", manifest));
  TEST_ASSERT_TRUE(!onion::update::manifest_newer("0.06a", manifest));
  TEST_ASSERT_TRUE(!onion::update::manifest_newer("0.04n-dirty", manifest));
  return 0;
}

int test_channel_and_paths(void) {
  TEST_ASSERT_TRUE(onion::update::valid_channel("stable"));
  TEST_ASSERT_TRUE(!onion::update::valid_channel("beta"));
  TEST_ASSERT_TRUE(!onion::update::valid_channel("nightly"));
  TEST_ASSERT_TRUE(onion::update::manifest_url("stable").ends_with(
      "latest/download/update.json"));
  const std::string invalid = onion::update::manifest_cache_path("../../x");
  TEST_ASSERT_TRUE(invalid.ends_with("/latest-stable.json"));
  return 0;
}

} // namespace

extern "C" int test_update_manifest_suite(void) {
  int failures = 0;
  failures += onion_test_run("update.manifest_valid", test_accepts_valid_manifest);
  failures += onion_test_run("update.manifest_rejects", test_rejects_bad_manifest);
  failures += onion_test_run("update.version_helpers", test_version_helpers);
  failures += onion_test_run("update.channel_paths", test_channel_and_paths);
  return failures;
}
