#include "aniketos_shell/system_ui/game_icons.hpp"

#include "aniketos_shell/core/host_scene.hpp"
#include "aniketos_shell/system_ui/account_avatar.hpp"
#include "aniketos_shell/system_ui/avatar_codec.h"
#include "aniketos_shell/system_ui/game_icon_uri.hpp"
#include "aniketos_shell/rn_scripts/js_string.hpp"
#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/small_file.hpp"
#include "hooked_funcs.hpp"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <iterator>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace GameIcons {
namespace {

namespace Reflect = AniketosUI::Reflect;
namespace HomeScene = AniketosUI::HomeScene;

constexpr const char *kDirectory = ONION_DIST_USER_DATA_ROOT "/ShellUI/icons";
constexpr const char *kCacheDirectory =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/icons/cache";
/* The order one is chosen in when a title has several. */
constexpr const char *kExtensions[] = {".gif", ".webp", ".png", ".jpg",
                                       ".jpeg"};
constexpr size_t kMaxBoxes = 32;
/* Animated icons loaded at once; the system decodes each whole, so any more
   show still. */
constexpr int kMaxAnimated = 6;
/* Larger files show still: no icon that plays is this big. */
constexpr size_t kMaxInspected = 2 * 1024 * 1024;
constexpr size_t kMaxLogged = 128;
constexpr int kVerifyFrames = 30;
constexpr int kPlaybackFrames = 15;
/* ImageAnimationState */
constexpr int32_t kNone = -1;
constexpr int32_t kPlaying = 0;
constexpr int32_t kStopped = 1;
constexpr int32_t kPaused = 2;

/* What a box was last asked to load: when that has not shown after
   kVerifyFrames, the next one down is tried (as account_avatar does). */
enum Stage { kTryAnimated, kTryStill, kTrySystem, kLoaded };

struct Box {
  uint32_t box = 0;
  uint32_t action = 0;
  std::string uri;    /* the system's icon */
  std::string source; /* the user's file */
  std::string path;   /* what was loaded: the file or its fitted copy */
  bool set_image = false;
  bool animated = false;
  bool replayed = false;
  Stage stage = kTryAnimated;
  int frames = 0;
};

struct Icon {
  std::string id;
  std::string path;
  size_t rank = 0;
};

struct Inspected {
  std::string path;
  off_t size = 0;
  time_t mtime = 0;
  bool plays = false;
};

std::vector<Box> s_boxes;
std::vector<Icon> s_icons;
time_t s_scanned_mtime = -1;
std::vector<Inspected> s_inspected;
std::vector<std::string> s_logged;
int s_frames = 0;

/* GIFs past what the system plays are fitted one at a time, in the order
   they were asked for (avatar_codec). */
std::vector<std::string> s_fit_queue;
std::vector<std::string> s_fit_failed; /* "path|mtime" */
std::string s_fit_source;
std::string s_fit_cache;
std::atomic<bool> s_fit_running{false};
std::atomic<bool> s_fit_done{false};
std::atomic<bool> s_fit_ok{false};

bool first_time(const std::string &key) {
  if (std::find(s_logged.begin(), s_logged.end(), key) != s_logged.end())
    return false;
  if (s_logged.size() >= kMaxLogged)
    s_logged.erase(s_logged.begin());
  s_logged.push_back(key);
  return true;
}

bool file_info(const std::string &path, struct stat &info) {
  return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
         info.st_size > 0;
}

/* The folder is read again whenever it changed. */
void scan() {
  struct stat folder {};
  const time_t mtime = stat(kDirectory, &folder) == 0 ? folder.st_mtime : 0;
  if (mtime == s_scanned_mtime)
    return;
  s_scanned_mtime = mtime;
  s_icons.clear();
  DIR *handle = opendir(kDirectory);
  if (!handle)
    return;
  while (dirent *entry = readdir(handle)) {
    std::string name = entry->d_name;
    if (name.size() < 10 || name[9] != '.')
      continue;
    std::string id = name.substr(0, 9);
    std::string extension = name.substr(9);
    std::transform(id.begin(), id.end(), id.begin(),
                   [](unsigned char ch) { return std::toupper(ch); });
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    const auto found = std::find_if(
        std::begin(kExtensions), std::end(kExtensions),
        [&](const char *known) { return extension == known; });
    const std::string path = std::string(kDirectory) + "/" + name;
    struct stat info {};
    if (found == std::end(kExtensions) || !GameIconUri::IsTitleId(id) ||
        !file_info(path, info))
      continue;
    const size_t rank = static_cast<size_t>(found - std::begin(kExtensions));
    auto same = std::find_if(s_icons.begin(), s_icons.end(),
                             [&](const Icon &icon) { return icon.id == id; });
    if (same == s_icons.end())
      s_icons.push_back({id, path, rank});
    else if (rank < same->rank)
      *same = {id, path, rank};
  }
  closedir(handle);
  LOG_INFO("[ICONS] folder '%s' icons=%zu", kDirectory, s_icons.size());
}

std::string icon_of(const std::string &id) {
  scan();
  for (const Icon &icon : s_icons)
    if (icon.id == id)
      return icon.path;
  return {};
}

std::string cache_path_for(const std::string &source) {
  const std::string name = AniketosUI::FileName(source);
  return std::string(kCacheDirectory) + "/" + name.substr(0, name.rfind('.')) +
         "_fit.gif";
}

std::string failure_key(const std::string &source) {
  struct stat info {};
  return source + "|" +
         std::to_string(file_info(source, info) ? info.st_mtime : 0);
}

bool fitting(const std::string &source) {
  return (s_fit_running.load() && s_fit_source == source) ||
         std::find(s_fit_queue.begin(), s_fit_queue.end(), source) !=
             s_fit_queue.end();
}

void *fit_worker(void *) {
  char error[256] = {};
  const int result = avatar_codec_fit(s_fit_source.c_str(), s_fit_cache.c_str(),
                                      error, sizeof(error));
  LOG_INFO("[ICONS] fit %s src=%s%s%s", result == 0 ? "done" : "failed",
           s_fit_source.c_str(), result == 0 ? "" : " reason=",
           result == 0 ? "" : (error[0] ? error : "unknown"));
  s_fit_ok.store(result == 0);
  s_fit_done.store(true);
  return nullptr;
}

void start_next_fit() {
  if (s_fit_running.load() || s_fit_queue.empty())
    return;
  s_fit_source = s_fit_queue.front();
  s_fit_queue.erase(s_fit_queue.begin());
  s_fit_cache = cache_path_for(s_fit_source);
  s_fit_done.store(false);
  s_fit_running.store(true);
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, fit_worker, nullptr) != 0) {
    s_fit_running.store(false);
    s_fit_failed.push_back(failure_key(s_fit_source));
    LOG_WARN("[ICONS] fit failed reason=thread");
    return;
  }
  pthread_detach(thread);
}

