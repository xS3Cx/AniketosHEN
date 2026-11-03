#include "web_tile_icon.hpp"

#include "cheats/sync/http_transport_ps5.hpp"
#include "tile_picture.hpp"
#include "web_icon.hpp"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>

#include <algorithm>
#include <cerrno>
#include <memory>
#include <pthread.h>
#include <sys/stat.h>
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

/* The reader goes by the bytes, so every format has the one file name. */
bool save(const std::string &id, const std::vector<uint8_t> &bytes) {
  if (bytes.empty() || (mkdir(kDir, 0777) != 0 && errno != EEXIST))
    return false;
  const std::string path = std::string(kDir) + "/" + id + ".png";
  if (!write_file_atomic(path.c_str(), bytes.data(), bytes.size()))
    return false;
  chmod(path.c_str(), 0644); /* write_file_atomic leaves 0777 */
  return true;
}

/* Saved in a form the system's reader takes; false when these bytes are no
   picture, or one too big to be sure of whole. */
bool save_picture(const std::string &id, const std::vector<uint8_t> &bytes) {
  if (bytes.size() >= WebIcon::kMaxIcon)
    return false;
  const TilePicture::Format format =
      TilePicture::Sniff(bytes.data(), bytes.size());
  switch (format) {
  case TilePicture::Format::Png:
  case TilePicture::Format::Jpeg:
  case TilePicture::Format::Gif:
  case TilePicture::Format::WebP:
    return TilePicture::FitsTile(bytes.data(), bytes.size(), format) &&
           save(id, bytes);
  case TilePicture::Format::Ico: {
    const std::vector<uint8_t> png =
        TilePicture::IcoToPng(bytes.data(), bytes.size());
    return TilePicture::FitsTile(png.data(), png.size(),
                                 TilePicture::Format::Png) &&
           save(id, png);
  }
  case TilePicture::Format::Svg: /* a page's own markup, at every Home start */
  case TilePicture::Format::Unknown:
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
      !saved &&
      save(job->id, TilePicture::LetterTile(
                        TilePicture::TileLetter(job->title, job->url), job->url));
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
