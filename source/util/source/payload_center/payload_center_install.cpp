#include "payload_center/payload_center_install.hpp"

#include "cheats/sync/http_transport_ps5.hpp"
#include "payload_center/payload_center_store.hpp"

#include <onion/payload_catalog_policy.hpp>
#include <onion/payload_identity.h>

extern "C" {
#include <sha256.h>
}

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

namespace onion::payload_center {
namespace {

constexpr uint64_t kMaxDownloadBytes = 128ull * 1024ull * 1024ull;
constexpr uint64_t kFreeSpaceMargin = 64ull * 1024ull * 1024ull;

std::string status_error(onion::cheats::sync::SyncStatus status) {
  using onion::cheats::sync::SyncStatus;
  switch (status) {
  case SyncStatus::Ok:
    return {};
  case SyncStatus::Network:
  case SyncStatus::Unavailable:
    return "network";
  case SyncStatus::Io:
    return "io";
  case SyncStatus::Protocol:
    return "protocol";
  case SyncStatus::NoSpace:
    return "space";
  case SyncStatus::Busy:
    return "busy";
  case SyncStatus::Rejected:
    return "rejected";
  case SyncStatus::Tls:
    return "tls";
  case SyncStatus::Clock:
    return "clock";
  case SyncStatus::Cancelled:
    return "cancelled";
  }
  return "network";
}

bool enough_space(const char *path) {
  struct statvfs st {};
  if (statvfs(path, &st) != 0)
    return true;
  const uint64_t available =
      static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_frsize);
  return available >= kFreeSpaceMargin;
}

std::string hex_digest(const uint8_t *digest) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(64, '0');
  for (std::size_t i = 0; i < 32; ++i) {
    out[i * 2] = kHex[digest[i] >> 4];
    out[i * 2 + 1] = kHex[digest[i] & 0x0f];
  }
  return out;
}

bool write_all(int fd, const uint8_t *data, std::size_t size) {
  std::size_t written = 0;
  while (written < size) {
    const ssize_t n = write(fd, data + written, size - written);
    if (n <= 0)
      return false;
    written += static_cast<std::size_t>(n);
  }
  return true;
}

void fsync_directory(const std::string &directory) {
  const int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd >= 0) {
    fsync(fd);
    close(fd);
  }
}

std::string choose_target(const InstallRequest &request, std::string *error) {
  const std::string base =
      std::string(kPayloadDir) + "/" + onion::payload_catalog::slug(request.name) +
      ".elf";
  std::string target = base;
  struct stat st {};
  if (lstat(target.c_str(), &st) != 0)
    return target;
  ItemMeta existing;
  if (!read_meta(target, &existing)) {
    if (!request.replace) {
      if (error)
        *error = "rejected";
      return {};
    }
    return target;
  }
  if (existing.match_key == request.match_key)
    return target;
  const std::string suffix =
      onion::payload_catalog::source_hash8(request.source_url.empty()
                                               ? request.url
                                               : request.source_url);
  target = std::string(kPayloadDir) + "/" +
           onion::payload_catalog::slug(request.name) + "-" + suffix + ".elf";
  if (lstat(target.c_str(), &st) == 0) {
    ItemMeta alternate;
    if (!read_meta(target, &alternate) && !request.replace) {
      if (error)
        *error = "rejected";
      return {};
    }
  }
  return target;
}

}