/* The file to load: a GIF past what the system plays is fitted, and shown
   still from the file meanwhile. */
std::string effective_path(const std::string &source) {
  if (!avatar_codec_should_fit(source.c_str()))
    return source;
  (void)mkdir_tree(kCacheDirectory);
  const std::string cache = cache_path_for(source);
  if (avatar_codec_cache_valid(source.c_str(), cache.c_str()))
    return cache;
  const std::string key = failure_key(source);
  if (!fitting(source) &&
      std::find(s_fit_failed.begin(), s_fit_failed.end(), key) ==
          s_fit_failed.end()) {
    s_fit_queue.push_back(source);
    start_next_fit();
  }
  return source;
}

bool plays(const std::string &path) {
  struct stat info {};
  if (!file_info(path, info) || static_cast<size_t>(info.st_size) > kMaxInspected)
    return false;
  for (const Inspected &known : s_inspected)
    if (known.path == path && known.size == info.st_size &&
        known.mtime == info.st_mtime)
      return known.plays;
  std::string bytes;
  if (!AniketosUI::ReadSmallFile(path, kMaxInspected, bytes))
    return false;
  const AvatarImage::Info picture = AvatarImage::Inspect(
      reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
  const bool result = GameIconUri::Plays(picture);
  LOG_INFO("[ICONS] '%s' format=%d %dx%d frames=%d plays=%d",
           AniketosUI::FileName(path).c_str(), static_cast<int>(picture.format),
           picture.width, picture.height, picture.frames, result ? 1 : 0);
  if (s_inspected.size() >= kMaxBoxes)
    s_inspected.erase(s_inspected.begin());
  s_inspected.push_back({path, info.st_size, info.st_mtime, result});
  return result;
}

void free_box(Box &entry) {
  Reflect::Release(entry.action);
  Reflect::Release(entry.box);
}

/* Drops the boxes Mono has collected and visits the others. */
template <typename Visit> void for_each_live_box(Visit visit) {
  for (auto it = s_boxes.begin(); it != s_boxes.end();) {
    MonoObject *box = Reflect::Target(it->box);
    if (!box) {
      free_box(*it);
      it = s_boxes.erase(it);
      continue;
    }
    visit(*it, box);
    ++it;
  }
}

Box *find_box(MonoObject *box) {
  Box *found = nullptr;
  for_each_live_box([&](Box &entry, MonoObject *live) {
    if (live == box)
      found = &entry;
  });
  return found;
}

/* A box given another picture since (React reuses them) is not ours now. */
void forget(MonoObject *box) {
  for_each_live_box([&](Box &entry, MonoObject *live) {
    if (live == box)
      entry.frames = -1;
  });
  s_boxes.erase(std::remove_if(s_boxes.begin(), s_boxes.end(),
                               [](Box &entry) {
                                 if (entry.frames >= 0)
                                   return false;
                                 free_box(entry);
                                 return true;
                               }),
                s_boxes.end());
}

Box &remember(MonoObject *box) {
  if (Box *found = find_box(box))
    return *found;
  if (s_boxes.size() >= kMaxBoxes) {
    free_box(s_boxes.front());
    s_boxes.erase(s_boxes.begin());
  }
  s_boxes.push_back({});
  Reflect::Hold(s_boxes.back().box, box);
  return s_boxes.back();
}

bool may_animate(const Box &self, const std::string &source,
                 const std::string &path) {
  int animated = 0;
  for (const Box &entry : s_boxes)
    if (&entry != &self && entry.animated)
      ++animated;
  return animated < kMaxAnimated && !fitting(source) && plays(path);
}

void load(Box &entry, MonoObject *box, MonoObject *action, MonoObject *option) {
  entry.path = effective_path(entry.source);
  entry.animated = may_animate(entry, entry.source, entry.path);
  entry.stage = entry.animated ? kTryAnimated : kTryStill;
  entry.frames = kVerifyFrames;
  entry.replayed = false;
  if (first_time(entry.uri + "|" + entry.path))
    LOG_INFO("[ICONS] %s -> %s animated=%d", entry.uri.c_str(),
             entry.path.c_str(), entry.animated ? 1 : 0);
  AccountAvatar::LoadImage(box, entry.path, action, option, entry.set_image,
                           entry.animated);
}

/* The next picture down after the last one failed: the file still, then the
   system's own icon (React's fallback is a blank app picture, not that). */
void step_down(Box &entry, MonoObject *box, MonoObject *action) {
  entry.frames = kVerifyFrames;
  if (entry.stage == kTryAnimated) {
    entry.stage = kTryStill;
    entry.animated = false;
    LOG_INFO("[ICONS] animated fallback path=%s", entry.path.c_str());
    AccountAvatar::LoadImage(box, entry.path, action, nullptr, entry.set_image,
                             false);
    return;
  }
  entry.stage = kTrySystem;
  entry.frames = 0;
  LOG_INFO("[ICONS] system fallback uri=%s", entry.uri.c_str());
  if (first_time("failed|" + entry.source))
    notify("notify.vsh.game_icons.load_failed",
           AniketosUI::FileName(entry.source).c_str());
  AccountAvatar::LoadSystemImage(box, entry.uri, action, entry.set_image);
}

/* ReactImageBox.CurrentLoadStatus: 1 OnError, 4 OnLoadStart, 5 OnProgress.
   A box without it is left as it is. */
void verify(Box &entry, MonoObject *box) {
  int32_t status = -1;
  if (HomeScene::IsDisposed(box) ||
      !Reflect::Read(box, "get_CurrentLoadStatus", &status))
    return;
  if (status == 1)
    step_down(entry, box, Reflect::Target(entry.action));
  else if (status == 4 || status == 5)
    entry.frames = kVerifyFrames;
  else
    entry.stage = kLoaded;
}

void consume_fit() {
  if (!s_fit_done.load())
    return;
  const bool ok = s_fit_ok.load();
  s_fit_done.store(false);
  s_fit_running.store(false);
  if (!ok)
    s_fit_failed.push_back(failure_key(s_fit_source));
  else if (g_settings.game_icons_enabled)
    for_each_live_box([](Box &entry, MonoObject *box) {
      MonoObject *action = Reflect::Target(entry.action);
      if (entry.source == s_fit_source && !HomeScene::IsDisposed(box) &&
          !(entry.action && !action))
        load(entry, box, action, nullptr);
    });
  start_next_fit();
}

bool on_screen(MonoObject *box, float width, float height) {
  int32_t x = 0;
  int32_t y = 0;
  if (!Reflect::Read(box, "get_ScreenX", &x) ||
      !Reflect::Read(box, "get_ScreenY", &y))
    return true;
  const float box_width = Reflect::FloatProperty(box, "get_Width");
  const float box_height = Reflect::FloatProperty(box, "get_Height");
  return x + box_width > 0 && y + box_height > 0 && x < width && y < height;
}

/* Animated icons play only while Home is in sight and they are in it, on
   screen; otherwise they hold their frame. */
void update_playback() {
  const bool home = AniketosShell::HomeInSight();
  float width = 1920.0f;
  float height = 1080.0f;
  if (MonoObject *root = HomeScene::OwnRoot()) {
    Reflect::FloatProperty(root, "get_Width", &width);
    Reflect::FloatProperty(root, "get_Height", &height);
  }
  for_each_live_box([&](Box &entry, MonoObject *box) {
    if (!entry.animated || HomeScene::IsDisposed(box))
      return;
    int32_t state = -1;
    if (!Reflect::Read(box, "get_AnimationState", &state)) {
      if (first_time("state"))
        LOG_WARN("[ICONS] ImageBox.AnimationState not found: icons not paused");
      return;
    }
    const bool want = home && HomeScene::Contains(box) &&
                      on_screen(box, width, height);
    /* React loads the picture without setting it on the box, so it shows
       as a still (state None): the box gets it again itself, once a load. */
    if (state == kNone && entry.stage == kLoaded && !entry.replayed) {
      entry.replayed = true;
      AccountAvatar::LoadImage(box, entry.path, nullptr, nullptr, true, true);
      return;
    }
    if (state == kPlaying && !want)
      (void)Reflect::Call(box, "Pause", 0);
    else if ((state == kPaused || state == kStopped) && want)
      (void)Reflect::Call(box, "Play", 0);
  });
}

} // namespace

