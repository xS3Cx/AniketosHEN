#include "update/update_service.hpp"

#include "cheats/sync/http_transport_ps5.hpp"
#include "update/update_service.hpp"

#include <onion/net.h>
#include <onion/payload_catalog_policy.hpp>
#include <onion/platform.h>
#include <onion/settings.hpp>
#include <onion/update_store.hpp>

extern "C" {
#include <sha256.h>
}

#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>
#include <memory>
#include <pthread.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

extern onion::SettingsStore g_settings;

namespace onion::update {
namespace {

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

std::string hex_digest(const std::uint8_t *digest) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(64, '0');
  for (std::size_t i = 0; i < 32; ++i) {
    out[i * 2] = kHex[digest[i] >> 4];
    out[i * 2 + 1] = kHex[digest[i] & 0x0f];
  }
  return out;
}

std::string channel_from_setting() {
  return std::string(kStableChannel);
}

bool valid_update_elf(const std::uint8_t *header, std::size_t size) {
  if (!onion::payload_catalog::has_elf_header(header, size) || size < 20)
    return false;
  return header[18] == 0x3e && header[19] == 0x00;
}

struct WorkerArg {
  Service *service = nullptr;
  bool download = false;
  bool notify = false;
  std::string channel;
  std::uint32_t task_id = 0;
};

void *worker_thunk(void *raw) {
  std::unique_ptr<WorkerArg> arg(static_cast<WorkerArg *>(raw));
  if (!arg)
    return nullptr;
  if (arg->download)
    arg->service->worker_download(std::move(arg->channel), arg->task_id);
  else
    arg->service->worker_check(std::move(arg->channel), arg->notify,
                               arg->task_id);
  return nullptr;
}

bool cancel_thunk(void *user) {
  auto *service = static_cast<Service *>(user);
  return service && service->cancellation_requested();
}

bool fetch_manifest(const std::string &channel, std::string *body,
                    std::string *error) {
  using onion::cheats::sync::HttpRequest;
  using onion::cheats::sync::Ps5HttpTransport;
  using onion::cheats::sync::SyncStatus;
  if (!body || !valid_channel(channel)) {
    if (error)
      *error = "rejected";
    return false;
  }
  body->clear();
  HttpRequest request;
  const std::string_view url = manifest_url(channel);
  const std::string url_text(url);
  request.url = url_text.c_str();
  request.timeout_ms = 30000;
  request.max_body_bytes = kMaxManifestBytes;
  request.host_allow = "github.com";
  Ps5HttpTransport transport;
  const SyncStatus status = transport.perform(
      request, [&](const void *data, std::size_t size) {
        if (body->size() + size > kMaxManifestBytes)
          return SyncStatus::Protocol;
        body->append(static_cast<const char *>(data), size);
        return SyncStatus::Ok;
      });
  if (status != SyncStatus::Ok) {
    if (error)
      *error = status_error(status);
    return false;
  }
  if (body->empty()) {
    if (error)
      *error = "protocol";
    return false;
  }
  return true;
}

bool fetch_checked_manifest(const std::string &channel, Manifest *manifest,
                            std::string *body, std::string *error) {
  std::string fetched;
  std::string fetch_error;
  if (!fetch_manifest(channel, &fetched, &fetch_error)) {
    if (error)
      *error = fetch_error;
    return false;
  }
  ParseResult parsed = parse_manifest(fetched);
  if (!parsed.ok) {
    if (error)
      *error = parsed.error;
    return false;
  }
  if (parsed.manifest.channel != channel) {
    if (error)
      *error = "channel";
    return false;
  }
  *manifest = std::move(parsed.manifest);
  if (body)
    *body = std::move(fetched);
  return true;
}

bool load_cached_manifest(const std::string &channel, Manifest *manifest,
                          std::string *body) {
  std::string cached;
  if (!read_text_limited(manifest_cache_path(channel), &cached,
                         kMaxManifestBytes))
    return false;
  ParseResult parsed = parse_manifest(cached);
  if (!parsed.ok || parsed.manifest.channel != channel)
    return false;
  *manifest = std::move(parsed.manifest);
  if (body)
    *body = std::move(cached);
  return true;
}

void *boot_thread(void *) {
  bool ready = false;
  for (int attempt = 0; attempt < 60; ++attempt) {
    if (onion_net_has_ipv4()) {
      ready = true;
      break;
    }
    usleep(1000000);
  }
  if (!ready)
    return nullptr;
  const onion::Settings settings = g_settings.snapshot();
  if (!settings.update_auto_check)
    return nullptr;
  if (development_version(installed_version()))
    return nullptr;
  (void)Service::instance().check(channel_from_setting(),
                                  true);
  return nullptr;
}

} // namespace

Service &Service::instance() {
  static Service service;
  return service;
}

Service::Service() {
  status_.installed = installed_version();
  status_.channel = std::string(kStableChannel);
}

Service::~Service() = default;

Status Service::status() const {
  std::lock_guard<std::mutex> lock(mu_);
  return status_;
}

bool Service::cancellation_requested() const {
  std::lock_guard<std::mutex> lock(mu_);
  return cancel_requested_;
}

