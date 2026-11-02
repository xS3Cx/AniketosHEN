#include "aniketos_shell/system_ui/account_avatar.hpp"

#include "aniketos_shell/system_ui/avatar_codec.h"
#include "aniketos_shell/system_ui/avatar_image.hpp"
#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/small_file.hpp"
#include "hooked_funcs.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <initializer_list>
#include <iterator>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

namespace AccountAvatar {
namespace {

namespace Reflect = AniketosUI::Reflect;
using AniketosUI::FileName;

constexpr const char *kDirectory = ONION_DIST_USER_DATA_ROOT "/ShellUI/avatars";
constexpr const char *kCacheDirectory =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/avatars/cache";
constexpr const char *kFacePrefix = "profilecache:image/face/";
constexpr std::initializer_list<const char *> kPictures = {
    ".gif", ".webp", ".png", ".jpg", ".jpeg"};
constexpr std::initializer_list<const char *> kAnimated = {".gif", ".webp"};
constexpr std::initializer_list<const char *> kFitted = {".gif"};

constexpr size_t kMaxBoxes = 32;
/* Enough of a file to count the frames of any picture that can play. */
constexpr size_t kMaxInspected = 16 * 1024 * 1024;
constexpr size_t kMaxLogged = 128;
constexpr int kVerifyFrames = 30;

using BoxLoadFn = void (*)(MonoObject *box, MonoString *uri,
                           MonoObject *action, MonoObject *option,
                           bool set_image);
BoxLoadFn s_box_original = nullptr;
MonoClass *s_box_class = nullptr;
MonoClass *s_options_class = nullptr;

/* What a box was last asked to load: when that has not shown after
   kVerifyFrames, the next one down is tried. */
enum Stage { kTryAnimated, kTryStill, kTrySystem, kLoaded };

struct TrackedBox {
  uint32_t box = 0;
  uint32_t action = 0;
  std::string uri;
  std::string path;
  bool set_image = false;
  Stage stage = kTryAnimated;
  int frames = 0;
};

std::vector<std::string> s_files;
size_t s_unsupported = 0; /* other files in the folder */
bool s_scanned = false;
std::vector<TrackedBox> s_boxes;
std::string s_logged_path;
std::vector<std::string> s_logged_faces;
std::vector<std::string> s_logged_skips;
std::vector<std::string> s_logged_unreadable;
std::vector<std::string> s_logged_loads;
std::vector<std::string> s_logged_fallbacks;
std::vector<std::string> s_logged_failures;

int s_frames = 0;
size_t s_cache_attempt = 0;
bool s_cache_done = false;

struct FitJob {
  std::string source;
  std::string cache;
  unsigned long generation = 0;
};

FitJob s_fit_job;
std::string s_fit_desired_source;
std::string s_fit_desired_cache;
unsigned long s_fit_generation = 0;
/* not tried again while the file stays as it was */
unsigned long s_fit_failed_generation = 0;
time_t s_fit_failed_mtime = 0;
std::atomic<bool> s_fit_running{false};
std::atomic<bool> s_fit_done{false};
std::atomic<bool> s_fit_ok{false};

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool first_time(std::vector<std::string> &seen, const std::string &key) {
  if (std::find(seen.begin(), seen.end(), key) != seen.end())
    return false;
  if (seen.size() >= kMaxLogged)
    seen.erase(seen.begin());
  seen.push_back(key);
  return true;
}

bool has_extension(const std::string &name,
                   std::initializer_list<const char *> extensions) {
  const std::string folded = lower(name);
  for (const char *extension : extensions) {
    const size_t length = std::strlen(extension);
    if (folded.size() > length &&
        folded.compare(folded.size() - length, length, extension) == 0)
      return true;
  }
  return false;
}

bool is_nonempty_file(const std::string &path) {
  struct stat info {};
  return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
         info.st_size > 0;
}

time_t modified_at(const std::string &path) {
  struct stat info {};
  return stat(path.c_str(), &info) == 0 ? info.st_mtime : 0;
}

uint32_t foreground_user() {
  int user = -1;
  return sceUserServiceGetForegroundUser(&user) == 0 && user > 0
             ? static_cast<uint32_t>(user)
             : 0;
}

uint32_t owner_id() {
  if (!g_settings.account_avatar_user.empty())
    return static_cast<uint32_t>(
        std::strtoul(g_settings.account_avatar_user.c_str(), nullptr, 0));
  return foreground_user();
}

uint32_t matching_user() {
  const uint32_t saved = owner_id();
  const uint32_t foreground = foreground_user();
  if (saved && foreground && saved != foreground) {
    static uint32_t logged_saved = 0;
    static uint32_t logged_foreground = 0;
    if (saved != logged_saved || foreground != logged_foreground) {
      logged_saved = saved;
      logged_foreground = foreground;
      LOG_INFO("[AVATAR] user switch saved=0x%x foreground=0x%x",
               static_cast<unsigned>(saved), static_cast<unsigned>(foreground));
    }
    return foreground;
  }
  return saved;
}

void log_skip(const std::string &path, const char *reason) {
  if (path.empty())
    return;
  const std::string key = path + "|" + reason;
  if (first_time(s_logged_skips, key))
    LOG_INFO("[AVATAR] skip path=%s reason=%s", path.c_str(), reason);
}

bool is_own_face(const std::string &uri) {
  if (uri.compare(0, std::strlen(kFacePrefix), kFacePrefix) != 0)
    return false;
  const std::string folded = lower(uri);
  size_t at = folded.find("userid=");
  size_t length = 7;
  if (at == std::string::npos) {
    at = folded.find("userid%3d");
    length = 9;
  }
  bool match = false;
  if (at != std::string::npos) {
    const char *number = uri.c_str() + at + length;
    const long long id = std::strtoll(number, nullptr, 0);
    const uint32_t user = matching_user();
    match = user && user == static_cast<uint32_t>(id);
  }
  if (first_time(s_logged_faces, uri))
    LOG_INFO("[AVATAR] face uri=%s owner=0x%x match=%d", uri.c_str(),
             static_cast<unsigned>(matching_user()), match ? 1 : 0);
  return match;
}

std::string chosen_path() {
  const std::string &path = g_settings.account_avatar_path;
  if (!g_settings.account_avatar_enabled) {
    log_skip(path, "disabled");
    return {};
  }
  if (path.empty())
    return {};
  if (!has_extension(path, kPictures)) {
    log_skip(path, "extension");
    return {};
  }
  if (!is_nonempty_file(path)) {
    log_skip(path, "missing");
    return {};
  }
  if (access(path.c_str(), R_OK) != 0) {
    const int error = errno;
    if (first_time(s_logged_unreadable, path))
      LOG_INFO("[AVATAR] unreadable path=%s errno=%d", path.c_str(), error);
    log_skip(path, "unreadable");
    return {};
  }
  return path;
}

/* Tells the user at once why the chosen picture will not show. A GIF over the
   limits is fitted instead (avatar_codec), so only other formats, and a GIF too
   big to decode for fitting, are warned about those. */
void report_chosen() {
  const std::string path = chosen_path();
  if (path.empty())
    return;
  struct stat file_info {};
  std::string bytes;
  if (stat(path.c_str(), &file_info) != 0 ||
      !AniketosUI::ReadSmallFile(
          path, std::min(static_cast<size_t>(file_info.st_size), kMaxInspected),
          bytes))
    return;
  const AvatarImage::Info info = AvatarImage::Inspect(
      reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
  const std::string name = AniketosUI::FileName(path);
  LOG_INFO("[AVATAR] '%s' format=%d %dx%d frames=%d", name.c_str(),
           static_cast<int>(info.format), info.width, info.height, info.frames);
  const AvatarImage::Problem problem = AvatarImage::Check(info);
  if (problem != AvatarImage::Problem::NotPicture &&
      has_extension(path, kFitted) && AvatarImage::DecodeFits(info))
    return;
  switch (problem) {
  case AvatarImage::Problem::NotPicture:
    notify("notify.vsh.account_avatar.not_picture", name.c_str());
    break;
  case AvatarImage::Problem::TooLarge:
    notify("notify.vsh.account_avatar.too_large", name.c_str(), info.width,
           info.height);
    break;
  case AvatarImage::Problem::TooManyFrames:
    notify("notify.vsh.account_avatar.too_many_frames", name.c_str(),
           info.frames);
    break;
  case AvatarImage::Problem::None:
    break;
  }
}

std::string cache_path_for(const std::string &source) {
  const std::string name = AniketosUI::FileName(source);
  const size_t dot = name.rfind('.');
  const std::string stem =
      dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
  return std::string(kCacheDirectory) + "/" + stem + "_fit.gif";
}

void *fit_worker(void *) {
  const std::string source = s_fit_job.source;
  const std::string cache = s_fit_job.cache;
  char error[256] = {};
  const int result =
      avatar_codec_fit(source.c_str(), cache.c_str(), error, sizeof(error));
  if (result == 0) {
    struct stat info {};
    const long long bytes =
        stat(cache.c_str(), &info) == 0 ? static_cast<long long>(info.st_size)
                                        : 0;
    LOG_INFO("[AVATAR] fit done cache=%s bytes=%lld", cache.c_str(), bytes);
  } else {
    LOG_INFO("[AVATAR] fit failed reason=%s", error[0] ? error : "unknown");
  }
  s_fit_ok.store(result == 0);
  s_fit_done.store(true);
  return nullptr;
}

void start_fit() {
  pthread_t thread{};
  s_fit_job.source = s_fit_desired_source;
  s_fit_job.cache = s_fit_desired_cache;
  s_fit_job.generation = s_fit_generation;
  s_fit_done.store(false);
  s_fit_ok.store(false);
  s_fit_running.store(true);
  LOG_INFO("[AVATAR] fit start src=%s", s_fit_desired_source.c_str());
  if (pthread_create(&thread, nullptr, fit_worker, nullptr) != 0) {
    s_fit_running.store(false);
    s_fit_done.store(false);
    LOG_INFO("[AVATAR] fit failed reason=thread");
    return;
  }
  pthread_detach(thread);
}

void request_fit(const std::string &source, const std::string &cache) {
  if (source != s_fit_desired_source || cache != s_fit_desired_cache) {
    s_fit_desired_source = source;
    s_fit_desired_cache = cache;
    ++s_fit_generation;
  }
  if (s_fit_running.load() || (s_fit_failed_generation == s_fit_generation &&
                               s_fit_failed_mtime == modified_at(source)))
    return;
  start_fit();
}

std::string effective_path() {
  const std::string source = chosen_path();
  if (source.empty() || !avatar_codec_should_fit(source.c_str()))
    return source;
  (void)mkdir_tree(kCacheDirectory);
  const std::string cache = cache_path_for(source);
  if (avatar_codec_cache_valid(source.c_str(), cache.c_str()))
    return cache;
  request_fit(source, cache);
  return source;
}

void consume_fit() {
  if (!s_fit_done.load())
    return;
  const bool ok = s_fit_ok.load();
  s_fit_done.store(false);
  s_fit_running.store(false);
  if (s_fit_job.generation != s_fit_generation) {
    if (!s_fit_desired_source.empty())
      start_fit();
    return;
  }
  if (!ok) {
    s_fit_failed_generation = s_fit_generation;
    s_fit_failed_mtime = modified_at(s_fit_job.source);
  } else if (avatar_codec_cache_valid(s_fit_job.source.c_str(),
                                    s_fit_job.cache.c_str()))
    Refresh();
}

void free_box(TrackedBox &entry) {
  Reflect::Release(entry.action);
  Reflect::Release(entry.box);
}

TrackedBox *find_box(MonoObject *box) {
  for (auto it = s_boxes.begin(); it != s_boxes.end();) {
    MonoObject *known = Reflect::Target(it->box);
    if (!known) {
      free_box(*it);
      it = s_boxes.erase(it);
      continue;
    }
    if (known == box)
      return &*it;
    ++it;
  }
  return nullptr;
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

TrackedBox &remember(MonoObject *box) {
  if (TrackedBox *found = find_box(box))
    return *found;
  if (s_boxes.size() >= kMaxBoxes) {
    free_box(s_boxes.front());
    s_boxes.erase(s_boxes.begin());
  }
  s_boxes.push_back({});
  TrackedBox &entry = s_boxes.back();
  Reflect::Hold(entry.box, box);
  return entry;
}

void configure_options(MonoObject *option, bool animated) {
  Set_Property(s_options_class, option, "IsAnimatedImage", animated);
  Set_Property(s_options_class, option, "AnimationDecodeMode",
               animated ? 1 : 0);
}

MonoObject *make_options(bool animated) {
  MonoObject *option = New_Object(s_options_class);
  if (!option)
    return nullptr;
  mono_runtime_object_init(option);
  configure_options(option, animated);
  return option;
}

void keep_playing(MonoObject *box) {
  Set_Property(s_box_class, box, "Loop", true);
  Set_Property(s_box_class, box, "Autoplay", true);
}

void call_original(MonoObject *box, const std::string &uri, MonoObject *action,
                   MonoObject *option, bool set_image, bool play) {
  if (!box || !s_box_original)
    return;
  if (play)
    keep_playing(box);
  MonoString *managed = mono_string_new(mono_domain_get(), uri.c_str());
  s_box_original(box, managed, action, option, set_image);
  if (play)
    keep_playing(box);
}

MonoClass *property_class(const char *name) {
  if (!s_box_class || !mono_class_get_property_from_name)
    return nullptr;
  for (MonoClass *klass = s_box_class; klass;
       klass = mono_class_get_parent(klass))
    if (mono_class_get_property_from_name(klass, name))
      return klass;
  return nullptr;
}

bool image_loaded(MonoObject *box) {
  if (!box)
    return true;
  MonoClass *klass = property_class("IsLoaded");
  const char *name = "IsLoaded";
  if (!klass) {
    klass = property_class("HasImage");
    name = "HasImage";
  }
  if (!klass)
    return true;
  return Get_Property<bool>(klass, box, name);
}

void verify_box(TrackedBox &entry, MonoObject *box) {
  entry.frames = 0;
  if (AniketosUI::HomeScene::IsDisposed(box))
    return;
  if (image_loaded(box)) {
    entry.stage = kLoaded;
    if (!entry.uri.empty() && first_time(s_logged_loads, entry.uri))
      LOG_INFO("[AVATAR] loaded ok box=%p stage=%d", static_cast<void *>(box),
               entry.stage);
    return;
  }
  MonoObject *action = Reflect::Target(entry.action);
  if (entry.stage == kTryAnimated) {
    entry.stage = kTryStill;
    entry.frames = kVerifyFrames;
    call_original(box, entry.path, action, make_options(false), entry.set_image,
                  false);
    if (first_time(s_logged_fallbacks, entry.uri))
      LOG_INFO("[AVATAR] animated fallback path=%s", entry.path.c_str());
  } else if (entry.stage == kTryStill) {
    entry.stage = kTrySystem;
    entry.frames = kVerifyFrames;
    call_original(box, entry.uri, action, nullptr, entry.set_image, false);
    if (first_time(s_logged_fallbacks, entry.uri + "|system"))
      LOG_INFO("[AVATAR] system fallback uri=%s", entry.uri.c_str());
  } else if (entry.stage == kTrySystem &&
             first_time(s_logged_failures, entry.uri)) {
    LOG_INFO("[AVATAR] load failed uri=%s", entry.uri.c_str());
    notify("notify.vsh.account_avatar.load_failed",
           AniketosUI::FileName(entry.path).c_str());
  }
}

void box_hook(MonoObject *box, MonoString *uri, MonoObject *action,
              MonoObject *option, bool set_image) {
  const std::string text = Mono_to_String(uri);
  const std::string path =
      box && is_own_face(text) ? effective_path() : std::string();
  if (path.empty()) {
    s_box_original(box, uri, action, option, set_image);
    return;
  }
  const bool animated = has_extension(path, kAnimated);
  TrackedBox &entry = remember(box);
  if (action)
    Reflect::Hold(entry.action, action);
  entry.uri = text;
  entry.path = path;
  entry.set_image = set_image;
  entry.stage = animated ? kTryAnimated : kTryStill;
  entry.frames = kVerifyFrames;
  if (option)
    configure_options(option, animated);
  else
    option = make_options(animated);
  if (path != s_logged_path) {
    s_logged_path = path;
    LOG_INFO("[AVATAR] %s -> %s animated=%d", text.c_str(), path.c_str(),
             animated ? 1 : 0);
  }
  call_original(box, path, action, option, set_image, true);
}

bool refresh_cache() {
  int user = static_cast<int>(owner_id());
  static MonoMethod *method = nullptr;
  static bool resolved = false;
  if (!resolved) {
    MonoImage *image = getDLLimage("Sce.Vsh.ProfileCache.dll");
    MonoClass *utilities =
        image ? mono_class_from_name(image, "Sce.Vsh.ProfileCache", "Utilities")
              : nullptr;
    method = utilities
                 ? mono_class_get_method_from_name(utilities, "RefreshCache", 1)
                 : nullptr;
    if (method)
      resolved = true;
    LOG_INFO("[AVATAR] ProfileCache.Utilities.RefreshCache=%p",
             static_cast<void *>(method));
  }
  if (!method || user <= 0)
    return false;
  void *arguments[1] = {&user};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, nullptr, arguments, &exception);
  LOG_INFO("[AVATAR] profile cache refresh user=0x%x exception=%d",
           static_cast<unsigned>(user), exception ? 1 : 0);
  return exception == nullptr;
}

} // namespace

void Install(MonoImage *pui) {
  if (!pui || s_box_original)
    return;
  s_box_class = mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "ImageBox");
  s_options_class =
      mono_class_from_name(pui, "Sce.PlayStation.PUI", "ImageOptions");
  const uint64_t address =
      s_box_class ? Get_Address_of_Method(pui, s_box_class, "LoadAsync", 4) : 0;
  const bool installed =
      address && s_options_class &&
      InstallDetour(address, reinterpret_cast<void *>(&box_hook),
                    reinterpret_cast<void **>(&s_box_original));
  LOG_INFO("[AVATAR] ImageBox.LoadAsync hook at 0x%lx installed=%d",
           static_cast<unsigned long>(address), installed ? 1 : 0);
  if (!installed)
    s_box_original = nullptr;
}

const std::vector<std::string> &Files(bool rescan) {
  if (s_scanned && !rescan)
    return s_files;
  (void)mkdir_tree(kDirectory);
  s_files.clear();
  s_unsupported = 0;
  if (DIR *handle = opendir(kDirectory)) {
    while (dirent *entry = readdir(handle)) {
      const std::string path = std::string(kDirectory) + "/" + entry->d_name;
      if (entry->d_name[0] == '.' || !is_nonempty_file(path))
        continue;
      if (has_extension(entry->d_name, kPictures))
        s_files.push_back(path);
      else
        ++s_unsupported;
    }
    closedir(handle);
  }
  std::sort(s_files.begin(), s_files.end(),
            [](const std::string &left, const std::string &right) {
              return lower(AniketosUI::FileName(left)) < lower(AniketosUI::FileName(right));
            });
  s_scanned = true;
  LOG_INFO("[AVATAR] catalog '%s' files=%zu", kDirectory, s_files.size());
  return s_files;
}

size_t Unsupported() { return s_unsupported; }

void Tick() {
  consume_fit();
  if (s_frames < 3601)
    ++s_frames;
  static constexpr int kCacheFrames[] = {300, 900, 1800, 3600};
  if (!s_cache_done &&
      s_cache_attempt < std::size(kCacheFrames) &&
      s_frames >= kCacheFrames[s_cache_attempt]) {
    LOG_INFO("[AVATAR] refresh attempt=%zu frame=%d", s_cache_attempt + 1,
             s_frames);
    ++s_cache_attempt;
    if (s_box_original && !chosen_path().empty() && refresh_cache()) {
      s_cache_done = true;
      LOG_INFO("[AVATAR] refresh cache ok frame=%d", s_frames);
    }
  }
  for_each_live_box([](TrackedBox &entry, MonoObject *box) {
    if (entry.frames > 0 && --entry.frames == 0)
      verify_box(entry, box);
  });
}

std::string SignedInUser() {
  const uint32_t user = foreground_user();
  if (!user)
    return {};
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(user));
  return text;
}

