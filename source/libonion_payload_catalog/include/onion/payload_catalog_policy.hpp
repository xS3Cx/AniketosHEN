#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace onion::payload_catalog {

inline constexpr std::string_view kBuiltinSourceName = "PS5 Payloads";
inline constexpr std::string_view kBuiltinSourceUrl =
    "https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json";
inline constexpr std::string_view kBuiltinSourceFallbackUrl =
    "https://raw.githubusercontent.com/itsPLK/ps5-payloads-mirror/main/"
    "payloads_v2.json";
inline constexpr std::size_t kMaxSources = 8;
inline constexpr std::size_t kMaxUrlLength = 2048;
inline constexpr std::size_t kMaxNameLength = 256;

std::string trim(std::string_view text);
std::string ascii_lower(std::string_view text);

uint32_t fnv1a32(std::string_view text);
uint64_t fnv1a64(std::string_view text);
std::string hex32(uint32_t value);
std::string hex64(uint64_t value);
std::string source_hash8(std::string_view url);
std::string icon_cache_name(std::string_view icon_key);

bool is_https_url(std::string_view url);
bool is_http_url(std::string_view url);
bool valid_url(std::string_view url);
bool valid_filename(std::string_view filename);
bool is_binary_filename(std::string_view filename);
bool valid_checksum(std::string_view checksum);
bool normalize_checksum(std::string_view checksum, std::string *out);
bool has_elf_header(const void *data, std::size_t size);

std::string slug(std::string_view name);
int version_compare(std::string_view first, std::string_view second);
int compare_versions(std::string_view first, std::string_view first_update,
                     std::string_view second, std::string_view second_update);

bool is_system_or_conflicting_name(std::string_view name);
bool autostart_allowed(std::string_view filename);

}
