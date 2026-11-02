#include "aniketos_shell/system_ui/account_avatar.hpp"

#include "aniketos_shell/system_ui/avatar_image.hpp"
#include "aniketos_shell/system_ui/avatar_codec.h"
#include "hooked_funcs.hpp"
#include "aniketos_theme/clock/shell_clock.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <initializer_list>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

extern onion::Settings g_settings;
extern "C" int sceUserServiceGetForegroundUser(int *user_id);

namespace AccountAvatar {
namespace {

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

struct TrackedBox {
  uint32_t box = 0;
  uint32_t action = 0;
  std::string uri;
  std::string path;
  bool set_image = false;
  bool animated = false;
  int stage = 0;
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
std::atomic<bool> s_fit_running{false};
std::atomic<bool> s_fit_done{false};
std::atomic<bool> s_fit_ok{false};

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool first_time(std::vector<std::string> &seen, const std::string &key) {
  for (const std::string &known : seen)
    if (known == key)
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

bool exists(const std::string &path) {
  struct stat info {};
  return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
         info.st_size > 0;
}

std::string to_string(MonoString *text) {
  std::string out;
  if (!text || !mono_string_to_utf8)
    return out;
  const char *utf8 = mono_string_to_utf8(text);
  if (utf8) {
    out = utf8;
    if (mono_free)
      mono_free(const_cast<char *>(utf8));
  }
  return out;
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

std::string chosen() {
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
  if (!exists(path)) {
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

std::string cache_path_for(const std::string &source) {
  const std::string name = FileName(source);
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
  if (s_fit_running.load())
    return;
  start_fit();
}

std::string effective_path() {
  const std::string source = chosen();
  std::string cache;
  if (source.empty())
    return source;
  if (!avatar_codec_should_fit(source.c_str()))
    return source;
  (void)mkdir(ONION_DIST_USER_DATA_ROOT "/ShellUI", 0777);
  (void)mkdir(kDirectory, 0777);
  (void)mkdir(kCacheDirectory, 0777);
  cache = cache_path_for(source);
  if (avatar_codec_cache_valid(source.c_str(), cache.c_str()))
    return cache;
  request_fit(source, cache);
  return source;
}

void consume_fit() {
  unsigned long generation = 0;
  bool ok = false;
  std::string source;
  std::string cache;
  if (!s_fit_done.load())
    return;
  ok = s_fit_ok.load();
  generation = s_fit_job.generation;
  source = s_fit_job.source;
  cache = s_fit_job.cache;
  s_fit_done.store(false);
  s_fit_running.store(false);
  if (generation != s_fit_generation) {
    if (!s_fit_desired_source.empty())
      start_fit();
    return;
  }
  if (ok && avatar_codec_cache_valid(source.c_str(), cache.c_str()))
    Refresh();
}

void free_box(TrackedBox &entry) {
  if (entry.action)
    mono_gchandle_free(entry.action);
  if (entry.box)
    mono_gchandle_free(entry.box);
  entry.action = 0;
  entry.box = 0;
}

TrackedBox *find_box(MonoObject *box) {
  for (auto it = s_boxes.begin(); it != s_boxes.end();) {
    MonoObject *known = mono_gchandle_get_target(it->box);
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

TrackedBox *remember(MonoObject *box) {
  if (TrackedBox *found = find_box(box))
    return found;
  if (s_boxes.size() >= kMaxBoxes) {
    free_box(s_boxes.front());
    s_boxes.erase(s_boxes.begin());
  }
  s_boxes.push_back({});
  TrackedBox &entry = s_boxes.back();
  entry.box = mono_gchandle_new_weakref(box, 0);
  return &entry;
}

MonoObject *make_options(bool animated) {
  if (!s_options_class)
    return nullptr;
  MonoObject *option = New_Object(s_options_class);
  if (!option)
    return nullptr;
  mono_runtime_object_init(option);
  Set_Property(s_options_class, option, "IsAnimatedImage", animated);
  Set_Property(s_options_class, option, "AnimationDecodeMode",
               animated ? 1 : 0);
  return option;
}

void configure_options(MonoObject *option, bool animated) {
  if (!option || !s_options_class)
    return;
  Set_Property(s_options_class, option, "IsAnimatedImage", animated);
  Set_Property(s_options_class, option, "AnimationDecodeMode",
               animated ? 1 : 0);
}

void call_original(MonoObject *box, const std::string &uri, MonoObject *action,
                   MonoObject *option, bool set_image, bool play) {
  if (!box || !s_box_original)
    return;
  if (play && s_box_class) {
    Set_Property(s_box_class, box, "Loop", true);
    Set_Property(s_box_class, box, "Autoplay", true);
  }
  MonoString *managed = mono_string_new(mono_domain_get(), uri.c_str());
  s_box_original(box, managed, action, option, set_image);
  if (play && s_box_class) {
    Set_Property(s_box_class, box, "Loop", true);
    Set_Property(s_box_class, box, "Autoplay", true);
  }
}

MonoObject *action_of(const TrackedBox &entry) {
  return entry.action ? mono_gchandle_get_target(entry.action) : nullptr;
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
  if (ShellClock::IsWidgetDisposed(box)) {
    entry.frames = 0;
    return;
  }
  if (image_loaded(box)) {
    entry.stage = 3;
    entry.frames = 0;
    if (!entry.uri.empty() && first_time(s_logged_loads, entry.uri))
      LOG_INFO("[AVATAR] loaded ok box=%p stage=%d", static_cast<void *>(box),
               entry.stage);
    return;
  }
  if (entry.stage == 0) {
    entry.animated = false;
    entry.stage = 1;
    entry.frames = kVerifyFrames;
    call_original(box, entry.path, action_of(entry), make_options(false),
                  entry.set_image, false);
    if (first_time(s_logged_fallbacks, entry.uri))
      LOG_INFO("[AVATAR] animated fallback path=%s", entry.path.c_str());
    return;
  }
  if (entry.stage == 1) {
    entry.animated = false;
    entry.stage = 2;
    entry.frames = kVerifyFrames;
    call_original(box, entry.uri, action_of(entry), nullptr, entry.set_image,
                  false);
    if (first_time(s_logged_fallbacks, entry.uri + "|system"))
      LOG_INFO("[AVATAR] system fallback uri=%s", entry.uri.c_str());
    return;
  }
  if (entry.stage == 2) {
    entry.frames = 0;
    if (first_time(s_logged_failures, entry.uri)) {
      LOG_INFO("[AVATAR] load failed uri=%s", entry.uri.c_str());
      notify("notify.vsh.account_avatar.load_failed",
             FileName(entry.path).c_str());
    }
    return;
  }
  entry.frames = 0;
}

void box_hook(MonoObject *box, MonoString *uri, MonoObject *action,
              MonoObject *option, bool set_image) {
  const std::string text = to_string(uri);
  if (!box || !is_own_face(text)) {
    s_box_original(box, uri, action, option, set_image);
    return;
  }
  const std::string path = effective_path();
  if (path.empty()) {
    s_box_original(box, uri, action, option, set_image);
    return;
  }
  const bool animated = has_extension(path, kAnimated);
  if (TrackedBox *entry = remember(box)) {
    if (action) {
      if (entry->action)
        mono_gchandle_free(entry->action);
      entry->action = mono_gchandle_new_weakref(action, 0);
    }
    entry->uri = text;
    entry->path = path;
    entry->set_image = set_image;
    entry->animated = animated;
    entry->stage = animated ? 0 : 1;
    entry->frames = kVerifyFrames;
  }
  if (!option && s_options_class) {
    option = New_Object(s_options_class);
    if (option)
      mono_runtime_object_init(option);
  }
  configure_options(option, animated);
  if (path != s_logged_path) {
    s_logged_path = path;
    LOG_INFO("[AVATAR] %s -> %s animated=%d", text.c_str(), path.c_str(),
             animated ? 1 : 0);
  }
  call_original(box, path, action, option, set_image, true);
}

bool refresh_cache(int user) {
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

int owner() { return static_cast<int>(owner_id()); }

}

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
  (void)mkdir(ONION_DIST_USER_DATA_ROOT "/ShellUI", 0777);
  (void)mkdir(kDirectory, 0777);
  s_files.clear();
  s_unsupported = 0;
  if (DIR *handle = opendir(kDirectory)) {
    while (dirent *entry = readdir(handle)) {
      const std::string path = std::string(kDirectory) + "/" + entry->d_name;
      if (entry->d_name[0] == '.' || !exists(path))
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
              return lower(FileName(left)) < lower(FileName(right));
            });
  s_scanned = true;
  LOG_INFO("[AVATAR] catalog '%s' files=%zu", kDirectory, s_files.size());
  return s_files;
}

std::string FileName(const std::string &path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

size_t Unsupported() { return s_unsupported; }

const char *Directory() { return kDirectory; }

namespace {

/* Tells the user at once why the chosen picture will not show. A GIF over the
   limits is fitted instead (avatar_codec), so only other formats are warned
   about those. */
void report_chosen() {
  const std::string path = chosen();
  if (path.empty())
    return;
  struct stat file_info {};
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return;
  std::vector<uint8_t> bytes;
  if (fstat(fileno(file), &file_info) == 0)
    bytes.resize(std::min(static_cast<size_t>(file_info.st_size), kMaxInspected));
  bytes.resize(std::fread(bytes.data(), 1, bytes.size(), file));
  std::fclose(file);
  const AvatarImage::Info info = AvatarImage::Inspect(bytes.data(), bytes.size());
  const std::string name = FileName(path);
  LOG_INFO("[AVATAR] '%s' format=%d %dx%d frames=%d", name.c_str(),
           static_cast<int>(info.format), info.width, info.height, info.frames);
  const AvatarImage::Problem problem = AvatarImage::Check(info);
  if (problem != AvatarImage::Problem::NotPicture && has_extension(path, kFitted))
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

} // namespace

void Tick() {
  consume_fit();
  if (s_frames < 3601)
    ++s_frames;
  static constexpr int kCacheFrames[] = {300, 900, 1800, 3600};
  if (!s_cache_done &&
      s_cache_attempt < sizeof(kCacheFrames) / sizeof(kCacheFrames[0]) &&
      s_frames >= kCacheFrames[s_cache_attempt]) {
    LOG_INFO("[AVATAR] refresh attempt=%zu frame=%d", s_cache_attempt + 1,
             s_frames);
    ++s_cache_attempt;
    if (s_box_original && !chosen().empty() && refresh_cache(owner())) {
      s_cache_done = true;
      LOG_INFO("[AVATAR] refresh cache ok frame=%d", s_frames);
    }
  }
  for (auto it = s_boxes.begin(); it != s_boxes.end();) {
    MonoObject *box = mono_gchandle_get_target(it->box);
    if (!box) {
      free_box(*it);
      it = s_boxes.erase(it);
      continue;
    }
    if (it->frames > 0) {
      --it->frames;
      if (it->frames <= 0)
        verify_box(*it, box);
    }
    ++it;
  }
}

std::string SignedInUser() {
  int user = -1;
  if (sceUserServiceGetForegroundUser(&user) != 0 || user <= 0)
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
  for (auto it = s_boxes.begin(); it != s_boxes.end();) {
    MonoObject *box = mono_gchandle_get_target(it->box);
    if (!box) {
      free_box(*it);
      it = s_boxes.erase(it);
      continue;
    }
    if (ShellClock::IsWidgetDisposed(box)) {
      ++it;
      continue;
    }
    MonoObject *action = nullptr;
    if (it->action) {
      action = mono_gchandle_get_target(it->action);
      if (!action) {
        log_skip(it->path, "action");
        ++it;
        continue;
      }
    }
    if (path.empty()) {
      it->animated = false;
      it->stage = 2;
      it->frames = kVerifyFrames;
      call_original(box, it->uri, action, nullptr, it->set_image, false);
    } else {
      it->path = path;
      it->animated = animated;
      it->stage = animated ? 0 : 1;
      it->frames = kVerifyFrames;
      call_original(box, path, action, make_options(animated), it->set_image,
                    animated);
    }
    ++reloaded;
    ++it;
  }
  LOG_INFO("[AVATAR] refreshed %d avatar(s) path='%s' enabled=%d", reloaded,
           path.c_str(), g_settings.account_avatar_enabled ? 1 : 0);
  refresh_cache(owner());
}

}