void Refresh() {
  if (!s_box_original)
    return;
  report_chosen();
  const std::string path = effective_path();
  const bool animated = !path.empty() && has_extension(path, kAnimated);
  int reloaded = 0;
  for_each_live_box([&](TrackedBox &entry, MonoObject *box) {
    if (AniketosUI::HomeScene::IsDisposed(box))
      return;
    MonoObject *action = Reflect::Target(entry.action);
    if (entry.action && !action) {
      log_skip(entry.path, "action");
      return;
    }
    entry.frames = kVerifyFrames;
    if (path.empty()) {
      entry.stage = kTrySystem;
      call_original(box, entry.uri, action, nullptr, entry.set_image, false);
    } else {
      entry.path = path;
      entry.stage = animated ? kTryAnimated : kTryStill;
      call_original(box, path, action, make_options(animated), entry.set_image,
                    animated);
    }
    ++reloaded;
  });
  LOG_INFO("[AVATAR] refreshed %d avatar(s) path='%s' enabled=%d", reloaded,
           path.c_str(), g_settings.account_avatar_enabled ? 1 : 0);
  refresh_cache();
}

} // namespace AccountAvatar

extern "C" int avatar_codec_decode_fits(const unsigned char *data, size_t size) {
  return AvatarImage::DecodeFits(AvatarImage::Inspect(data, size));
}
