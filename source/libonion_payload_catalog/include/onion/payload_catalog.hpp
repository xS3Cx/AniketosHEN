#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace onion::payload_catalog {

inline constexpr std::size_t kMaxCatalogBytes = 16u * 1024u * 1024u;
inline constexpr std::size_t kMaxItems = 4096;
inline constexpr std::size_t kMaxItemName = 256;
inline constexpr std::size_t kMaxDescription = 2048;
inline constexpr std::size_t kMaxCategory = 128;
inline constexpr std::size_t kMaxItemUrl = 2048;
inline constexpr std::size_t kMaxFilename = 256;

struct Source {
  std::string name;
  std::string url;
  bool builtin = false;
};

struct Item {
  std::string id;
  std::string name;
  std::string filename;
  std::string url;
  std::string source;
  std::string description;
  std::string last_update;
  std::string version;
  std::string category;
  std::string checksum;
  std::string icon_key;
  bool has_checksum = false;
  bool insecure = false;
  bool unsupported_binary = false;
};

struct Catalog {
  std::string name;
  std::string icons_url;
  std::vector<Item> items;
};

struct ParseResult {
  bool ok = false;
  Catalog catalog;
  std::string error;
  std::size_t skipped = 0;
};

ParseResult parse_catalog(std::string_view json);
std::string truncate_utf8(std::string_view text, std::size_t maximum);
std::string item_match_key(const Item &item, std::string_view source_url);

}
