#include "payload_center/payload_center_store.hpp"

#include "cheats/sync/http_transport_ps5.hpp"

#include <onion/payload_identity.h>

#include <cJSON.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::payload_center {
namespace {

bool make_directory(const char *path) {
  if (!path || !path[0])
    return false;
  if (mkdir(path, 0755) == 0)
    return true;
  return errno == EEXIST;
}

std::string directory_of(const std::string &path) {
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

bool write_all(int fd, const char *data, std::size_t size) {
  std::size_t written = 0;
  while (written < size) {
    const ssize_t n = write(fd, data + written, size - written);
    if (n <= 0)
      return false;
    written += static_cast<std::size_t>(n);
  }
  return true;
}

void sync_directory(const std::string &directory) {
  if (directory.empty())
    return;
  const int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd >= 0) {
    fsync(fd);
    close(fd);
  }
}

long long now_seconds() { return static_cast<long long>(std::time(nullptr)); }

bool parse_number(const cJSON *root, const char *key, long long *out) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(value))
    return false;
  *out = static_cast<long long>(value->valuedouble);
  return true;
}

std::string json_string(const cJSON *root, const char *key) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

}

bool ensure_layout() {
  if (!make_directory("/data/AniketosHEN"))
    return false;
  return make_directory(kRoot) && make_directory(kIconDir) &&
         make_directory(kCacheDir) && make_directory(kTmpDir) &&
         make_directory(kPayloadDir);
}

bool read_text_file(const char *path, std::string *out, std::size_t maximum) {
  if (!path || !out)
    return false;
  out->clear();
  const int fd = open(path, O_RDONLY);
  if (fd < 0)
    return false;
  struct stat st {};
  if (fstat(fd, &st) != 0 || st.st_size < 0 ||
      static_cast<std::size_t>(st.st_size) > maximum) {
    close(fd);
    return false;
  }
  out->resize(static_cast<std::size_t>(st.st_size));
  std::size_t read_bytes = 0;
  while (read_bytes < out->size()) {
    const ssize_t n = read(fd, out->data() + read_bytes, out->size() - read_bytes);
    if (n <= 0)
      break;
    read_bytes += static_cast<std::size_t>(n);
  }
  close(fd);
  if (read_bytes != out->size())
    out->clear();
  return read_bytes == out->size();
}

bool write_text_atomic(const char *path, const std::string &body) {
  if (!path || !path[0])
    return false;
  const std::string full(path);
  const std::string directory = directory_of(full);
  if (!directory.empty())
    make_directory(directory.c_str());
  std::string pattern = full + ".tmp.XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  const int fd = mkstemp(buffer.data());
  if (fd < 0)
    return false;
  bool ok = write_all(fd, body.data(), body.size());
  ok = ok && fsync(fd) == 0;
  ok = ok && close(fd) == 0;
  if (!ok) {
    unlink(buffer.data());
    return false;
  }
  chmod(buffer.data(), 0644);
  if (rename(buffer.data(), full.c_str()) != 0) {
    unlink(buffer.data());
    return false;
  }
  sync_directory(directory);
  return true;
}

bool remove_tree_entry(const char *path) {
  return path && unlink(path) == 0;
}

std::string cache_catalog_path(const std::string &source_url) {
  return std::string(kCacheDir) + "/catalog-" +
         onion::payload_catalog::source_hash8(source_url) + ".json";
}

std::string cache_status_path(const std::string &source_url) {
  return std::string(kCacheDir) + "/catalog-" +
         onion::payload_catalog::source_hash8(source_url) + ".status.json";
}

std::string cache_icons_status_path(const std::string &source_url) {
  return std::string(kCacheDir) + "/icons-" +
         onion::payload_catalog::source_hash8(source_url) + ".status.json";
}

std::string icon_path(std::string_view icon_key) {
  return std::string(kIconDir) + "/" +
         onion::payload_catalog::icon_cache_name(icon_key) + ".png";
}

std::string meta_path(const std::string &payload_path) {
  return payload_path + ".meta.json";
}

bool read_catalog_cache(const std::string &source_url, std::string *body,
                        CacheStatus *status) {
  if (!read_text_file(cache_catalog_path(source_url).c_str(), body,
                      onion::payload_catalog::kMaxCatalogBytes))
    return false;
  std::string raw;
  if (!read_text_file(cache_status_path(source_url).c_str(), &raw, 64 * 1024))
    return true;
  cJSON *root = cJSON_Parse(raw.c_str());
  if (!root)
    return true;
  if (status) {
    status->url = json_string(root, "url");
    parse_number(root, "fetched_at", &status->fetched_at);
    long long bytes = 0;
    parse_number(root, "bytes", &bytes);
    status->bytes = static_cast<std::size_t>(std::max<long long>(bytes, 0));
    status->sha256 = json_string(root, "sha256");
  }
  cJSON_Delete(root);
  return true;
}

bool write_catalog_cache(const std::string &source_url,
                         const std::string &body, const CacheStatus &status) {
  if (!write_text_atomic(cache_catalog_path(source_url).c_str(), body))
    return false;
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return false;
  cJSON_AddStringToObject(root, "url", status.url.c_str());
  cJSON_AddNumberToObject(root, "fetched_at",
                          static_cast<double>(status.fetched_at));
  cJSON_AddNumberToObject(root, "bytes",
                          static_cast<double>(status.bytes));
  cJSON_AddStringToObject(root, "sha256", status.sha256.c_str());
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text)
    return false;
  const std::string out = text;
  cJSON_free(text);
  return write_text_atomic(cache_status_path(source_url).c_str(), out);
}

