#include "payload_center/payload_center_service.hpp"

#include "payload_center/payload_center_icons.hpp"
#include "payload_center/payload_center_install.hpp"
#include "payload_center/payload_center_store.hpp"

#include <onion/payload_catalog.hpp>
#include <onion/payload_catalog_policy.hpp>
#include <onion/payload_sources_json.hpp>

extern "C" {
#include <sha256.h>
}

#include <algorithm>
#include <ctime>
#include <memory>
#include <pthread.h>
#include <string>
#include <utility>
#include <vector>

namespace onion::payload_center {
namespace {

std::string sha256_hex(const std::string &body) {
  SHA256_CTX ctx{};
  sha256_init(&ctx);
  sha256_update(&ctx, reinterpret_cast<const uint8_t *>(body.data()),
                body.size());
  uint8_t digest[32] = {};
  sha256_final(&ctx, digest);
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(64, '0');
  for (std::size_t i = 0; i < 32; ++i) {
    out[i * 2] = kHex[digest[i] >> 4];
    out[i * 2 + 1] = kHex[digest[i] & 0x0f];
  }
  return out;
}

struct WorkerArg {
  Service *service = nullptr;
  bool refresh = false;
  std::string source_id;
  std::string item_id;
  std::string url;
  bool replace = false;
  uint32_t task_id = 0;
};

void *worker_thunk(void *raw) {
  std::unique_ptr<WorkerArg> arg(static_cast<WorkerArg *>(raw));
  if (!arg)
    return nullptr;
  if (arg->refresh)
    arg->service->worker_refresh(std::move(arg->source_id), arg->task_id);
  else
    arg->service->worker_install(std::move(arg->source_id),
                                 std::move(arg->item_id), std::move(arg->url),
                                 arg->replace, arg->task_id);
  return nullptr;
}

void progress_thunk(const char *phase, uint64_t completed, uint64_t total,
                    void *user) {
  auto *service = static_cast<Service *>(user);
  if (service)
    service->note_progress(phase, completed, total);
}

bool cancel_thunk(void *user) {
  auto *service = static_cast<Service *>(user);
  return service && service->cancellation_requested();
}

const onion::payload_catalog::Source *find_source(
    const onion::payload_catalog::SourcesFile &sources,
    const std::string &source_id) {
  if (source_id.empty())
    return sources.sources.empty() ? nullptr : &sources.sources.front();
  for (const auto &source : sources.sources) {
    if (source.url == source_id ||
        onion::payload_catalog::source_hash8(source.url) == source_id)
      return &source;
  }
  return nullptr;
}

std::string name_from_url(const std::string &url) {
  const std::size_t query = url.find_first_of("?#");
  const std::string clean = url.substr(0, query);
  const std::size_t slash = clean.find_last_of('/');
  std::string name =
      slash == std::string::npos ? clean : clean.substr(slash + 1);
  if (name.empty())
    name = "payload.elf";
  return name;
}

bool write_catalog(const std::string &source_url, const std::string &body) {
  CacheStatus status;
  status.url = source_url;
  status.fetched_at = static_cast<long long>(std::time(nullptr));
  status.bytes = body.size();
  status.sha256 = sha256_hex(body);
  return write_catalog_cache(source_url, body, status);
}

bool load_or_fetch_catalog(const std::string &source_url, std::string *body,
                           onion::payload_catalog::Catalog *catalog,
                           std::string *error) {
  if (!read_catalog_cache(source_url, body, nullptr)) {
    if (!fetch_source_catalog(source_url, body, error))
      return false;
    (void)write_catalog(source_url, *body);
  }
  const auto parsed = onion::payload_catalog::parse_catalog(*body);
  if (!parsed.ok) {
    if (error)
      *error = parsed.error.empty() ? "protocol" : parsed.error;
    return false;
  }
  *catalog = parsed.catalog;
  return true;
}

}

Service &Service::instance() {
  static Service service;
  return service;
}

Service::Service() = default;
Service::~Service() = default;

Status Service::status() const {
  std::lock_guard<std::mutex> lock(mu_);
  return status_;
}

bool Service::cancellation_requested() const {
  std::lock_guard<std::mutex> lock(mu_);
  return cancel_requested_;
}

void Service::note_progress(const char *phase, uint64_t completed,
                            uint64_t total) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!running_ || cancel_requested_)
    return;
  if (phase && phase[0])
    status_.phase = phase;
  status_.completed = completed;
  status_.total = total;
  if (status_.phase == "start") {
    status_.progress = 0;
  } else if (total > 0) {
    const uint64_t percent = completed * 100ull / total;
    status_.progress = static_cast<int>(percent > 100ull ? 100ull : percent);
  } else if (status_.phase == "verify" || status_.phase == "install" ||
             status_.phase == "cleanup") {
    status_.progress = 100;
  } else {
    status_.progress = -1;
  }
}

void Service::finish(Status::State state, const std::string &error) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    status_.state = state;
    status_.error = error;
    status_.phase = state == Status::State::Ok ? "cleanup" : status_.phase;
    if (state == Status::State::Ok)
      status_.progress = 100;
    running_ = false;
    cancel_requested_ = false;
  }
}

bool Service::cancel(uint32_t task_id) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!running_ || cancel_requested_ || task_id == 0 ||
      task_id != status_.task_id)
    return false;
  cancel_requested_ = true;
  status_.phase = "cleanup";
  status_.progress = -1;
  return true;
}

Service::StartResult Service::refresh(const std::string &source_id,
                                      uint32_t *task_id) {
  return start(JobKind::Refresh, source_id, {}, {}, false, task_id);
}

Service::StartResult Service::install(const std::string &source_id,
                                      const std::string &item_id, bool replace,
                                      uint32_t *task_id) {
  return start(JobKind::Install, source_id, item_id, {}, replace, task_id);
}

