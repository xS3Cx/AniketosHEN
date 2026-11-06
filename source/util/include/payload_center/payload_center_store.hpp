#pragma once

#include <onion/payload_catalog.hpp>
#include <onion/payload_sources_json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace onion::payload_center {

inline constexpr const char *kRoot = "/data/AniketosHEN/payload_center";
inline constexpr const char *kIconDir = "/data/AniketosHEN/payload_center/icons";
inline constexpr const char *kCacheDir = "/data/AniketosHEN/payload_center/cache";
inline constexpr const char *kTmpDir = "/data/AniketosHEN/payload_center/cache/tmp";
inline constexpr const char *kSourcesPath =
    "/data/AniketosHEN/payload_center/sources.json";
inline constexpr const char *kPayloadDir = "/data/AniketosHEN/payloads";

struct CacheStatus {
  std::string url;
  long long fetched_at = 0;
  std::size_t bytes = 0;
  std::string sha256;
};

struct IconStatus {
  std::string url;
  long long fetched_at = 0;
  std::size_t count = 0;
};

struct ItemMeta {
  int version = 1;
  std::string id;
  std::string match_key;
  std::string source_url;
  std::string source_name;
  std::string name;
  std::string item_version;
  std::string filename;
  std::string checksum;
  std::string last_update;
  std::string category;
  long long installed_at = 0;
};

bool ensure_layout();
bool read_text_file(const char *path, std::string *out, std::size_t maximum);
bool write_text_atomic(const char *path, const std::string &body);
bool remove_tree_entry(const char *path);

std::string cache_catalog_path(const std::string &source_url);
std::string cache_status_path(const std::string &source_url);
std::string cache_icons_status_path(const std::string &source_url);
std::string icon_path(std::string_view icon_key);
std::string meta_path(const std::string &payload_path);

bool read_catalog_cache(const std::string &source_url, std::string *body,
                        CacheStatus *status);
bool write_catalog_cache(const std::string &source_url,
                         const std::string &body, const CacheStatus &status);
bool read_icons_status(const std::string &source_url, IconStatus *status);
bool write_icons_status(const std::string &source_url,
                        const IconStatus &status);

bool read_meta(const std::string &payload_path, ItemMeta *meta);
bool write_meta(const std::string &payload_path, const ItemMeta &meta);

std::vector<std::string> list_payload_files();
bool find_payload_by_identity(const std::string &identity, std::string *path);

bool fetch_source_catalog(const std::string &source_url, std::string *body,
                          std::string *error);

}