std::string ScriptConfig() {
  std::string icons;
  if (g_settings.game_icons_enabled) {
    scan();
    for (const Icon &icon : s_icons)
      icons += (icons.empty() ? "" : ",") + JsString(icon.id) + ":" +
               JsString(icon.path);
  }
  return std::string("{\"on\":") +
         (g_settings.game_icons_enabled ? "true" : "false") + ",\"icons\":{" +
         icons + "}}";
}

bool Load(MonoObject *box, MonoString *uri, MonoObject *action,
          MonoObject *option, bool set_image) {
  if (!g_settings.game_icons_enabled || !box || !uri)
    return false;
  const std::string text = Mono_to_String(uri);
  const std::string id = GameIconUri::TitleId(text);
  const std::string source = id.empty() ? std::string() : icon_of(id);
  if (source.empty()) {
    /* React's answer to our file failing: its blank app picture */
    Box *entry = text.find("texture_app_fallback") != std::string::npos
                     ? find_box(box)
                     : nullptr;
    if (entry && (entry->stage == kTryAnimated || entry->stage == kTryStill)) {
      step_down(*entry, box, action);
      return true;
    }
    if (id.empty() && text.find("appmeta") != std::string::npos &&
        first_time("uri|" + text))
      LOG_INFO("[ICONS] icon uri not recognised: %s", text.c_str());
    forget(box);
    return false;
  }
  Box &entry = remember(box);
  Reflect::Hold(entry.action, action);
  entry.uri = text;
  entry.source = source;
  entry.set_image = set_image;
  load(entry, box, action, option);
  return true;
}

void Tick() {
  consume_fit();
  if (!g_settings.game_icons_enabled) {
    /* switched off: Home loads the system's icons again */
    for (Box &entry : s_boxes)
      free_box(entry);
    s_boxes.clear();
    return;
  }
  for_each_live_box([](Box &entry, MonoObject *box) {
    if (entry.frames > 0 && --entry.frames == 0)
      verify(entry, box);
  });
  if (++s_frames >= kPlaybackFrames) {
    s_frames = 0;
    update_playback();
  }
}

} // namespace GameIcons