Service::StartResult Service::install_url(const std::string &url, bool replace,
                                          uint32_t *task_id) {
  return start(JobKind::Install, {}, {}, url, replace, task_id);
}

Service::StartResult Service::start(JobKind kind, std::string source_id,
                                    std::string item_id, std::string url,
                                    bool replace, uint32_t *task_id) {
  std::lock_guard<std::mutex> lock(mu_);
  if (running_) {
    if (task_id)
      *task_id = status_.task_id;
    return StartResult::AlreadyRunning;
  }
  if (kind == JobKind::Install && url.empty() &&
      (source_id.empty() || item_id.empty())) {
    return StartResult::Rejected;
  }
  if (kind == JobKind::Install && !url.empty() &&
      (!onion::payload_catalog::is_https_url(url) ||
       !onion::payload_catalog::valid_url(url))) {
    return StartResult::Rejected;
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
  if (task_id)
    *task_id = next_task_id_;
  auto *arg = new WorkerArg{this, kind == JobKind::Refresh,
                            std::move(source_id), std::move(item_id),
                            std::move(url), replace, next_task_id_};
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

void Service::worker_refresh(std::string source_id, uint32_t task_id) {
  (void)task_id;
  note_progress("start", 0, 0);
  onion::payload_catalog::SourcesFile sources;
  (void)onion::payload_catalog::load_sources_file(kSourcesPath, &sources);
  std::vector<const onion::payload_catalog::Source *> selected;
  if (source_id.empty()) {
    for (const auto &source : sources.sources)
      selected.push_back(&source);
  } else {
    const auto *source = find_source(sources, source_id);
    if (source)
      selected.push_back(source);
  }
  if (selected.empty()) {
    finish(Status::State::Error, "rejected");
    return;
  }
  std::size_t succeeded = 0;
  std::string last_error = "network";
  for (std::size_t i = 0; i < selected.size(); ++i) {
    if (cancellation_requested()) {
      finish(Status::State::Error, "cancelled");
      return;
    }
    const auto &source = *selected[i];
    {
      std::lock_guard<std::mutex> lock(mu_);
      status_.item = source.name;
      status_.progress = static_cast<int>(i * 100 / selected.size());
    }
    note_progress("download", i, selected.size());
    std::string body;
    std::string error;
    if (!fetch_source_catalog(source.url, &body, &error)) {
      last_error = error.empty() ? "network" : error;
      continue;
    }
    const auto parsed = onion::payload_catalog::parse_catalog(body);
    if (!parsed.ok) {
      last_error = parsed.error.empty() ? "protocol" : parsed.error;
      continue;
    }
    if (!write_catalog(source.url, body)) {
      last_error = "io";
      continue;
    }
    std::vector<IconSourceItem> icons;
    icons.reserve(parsed.catalog.items.size());
    for (const auto &item : parsed.catalog.items)
      icons.push_back({item.icon_key, item.name, item.url});
    if (icons_refresh_due(source.url)) {
      note_progress("install", i, selected.size());
      std::string icon_error;
      (void)refresh_icons(source.url, parsed.catalog.icons_url, icons,
                          &icon_error);
    }
    ++succeeded;
  }
  if (cancellation_requested()) {
    finish(Status::State::Error, "cancelled");
    return;
  }
  if (succeeded == 0)
    finish(Status::State::Error, last_error);
  else
    finish(Status::State::Ok, {});
}

void Service::worker_install(std::string source_id, std::string item_id,
                             std::string url, bool replace, uint32_t task_id) {
  (void)task_id;
  note_progress("start", 0, 0);
  InstallRequest request;
  request.replace = replace;
  if (!url.empty()) {
    request.from_url = true;
    request.url = url;
    request.name = name_from_url(url);
    request.filename = request.name;
    request.item_id = onion::payload_catalog::slug(request.name);
    request.source_name = "URL";
    request.source_url = url;
    request.match_key = url + "#" + request.item_id;
    request.item_id = request.match_key;
  } else {
    onion::payload_catalog::SourcesFile sources;
    (void)onion::payload_catalog::load_sources_file(kSourcesPath, &sources);
    const auto *source = find_source(sources, source_id);
    if (!source) {
      finish(Status::State::Error, "rejected");
      return;
    }
    std::string body;
    onion::payload_catalog::Catalog catalog;
    std::string error;
    if (!load_or_fetch_catalog(source->url, &body, &catalog, &error)) {
      finish(Status::State::Error, error.empty() ? "network" : error);
      return;
    }
    const auto found = std::find_if(
        catalog.items.begin(), catalog.items.end(),
        [&](const onion::payload_catalog::Item &item) {
          return item.id == item_id;
        });
    if (found == catalog.items.end()) {
      finish(Status::State::Error, "rejected");
      return;
    }
    if (found->insecure || found->unsupported_binary) {
      finish(Status::State::Error, "rejected");
      return;
    }
    request.source_id = onion::payload_catalog::source_hash8(source->url);
    request.source_url = source->url;
    request.source_name = source->name;
    request.item_id = found->id;
    request.name = found->name;
    request.filename = found->filename;
    request.url = found->url;
    request.checksum = found->checksum;
    request.has_checksum = found->has_checksum;
    request.version = found->version;
    request.last_update = found->last_update;
    request.category = found->category;
    request.match_key =
        onion::payload_catalog::item_match_key(*found, source->url);
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    status_.item = request.name;
  }
  const InstallResult result =
      install_payload(request, progress_thunk, this, cancel_thunk, this);
  if (result.ok) {
    finish(Status::State::Ok, {});
    return;
  }
  finish(Status::State::Error, result.error.empty() ? "io" : result.error);
}

}
