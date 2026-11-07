#pragma once

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/update_manifest.hpp>

#include <onion_cjson.hpp>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace onion::update {

inline constexpr const char *kUpdateDir = ONION_DIST_DATA_ROOT "/update";
inline constexpr const char *kUpdateTmpDir = ONION_DIST_DATA_ROOT "/update/tmp";
inline constexpr const char *kUpdateElfPath =
    ONION_DIST_DATA_ROOT "/update/AniketosHEN.elf";
inline constexpr const char *kUpdateReadyPath =
    ONION_DIST_DATA_ROOT "/update/ready.json";
inline constexpr const char *kUpdateCheckPath =
    ONION_DIST_DATA_ROOT "/update/check.json";
/* The version the boot check last announced: told once, not every boot. */
inline constexpr const char *kUpdateNotifiedPath =
    ONION_DIST_DATA_ROOT "/update/notified.txt";

struct ReadyInfo {
  std::string version;
  std::string sha256;
  std::string summary;
  std::uint64_t size = 0;
  long long downloaded_at = 0;
};

struct CheckInfo {
  std::string channel;
  std::string error;
  long long fetched_at = 0;
};

inline std::string manifest_cache_path(std::string_view channel) {
  const std::string safe =
      valid_channel(channel) ? std::string(channel) : std::string(kStableChannel);
  return std::string(kUpdateDir) + "/latest-" + safe + ".json";
}

inline bool ensure_update_layout() {
  const char *directories[] = {ONION_DIST_DATA_ROOT, kUpdateDir, kUpdateTmpDir};
  for (const char *directory : directories) {
    if (!mkdir_tree(directory))
      return false;
  }
  return true;
}

inline bool read_text_limited(const std::string &path, std::string *out,
                              std::size_t maximum) {
  if (!out)
    return false;
  out->clear();
  const int fd = open(path.c_str(), O_RDONLY);
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
    const ssize_t n =
        read(fd, out->data() + read_bytes, out->size() - read_bytes);
    if (n <= 0)
      break;
    read_bytes += static_cast<std::size_t>(n);
  }
  close(fd);
  if (read_bytes != out->size()) {
    out->clear();
    return false;
  }
  return true;
}

inline bool write_all_bytes(int fd, const char *data, std::size_t size) {
  std::size_t written = 0;
  while (written < size) {
    const ssize_t n = write(fd, data + written, size - written);
    if (n <= 0)
      return false;
    written += static_cast<std::size_t>(n);
  }
  return true;
}

inline std::string directory_of(const std::string &path) {
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

inline void sync_directory(const std::string &directory) {
  if (directory.empty())
    return;
  const int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd >= 0) {
    fsync(fd);
    close(fd);
  }
}

inline bool write_text_atomic(const std::string &path, const std::string &body) {
  if (path.empty())
    return false;
  const std::string directory = directory_of(path);
  if (!directory.empty() && !mkdir_tree(directory.c_str()))
    return false;
  std::string pattern = path + ".tmp.XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  const int fd = mkstemp(buffer.data());
  if (fd < 0)
    return false;
  bool ok = write_all_bytes(fd, body.data(), body.size());
  if (ok && fsync(fd) != 0)
    ok = false;
  if (close(fd) != 0)
    ok = false;
  if (!ok) {
    unlink(buffer.data());
    return false;
  }
  chmod(buffer.data(), 0644);
  if (rename(buffer.data(), path.c_str()) != 0) {
    unlink(buffer.data());
    return false;
  }
  sync_directory(directory);
  return true;
}

inline bool write_ready_info(const ReadyInfo &info) {
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return false;
  }
  const bool ok =
      cJSON_AddStringToObject(root, "version", info.version.c_str()) &&
      cJSON_AddStringToObject(root, "sha256", info.sha256.c_str()) &&
      cJSON_AddNumberToObject(root, "size",
                              static_cast<double>(info.size)) &&
      cJSON_AddStringToObject(root, "summary", info.summary.c_str()) &&
      cJSON_AddNumberToObject(root, "downloaded_at",
                              static_cast<double>(info.downloaded_at));
  const std::string body = ok ? onion_cjson::print_owned(root) : std::string{};
  if (!ok) {
    cJSON_Delete(root);
    return false;
  }
  return write_text_atomic(kUpdateReadyPath, body);
}

inline bool read_ready_info(ReadyInfo *out, std::string *error) {
  if (!out) {
    return false;
  }
  std::string body;
  if (!read_text_limited(kUpdateReadyPath, &body,
                         static_cast<std::size_t>(64 * 1024))) {
    if (error)
      *error = "io";
    return false;
  }
  onion_cjson::Root root(body);
  if (!root || !cJSON_IsObject(root.get())) {
    if (error)
      *error = "protocol";
    return false;
  }
  const char *version = onion_cjson::string_item(root.get(), "version", "");
  const char *sha256 = onion_cjson::string_item(root.get(), "sha256", "");
  const char *summary = onion_cjson::string_item(root.get(), "summary", "");
  const int size = onion_cjson::int_item(root.get(), "size", 0);
  const long long downloaded_at =
      static_cast<long long>(onion_cjson::int_item(root.get(), "downloaded_at", 0));
  if (!version || !version[0] || !sha256 || !sha256[0] || size <= 0) {
    if (error)
      *error = "protocol";
    return false;
  }
  out->version = version;
  out->sha256 = sha256;
  out->summary = summary ? summary : "";
  out->size = static_cast<std::uint64_t>(size);
  out->downloaded_at = downloaded_at;
  return true;
}

inline bool write_check_info(const CheckInfo &info) {
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return false;
  }
  const bool ok =
      cJSON_AddStringToObject(root, "channel", info.channel.c_str()) &&
      cJSON_AddStringToObject(root, "error", info.error.c_str()) &&
      cJSON_AddNumberToObject(root, "fetched_at",
                              static_cast<double>(info.fetched_at));
  const std::string body = ok ? onion_cjson::print_owned(root) : std::string{};
  if (!ok) {
    cJSON_Delete(root);
    return false;
  }
  return write_text_atomic(kUpdateCheckPath, body);
}

inline bool read_check_info(CheckInfo *out) {
  if (!out)
    return false;
  std::string body;
  if (!read_text_limited(kUpdateCheckPath, &body,
                         static_cast<std::size_t>(64 * 1024)))
    return false;
  onion_cjson::Root root(body);
  if (!root || !cJSON_IsObject(root.get()))
    return false;
  const char *channel = onion_cjson::string_item(root.get(), "channel", "");
  const char *error = onion_cjson::string_item(root.get(), "error", "");
  out->channel = channel ? channel : "";
  out->error = error ? error : "";
  out->fetched_at =
      static_cast<long long>(onion_cjson::int_item(root.get(), "fetched_at", 0));
  return true;
}

inline bool remove_downloaded_update() {
  bool removed = false;
  if (unlink(kUpdateElfPath) == 0)
    removed = true;
  if (unlink(kUpdateReadyPath) == 0)
    removed = true;
  sync_directory(kUpdateDir);
  return removed;
}

} // namespace onion::update