void Service::note_progress(const char *phase, std::uint64_t completed,
                            std::uint64_t total) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!running_ || cancel_requested_)
    return;
  if (phase && phase[0])
    status_.phase = phase;
  if (status_.phase == "start") {
    status_.progress = 0;
  } else if (total > 0) {
    const std::uint64_t percent = completed * 100ull / total;
    status_.progress = static_cast<int>(percent > 100ull ? 100ull : percent);
  } else if (status_.phase == "verify" || status_.phase == "save") {
    status_.progress = 100;
  } else {
    status_.progress = -1;
  }
}

void Service::finish(Status::State state, const std::string &error) {
  std::lock_guard<std::mutex> lock(mu_);
  status_.state = state;
  status_.error = error;
  if (state == Status::State::Ok) {
    if (status_.phase != "check")
      status_.phase = "save";
    status_.progress = 100;
  }
  running_ = false;
  cancel_requested_ = false;
}

Service::StartResult Service::check(const std::string &channel, bool notify,
                                    std::uint32_t *task_id) {
  return start(JobKind::Check, channel, notify, task_id);
}

Service::StartResult Service::download(const std::string &channel,
                                       std::uint32_t *task_id) {
  return start(JobKind::Download, channel, false, task_id);
}

Service::StartResult Service::start(JobKind kind, std::string channel,
                                    bool notify, std::uint32_t *task_id) {
  if (!valid_channel(channel))
    return StartResult::Rejected;
  (void)ensure_update_layout();
  std::lock_guard<std::mutex> lock(mu_);
  if (running_) {
    if (task_id)
      *task_id = status_.task_id;
    return StartResult::AlreadyRunning;
  }
  if (next_task_id_ >= 0x7fffffffu)
    next_task_id_ = 1;
  else
    ++next_task_id_;
  running_ = true;
  cancel_requested_ = false;
  status_ = {};
  status_.state = Status::State::Running;
  status_.task_id = next_task_id_;
  status_.phase = "start";
  status_.progress = 0;
  status_.installed = installed_version();
  status_.channel = channel;
  if (task_id)
    *task_id = next_task_id_;
  auto *arg = new WorkerArg{this, kind == JobKind::Download, notify,
                            std::move(channel), next_task_id_};
  pthread_t thread;
  if (pthread_create(&thread, nullptr, worker_thunk, arg) != 0) {
    delete arg;
    running_ = false;
    status_.state = Status::State::Error;
    status_.error = "busy";
    return StartResult::Rejected;
  }
  pthread_detach(thread);
  return StartResult::Started;
}

bool Service::cancel(std::uint32_t task_id) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!running_ || cancel_requested_ || task_id == 0 ||
      task_id != status_.task_id)
    return false;
  cancel_requested_ = true;
  status_.phase = "cancel";
  status_.progress = -1;
  return true;
}

void Service::worker_check(std::string channel, bool notify,
                           std::uint32_t task_id) {
  (void)task_id;
  if (!ensure_update_layout()) {
    finish(Status::State::Error, "io");
    return;
  }
  note_progress("check", 0, 0);
  Manifest manifest;
  std::string body;
  std::string error;
  if (!fetch_checked_manifest(channel, &manifest, &body, &error)) {
    CheckInfo check;
    check.channel = channel;
    check.error = error;
    check.fetched_at = static_cast<long long>(std::time(nullptr));
    (void)write_check_info(check);
    finish(Status::State::Error, error);
    return;
  }
  if (!write_text_atomic(manifest_cache_path(channel), body)) {
    finish(Status::State::Error, "io");
    return;
  }
  CheckInfo check;
  check.channel = channel;
  check.fetched_at = static_cast<long long>(std::time(nullptr));
  (void)write_check_info(check);

  const std::string installed = installed_version();
  const bool newer =
      !development_version(installed) && manifest_newer(installed, manifest);
  {
    std::lock_guard<std::mutex> lock(mu_);
    status_.version = manifest.version;
    status_.summary = manifest.summary;
    status_.size = manifest.size;
    status_.installed = installed;
    status_.newer = newer;
  }
  std::string announced;
  if (notify && newer &&
      (!read_text_limited(kUpdateNotifiedPath, &announced, 64) ||
       announced != manifest.version)) {
    onion_notify(true, "notify.update.available", manifest.version.c_str());
    (void)write_text_atomic(kUpdateNotifiedPath, manifest.version);
  }
  finish(Status::State::Ok, {});
}