bool read_icons_status(const std::string &source_url, IconStatus *status) {
  std::string raw;
  if (!read_text_file(cache_icons_status_path(source_url).c_str(), &raw,
                      64 * 1024))
    return false;
  cJSON *root = cJSON_Parse(raw.c_str());
  if (!root)
    return false;
  if (status) {
    status->url = json_string(root, "url");
    parse_number(root, "fetched_at", &status->fetched_at);
    long long count = 0;
    parse_number(root, "count", &count);
    status->count = static_cast<std::size_t>(std::max<long long>(count, 0));
  }
  cJSON_Delete(root);
  return true;
}

bool write_icons_status(const std::string &source_url,
                        const IconStatus &status) {
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return false;
  cJSON_AddStringToObject(root, "url", status.url.c_str());
  cJSON_AddNumberToObject(root, "fetched_at",
                          static_cast<double>(status.fetched_at));
  cJSON_AddNumberToObject(root, "count",
                          static_cast<double>(status.count));
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text)
    return false;
  const std::string out = text;
  cJSON_free(text);
  return write_text_atomic(cache_icons_status_path(source_url).c_str(), out);
}

bool read_meta(const std::string &payload_path, ItemMeta *meta) {
  std::string raw;
  if (!read_text_file(meta_path(payload_path).c_str(), &raw, 128 * 1024))
    return false;
  cJSON *root = cJSON_Parse(raw.c_str());
  if (!root)
    return false;
  ItemMeta parsed;
  long long schema = 0;
  if (!parse_number(root, "version", &schema) || schema != 1) {
    cJSON_Delete(root);
    return false;
  }
  parsed.id = json_string(root, "id");
  parsed.match_key = json_string(root, "match_key");
  parsed.source_url = json_string(root, "source_url");
  parsed.source_name = json_string(root, "source_name");
  parsed.name = json_string(root, "name");
  parsed.item_version = json_string(root, "item_version");
  parsed.filename = json_string(root, "filename");
  parsed.checksum = json_string(root, "checksum");
  parsed.last_update = json_string(root, "last_update");
  parsed.category = json_string(root, "category");
  parse_number(root, "installed_at", &parsed.installed_at);
  cJSON_Delete(root);
  if (parsed.name.empty() || parsed.match_key.empty())
    return false;
  if (meta)
    *meta = std::move(parsed);
  return true;
}

bool write_meta(const std::string &payload_path, const ItemMeta &meta) {
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return false;
  cJSON_AddNumberToObject(root, "version", 1);
  cJSON_AddStringToObject(root, "id", meta.id.c_str());
  cJSON_AddStringToObject(root, "match_key", meta.match_key.c_str());
  cJSON_AddStringToObject(root, "source_url", meta.source_url.c_str());
  cJSON_AddStringToObject(root, "source_name", meta.source_name.c_str());
  cJSON_AddStringToObject(root, "name", meta.name.c_str());
  cJSON_AddStringToObject(root, "item_version", meta.item_version.c_str());  cJSON_AddStringToObject(root, "filename", meta.filename.c_str());
  cJSON_AddStringToObject(root, "checksum", meta.checksum.c_str());
  cJSON_AddStringToObject(root, "last_update", meta.last_update.c_str());
  cJSON_AddStringToObject(root, "category", meta.category.c_str());
  cJSON_AddNumberToObject(root, "installed_at",
                          static_cast<double>(meta.installed_at));
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text)
    return false;
  const std::string body = text;
  cJSON_free(text);
  return write_text_atomic(meta_path(payload_path).c_str(), body);
}

std::vector<std::string> list_payload_files() {
  std::vector<std::string> out;
  DIR *dir = opendir(kPayloadDir);
  if (!dir)
    return out;
  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    if (entry->d_name[0] == '.')
      continue;
    const std::size_t length = std::strlen(entry->d_name);
    if (length < 5 || std::strcmp(entry->d_name + length - 4, ".elf") != 0)
      continue;
    out.emplace_back(std::string(kPayloadDir) + "/" + entry->d_name);
  }
  closedir(dir);
  std::sort(out.begin(), out.end());
  return out;
}

bool find_payload_by_identity(const std::string &identity, std::string *path) {
  for (const std::string &candidate : list_payload_files()) {
    char id[ONION_PAYLOAD_IDENTITY_SIZE] = {};
    if (!onion_payload_identity_from_path(candidate.c_str(), id, sizeof(id)))
      continue;
    if (identity == id) {
      if (path)
        *path = candidate;
      return true;
    }
  }
  return false;
}

bool fetch_source_catalog(const std::string &source_url, std::string *body,
                          std::string *error) {
  using onion::cheats::sync::HttpRequest;
  using onion::cheats::sync::Ps5HttpTransport;
  using onion::cheats::sync::SyncStatus;
  const auto fetch = [&](const std::string &url) {
    body->clear();
    HttpRequest request;
    request.url = url.c_str();
    request.timeout_ms = 60000;
    request.max_body_bytes = onion::payload_catalog::kMaxCatalogBytes;
    Ps5HttpTransport http;
    const SyncStatus status = http.perform(request, [&](const void *data,
                                                        std::size_t size) {
      body->append(static_cast<const char *>(data), size);
      return SyncStatus::Ok;
    });
    return status;
  };
  SyncStatus status = fetch(source_url);
  if (status != SyncStatus::Ok &&
      source_url == onion::payload_catalog::kBuiltinSourceUrl) {
    status = fetch(std::string(onion::payload_catalog::kBuiltinSourceFallbackUrl));
  }
  if (status == SyncStatus::Ok)
    return true;
  if (error)
    *error = onion::cheats::sync::sync_status_name(status);
  return false;
}

}
