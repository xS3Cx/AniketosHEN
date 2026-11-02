#include "aniketos_theme/custom_font.hpp"

#include "aniketos_shell/rn_scripts/rn_reload.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>
#include <onion/settings.hpp>

#include <ps5/kernel.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace AniketosTheme::CustomFont {
namespace {

using AniketosUI::MonotonicMs;
using AniketosUI::Reflect::MethodOf;

/* libkernel's description of a loaded module (sceKernelGetModuleInfo). */
struct ModuleSegment {
  uint64_t address;
  uint32_t size;
  int32_t prot;
};
struct ModuleInfo {
  uint64_t size;
  char name[256];
  ModuleSegment segments[4];
  uint32_t segment_count;
  uint8_t fingerprint[20];
};
static_assert(sizeof(ModuleInfo) == 0x160);

using OpenFn = int (*)(const char *path, int flags, int mode);
using QueryFn = int (*)(void *address, void **start, void **end,
                        uint32_t *prot);
OpenFn s_open = nullptr;   /* libkernel's sceKernelOpen */
QueryFn s_query = nullptr; /* sceKernelQueryMemoryProtection */
uint64_t *s_slot = nullptr; /* libScePsm's import of sceKernelOpen */

/* libScePsm builds a font's path from a table of file names, one row per
   script, and keeps every file it opened, by path, for good. To make it
   open the Latin fonts again, their names in that table are swapped for
   names of the same length that no file has (SST-Roman.otf ->
   SST-Roman.k01); open_hook hands it the user's file for those. */
struct Name {
  uint64_t *pointer = nullptr; /* the table's word holding the name */
  uint64_t original = 0;
  char renamed[32] = {};
};
constexpr int kMaxNames = 16;
Name s_names[kMaxNames];
int s_name_count = 0;
int s_generation = 0;

/* The file handed out for the renamed fonts. Written on the UI thread, read
   on the text engine's threads: two buffers, swapped. */
char s_paths[2][256];
std::atomic<const char *> s_swap{nullptr};

/* Written while a font is being put in place and for a while after it: if
   ShellUI goes down meanwhile, its next start finds it and keeps the
   system's font, so a font the engine cannot take is no restart loop. */
constexpr const char *kPending = ONION_DIST_USER_DATA_ROOT "/font_pending";
constexpr time_t kPendingSeconds = 30;
time_t s_pending_since = 0;
/* A second relayout a moment after a switch: a label the text thread was
   still drawing with the old font kept that drawing in a box measured with
   the new one, cut short (Toolbox subtitles). 0: none due. */
uint64_t s_relayout_at_ms = 0;
constexpr uint64_t kRelayoutDelayMs = 600;
constexpr int kReloadDelayMs = 1500;
bool s_held_back = false;

std::vector<std::string> s_files;
bool s_scanned = false;
std::string s_applied; /* the file in effect; empty: the system's font */

bool readable(const void *address, size_t length) {
  void *start = nullptr, *end = nullptr;
  uint32_t prot = 0;
  return s_query &&
         s_query(const_cast<void *>(address), &start, &end, &prot) == 0 &&
         (prot & PROT_READ) &&
         static_cast<const char *>(address) + length <=
             static_cast<char *>(end);
}

/* SST-Roman.otf, SST-BoldItalic.otf, ... but not SSTArabic-, SSTJpPro-,
   SSTThai-, SSTTypewriter-, SSTVietnamese-: those carry other scripts. */
bool latin_name(const char *name, size_t length) {
  return length > 8 && length < sizeof(Name::renamed) &&
         std::strncmp(name, "SST-", 4) == 0 &&
         std::strncmp(name + length - 4, ".otf", 4) == 0;
}

/* A name we gave: SST-<style>.k<two base-36 digits>. */
bool renamed(const char *path) {
  const char *name = std::strrchr(path, '/');
  name = name ? name + 1 : path;
  const size_t length = std::strlen(name);
  return length > 8 && std::strncmp(name, "SST-", 4) == 0 &&
         name[length - 4] == '.' && name[length - 3] == 'k' &&
         std::isalnum(static_cast<unsigned char>(name[length - 2])) &&
         std::isalnum(static_cast<unsigned char>(name[length - 1]));
}

int open_hook(const char *path, int flags, int mode) {
  if (!path || !renamed(path))
    return s_open(path, flags, mode);
  const char *swap = s_swap.load(std::memory_order_acquire);
  const int fd = swap ? s_open(swap, flags, mode) : -1;
  if (fd >= 0)
    return fd;
  /* the user's file went away: the system's own font after all */
  char original[512];
  std::snprintf(original, sizeof(original), "%s", path);
  std::memcpy(original + std::strlen(original) - 3, "otf", 3);
  return s_open(original, flags, mode);
}

/* Finds the table's Latin font names in a writable segment: (pointer,
   length, 0) words whose string is SST-*.otf of that length. Each string is
   checked with the kernel's protection query first: nothing unmapped is
   read. */
void find_names(const ModuleSegment &segment) {
  auto *word = reinterpret_cast<uint64_t *>(segment.address);
  auto *last = word + segment.size / 8 - 3;
  for (; word <= last && s_name_count < kMaxNames; ++word) {
    const uint64_t length = word[1];
    if (!word[0] || word[2] != 0 || length < 9 ||
        length >= sizeof(Name::renamed))
      continue;
    const char *text = reinterpret_cast<const char *>(word[0]);
    if (!readable(text, length + 1) || text[length] != '\0' ||
        !latin_name(text, length))
      continue;
    Name &name = s_names[s_name_count++];
    name.pointer = word;
    name.original = word[0];
    std::memcpy(name.renamed, text, length);
    LOG_INFO("[FONT] font name '%s' at %p", text, static_cast<void *>(word));
  }
}

/* Points libScePsm's sceKernelOpen import at open_hook: the slot is the word
   in its data segments that holds libkernel's sceKernelOpen. */
bool patch_import() {
  using GetModuleInfoFn = int (*)(uint32_t handle, ModuleInfo *info);
  uint32_t kernel = 0, psm = 0;
  if (kernel_dynlib_handle(getpid(), "libkernel_sys.sprx", &kernel) != 0 &&
      kernel_dynlib_handle(getpid(), "libkernel.sprx", &kernel) != 0)
    return false;
  if (kernel_dynlib_handle(getpid(), "libScePsm.sprx", &psm) != 0 || !psm)
    return false;
  s_open = reinterpret_cast<OpenFn>(
      kernel_dynlib_dlsym(getpid(), kernel, "sceKernelOpen"));
  s_query = reinterpret_cast<QueryFn>(
      kernel_dynlib_dlsym(getpid(), kernel, "sceKernelQueryMemoryProtection"));
  const auto get_info = reinterpret_cast<GetModuleInfoFn>(
      kernel_dynlib_dlsym(getpid(), kernel, "sceKernelGetModuleInfo"));
  ModuleInfo info{};
  info.size = sizeof(info);
  if (!s_open || !s_query || !get_info || get_info(psm, &info) != 0 ||
      info.segment_count == 0 || info.segment_count > 4) {
    LOG_WARN("[FONT] libScePsm not described (open=%p info=%p count=%u)",
             reinterpret_cast<void *>(s_open),
             reinterpret_cast<void *>(get_info), info.segment_count);
    return false;
  }
  const uint64_t wanted = reinterpret_cast<uint64_t>(s_open);
  for (uint32_t i = 0; i < info.segment_count; ++i) {
    const ModuleSegment &segment = info.segments[i];
    LOG_INFO("[FONT] libScePsm segment %u 0x%lx+0x%x prot=%d", i,
             static_cast<unsigned long>(segment.address), segment.size,
             segment.prot);
    /* code is execute-only: reading it faults */
    if ((segment.prot & PROT_EXEC) || !(segment.prot & PROT_READ))
      continue;
    if (segment.prot & PROT_WRITE)
      find_names(segment);
    if (s_slot)
      continue;
    auto *words = reinterpret_cast<uint64_t *>(segment.address);
    for (uint32_t w = 0; w < segment.size / 8; ++w) {
      if (words[w] != wanted)
        continue;
      if (!(segment.prot & PROT_WRITE) &&
          kernel_mprotect(getpid(), segment.address, segment.size,
                          PROT_READ | PROT_WRITE) != 0) {
        LOG_WARN("[FONT] import slot %p not writable",
                 static_cast<void *>(&words[w]));
        return false;
      }
      s_slot = &words[w];
      break;
    }
  }
  if (!s_slot || s_name_count == 0) {
    LOG_WARN("[FONT] libScePsm: open import %p, %d font names",
             static_cast<void *>(s_slot), s_name_count);
    s_slot = nullptr;
    return false;
  }
  __atomic_store_n(s_slot, reinterpret_cast<uint64_t>(&open_hook),
                   __ATOMIC_RELEASE);
  LOG_INFO("[FONT] libScePsm sceKernelOpen import %p redirected",
           static_cast<void *>(s_slot));
  return true;
}

/* The table's names: the system's (on == false) or this generation's. */
void rename_fonts(bool on) {
  for (int i = 0; i < s_name_count; ++i) {
    Name &name = s_names[i];
    const size_t length = std::strlen(name.renamed);
    if (on) {
      constexpr char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
      const int generation = s_generation % (36 * 36);
      name.renamed[length - 3] = 'k';
      name.renamed[length - 2] = kDigits[generation / 36];
      name.renamed[length - 1] = kDigits[generation % 36];
    }
    __atomic_store_n(name.pointer,
                     on ? reinterpret_cast<uint64_t>(name.renamed)
                        : name.original,
                     __ATOMIC_RELEASE);
  }
}

std::string wanted_file() {
  if (!g_settings.custom_font_enabled)
    return {};
  const std::vector<std::string> &files = Files();
  if (std::find(files.begin(), files.end(), g_settings.custom_font_path) !=
      files.end())
    return g_settings.custom_font_path;
  return files.empty() ? std::string() : files.front();
}

/* A static PUI method by name, run here; false on a missing method or an
   exception. */
bool call_static(const char *name_space, const char *klass, const char *method,
                 int count, void **args) {
  MonoClass *type =
      pui_img ? mono_class_from_name(pui_img, name_space, klass) : nullptr;
  MonoMethod *found = MethodOf(type, method, count);
  MonoObject *exception = nullptr;
  if (found)
    mono_runtime_invoke(found, nullptr, args, &exception);
  LOG_INFO("[FONT] %s.%s %s", klass, method,
           !found ? "not found" : exception ? "threw" : "done");
  return found && !exception;
}

/* UI3 labels lay their text out again when the layout's forced locale
   changes: toggling it between null and "" (which the string loader
   ignores) redraws every label with the fonts PUI hands out now. */
void relayout_labels() {
  MonoClass *system =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UISystem");
  MonoMethod *get_layout = MethodOf(system, "get_LayoutSettings", 0);
  MonoObject *exception = nullptr;
  MonoObject *layout = get_layout ? mono_runtime_invoke(get_layout, nullptr,
                                                        nullptr, &exception)
                                  : nullptr;
  MonoMethod *get_locale = MethodOf(layout, "get_ForceOverriddenLocale", 0);
  MonoMethod *set_locale = MethodOf(layout, "set_ForceOverriddenLocale", 1);
  if (!get_locale || !set_locale || exception) {
    LOG_WARN("[FONT] LayoutSettings.ForceOverriddenLocale not found");
    return;
  }
  MonoObject *locale =
      mono_runtime_invoke(get_locale, layout, nullptr, &exception);
  /* only our own toggle: a locale someone really forced stays */
  const char *text =
      locale ? mono_string_to_utf8(reinterpret_cast<MonoString *>(locale))
             : nullptr;
  const bool ours = !locale || (text && text[0] == '\0');
  if (text)
    mono_free(const_cast<char *>(text));
  void *args[1] = {locale ? nullptr
                          : mono_string_new(mono_domain_get(), "")};
  if (ours && !exception)
    mono_runtime_invoke(set_locale, layout, args, &exception);
  LOG_INFO("[FONT] labels relaid out=%d exception=%d", ours ? 1 : 0,
           exception ? 1 : 0);
}

/* Makes the text on screen take the font now in the table (UI thread).
   PUI keeps the Core fonts it made in an LRU; disposing them lets the engine
   drop its faces, so the next text builds new ones and opens the files
   again, and every label is laid out again. */
void refresh_text() {
  if (!call_static("Sce.PlayStation.PUI.UI3", "UIFont", "ClearCache", 0,
                   nullptr))
    return;
  /* PUI's LruCache.Clear() empties its map and list but keeps its count, so
     after a second clear the cache thinks it is full and evicts from an
     empty list (NullReferenceException in TryAdd, ShellUI killed). The count
     is set back here; a font added meanwhile only lets the cache grow by one. */
  MonoClass *manager =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UIFontManager");
  MonoClassField *cache_field =
      manager ? mono_class_get_field_from_name(manager, "lruCache") : nullptr;
  MonoObject *cache = nullptr;
  if (cache_field)
    mono_field_static_get_value(mono_class_vtable(mono_domain_get(), manager),
                                cache_field, &cache);
  MonoClassField *count =
      cache ? mono_class_get_field_from_name(mono_object_get_class(cache),
                                             "_count")
            : nullptr;
  int zero = 0;
  if (count)
    mono_field_set_value(cache, count, &zero);
  else
    LOG_WARN("[FONT] UIFontManager.lruCache._count not found");
  relayout_labels();
}

} // namespace

