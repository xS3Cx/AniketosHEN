#include <onion/payload_catalog.hpp>

#include <onion/payload_catalog_policy.hpp>

#include <cJSON.hpp>

#include <cctype>
#include <string>
#include <utility>

namespace onion::payload_catalog {
namespace {

std::string optional_string(const cJSON *object, const char *key) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsString(value) || !value->valuestring)
    return {};
  return trim(value->valuestring);
}

bool required_string(const cJSON *object, const char *key, std::string *out) {
  const std::string value = optional_string(object, key);
  if (value.empty())
    return false;
  *out = value;
  return true;
}

std::string filename_from_url(std::string_view url) {
  const std::size_t end = url.find_first_of("?#");
  const std::string_view clean = url.substr(0, end);
  const std::size_t slash = clean.find_last_of('/');
  if (slash == std::string_view::npos)
    return {};
  return std::string(clean.substr(slash + 1));
}

bool parse_item(const cJSON *entry, Catalog *catalog) {
  if (!cJSON_IsObject(entry))
    return false;
  Item item;
  std::string raw_name;
  if (!required_string(entry, "name", &raw_name))
    return false;
  item.name = truncate_utf8(raw_name, kMaxItemName);
  std::string raw_url;
  if (!required_string(entry, "url", &raw_url))
    return false;
  if (raw_url.size() > kMaxItemUrl || !valid_url(raw_url))
    return false;
  item.url = std::move(raw_url);
  item.insecure = is_http_url(item.url);
  std::string id = optional_string(entry, "id");
  item.id = id.empty() ? slug(item.name) : truncate_utf8(id, kMaxItemName);
  if (item.id.empty())
    return false;
  item.filename = optional_string(entry, "filename");
  if (item.filename.empty())
    item.filename = filename_from_url(item.url);
  if (!item.filename.empty() && !valid_filename(item.filename))
    return false;
  if (item.filename.size() > kMaxFilename)
    return false;
  item.source = truncate_utf8(optional_string(entry, "source"), kMaxItemUrl);
  item.description =
      truncate_utf8(optional_string(entry, "description"), kMaxDescription);
  item.last_update =
      truncate_utf8(optional_string(entry, "last_update"), 64);
  item.version = truncate_utf8(optional_string(entry, "version"), 64);
  item.category = truncate_utf8(optional_string(entry, "category"), kMaxCategory);
  item.icon_key = truncate_utf8(optional_string(entry, "icon_key"), 512);
  const std::string raw_checksum = optional_string(entry, "checksum");
  if (!raw_checksum.empty()) {
    std::string normalized;
    if (!normalize_checksum(raw_checksum, &normalized))
      return false;
    item.checksum = std::move(normalized);
    item.has_checksum = true;
  }
  item.unsupported_binary = is_binary_filename(item.name) ||
                            is_binary_filename(item.filename);
  catalog->items.push_back(std::move(item));
  return true;
}

}

std::string truncate_utf8(std::string_view text, std::size_t maximum) {
  if (text.size() <= maximum)
    return std::string(text);
  std::size_t end = maximum;
  while (end > 0 &&
         (static_cast<unsigned char>(text[end]) & 0xc0u) == 0x80u)
    --end;
  return std::string(text.substr(0, end));
}

std::string item_match_key(const Item &item, std::string_view source_url) {
  if (!item.source.empty())
    return item.source;
  return std::string(source_url) + "#" + item.id;
}

ParseResult parse_catalog(std::string_view json) {
  ParseResult result;
  if (json.empty() || json.size() > kMaxCatalogBytes) {
    result.error = "size";
    return result;
  }
  const std::string text(json);
  const char *end = nullptr;
  cJSON *root = cJSON_ParseWithOpts(text.c_str(), &end, 0);
  if (root) {
    while (end && *end &&
           std::isspace(static_cast<unsigned char>(*end)))
      ++end;
    if (end && *end) {
      cJSON_Delete(root);
      root = nullptr;
    }
  }
  if (!root) {
    result.error = "json";
    return result;
  }
  const cJSON *items = root;
  if (cJSON_IsObject(root)) {
    result.catalog.name = truncate_utf8(optional_string(root, "name"), kMaxNameLength);
    result.catalog.icons_url =
        truncate_utf8(optional_string(root, "icons_url"), kMaxItemUrl);
    items = cJSON_GetObjectItemCaseSensitive(root, "payloads");
    if (!items) {
      cJSON_Delete(root);
      result.error = "payloads";
      return result;
    }
  }
  if (!cJSON_IsArray(items)) {
    cJSON_Delete(root);
    result.error = "array";
    return result;
  }
  const int count = cJSON_GetArraySize(items);
  if (count < 0 || static_cast<std::size_t>(count) > kMaxItems) {
    cJSON_Delete(root);
    result.error = "items";
    return result;
  }
  result.catalog.items.reserve(static_cast<std::size_t>(count));
  const cJSON *entry = nullptr;
  cJSON_ArrayForEach(entry, items) {
    if (!parse_item(entry, &result.catalog)) {
      ++result.skipped;
      continue;
    }
    if (result.catalog.items.size() >= kMaxItems)
      break;
  }
  cJSON_Delete(root);
  if (result.catalog.items.empty() && result.skipped > 0) {
    result.error = "entries";
    return result;
  }
  result.ok = true;
  return result;
}

}
