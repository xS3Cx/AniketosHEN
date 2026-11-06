#include "payload_center/payload_center_icons.hpp"

#include "payload_center/payload_center_store.hpp"
#include "tile_picture.hpp"

#include "cheats/sync/http_transport_ps5.hpp"

#include <onion/payload_catalog_policy.hpp>

extern "C" {
#include <mc4/base64.h>
}

#include <cJSON.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace onion::payload_center {
namespace {

constexpr std::size_t kMaxIconBytes = 4u * 1024u * 1024u;
constexpr long long kIconRefreshSeconds = 7ll * 24ll * 60ll * 60ll;

bool fetch_bytes(const std::string &url, std::vector<uint8_t> *out) {
  using onion::cheats::sync::HttpRequest;
  using onion::cheats::sync::Ps5HttpTransport;
  using onion::cheats::sync::SyncStatus;
  out->clear();
  HttpRequest request;
  request.url = url.c_str();
  request.timeout_ms = 30000;
  request.max_body_bytes = kMaxIconBytes;
  Ps5HttpTransport http;
  const SyncStatus status = http.perform(request, [&](const void *data,
                                                      std::size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    out->insert(out->end(), bytes, bytes + size);
    return SyncStatus::Ok;
  });
  return status == SyncStatus::Ok && !out->empty() &&
         out->size() <= kMaxIconBytes;
}

bool decode_data_uri(const std::string &uri, std::vector<uint8_t> *out) {
  if (uri.rfind("data:", 0) != 0)
    return false;
  const std::size_t comma = uri.find(',');
  if (comma == std::string::npos)
    return false;
  const std::string header = uri.substr(5, comma - 5);
  if (header.find(";base64") == std::string::npos)
    return false;
  const std::string payload = uri.substr(comma + 1);
  if (payload.empty() || payload.size() > kMaxIconBytes * 2)
    return false;
  std::size_t decoded_size = 0;
  unsigned char *decoded = base64_decode(
      reinterpret_cast<const unsigned char *>(payload.data()), payload.size(),
      &decoded_size);
  if (!decoded)
    return false;
  if (decoded_size > kMaxIconBytes) {
    free(decoded);
    return false;
  }
  out->assign(decoded, decoded + decoded_size);
  free(decoded);
  return true;
}

bool write_icon(const std::string &path, const std::vector<uint8_t> &bytes) {
  if (bytes.empty())
    return false;
  return write_text_atomic(path.c_str(),
                           std::string(reinterpret_cast<const char *>(bytes.data()),
                                       bytes.size()));
}

std::string lookup_icon_value(const std::map<std::string, std::string> &icons,
                              const std::string &key) {
  const auto found = icons.find(key);
  return found == icons.end() ? std::string{} : found->second;
}

bool map_from_json(const std::vector<uint8_t> &body,
                   std::map<std::string, std::string> *icons) {
  if (body.empty())
    return false;
  cJSON *root = cJSON_ParseWithLength(
      reinterpret_cast<const char *>(body.data()), body.size());
  if (!root)
    return false;
  const cJSON *object = root;
  if (cJSON_IsObject(root)) {
    const cJSON *wrapper = cJSON_GetObjectItemCaseSensitive(root, "icons");
    if (cJSON_IsObject(wrapper))
      object = wrapper;
  }
  bool found = false;
  if (cJSON_IsObject(object)) {
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, object) {
      if (!entry->string)
        continue;
      if (cJSON_IsString(entry) && entry->valuestring) {
        (*icons)[entry->string] = entry->valuestring;
        found = true;
      } else if (cJSON_IsObject(entry)) {
        for (const char *key : {"data", "icon", "url"}) {
          const cJSON *value =
              cJSON_GetObjectItemCaseSensitive(entry, key);
          if (cJSON_IsString(value) && value->valuestring) {
            (*icons)[entry->string] = value->valuestring;
            found = true;
            break;
          }
        }
      }
    }
  }
  cJSON_Delete(root);
  return found;
}

bool icon_body_for(const std::string &key, const std::string &icons_url,
                   const std::map<std::string, std::string> &icons,
                   const std::vector<uint8_t> &icons_body,
                   std::vector<uint8_t> *out) {
  if (key.rfind("data:image/", 0) == 0)
    return decode_data_uri(key, out);
  const std::string mapped = lookup_icon_value(icons, key);
  if (!mapped.empty()) {
    if (mapped.rfind("data:image/", 0) == 0)
      return decode_data_uri(mapped, out);
    if (onion::payload_catalog::is_https_url(mapped))
      return fetch_bytes(mapped, out);
  }
  if (decode_data_uri(icons_url, out))
    return true;
  if (!icons_body.empty()) {
    if (icons_url.find(".json") == std::string::npos &&
        icons_url.find('{') == std::string::npos) {
      const TilePicture::Format format =
          TilePicture::Sniff(icons_body.data(), icons_body.size());
      if (format == TilePicture::Format::Png ||
          format == TilePicture::Format::Jpeg) {
        *out = icons_body;
        return true;
      }
    }
  }
  if (onion::payload_catalog::is_https_url(icons_url)) {
    std::string base = icons_url;
    if (!base.empty() && base.back() != '/')
      base.push_back('/');
    if (fetch_bytes(base + key + ".png", out))
      return true;
  }
  return false;
}

}

bool icons_refresh_due(const std::string &source_url) {
  IconStatus status;
  if (!read_icons_status(source_url, &status))
    return true;
  const long long now = static_cast<long long>(std::time(nullptr));
  return status.fetched_at <= 0 ||
         now - status.fetched_at >= kIconRefreshSeconds;
}

bool refresh_icons(const std::string &source_url, const std::string &icons_url,
                   const std::vector<IconSourceItem> &items,
                   std::string *error) {
  if (!ensure_layout()) {
    if (error)
      *error = "io";
    return false;
  }
  std::map<std::string, std::string> icons;
  std::vector<uint8_t> icons_body;
  if (!icons_url.empty() && onion::payload_catalog::is_https_url(icons_url))
    (void)fetch_bytes(icons_url, &icons_body);
  (void)map_from_json(icons_body, &icons);

  std::set<std::string> seen;
  std::size_t written = 0;
  for (const IconSourceItem &item : items) {
    if (item.icon_key.empty() || !seen.insert(item.icon_key).second)
      continue;
    const std::string path = icon_path(item.icon_key);
    std::vector<uint8_t> bytes;
    bool usable = icon_body_for(item.icon_key, icons_url, icons, icons_body,
                                &bytes);
    if (usable) {
      const TilePicture::Format format =
          TilePicture::Sniff(bytes.data(), bytes.size());
      usable = (format == TilePicture::Format::Png ||
                format == TilePicture::Format::Jpeg) &&
               TilePicture::FitsTile(bytes.data(), bytes.size(), format);
    }
    if (!usable) {
      struct stat st {};
      if (stat(path.c_str(), &st) == 0 && st.st_size > 0)
        continue;
      bytes = TilePicture::LetterTile(
          TilePicture::TileLetter(item.name, item.url), item.url);
    }
    if ((mkdir(kIconDir, 0755) != 0 && errno != EEXIST) ||
        !write_icon(path, bytes))
      continue;
    ++written;
  }
  IconStatus status;
  status.url = icons_url;
  status.fetched_at = static_cast<long long>(std::time(nullptr));
  status.count = written;
  if (!write_icons_status(source_url, status)) {
    if (error)
      *error = "io";
    return false;
  }
  return true;
}

}