void Service::worker_download(std::string channel, std::uint32_t task_id) {
  using onion::cheats::sync::HttpRequest;
  using onion::cheats::sync::Ps5HttpTransport;
  using onion::cheats::sync::SyncStatus;
  (void)task_id;
  if (!ensure_update_layout()) {
    finish(Status::State::Error, "io");
    return;
  }
  note_progress("check", 0, 0);
  Manifest manifest;
  std::string cached;
  if (!load_cached_manifest(channel, &manifest, &cached)) {
    std::string error;
    if (!fetch_checked_manifest(channel, &manifest, &cached, &error)) {
      finish(Status::State::Error, error);
      return;
    }
    (void)write_text_atomic(manifest_cache_path(channel), cached);
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    status_.version = manifest.version;
    status_.summary = manifest.summary;
    status_.size = manifest.size;
    status_.installed = installed_version();
    status_.newer = manifest_newer(status_.installed, manifest);
  }
  if (cancellation_requested()) {
    finish(Status::State::Error, "cancelled");
    return;
  }

  std::string pattern =
      std::string(kUpdateTmpDir) + "/AniketosHEN.elf.part.XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  const int fd = mkstemp(buffer.data());
  if (fd < 0) {
    finish(Status::State::Error, "io");
    return;
  }
  const std::string temp_path(buffer.data());

  SHA256_CTX sha{};
  sha256_init(&sha);
  std::array<std::uint8_t, 20> header{};
  std::size_t header_size = 0;
  std::uint64_t received = 0;
  bool write_failed = false;

  struct ProgressJob {
    Service *service = nullptr;
  } job{this};

  HttpRequest request;
  request.url = manifest.url.c_str();
  request.timeout_ms = 180000;
  request.max_body_bytes = static_cast<std::size_t>(manifest.size);
  request.should_cancel = cancel_thunk;
  request.cancel_user = this;
  request.on_progress = +[](std::size_t bytes, std::size_t total, void *user) {
    auto *current = static_cast<ProgressJob *>(user);
    if (current && current->service)
      current->service->note_progress("download", bytes, total);
  };
  request.progress_user = &job;

  note_progress("download", 0, static_cast<std::uint64_t>(manifest.size));
  Ps5HttpTransport transport;
  const SyncStatus status = transport.perform(
      request, [&](const void *data, std::size_t size) {
        if (write_failed || cancellation_requested())
          return SyncStatus::Cancelled;
        const auto *bytes = static_cast<const std::uint8_t *>(data);
        if (received + size > manifest.size ||
            received + size > kMaxUpdateBytes)
          return SyncStatus::Protocol;
        if (header_size < header.size()) {
          const std::size_t take =
              std::min<std::size_t>(header.size() - header_size, size);
          std::memcpy(header.data() + header_size, bytes, take);
          header_size += take;
        }
        sha256_update(&sha, bytes, size);
        received += size;
        if (!write_all_bytes(fd, reinterpret_cast<const char *>(bytes), size)) {
          write_failed = true;
          return SyncStatus::Io;
        }
        return SyncStatus::Ok;
      });

  if (write_failed || status == SyncStatus::Io) {
    close(fd);
    unlink(temp_path.c_str());
    finish(Status::State::Error, "io");
    return;
  }
  if (status != SyncStatus::Ok) {
    close(fd);
    unlink(temp_path.c_str());
    finish(Status::State::Error, status_error(status));
    return;
  }
  if (cancellation_requested()) {
    close(fd);
    unlink(temp_path.c_str());
    finish(Status::State::Error, "cancelled");
    return;
  }
  if (received != manifest.size) {
    close(fd);
    unlink(temp_path.c_str());
    finish(Status::State::Error, "protocol");
    return;
  }
  const bool synced = fsync(fd) == 0;
  const bool closed = close(fd) == 0;
  if (!synced || !closed) {
    unlink(temp_path.c_str());
    finish(Status::State::Error, "io");
    return;
  }

  note_progress("verify", received, received);
  std::uint8_t digest[32] = {};
  sha256_final(&sha, digest);
  const std::string actual = hex_digest(digest);
  if (actual != manifest.sha256) {
    unlink(temp_path.c_str());
    finish(Status::State::Error, "hash");
    return;
  }
  if (header_size < header.size() ||
      !valid_update_elf(header.data(), header_size)) {
    unlink(temp_path.c_str());
    finish(Status::State::Error, "elf");
    return;
  }
  if (cancellation_requested()) {
    unlink(temp_path.c_str());
    finish(Status::State::Error, "cancelled");
    return;
  }

  note_progress("save", received, received);
  if (chmod(temp_path.c_str(), 0644) != 0) {
    unlink(temp_path.c_str());
    finish(Status::State::Error, "io");
    return;
  }
  if (rename(temp_path.c_str(), kUpdateElfPath) != 0) {
    unlink(temp_path.c_str());
    finish(Status::State::Error, "io");
    return;
  }
  sync_directory(kUpdateDir);

  ReadyInfo ready;
  ready.version = manifest.version;
  ready.sha256 = manifest.sha256;
  ready.summary = manifest.summary;
  ready.size = manifest.size;
  ready.downloaded_at = static_cast<long long>(std::time(nullptr));
  if (!write_ready_info(ready)) {
    unlink(kUpdateElfPath);
    finish(Status::State::Error, "io");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    status_.downloaded = true;
    status_.version = manifest.version;
    status_.size = manifest.size;
  }
  onion_notify(true, "notify.update.downloaded", manifest.version.c_str());
  finish(Status::State::Ok, {});
}

void start_boot_check() {
  pthread_t thread;
  if (pthread_create(&thread, nullptr, boot_thread, nullptr) != 0)
    return;
  pthread_detach(thread);
}

} // namespace onion::update
