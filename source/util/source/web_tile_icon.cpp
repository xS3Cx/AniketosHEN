#include "web_tile_icon.hpp"

#include "cheats/sync/http_transport_ps5.hpp"
#include "web_icon.hpp"

#include <onion/distribution.h>
#include <onion/log.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using onion::cheats::sync::HttpRequest;
using onion::cheats::sync::Ps5HttpTransport;
using onion::cheats::sync::SyncStatus;

constexpr const char *kDir = ONION_DIST_USER_DATA_ROOT "/web_tiles";
constexpr int kTimeoutMs = 6000;

struct Job {
  std::string id, url, title;
};

/* The body as far as `limit`; its status does not matter, because what came
   is judged by its bytes. */
std::vector<uint8_t> fetch(const std::string &url, size_t limit) {
  std::vector<uint8_t> body;
  HttpRequest request;
  request.url = url.c_str();
  request.timeout_ms = kTimeoutMs;
  Ps5HttpTransport http;
  (void)http.perform(request, [&](const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    const size_t take = std::min(size, limit - body.size());
    body.insert(body.end(), bytes, bytes + take);
    return take < size ? SyncStatus::Cancelled : SyncStatus::Ok;
  });
  return body;
}

bool save(const std::string &id, const char *extension,
          const std::vector<uint8_t> &bytes) {
  if (mkdir(kDir, 0777) != 0 && errno != EEXIST)
    return false;
  const std::string path = std::string(kDir) + "/" + id + extension;
  const std::string temporary = path + ".tmp";
  FILE *file = std::fopen(temporary.c_str(), "wb");
  if (!file)
    return false;
  bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
  written = std::fclose(file) == 0 && written;
  if (!written || std::rename(temporary.c_str(), path.c_str()) != 0) {
    unlink(temporary.c_str());
    return false;
  }
  return true;
}

/* Saved in a form the system's reader takes; false when these bytes are no
   picture, or one too big to be sure of whole. */
bool save_picture(const std::string &id, const std::vector<uint8_t> &bytes) {
  if (bytes.size() >= WebIcon::kMaxIcon)
    return false;
  const WebIcon::Format format = WebIcon::Sniff(bytes.data(), bytes.size());
  switch (format) {
  case WebIcon::Format::Png:
  case WebIcon::Format::Jpeg:
  case WebIcon::Format::Gif:
  case WebIcon::Format::WebP:
    /* the reader goes by the bytes, as the avatar's does */
    return WebIcon::FitsTile(bytes.data(), bytes.size(), format) &&
           save(id, ".png", bytes);
  case WebIcon::Format::Ico: {
    const std::vector<uint8_t> png =
        WebIcon::IcoToPng(bytes.data(), bytes.size());
    return WebIcon::FitsTile(png.data(), png.size(), WebIcon::Format::Png) &&
           save(id, ".png", png);
  }
  case WebIcon::Format::Svg: /* a page's own markup, at every Home start */
  case WebIcon::Format::Unknown:
    break;
  }
  return false;
}

void *worker(void *raw) {
  const std::unique_ptr<Job> job(static_cast<Job *>(raw));
  const std::vector<uint8_t> page = fetch(job->url, WebIcon::kMaxHtml);
  const std::string href = WebIcon::FindIconHref(
      {reinterpret_cast<const char *>(page.data()), page.size()});
  const std::string named = WebIcon::ResolveUrl(job->url, href);
  const std::string usual = WebIcon::ResolveUrl(job->url, "/favicon.ico");
  bool saved = !named.empty() &&
               save_picture(job->id, fetch(named, WebIcon::kMaxIcon));
  if (!saved && !usual.empty() && usual != named)
    saved = save_picture(job->id, fetch(usual, WebIcon::kMaxIcon));
  const bool plain =
      !saved && save(job->id, ".png",
                     WebIcon::LetterTile(
                         WebIcon::TileLetter(job->title, job->url), job->url));
  LOG_INFO("web tile %s: %s", job->id.c_str(),
           saved ? "site icon" : plain ? "plain tile" : "no picture written");
  return nullptr;
}

} // namespace

bool web_tile_icon_start(const std::string &id, const std::string &url,
                         const std::string &title) {
  if (!WebIcon::ValidTileId(id) || !WebIcon::ValidPageUrl(url))
    return false;
  auto job = std::make_unique<Job>(Job{id, url, title});
  pthread_t thread;
  if (pthread_create(&thread, nullptr, worker, job.get()) != 0)
    return false;
  job.release();
  pthread_detach(thread);
  return true;
}
