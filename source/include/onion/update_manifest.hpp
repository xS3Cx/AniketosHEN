#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace onion::update {

inline constexpr std::size_t kMaxManifestBytes = 64 * 1024;
inline constexpr std::uint64_t kMaxUpdateBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxVersionLength = 32;
inline constexpr std::size_t kMaxSummaryLength = 300;
inline constexpr std::size_t kMaxPublishedAtLength = 40;

inline constexpr std::string_view kStableChannel = "stable";
inline constexpr std::string_view kBetaChannel = "beta";

inline constexpr std::string_view kStableManifestUrl =
    "https://github.com/xS3Cx/AniketosHEN/releases/latest/download/update.json";
inline constexpr std::string_view kBetaManifestUrl =
    "https://github.com/xS3Cx/AniketosHEN/releases/download/beta/update.json";

struct Manifest {
  std::string version;
  std::string tag;
  std::string channel;
  std::string published_at;
  std::string url;
  std::string sha256;
  std::string summary;
  std::uint64_t size = 0;
};

struct ParseResult {
  bool ok = false;
  Manifest manifest;
  std::string error;
};

bool valid_channel(std::string_view channel);
std::string_view manifest_url(std::string_view channel);

bool valid_version(std::string_view version);
std::string normalize_version(std::string_view version);
bool development_version(std::string_view version);

bool valid_manifest_url(std::string_view url);
bool valid_sha256(std::string_view sha256);

ParseResult parse_manifest(std::string_view body);
ParseResult load_manifest_text(std::string_view body);

bool manifest_newer(std::string_view installed,
                    const Manifest &manifest);

std::string installed_version();

} // namespace onion::update