InstallResult install_payload(const InstallRequest &request,
                              InstallProgressFn progress, void *progress_user,
                              InstallCancelFn cancelled, void *cancel_user) {
  using onion::cheats::sync::HttpRequest;
  using onion::cheats::sync::Ps5HttpTransport;
  using onion::cheats::sync::SyncStatus;
  InstallResult result;
  const auto report = [&](const char *phase, uint64_t completed,
                          uint64_t total) {
    if (progress)
      progress(phase, completed, total, progress_user);
  };
  const auto is_cancelled = [&]() {
    return cancelled && cancelled(cancel_user);
  };
  const auto fail = [&](const char *error) {
    result.ok = false;
    result.error = error;
    return result;
  };

  report("start", 0, 0);
  const std::string validation = validate_install_request(request);
  if (!validation.empty())
    return fail(validation.c_str());
  if (!ensure_layout())
    return fail("io");
  if (!enough_space(kPayloadDir))
    return fail("space");
  std::string target_error;
  const std::string target = choose_target(request, &target_error);
  if (target.empty())
    return fail(target_error.empty() ? "rejected" : target_error.c_str());

  std::string temp_pattern = std::string(kTmpDir) + "/" +
                             onion::payload_catalog::slug(request.name) +
                             "-XXXXXX";
  std::vector<char> temp_buffer(temp_pattern.begin(), temp_pattern.end());
  temp_buffer.push_back('\0');
  const int fd = mkstemp(temp_buffer.data());
  if (fd < 0)
    return fail("io");
  const std::string temp_path(temp_buffer.data());

  SHA256_CTX sha{};
  sha256_init(&sha);
  std::array<uint8_t, 16> header{};
  std::size_t header_size = 0;
  uint64_t received = 0;
  bool write_failed = false;

  struct ProgressJob {
    InstallProgressFn progress;
    void *user;
  } job{progress, progress_user};

  HttpRequest http_request;
  http_request.url = request.url.c_str();
  http_request.timeout_ms = 120000;
  http_request.max_body_bytes = static_cast<std::size_t>(kMaxDownloadBytes);
  http_request.should_cancel = cancelled;
  http_request.cancel_user = cancel_user;
  http_request.on_progress = +[](std::size_t bytes, std::size_t total,
                                 void *user) {
    auto *job = static_cast<ProgressJob *>(user);
    if (job && job->progress)
      job->progress("download", bytes, total, job->user);
  };
  http_request.progress_user = &job;

  report("download", 0, 0);
  Ps5HttpTransport transport;
  const SyncStatus status = transport.perform(
      http_request, [&](const void *data, std::size_t size) {
        if (write_failed || is_cancelled())
          return SyncStatus::Cancelled;
        const auto *bytes = static_cast<const uint8_t *>(data);
        if (received + size > kMaxDownloadBytes)
          return SyncStatus::NoSpace;
        if (header_size < header.size()) {
          const std::size_t take =
              std::min(header.size() - header_size, size);
          std::memcpy(header.data() + header_size, bytes, take);
          header_size += take;
        }
        sha256_update(&sha, bytes, size);
        received += size;
        if (!write_all(fd, bytes, size)) {
          write_failed = true;
          return SyncStatus::Io;
        }
        return SyncStatus::Ok;
      });

  if (write_failed || status == SyncStatus::Io) {
    close(fd);
    unlink(temp_path.c_str());
    return fail("io");
  }
  if (status != SyncStatus::Ok) {
    close(fd);
    unlink(temp_path.c_str());
    return fail(status_error(status).c_str());
  }
  if (is_cancelled()) {
    close(fd);
    unlink(temp_path.c_str());
    return fail("cancelled");
  }
  const bool synced = fsync(fd) == 0;
  const bool closed = close(fd) == 0;
  if (!synced || !closed) {
    unlink(temp_path.c_str());
    return fail("io");
  }

  report("verify", received, received);
  uint8_t digest[32] = {};
  sha256_final(&sha, digest);
  const std::string actual = hex_digest(digest);
  if (request.has_checksum) {
    std::string expected;
    if (!onion::payload_catalog::normalize_checksum(request.checksum, &expected) ||
        expected != actual) {
      unlink(temp_path.c_str());
      return fail("hash");
    }
  }
  if (header_size < header.size() ||
      !onion::payload_catalog::has_elf_header(header.data(), header_size)) {
    unlink(temp_path.c_str());
    return fail("elf");
  }
  if (is_cancelled()) {
    unlink(temp_path.c_str());
    return fail("cancelled");
  }

  report("install", received, received);
  if (chmod(temp_path.c_str(), 0644) != 0) {
    unlink(temp_path.c_str());
    return fail("io");
  }
  if (rename(temp_path.c_str(), target.c_str()) != 0) {
    unlink(temp_path.c_str());
    return fail("io");
  }
  fsync_directory(kPayloadDir);

  ItemMeta meta;
  meta.id = request.item_id;
  meta.match_key = request.match_key;
  meta.source_url = request.source_url;
  meta.source_name = request.source_name;
  meta.name = request.name;
  meta.item_version = request.version;
  meta.filename = request.filename;
  meta.checksum = actual;
  meta.last_update = request.last_update;
  meta.category = request.category;
  meta.icon_key = request.icon_key;
  meta.installed_at = static_cast<long long>(time(nullptr));
  if (!write_meta(target, meta)) {
    unlink(target.c_str());
    return fail("io");
  }

  char identity[ONION_PAYLOAD_IDENTITY_SIZE] = {};
  if (!onion_payload_identity_from_path(target.c_str(), identity,
                                        sizeof(identity))) {
    unlink(target.c_str());
    unlink(meta_path(target).c_str());
    return fail("io");
  }
  report("cleanup", received, received);
  result.ok = true;
  result.path = target;
  result.identity = identity;
  return result;
}

InstallResult remove_payload_by_identity(const std::string &identity) {
  InstallResult result;
  std::string path;
  if (identity.size() != 17 || identity[0] != 'p') {
    result.error = "rejected";
    return result;
  }
  for (std::size_t i = 1; i < identity.size(); ++i) {
    const char c = identity[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      result.error = "rejected";
      return result;
    }
  }
  if (!find_payload_by_identity(identity, &path)) {
    result.error = "rejected";
    return result;
  }
  const std::array<std::string, 4> sidecars = {
      path, path + ".json", path + ".meta.json", path + ".auto_start"};
  bool removed = false;
  for (const std::string &file : sidecars) {
    if (unlink(file.c_str()) == 0)
      removed = true;
  }
  if (!removed) {
    result.error = "io";
    return result;
  }
  fsync_directory(kPayloadDir);
  result.ok = true;
  result.path = path;
  result.identity = identity;
  return result;
}

}