const std::vector<std::string> &Files(bool rescan) {
  if (s_scanned && !rescan)
    return s_files;
  s_scanned = true;
  s_files.clear();
  (void)mkdir(kDirectory, 0777);
  if (DIR *directory = opendir(kDirectory)) {
    while (const dirent *entry = readdir(directory)) {
      std::string name = entry->d_name;
      std::string extension =
          name.size() > 4 ? name.substr(name.size() - 4) : std::string();
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char ch) { return std::tolower(ch); });
      if (name[0] != '.' && (extension == ".otf" || extension == ".ttf"))
        s_files.push_back(std::string(kDirectory) + "/" + name);
    }
    closedir(directory);
  }
  std::sort(s_files.begin(), s_files.end());
  return s_files;
}

int Generation() { return s_generation; }

void Install() {
  if (unlink(kPending) == 0) {
    s_held_back = true;
    LOG_WARN("[FONT] ShellUI went down with a custom font: system font kept");
  }
  if (!s_slot && !patch_import())
    LOG_WARN("[FONT] custom fonts unavailable");
}

void Tick() {
  if (!s_slot)
    return;
  if (s_relayout_at_ms && MonotonicMs() >= s_relayout_at_ms) {
    s_relayout_at_ms = 0;
    relayout_labels();
  }
  if (s_pending_since && std::time(nullptr) - s_pending_since > kPendingSeconds) {
    unlink(kPending);
    s_pending_since = 0;
  }
  std::string wanted = wanted_file();
  if (s_held_back) {
    /* until the switch is turned off and on again */
    if (!g_settings.custom_font_enabled)
      s_held_back = false;
    wanted.clear();
  }
  if (wanted == s_applied)
    return;
  s_applied = wanted;
  if (!wanted.empty()) {
    const int fd = open(kPending, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
      close(fd);
    s_pending_since = std::time(nullptr);
  }
  const char *current = s_swap.load(std::memory_order_relaxed);
  char *next = current == s_paths[0] ? s_paths[1] : s_paths[0];
  std::snprintf(next, sizeof(s_paths[0]), "%s", wanted.c_str());
  s_swap.store(wanted.empty() ? nullptr : next, std::memory_order_release);
  ++s_generation;
  rename_fonts(!wanted.empty());
  LOG_INFO("[FONT] text in %s (generation %d)",
           wanted.empty() ? "the system font" : wanted.c_str(), s_generation);
  refresh_text();
  s_relayout_at_ms = MonotonicMs() + kRelayoutDelayMs;
  /* Home and Control Center stay loaded and keep the text boxes React
     measured with the old font: a wider font is cut off with "...". They
     are reloaded once the choice rests (each switch pushes the time back);
     other apps start fresh when opened. */
  shellui_request_home_reload_after(kReloadDelayMs);
  shellui_request_presence_reload(kReloadDelayMs, false);
}

} // namespace AniketosTheme::CustomFont
