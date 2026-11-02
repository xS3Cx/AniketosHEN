#include "aniketos_shell/account_avatar.hpp"

#include "avatar_image.hpp"
#include "hooked_funcs.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <initializer_list>
#include <sys/stat.h>
#include <utility>

extern onion::Settings g_settings;
extern "C" int sceUserServiceGetForegroundUser(int *user_id);

namespace AccountAvatar {
namespace {

constexpr const char *kDirectory = ONION_DIST_USER_DATA_ROOT "/ShellUI/avatars";
constexpr const char *kFacePrefix = "profilecache:image/face/";
constexpr std::initializer_list<const char *> kPictures = {
    ".gif", ".webp", ".png", ".jpg", ".jpeg"};
constexpr std::initializer_list<const char *> kAnimated = {".gif", ".webp"};

constexpr size_t kMaxBoxes = 32;
/* Enough of a file to count the frames of any picture that can play. */
constexpr size_t kMaxInspected = 16 * 1024 * 1024;

/* UI3.ImageBox.LoadAsync(string uri, Action<...> loadedAction,
   ImageOptions option, bool setImage): the load every React Native image
   (RCTImageView, the avatar's RCTAlphaMaskedImage) goes through, on the UI
   thread. */
using BoxLoadFn = void (*)(MonoObject *box, MonoString *uri,
                           MonoObject *action, MonoObject *option,
                           bool set_image);
BoxLoadFn s_box_original = nullptr;
MonoClass *s_box_class = nullptr;
MonoClass *s_options_class = nullptr;

std::vector<std::string> s_files;
size_t s_unsupported = 0; /* other files in the folder */
bool s_scanned = false;

/* The boxes showing the user's picture (weak), with the URI they asked for,
   so a change of setting can load them again. */
std::vector<std::pair<uint32_t, std::string>> s_boxes;
std::string s_logged_path;

/* Times the chosen picture failed to load; at kMaxFailures the user's own
   picture stays. */
constexpr int kMaxFailures = 2;
int s_failures = 0;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
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

/* The user whose picture is replaced: the one who chose it, else whoever is
   signed in; 0 for none. */
uint32_t owner_id() {
  if (!g_settings.account_avatar_user.empty())
    return static_cast<uint32_t>(
        std::strtoul(g_settings.account_avatar_user.c_str(), nullptr, 0));
  int user = -1;
  return sceUserServiceGetForegroundUser(&user) == 0 && user > 0
             ? static_cast<uint32_t>(user)
             : 0;
}

/* userid=<decimal or 0x hex> of a face URI is the user who chose the
   picture (the signed-in one when not known): on the user selection screen
   nobody is signed in yet. */
bool is_own_face(const std::string &uri) {
  if (uri.compare(0, std::strlen(kFacePrefix), kFacePrefix) != 0)
    return false;
  const size_t at = uri.find("userid=");
  if (at == std::string::npos)
    return false;
  const char *number = uri.c_str() + at + 7;
  const long long id = std::strtoll(number, nullptr, 0);
  const uint32_t user = owner_id();
  return user && user == static_cast<uint32_t>(id);
}

/* The picture wanted in place of the user's own, or none. */
std::string chosen() {
  if (!g_settings.account_avatar_enabled || s_failures >= kMaxFailures ||
      g_settings.account_avatar_path.empty() ||
      !has_extension(g_settings.account_avatar_path, kPictures) ||
      !exists(g_settings.account_avatar_path))
    return {};
  return g_settings.account_avatar_path;
}

void remember(MonoObject *box, const std::string &uri) {
  for (auto it = s_boxes.begin(); it != s_boxes.end();) {
    MonoObject *known = mono_gchandle_get_target(it->first);
    if (!known) {
      mono_gchandle_free(it->first);
      it = s_boxes.erase(it);
      continue;
    }
    if (known == box) {
      it->second = uri;
      return;
    }
    ++it;
  }
  if (s_boxes.size() >= kMaxBoxes) {
    mono_gchandle_free(s_boxes.front().first);
    s_boxes.erase(s_boxes.begin());
  }
  s_boxes.push_back({mono_gchandle_new_weakref(box, 0), uri});
}

void box_hook(MonoObject *box, MonoString *uri, MonoObject *action,
              MonoObject *option, bool set_image) {
  const std::string text = to_string(uri);
  if (!box || !is_own_face(text)) {
    s_box_original(box, uri, action, option, set_image);
    return;
  }
  remember(box, text);
  const std::string path = chosen();
  if (path.empty()) {
    s_box_original(box, uri, action, option, set_image);
    return;
  }
  const bool animated = has_extension(path, kAnimated);
  if (!option && s_options_class) {
    option = New_Object(s_options_class);
    if (option)
      mono_runtime_object_init(option);
  }
  if (option && s_options_class) {
    Set_Property(s_options_class, option, "IsAnimatedImage", animated);
    /* ImageAnimationDecodeMode: BulkDecode decodes all frames at load, and the
       native reader refuses that for a big or long GIF; ProgressiveDecode
       decodes them as they play. */
    Set_Property(s_options_class, option, "AnimationDecodeMode",
                 !animated ? 0 : s_failures ? 2 : 1);
  }
  if (path != s_logged_path) {
    s_logged_path = path;
    LOG_INFO("[AVATAR] %s -> %s animated=%d", text.c_str(), path.c_str(),
             animated ? 1 : 0);
  }
  s_box_original(box, mono_string_new(mono_domain_get(), path.c_str()), action,
                 option, set_image);
  if (animated && s_box_class) {
    Set_Property(s_box_class, box, "Loop", true);
    Set_Property(s_box_class, box, "Autoplay", true);
  }
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

/* Sce.Vsh.ProfileCache.Utilities.RefreshCache(int userId): the profile
   cache tells every app the user's profile changed; their avatars load again
   (a new salt), through the hook above. Pictures loaded before this module
   was in (the user selection screen right after ShellUI starts) are brought
   over that way too. */
void refresh_cache(int user) {
  static MonoMethod *method = nullptr;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    MonoImage *image = getDLLimage("Sce.Vsh.ProfileCache.dll");
    MonoClass *utilities =
        image ? mono_class_from_name(image, "Sce.Vsh.ProfileCache", "Utilities")
              : nullptr;
    method = utilities
                 ? mono_class_get_method_from_name(utilities, "RefreshCache", 1)
                 : nullptr;
    LOG_INFO("[AVATAR] ProfileCache.Utilities.RefreshCache=%p",
             static_cast<void *>(method));
  }
  if (!method || user <= 0)
    return;
  void *arguments[1] = {&user};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, nullptr, arguments, &exception);
  LOG_INFO("[AVATAR] profile cache refresh user=0x%x exception=%d",
           static_cast<unsigned>(user), exception ? 1 : 0);
}

int owner() { return static_cast<int>(owner_id()); }

int s_frames = 0;
int s_check_frames = 0;

/* The box behind a handle, if it is still in use. */
MonoObject *live_box(uint32_t handle) {
  MonoObject *box = mono_gchandle_get_target(handle);
  return box && !Get_Property<bool>(s_box_class, box, "IsDisposed") ? box
                                                                    : nullptr;
}

int reload_boxes() {
  /* Copied: loading may remember the boxes again. */
  const auto boxes = s_boxes;
  int reloaded = 0;
  for (const auto &[handle, uri] : boxes) {
    MonoObject *box = live_box(handle);
    if (!box)
      continue;
    box_hook(box, mono_string_new(mono_domain_get(), uri.c_str()), nullptr,
             nullptr, true);
    ++reloaded;
  }
  return reloaded;
}

/* Tells the user at once why the chosen picture will not show; the checks
   are advice, the load still decides (check_failed). */
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
  switch (AvatarImage::Check(info)) {
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

/* A box the loader could not fill shows its error picture, and nothing else
   tells: load again, the next way box_hook has, until the user's own picture
   is back. */
void check_failed() {
  if (s_boxes.empty())
    return;
  const std::string path = chosen();
  if (path.empty())
    return;
  for (const auto &[handle, uri] : s_boxes) {
    MonoObject *box = live_box(handle);
    if (!box || !Get_Property<bool>(s_box_class, box, "IsErrorStateShown"))
      continue;
    ++s_failures;
    LOG_INFO("[AVATAR] load failed path='%s' failures=%d", path.c_str(),
             s_failures);
    if (s_failures >= kMaxFailures)
      notify("notify.vsh.account_avatar.load_failed", FileName(path).c_str());
    reload_boxes();
    return;
  }
}

} // namespace

void Tick() {
  /* Once, a few seconds in: whatever was drawn before the hook. */
  if (s_frames >= 0 && ++s_frames >= 300) {
    s_frames = -1;
    if (s_box_original && !chosen().empty())
      refresh_cache(owner());
  }

  if (++s_check_frames >= 30) {
    s_check_frames = 0;
    check_failed();
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
  s_failures = 0;
  report_chosen();
  const int reloaded = reload_boxes();
  LOG_INFO("[AVATAR] refreshed %d avatar(s) path='%s' enabled=%d", reloaded,
           chosen().c_str(), g_settings.account_avatar_enabled ? 1 : 0);
  refresh_cache(owner());
}

} // namespace AccountAvatar
