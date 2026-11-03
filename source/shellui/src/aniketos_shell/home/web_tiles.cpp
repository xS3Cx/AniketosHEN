#include "aniketos_shell/home/web_tiles.hpp"

#include "aniketos_shell/home/home_bookmark.hpp"
#include "aniketos_shell/rn_scripts/js_string.hpp"
#include "aniketos_ui/small_file.hpp"
#include "hooked_funcs.hpp"
#include "onion_cjson.hpp"
#include "toolbox_i18n.hpp"
#include "web_icon.hpp"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/ipc_client.hpp>
#include <onion/log.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace WebTiles {
namespace {

using toolbox_i18n::tr;

constexpr const char *kFile = ONION_DIST_USER_DATA_ROOT "/web_tiles.json";
constexpr const char *kDir = ONION_DIST_USER_DATA_ROOT "/web_tiles";
/* Home shows 14 tiles in its row: the games keep most of them. */
constexpr size_t kMaxTiles = 8;
/* The util process's three fetches, each bounded, with some to spare. */
constexpr time_t kIconWaitSeconds = 40;

struct Tile {
  std::string id, url, name, icon;
};

struct Request {
  bool add = false;
  std::string url, title;
};

struct Pending {
  std::string id;
  time_t since = 0;
};

std::mutex s_mutex; /* the list and s_revision */
bool s_loaded = false;
std::vector<Tile> s_tiles;
/* Changes whenever Home has to ask for its rows again. */
unsigned s_revision = 1;

std::mutex s_request_mutex;
std::vector<Request> s_requests;

/* UI thread only. */
std::vector<Pending> s_pending;
time_t s_last_look = 0;

std::string icon_path(const std::string &id) {
  return std::string(kDir) + "/" + id + ".png";
}

void load_locked() {
  if (s_loaded)
    return;
  s_loaded = true;
  std::string text;
  if (!AniketosUI::ReadSmallFile(kFile, 128 * 1024, text))
    return;
  onion_cjson::Root root(text);
  if (!root)
    return;
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root.get(), "tiles")) {
    Tile tile;
    tile.id = onion_cjson::string_item(item, "id", "");
    tile.url = onion_cjson::string_item(item, "url", "");
    tile.name = onion_cjson::string_item(item, "name", "");
    tile.icon = onion_cjson::string_item(item, "icon", "");
    /* the icon arrived while ShellUI restarted */
    if (tile.icon.empty() && if_exists(icon_path(tile.id).c_str()))
      tile.icon = icon_path(tile.id);
    if (WebIcon::ValidTileId(tile.id) && WebIcon::ValidPageUrl(tile.url) &&
        s_tiles.size() < kMaxTiles)
      s_tiles.push_back(tile);
  }
  LOG_INFO("[WEBTILES] loaded %zu sites", s_tiles.size());
}

void save_locked() {
  cJSON *root = cJSON_CreateObject();
  cJSON *array = cJSON_AddArrayToObject(root, "tiles");
  for (const Tile &tile : s_tiles) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "id", tile.id.c_str());
    cJSON_AddStringToObject(item, "url", tile.url.c_str());
    cJSON_AddStringToObject(item, "name", tile.name.c_str());
    cJSON_AddStringToObject(item, "icon", tile.icon.c_str());
    cJSON_AddItemToArray(array, item);
  }
  const std::string text = onion_cjson::print_owned(root, true);
  if (text.empty() || !write_file_atomic(kFile, text.data(), text.size())) {
    LOG_WARN("[WEBTILES] could not save %s", kFile);
    return;
  }
  chmod(kFile, 0644); /* write_file_atomic leaves 0777 */
}

void changed_locked() {
  ++s_revision;
  save_locked();
}

std::string next_id_locked() {
  for (int number = 1;; ++number) {
    char id[16];
    std::snprintf(id, sizeof(id), "WEBT%05d", number);
    if (std::none_of(s_tiles.begin(), s_tiles.end(),
                     [&](const Tile &tile) { return tile.id == id; }))
      return id;
  }
}

/* "site.example" out of the page's address, for a page without a title. */
std::string host_of(const std::string &url) {
  const size_t start = url.find("://") + 3;
  return url.substr(start, url.find_first_of(":/?#", start) - start);
}

/* Off the UI thread: the util process answers at once, unless it is busy. */
void *ask_for_icon(void *raw) {
  const std::unique_ptr<Tile> tile(static_cast<Tile *>(raw));
  if (!IPC_Client::getInstance(true).WebTileIcon(tile->id, tile->url,
                                                 tile->name))
    LOG_WARN("[WEBTILES] no icon asked for %s", tile->id.c_str());
  return nullptr;
}

void add_tile(const std::string &url, const std::string &title) {
  Tile tile;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (std::any_of(s_tiles.begin(), s_tiles.end(),
                    [&](const Tile &known) { return known.url == url; }))
      return;
    if (s_tiles.size() >= kMaxTiles) {
      notify("%s", tr("web_tiles.list_full"));
      return;
    }
    tile.id = next_id_locked();
    tile.url = url;
    tile.name = HomeBookmark::CleanName(title);
    if (tile.name.empty())
      tile.name = HomeBookmark::CleanName(host_of(url));
    s_tiles.insert(s_tiles.begin(), tile);
    changed_locked();
  }
  LOG_INFO("[WEBTILES] added %s", tile.id.c_str());
  notify("%s", tr("web_tiles.added"));
  /* The tile shows at once with Home's own blank picture; the icon follows. */
  s_pending.push_back({tile.id, std::time(nullptr)});
  auto copy = std::make_unique<Tile>(tile);
  pthread_t thread;
  if (pthread_create(&thread, nullptr, ask_for_icon, copy.get()) == 0) {
    copy.release();
    pthread_detach(thread);
  }
}

void remove_tile(const std::string &url) {
  std::string id;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    const auto at = std::find_if(s_tiles.begin(), s_tiles.end(),
                                 [&](const Tile &tile) { return tile.url == url; });
    if (at == s_tiles.end())
      return;
    id = at->id;
    s_tiles.erase(at);
    changed_locked();
  }
  s_pending.erase(std::remove_if(s_pending.begin(), s_pending.end(),
                                 [&](const Pending &pending) {
                                   return pending.id == id;
                                 }),
                  s_pending.end());
  unlink(icon_path(id).c_str());
  LOG_INFO("[WEBTILES] removed %s", id.c_str());
  notify("%s", tr("web_tiles.removed"));
}

/* Icons the util process has written by now. */
void look_for_icons() {
  if (s_pending.empty())
    return;
  const time_t now = std::time(nullptr);
  if (now == s_last_look)
    return;
  s_last_look = now;
  for (size_t i = 0; i < s_pending.size();) {
    const std::string id = s_pending[i].id;
    const std::string found = icon_path(id);
    if (if_exists(found.c_str())) {
      std::lock_guard<std::mutex> lock(s_mutex);
      for (Tile &tile : s_tiles)
        if (tile.id == id)
          tile.icon = found;
      changed_locked();
    } else if (now - s_pending[i].since < kIconWaitSeconds) {
      ++i;
      continue;
    } else {
      LOG_WARN("[WEBTILES] no picture came for %s", id.c_str());
    }
    s_pending.erase(s_pending.begin() + static_cast<ptrdiff_t>(i));
  }
}

const char kInjectedScript[] =
#include "../rn_scripts/web_browser_inject_js.inc"
    ;

} // namespace

std::string ScriptConfig() {
  std::string urls;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    for (const Tile &tile : s_tiles)
      urls += (urls.empty() ? "" : ", ") + JsString(tile.url);
  }
  return "globalThis.__aniketosWeb = {tiles: [" + urls + "], fullscreen: " +
         JsString(tr("web_tiles.fullscreen")) + ", addHome: " +
         JsString(tr("web_tiles.add")) + ", removeHome: " +
         JsString(tr("web_tiles.remove")) + "};";
}

const char *InjectedScript() { return kInjectedScript; }

std::string HomeState() {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  std::string tiles;
  for (const Tile &tile : s_tiles)
    tiles += (tiles.empty() ? "{" : ",{") + std::string("\"id\":") +
             JsString(tile.id) + ",\"url\":" +
             JsString(tile.url) + ",\"name\":" +
             JsString(tile.name) + ",\"icon\":" +
             JsString(tile.icon) + "}";
  return "{\"rev\":" + std::to_string(s_revision) + ",\"tiles\":[" + tiles +
         "]}";
}

void HandleScriptMessage(const std::string &message) {
  onion_cjson::Root root(message);
  if (!root)
    return;
  const std::string op = onion_cjson::string_item(root.get(), "op", "");
  Request request;
  request.add = op == "add";
  request.url = onion_cjson::string_item(root.get(), "url", "");
  request.title = onion_cjson::string_item(root.get(), "title", "");
  if ((!request.add && op != "remove") || !WebIcon::ValidPageUrl(request.url))
    return;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_requests.push_back(request);
}

void Tick() {
  std::vector<Request> requests;
  {
    std::lock_guard<std::mutex> lock(s_request_mutex);
    requests.swap(s_requests);
  }
  for (const Request &request : requests) {
    if (request.add)
      add_tile(request.url, request.title);
    else
      remove_tile(request.url);
  }
  look_for_icons();
}

} // namespace WebTiles
