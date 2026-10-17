#include <onion/distribution.h>
#include "aniketos_theme/customization.hpp"
#include "clock_text.hpp"
#include "shell_clock.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "aniketos_theme/animation.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/home_tiles.hpp"
#include "aniketos_theme/theme_config.hpp"
#include "toolbox_i18n.hpp"

#include <onion/detour.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <string>
#include <unistd.h>

/* Per-frame code: resolve each managed property once and reuse the compiled
 * accessor (see aniketos_ui/mono_fast.hpp) instead of looking it up by name on
 * every call. Same call syntax and semantics as the hooked_funcs.hpp helpers. */
#include "aniketos_ui/mono_fast.hpp"
#define Get_Property AniketosUI::MonoFast::Get
#define Set_Property AniketosUI::MonoFast::Set
#define Set_Property_Invoke AniketosUI::MonoFast::SetInvoke

extern onion::Settings g_settings;

namespace ShellClock {
/* Whether a widget lives in Home's scene (defined with the scene lookup). */
bool in_home_scene(MonoObject *widget);

namespace {

constexpr uint64_t kAnimationFrameMs = 50;
/* A clock label that sits behind another scene (Home behind Settings or the
 * Control Center) is not hidden outright: PUI parks it at a FinalOpacity of
 * about 0.02. Anything at or below this counts as not shown, so the date does
 * not stay painted over other screens and a clock that really is on screen
 * can take over from one that is only parked. */
constexpr float kShownOpacity = 0.05f;
constexpr size_t kMaxNativeClockAliases = 12;

uint32_t s_clock_handle = 0;
uint32_t s_custom_time_color_handle = 0;
void *s_custom_time_color_value = nullptr;
int s_custom_time_color_r = -1;
int s_custom_time_color_g = -1;
int s_custom_time_color_b = -1;

std::string s_last_stock_time;
std::string s_last_rendered;
time_t s_last_second = static_cast<time_t>(-1);
time_t s_last_language_retry = static_cast<time_t>(-1);
uint64_t s_settings_signature = 0;
int s_language_setting = -1;
bool s_force_refresh = false;
bool s_language_ready = false;
bool s_native_customized = false;
bool s_native_visible = false;
float s_native_opacity = 0.0f;
uint64_t s_animation_started_ms = 0;
uint64_t s_last_animation_frame_ms = 0;

float s_native_base_x = 0.0f;
float s_native_base_width = 0.0f;
float s_native_base_height = 0.0f;
float s_measured_time_width = 0.0f;
/* The geometry last given to the followed label, to notice when React
   writes its own back, and a few frames of forced redraw after React has
   touched the label. */
float s_applied_x = 0.0f;
float s_applied_width = 0.0f;
int s_redraw_frames = 0;
float s_visible_time_width = 0.0f;
float s_visible_time_center_x = 0.0f;
float s_visible_time_bottom_y = 0.0f;
bool s_has_global_time_geometry = false;
DateOverlayState s_date_overlay{};

// ShellUI can keep more than one native time label alive while switching
// between Home and system overlays. Keep each label's stock layout so a
// newly active clock never forces the previous visible one back to white.
struct NativeClockAlias {
  uint32_t handle = 0;
  float base_x = 0.0f;
  float base_width = 0.0f;
  float base_height = 0.0f;
  std::string stock_time;
  bool customized = false;
};
std::array<NativeClockAlias, kMaxNativeClockAliases> s_clock_aliases{};

#ifndef NDEBUG
// Debug-only, bounded evidence for clock visibility and lifecycle failures.
// Normal updates are recorded at 1 Hz; UI hooks remain free of per-frame file I/O.
struct ClockTraceEvent {
  uint64_t sequence = 0;
  time_t wall_time = 0;
  uint64_t monotonic_ms = 0;
  uintptr_t thread = 0;
  uintptr_t object = 0;
  uintptr_t parent = 0;
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float opacity = 0.0f;
  int disposed = -1;
  int finally_shown = -1;
  int final_opacity_visible = -1;
  char event[28]{};
  char class_name[80]{};
  char text[96]{};
};

constexpr size_t kClockTraceCapacity = 1000;
constexpr off_t kClockLogRotateBytes = 512 * 1024;
std::array<ClockTraceEvent, kClockTraceCapacity> s_clock_trace{};
size_t s_clock_trace_head = 0;
size_t s_clock_trace_count = 0;
uint64_t s_clock_trace_sequence = 0;
time_t s_trace_last_second = static_cast<time_t>(-1);
std::string s_trace_last_date;
int s_clock_log_fd = -1;
pthread_mutex_t s_clock_log_lock = PTHREAD_MUTEX_INITIALIZER;

bool make_clock_log_directory(const char *path) {
  if (!path || !path[0])
    return false;
  char copy[256]{};
  const int length = std::snprintf(copy, sizeof(copy), "%s", path);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(copy))
    return false;
  for (char *cursor = copy + 1; *cursor; ++cursor) {
    if (*cursor != '/')
      continue;
    *cursor = '\0';
    if (mkdir(copy, 0777) != 0 && errno != EEXIST)
      return false;
    *cursor = '/';
  }
  return mkdir(copy, 0777) == 0 || errno == EEXIST;
}

int open_clock_log_locked() {
  if (s_clock_log_fd >= 0)
    return s_clock_log_fd;
  static const char *const paths[] = {
      "/data/" HEN_FOLDER "/Aniketos/logs/clock",
      "/user/data/" HEN_FOLDER "/Aniketos/logs/clock",
  };
  for (const char *directory : paths) {
    if (!make_clock_log_directory(directory))
      continue;
    char path[320]{};
    const int length = std::snprintf(path, sizeof(path), "%s/clock.log",
                                     directory);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(path))
      continue;
    s_clock_log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (s_clock_log_fd >= 0)
      return s_clock_log_fd;
  }
  return -1;
}

void clock_log_write(const char *line, size_t length) {
  pthread_mutex_lock(&s_clock_log_lock);
  const int fd = open_clock_log_locked();
  if (fd >= 0) {
    struct stat st{};
    if (fstat(fd, &st) == 0 && st.st_size >= kClockLogRotateBytes) {
      close(s_clock_log_fd);
      s_clock_log_fd = -1;
      static const char *const paths[] = {
          "/data/" HEN_FOLDER "/Aniketos/logs/clock/clock.log",
          "/user/data/" HEN_FOLDER "/Aniketos/logs/clock/clock.log",
      };
      for (const char *path : paths) {
        if (access(path, F_OK) == 0) {
          char rotated[352]{};
          std::snprintf(rotated, sizeof(rotated), "%s.1", path);
          (void)rename(path, rotated);
          break;
        }
      }
    }
    const int write_fd = open_clock_log_locked();
    size_t offset = 0;
    while (write_fd >= 0 && offset < length) {
      const ssize_t written = write(write_fd, line + offset, length - offset);
      if (written <= 0)
        break;
      offset += static_cast<size_t>(written);
    }
  }
  pthread_mutex_unlock(&s_clock_log_lock);
}

void clock_logf(const char *format, ...) {
  char message[1400]{};
  va_list args;
  va_start(args, format);
  const int message_length = std::vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  if (message_length <= 0)
    return;
  const size_t used = std::min(static_cast<size_t>(message_length),
                               sizeof(message) - 1);
  const time_t now = std::time(nullptr);
  std::tm local{};
  char timestamp[40] = "unknown";
  if (localtime_r(&now, &local))
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &local);
  char line[1500]{};
  const int line_length = std::snprintf(
      line, sizeof(line), "[%s] [CLOCK] %.*s\n", timestamp,
      static_cast<int>(used), message);
  if (line_length > 0)
    clock_log_write(line, std::min(static_cast<size_t>(line_length),
                                   sizeof(line) - 1));
}
#endif

void (*s_native_label_set_text_orig)(MonoObject *, MonoString *) = nullptr;
void (*s_native_label_on_update_orig)(MonoObject *) = nullptr;
MonoMethod *s_is_disposed_getter = nullptr;

uint64_t monotonic_millis() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000ULL +
         static_cast<uint64_t>(now.tv_nsec) / 1000000ULL;
}

bool clock_animation_active() {
  return AniketosTheme::GlobalTheme::Animated() ||
         g_settings.shell_background_animation ==
             static_cast<int>(AniketosTheme::AnimationMode::Rainbow);
}

AniketosTheme::RGB effective_clock_color() {
  if (AniketosTheme::GlobalTheme::Active())
    return AniketosTheme::GlobalTheme::Color(monotonic_millis());
  if (!clock_animation_active()) {
    return {std::clamp(g_settings.shell_clock_time_r, 0, 255) / 255.0f,
            std::clamp(g_settings.shell_clock_time_g, 0, 255) / 255.0f,
            std::clamp(g_settings.shell_clock_time_b, 0, 255) / 255.0f};
  }
  const uint64_t now = monotonic_millis();
  if (!s_animation_started_ms)
    s_animation_started_ms = now;
  const AniketosTheme::ThemeConfig config =
      AniketosTheme::ConfigFromSettings(g_settings);
  const uint64_t elapsed_ms =
      ((now - s_animation_started_ms) / kAnimationFrameMs) *
      kAnimationFrameMs;
  return AniketosTheme::EvaluateAnimation(
             config, static_cast<float>(elapsed_ms) / 1000.0f)
      .glow;
}

MonoClass *resolve_property_class(MonoObject *object, const char *name) {
  if (!object || !name || !mono_object_get_class || !mono_class_get_parent ||
      !mono_class_get_property_from_name)
    return nullptr;
  for (MonoClass *klass = mono_object_get_class(object); klass;
       klass = mono_class_get_parent(klass)) {
    if (mono_class_get_property_from_name(klass, name))
      return klass;
  }
  return nullptr;
}

/* Which class in an object's hierarchy declares a property never changes, and
 * the clock asks the same dozen questions for every node of the parent chain
 * on every frame. Remember the answers (including "none") per concrete class.
 * Names are string literals of this file, so comparing pointers first is the
 * common hit. */
MonoClass *property_class(MonoObject *object, const char *name) {
  struct Entry {
    MonoClass *object_class;
    const char *name;
    MonoClass *declaring_class;
    bool used;
  };
  static Entry cache[128];
  MonoClass *object_class =
      object && mono_object_get_class ? mono_object_get_class(object) : nullptr;
  if (!object_class || !name)
    return resolve_property_class(object, name);
  const size_t start =
      ((reinterpret_cast<uintptr_t>(object_class) >> 4) ^
       (reinterpret_cast<uintptr_t>(name) >> 2)) % 128;
  for (size_t probe = 0; probe < 8; ++probe) {
    Entry &entry = cache[(start + probe) % 128];
    if (!entry.used) {
      entry = {object_class, name, resolve_property_class(object, name), true};
      return entry.declaring_class;
    }
    if (entry.object_class == object_class &&
        (entry.name == name || std::strcmp(entry.name, name) == 0))
      return entry.declaring_class;
  }
  return resolve_property_class(object, name);
}

template <typename Value>
void set_object_property(MonoObject *object, const char *name, Value value) {
  if (MonoClass *klass = property_class(object, name))
    Set_Property(klass, object, name, value);
}

template <typename Value>
void set_object_property_invoke(MonoObject *object, const char *name,
                                Value value) {
  if (MonoClass *klass = property_class(object, name))
    Set_Property_Invoke(klass, object, name, value);
}

float label_dimension(MonoObject *label, const char *property) {
  MonoClass *klass = property_class(label, property);
  return klass ? Get_Property<float>(klass, label, property) : 0.0f;
}

void prepare_space_tab_label(MonoObject *label, const char *tab_name) {
  if (!label)
    return;
  // The symbol is prepended to the existing UI3 label. Let the native layout
  // engine size both sibling tab labels to their complete strings; resizing
  // only Media can consume the shared row and collapse Games to an ellipsis.
  // Leave parent geometry, positions, styles, and focus widgets untouched.
  // Runtime property lookup makes unsupported variants a no-op.
  set_object_property(label, "FitWidthToText", true);
  set_object_property(label, "FitHeightToText", false);
  set_object_property(label, "TextTrimming", 0);
  set_object_property(label, "NumberOfLines", 1);
  set_object_property(label, "MaxLineCount", 1);
  LOG_INFO("[HOME-SPACES:NATIVE] %s label auto-width enabled label=%p "
           "width=%.1f height=%.1f",
           tab_name ? tab_name : "space-tab", static_cast<void *>(label),
           label_dimension(label, "Width"), label_dimension(label, "Height"));
}

void release_handle(uint32_t &handle) {
  if (handle && mono_gchandle_free)
    mono_gchandle_free(handle);
  handle = 0;
}

size_t utf8_glyph_count(const std::string &text) {
  size_t count = 0;
  for (unsigned char byte : text) {
    if ((byte & 0xc0u) != 0x80u)
      ++count;
  }
  return count;
}

// The clock's time in any system language: H:MM/HH:MM (H.MM in Finnish)
// with a 12-hour marker before or after it, nothing else with digits. See
// clock_text.hpp; "23.8  5:47" is still rejected.
bool looks_like_stock_clock(const char *text) {
  return ClockText::Parse(text);
}

bool plausible_native_clock_geometry(MonoObject *label) {
  if (!label)
    return false;
  const float width = label_dimension(label, "Width");
  const float height = label_dimension(label, "Height");
  const float y = label_dimension(label, "Y");
  if (width <= 0.0f || height <= 0.0f)
    return true;
  return width <= 220.0f && height >= 34.0f && height <= 82.0f &&
         y >= -12.0f && y <= 24.0f;
}

/* A label showing a time that is not taken for the clock (no test id, a
   size no stock clock has), said in the log a few times a session: without
   the clock, Home's root and what hangs there (the video, the picture) stay
   unknown. */
void note_unbound_clock(MonoObject *label, const std::string &stock) {
  static int noted = 0;
  static std::string last;
  if (noted >= 6 || stock == last)
    return;
  ++noted;
  last = stock;
  LOG_INFO("[CLOCK] '%s' looks like the time but is not taken for the clock: "
           "label=%p width=%.1f height=%.1f y=%.1f",
           stock.c_str(), static_cast<void *>(label),
           label_dimension(label, "Width"), label_dimension(label, "Height"),
           label_dimension(label, "Y"));
}

MonoObject *clock_target() {
  return s_clock_handle && mono_gchandle_get_target
             ? mono_gchandle_get_target(s_clock_handle)
             : nullptr;
}

int find_clock_alias(MonoObject *label) {
  if (!label || !mono_gchandle_get_target)
    return -1;
  for (size_t index = 0; index < s_clock_aliases.size(); ++index) {
    NativeClockAlias &alias = s_clock_aliases[index];
    if (!alias.handle)
      continue;
    MonoObject *target = mono_gchandle_get_target(alias.handle);
    if (!target) {
      release_handle(alias.handle);
      alias = {};
      continue;
    }
    if (target == label)
      return static_cast<int>(index);
  }
  return -1;
}

bool is_disposed(MonoObject *object);

int remember_clock_alias(MonoObject *label, const std::string &stock) {
  const int existing = find_clock_alias(label);
  if (existing >= 0) {
    NativeClockAlias &alias = s_clock_aliases[static_cast<size_t>(existing)];
    alias.stock_time = stock;
    return existing;
  }
  if (!label || !mono_gchandle_new_weakref)
    return -1;
  for (NativeClockAlias &alias : s_clock_aliases) {
    if (!alias.handle || !mono_gchandle_get_target)
      continue;
    MonoObject *target = mono_gchandle_get_target(alias.handle);
    if (!target || is_disposed(target)) {
      release_handle(alias.handle);
      alias = {};
    }
  }
  for (size_t index = 0; index < s_clock_aliases.size(); ++index) {
    NativeClockAlias &alias = s_clock_aliases[index];
    if (alias.handle)
      continue;
    const uint32_t handle = mono_gchandle_new_weakref(label, 0);
    if (!handle)
      return -1;
    alias.handle = handle;
    alias.base_x = label_dimension(label, "X");
    alias.base_width = label_dimension(label, "Width");
    alias.base_height = label_dimension(label, "Height");
    alias.stock_time = stock;
    alias.customized = false;
    return static_cast<int>(index);
  }
  LOG_WARN("[CLOCK] native clock alias table full capacity=%zu",
           s_clock_aliases.size());
  return -1;
}

void forget_clock_alias(MonoObject *label) {
  const int index = find_clock_alias(label);
  if (index < 0)
    return;
  NativeClockAlias &alias = s_clock_aliases[static_cast<size_t>(index)];
  release_handle(alias.handle);
  alias = {};
}

bool is_disposed(MonoObject *object) {
  if (!object || !s_is_disposed_getter)
    return false;
  /* Called for every node of the clock's parent chain on every frame. Use the
     compiled getter: mono_runtime_invoke boxes the bool, which meant a dozen
     managed allocations per frame for as long as ShellUI ran. IsDisposed is a
     plain field read and cannot throw. */
  static bool (*getter)(MonoObject *) = nullptr;
  if (!getter && mono_compile_method)
    getter = decltype(getter)(mono_compile_method(s_is_disposed_getter));
  return getter ? getter(object) : false;
}

std::string managed_string(MonoObject *object) {
  if (!object || !mono_string_to_utf8)
    return {};
  const char *utf = mono_string_to_utf8(reinterpret_cast<MonoString *>(object));
  if (!utf)
    return {};
  std::string result(utf);
  if (mono_free)
    mono_free(const_cast<char *>(utf));
  return result;
}

MonoString *ui_string(const std::string &text) {
  if (!mono_string_new)
    return nullptr;
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  if (!domain && mono_get_root_domain)
    domain = mono_get_root_domain();
  return domain ? mono_string_new(domain, text.c_str()) : nullptr;
}

std::string raw_text(MonoObject *raw) {
  if (!raw || !mono_object_get_class || !mono_class_get_method_from_name ||
      !mono_runtime_invoke)
    return {};
  MonoClass *klass = mono_object_get_class(raw);
  MonoMethod *getter =
      klass ? mono_class_get_method_from_name(klass, "get_Text", 0) : nullptr;
  if (!getter)
    return {};
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, raw, nullptr, &exception);
  return exception ? std::string{} : managed_string(value);
}

MonoObject *parent_of(MonoObject *child) {
  if (!child || !mono_object_get_class || !mono_class_get_method_from_name ||
      !mono_runtime_invoke || !mono_class_get_parent)
    return nullptr;
  for (MonoClass *klass = mono_object_get_class(child); klass;
       klass = mono_class_get_parent(klass)) {
    for (const char *getter_name : {"get_Parent", "get_NativeParent"}) {
      MonoMethod *getter =
          mono_class_get_method_from_name(klass, getter_name, 0);
      if (!getter)
        continue;
      MonoObject *exception = nullptr;
      MonoObject *parent =
          mono_runtime_invoke(getter, child, nullptr, &exception);
      if (!exception && parent)
        return parent;
    }
  }
  return nullptr;
}

bool native_clock_visible(MonoObject *label) {
  if (!label)
    return false;

  // PUI's final visibility flags on a leaf can remain true while its scene
  // root is being swapped.  During a game launch/return that left the hidden
  // Home clock looking active, so the newly visible SystemOverlay/Home clock
  // was never selected.  Check the bounded native-parent chain as well as the
  // label itself; parent_of() resolves the getter at runtime for firmware
  // portability and safely stops where a parent API is unavailable.
  MonoObject *node = label;
  for (int depth = 0; node && depth < 12; ++depth) {
    if (is_disposed(node))
      return false;
    if (MonoClass *klass = property_class(node, "IsFinallyShown")) {
      if (!Get_Property<bool>(klass, node, "IsFinallyShown"))
        return false;
    }
    if (MonoClass *klass = property_class(node, "IsFinalOpacityVisible")) {
      if (!Get_Property<bool>(klass, node, "IsFinalOpacityVisible"))
        return false;
    }
    if (MonoClass *klass = property_class(node, "FinalOpacity")) {
      if (Get_Property<float>(klass, node, "FinalOpacity") <= 0.001f)
        return false;
    } else if (MonoClass *klass = property_class(node, "Opacity")) {
      if (Get_Property<float>(klass, node, "Opacity") <= 0.001f)
        return false;
    }
    MonoObject *parent = parent_of(node);
    if (!parent || parent == node)
      break;
    node = parent;
  }
  return true;
}

float native_clock_opacity(MonoObject *label) {
  if (!native_clock_visible(label))
    return 0.0f;
  if (MonoClass *klass = property_class(label, "FinalOpacity"))
    return std::clamp(Get_Property<float>(klass, label, "FinalOpacity"),
                      0.0f, 1.0f);
  if (MonoClass *klass = property_class(label, "Opacity"))
    return std::clamp(Get_Property<float>(klass, label, "Opacity"), 0.0f,
                      1.0f);
  return 1.0f;
}

#ifndef NDEBUG
int trace_bool_property(MonoObject *object, const char *name) {
  MonoClass *klass = property_class(object, name);
  return klass ? (Get_Property<bool>(klass, object, name) ? 1 : 0) : -1;
}

void record_clock_event(const char *event, MonoObject *label,
                        const std::string &text = {}) {
  ClockTraceEvent trace{};
  trace.sequence = ++s_clock_trace_sequence;
  trace.wall_time = std::time(nullptr);
  trace.monotonic_ms = monotonic_millis();
  trace.thread = reinterpret_cast<uintptr_t>(pthread_self());
  trace.object = reinterpret_cast<uintptr_t>(label);
  std::snprintf(trace.event, sizeof(trace.event), "%s",
                event ? event : "unknown");

  bool valid = label != nullptr;
  trace.disposed = valid && is_disposed(label) ? 1 : (valid ? 0 : -1);
  if (valid && trace.disposed == 0) {
    MonoClass *klass = mono_object_get_class ? mono_object_get_class(label)
                                              : nullptr;
    const char *class_name =
        klass && mono_class_get_name ? mono_class_get_name(klass) : "?";
    const char *name_space =
        klass && mono_class_get_namespace ? mono_class_get_namespace(klass) : "";
    std::snprintf(trace.class_name, sizeof(trace.class_name), "%s%s%s",
                  name_space && name_space[0] ? name_space : "",
                  name_space && name_space[0] ? "." : "",
                  class_name ? class_name : "?");
    MonoObject *parent = parent_of(label);
    trace.parent = reinterpret_cast<uintptr_t>(parent);
    trace.x = label_dimension(label, "X");
    trace.y = label_dimension(label, "Y");
    trace.width = label_dimension(label, "Width");
    trace.height = label_dimension(label, "Height");
    trace.opacity = native_clock_opacity(label);
    trace.finally_shown = trace_bool_property(label, "IsFinallyShown");
    trace.final_opacity_visible =
        trace_bool_property(label, "IsFinalOpacityVisible");
  }
  std::string observed = text;
  if (observed.empty() && valid && trace.disposed == 0)
    observed = raw_text(label);
  std::snprintf(trace.text, sizeof(trace.text), "%s", observed.c_str());

  s_clock_trace[s_clock_trace_head] = trace;
  s_clock_trace_head = (s_clock_trace_head + 1) % s_clock_trace.size();
  s_clock_trace_count = std::min(s_clock_trace_count + 1,
                                 s_clock_trace.size());
  clock_logf("event=%s seq=%llu pid=%d tid=0x%lx object=%p parent=%p "
             "class=%s text='%s' disposed=%d finally_shown=%d "
             "final_opacity_visible=%d opacity=%.3f x=%.1f y=%.1f "
             "width=%.1f height=%.1f mono_ms=%llu",
             trace.event, static_cast<unsigned long long>(trace.sequence),
             getpid(), static_cast<unsigned long>(trace.thread), label,
             reinterpret_cast<void *>(trace.parent),
             trace.class_name[0] ? trace.class_name : "(none)", trace.text,
             trace.disposed, trace.finally_shown,
             trace.final_opacity_visible, trace.opacity, trace.x, trace.y,
             trace.width, trace.height,
             static_cast<unsigned long long>(trace.monotonic_ms));
}

void dump_clock_tree(MonoObject *label, const char *reason) {
  clock_logf("tree_begin reason=%s clock=%p", reason ? reason : "unknown",
             label);
  MonoObject *node = label;
  for (int depth = 0; node && depth < 12; ++depth) {
    const bool disposed = depth == 0 && is_disposed(node);
    MonoClass *klass =
        !disposed && mono_object_get_class ? mono_object_get_class(node)
                                           : nullptr;
    const char *class_name =
        klass && mono_class_get_name ? mono_class_get_name(klass) : "?";
    const char *name_space =
        klass && mono_class_get_namespace ? mono_class_get_namespace(klass) : "";
    if (disposed) {
      clock_logf("tree_node depth=%d object=%p disposed=1", depth, node);
      break;
    }
    clock_logf("tree_node depth=%d object=%p class=%s%s%s x=%.1f y=%.1f "
               "width=%.1f height=%.1f shown=%d opacity_visible=%d "
               "opacity=%.3f",
               depth, node, name_space && name_space[0] ? name_space : "",
               name_space && name_space[0] ? "." : "",
               class_name ? class_name : "?", label_dimension(node, "X"),
               label_dimension(node, "Y"), label_dimension(node, "Width"),
               label_dimension(node, "Height"),
               trace_bool_property(node, "IsFinallyShown"),
               trace_bool_property(node, "IsFinalOpacityVisible"),
               [&]() {
                 MonoClass *opacity_class = property_class(node, "FinalOpacity");
                 if (opacity_class)
                   return std::clamp(Get_Property<float>(opacity_class, node,
                                                         "FinalOpacity"),
                                     0.0f, 1.0f);
                 opacity_class = property_class(node, "Opacity");
                 return opacity_class
                            ? std::clamp(Get_Property<float>(opacity_class,
                                                            node, "Opacity"),
                                         0.0f, 1.0f)
                            : 1.0f;
               }());
    MonoObject *parent = parent_of(node);
    if (!parent || parent == node)
      break;
    node = parent;
  }
  clock_logf("tree_end reason=%s", reason ? reason : "unknown");
}

void dump_clock_history(const char *reason) {
  clock_logf("history_begin reason=%s events=%zu capacity=%zu",
             reason ? reason : "unknown", s_clock_trace_count,
             s_clock_trace.size());
  const size_t first =
      (s_clock_trace_head + s_clock_trace.size() - s_clock_trace_count) %
      s_clock_trace.size();
  for (size_t index = 0; index < s_clock_trace_count; ++index) {
    const ClockTraceEvent &trace =
        s_clock_trace[(first + index) % s_clock_trace.size()];
    std::tm local{};
    char timestamp[40] = "unknown";
    if (localtime_r(&trace.wall_time, &local))
      std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z",
                    &local);
    clock_logf("history seq=%llu at=%s tid=0x%lx event=%s object=0x%lx "
               "parent=0x%lx class=%s text='%s' disposed=%d shown=%d "
               "opacity_visible=%d opacity=%.3f x=%.1f y=%.1f "
               "width=%.1f height=%.1f mono_ms=%llu",
               static_cast<unsigned long long>(trace.sequence), timestamp,
               static_cast<unsigned long>(trace.thread), trace.event,
               static_cast<unsigned long>(trace.object),
               static_cast<unsigned long>(trace.parent),
               trace.class_name[0] ? trace.class_name : "(none)", trace.text,
               trace.disposed, trace.finally_shown,
               trace.final_opacity_visible, trace.opacity, trace.x, trace.y,
               trace.width, trace.height,
               static_cast<unsigned long long>(trace.monotonic_ms));
  }
  clock_logf("history_end reason=%s", reason ? reason : "unknown");
}

void trace_clock_update_once(MonoObject *label, time_t now,
                             const std::string &rendered) {
  if (now == s_trace_last_second)
    return;
  s_trace_last_second = now;
  record_clock_event("time-update", label, rendered);
}
#endif

// UI3 positions are relative to their parents, while the date is attached to
// the UI2 overlay root. Sum the native clock ancestry so both labels share a
// screen-space center. Ignore incomplete hierarchies during scene replacement.
bool global_label_geometry(MonoObject *label, float width, float height,
                           float &center_x, float &bottom_y) {
  if (!label || width <= 0.0f || height <= 0.0f)
    return false;
  float x = label_dimension(label, "X");
  float y = label_dimension(label, "Y");
  MonoObject *node = label;
  int parent_count = 0;
  for (; parent_count < 24; ++parent_count) {
    MonoObject *parent = parent_of(node);
    if (!parent || parent == node)
      break;
    x += label_dimension(parent, "X");
    y += label_dimension(parent, "Y");
    node = parent;
  }
  if (parent_count == 0)
    return false;
  center_x = x + width * 0.5f;
  bottom_y = y + height;
  return center_x > 0.0f && center_x < 4096.0f && bottom_y > 0.0f &&
         bottom_y < 512.0f;
}

void sync_clock_language(time_t now) {
  if (s_language_setting != g_settings.ui_lang) {
    s_language_setting = g_settings.ui_lang;
    s_language_ready = false;
    s_last_language_retry = static_cast<time_t>(-1);
  }
  if (s_language_ready || s_last_language_retry == now)
    return;
  s_last_language_retry = now;
  toolbox_i18n::apply_system_or_ui_lang(g_settings.ui_lang);
  s_language_ready = true;
  s_force_refresh = true;
}

uint64_t settings_signature() {
  uint64_t value = (AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled) ? 1u : 0u;
  value = value * 31u + (g_settings.shell_clock_show_seconds ? 1u : 0u);
  value = value * 31u + (g_settings.shell_clock_show_date ? 1u : 0u);
  value = value * 31u + (g_settings.shell_clock_show_year ? 1u : 0u);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_hour_format);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_date_order);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_month_style);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_weekday_style);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_date_separator);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_time_font_size);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_date_font_size);
  value = value * 31u +
          static_cast<unsigned>(
              std::clamp(g_settings.shell_clock_date_spacing, -80, 160) + 80);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_time_r);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_time_g);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_time_b);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_date_r);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_date_g);
  value = value * 31u + static_cast<unsigned>(g_settings.shell_clock_date_b);
  value = value * 31u +
          static_cast<unsigned>(toolbox_i18n::active_ui_lang_value());
  return value;
}

std::string two_digits(int value) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%02d", value);
  return buffer;
}

int effective_date_order() {
  if (g_settings.shell_clock_date_order != 0)
    return g_settings.shell_clock_date_order;
  switch (toolbox_i18n::active_lang()) {
  case toolbox_i18n::Lang::En: return 2;
  case toolbox_i18n::Lang::ZhHans:
  case toolbox_i18n::Lang::ZhHant:
  case toolbox_i18n::Lang::Ja:
  case toolbox_i18n::Lang::Ko: return 3;
  default: return 1;
  }
}

const char *date_separator() {
  switch (g_settings.shell_clock_date_separator) {
  case 1: return "/";
  case 2: return ".";
  case 3: return "-";
  case 4: return " ";
  case 5: return " · ";
  default:
    if (g_settings.shell_clock_month_style != 0)
      return " ";
    return effective_date_order() == 2 ? "/" : ".";
  }
}

std::string translated_indexed(const char *group, int index,
                               const char *style) {
  char key[64];
  std::snprintf(key, sizeof(key), "clock.%s.%s.list", group, style);
  const std::string list = toolbox_i18n::tr(key);
  size_t begin = 0;
  for (int current = 0; current < index; ++current) {
    begin = list.find('|', begin);
    if (begin == std::string::npos)
      return {};
    ++begin;
  }
  const size_t end = list.find('|', begin);
  return list.substr(begin, end == std::string::npos ? std::string::npos
                                                     : end - begin);
}

std::string format_time(const std::tm &local) {
  const int mode = std::clamp(g_settings.shell_clock_hour_format, 0, 2);
  bool twelve_hour = mode == 2;
  bool leading_zero = mode == 1;
  if (mode == 0) {
    /* follow the system: a marker in any language ("p. m.", "du.", "PM"
       before the time) means 12-hour */
    ClockText::Time stock;
    if (ClockText::Parse(s_last_stock_time.c_str(), &stock)) {
      twelve_hour = stock.twelve_hour;
      leading_zero = stock.hour_digits == 2 && stock.hour < 10;
    }
  }
  int hour = local.tm_hour;
  if (twelve_hour) {
    hour %= 12;
    if (hour == 0)
      hour = 12;
  }
  char buffer[48];
  if (leading_zero)
    std::snprintf(buffer, sizeof(buffer),
                  g_settings.shell_clock_show_seconds ? "%02d:%02d:%02d"
                                                      : "%02d:%02d",
                  hour, local.tm_min, local.tm_sec);
  else
    std::snprintf(buffer, sizeof(buffer),
                  g_settings.shell_clock_show_seconds ? "%d:%02d:%02d"
                                                      : "%d:%02d",
                  hour, local.tm_min, local.tm_sec);
  std::string result(buffer);
  if (twelve_hour)
    result += std::string(" ") +
              toolbox_i18n::tr(local.tm_hour < 12 ? "clock.am" : "clock.pm");
  return result;
}

std::string format_date(const std::tm &local) {
  const int month_style = std::clamp(g_settings.shell_clock_month_style, 0, 2);
  const int weekday_style =
      std::clamp(g_settings.shell_clock_weekday_style, 0, 2);
  const std::string day = two_digits(local.tm_mday);
  std::string month =
      month_style == 0
          ? two_digits(local.tm_mon + 1)
          : translated_indexed("month", local.tm_mon,
                               month_style == 1 ? "short" : "long");
  /* A language without a list of month names still gets a complete date. */
  if (month.empty())
    month = two_digits(local.tm_mon + 1);
  const std::string year = std::to_string(local.tm_year + 1900);
  const char *separator = date_separator();
  const int order = effective_date_order();
  std::string result;
  auto append = [&](const std::string &component) {
    if (component.empty())
      return;
    if (!result.empty())
      result += separator;
    result += component;
  };
  if (order == 2) {
    append(month);
    append(day);
    if (g_settings.shell_clock_show_year)
      append(year);
  } else if (order == 3) {
    if (g_settings.shell_clock_show_year)
      append(year);
    append(month);
    append(day);
  } else {
    append(day);
    append(month);
    if (g_settings.shell_clock_show_year)
      append(year);
  }
  if (weekday_style != 0) {
    const std::string weekday = translated_indexed(
        "weekday", local.tm_wday,
        weekday_style == 1 ? "short" : "long");
    if (!weekday.empty())
      result = weekday + ", " + result;
  }
  return result;
}

bool local_time(time_t now, std::tm &local) {
  return localtime_r(&now, &local) != nullptr;
}

void publish_date_overlay(time_t now, bool target_ready,
                          const std::string &time_text) {
  s_date_overlay.visible = false;
  s_date_overlay.wanted = false;
  s_date_overlay.text.clear();
  // UI3's FinalOpacity/IsFinallyShown can briefly report zero on this
  // firmware while the native clock is already painted.  Do not make the
  // independent date label disappear because of that transient state.
  /* The date belongs to the clock: it is exactly as opaque as the time and
     goes away with it. (It used to be forced to full opacity, which left it
     drawn on top of Settings, the Control Center and everything else.) The
     label that paints it smooths the value, so a one-frame dip in the native
     opacity during a scene swap does not make it blink. */
  s_date_overlay.opacity = target_ready && s_native_opacity > kShownOpacity
                               ? std::min(s_native_opacity, 1.0f)
                               : 0.0f;
  s_date_overlay.time_width = std::max(s_visible_time_width, 78.0f);
  s_date_overlay.time_center_x = s_visible_time_center_x;
  s_date_overlay.time_bottom_y = s_visible_time_bottom_y;
  /* The label is taller than the digits painted at its top; the date hangs
     off the digits, not off the box. */
  s_date_overlay.time_text_bottom_y =
      s_visible_time_bottom_y - s_native_base_height +
      static_cast<float>(
          std::clamp(g_settings.shell_clock_time_font_size, 24, 56)) * 1.32f;
  s_date_overlay.has_global_time_geometry = s_has_global_time_geometry;
  s_date_overlay.font_size =
      std::clamp(g_settings.shell_clock_date_font_size, 10, 28);
  s_date_overlay.date_spacing =
      std::clamp(g_settings.shell_clock_date_spacing, -80, 160);
  if (clock_animation_active()) {
    const AniketosTheme::RGB color = effective_clock_color();
    s_date_overlay.r = static_cast<int>(std::lround(color.r * 255.0f));
    s_date_overlay.g = static_cast<int>(std::lround(color.g * 255.0f));
    s_date_overlay.b = static_cast<int>(std::lround(color.b * 255.0f));
  } else {
    s_date_overlay.r = std::clamp(g_settings.shell_clock_date_r, 0, 255);
    s_date_overlay.g = std::clamp(g_settings.shell_clock_date_g, 0, 255);
    s_date_overlay.b = std::clamp(g_settings.shell_clock_date_b, 0, 255);
  }
  if (!target_ready || !(AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled) ||
      !g_settings.shell_clock_show_date)
    return;
  std::tm local{};
  if (!local_time(now, local))
    return;
  s_date_overlay.text = format_date(local);
#ifndef NDEBUG
  if (s_date_overlay.text != s_trace_last_date) {
    s_trace_last_date = s_date_overlay.text;
    record_clock_event("date-update", clock_target(), s_date_overlay.text);
  }
#endif
  s_date_overlay.wanted = !s_date_overlay.text.empty();
  s_date_overlay.visible =
      s_date_overlay.wanted && s_date_overlay.opacity > 0.0f;
  if (s_date_overlay.time_width <= 0.0f)
    s_date_overlay.time_width =
        std::max<float>(78.0f, utf8_glyph_count(time_text) * 18.0f);
}

bool ensure_custom_time_color() {
  const AniketosTheme::RGB color = effective_clock_color();
  const int r = static_cast<int>(std::lround(color.r * 255.0f));
  const int g = static_cast<int>(std::lround(color.g * 255.0f));
  const int b = static_cast<int>(std::lround(color.b * 255.0f));
  if (s_custom_time_color_value && s_custom_time_color_handle &&
      s_custom_time_color_r == r && s_custom_time_color_g == g &&
      s_custom_time_color_b == b)
    return true;
  MonoObject *boxed_color =
      CreateUIColorBoxed(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
  if (!boxed_color || !mono_object_unbox || !mono_gchandle_new ||
      !mono_gchandle_get_target)
    return false;
  const uint32_t color_handle = mono_gchandle_new(boxed_color, 1);
  if (!color_handle)
    return false;
  MonoObject *pinned_box = mono_gchandle_get_target(color_handle);
  if (!pinned_box) {
    if (mono_gchandle_free)
      mono_gchandle_free(color_handle);
    return false;
  }
  void *color_value = mono_object_unbox(pinned_box);
  if (!color_value) {
    if (mono_gchandle_free)
      mono_gchandle_free(color_handle);
    return false;
  }
  // Pin and retain the box, then keep the pointer to its UIColor payload.
  // The old CreateUIColor() API returns that payload directly; it is valid
  // for an immediate value copy, but is not a MonoObject suitable for a GC
  // handle and can move if left unrooted.
  release_handle(s_custom_time_color_handle);
  s_custom_time_color_handle = color_handle;
  s_custom_time_color_value = color_value;
  s_custom_time_color_r = r;
  s_custom_time_color_g = g;
  s_custom_time_color_b = b;
  return true;
}

void set_native_label_color(MonoObject *label, float r, float g, float b) {
  if (!label || !mono_gchandle_new || !mono_gchandle_get_target ||
      !mono_object_unbox)
    return;
  MonoObject *boxed = CreateUIColorBoxed(r, g, b, 1.0f);
  if (!boxed)
    return;
  const uint32_t handle = mono_gchandle_new(boxed, 1);
  if (!handle)
    return;
  MonoObject *pinned_box = mono_gchandle_get_target(handle);
  void *value = pinned_box ? mono_object_unbox(pinned_box) : nullptr;
  if (value)
    set_object_property_invoke(label, "TextColor", value);
  if (mono_gchandle_free)
    mono_gchandle_free(handle);
}

void restore_native_label(MonoObject *label) {
  if (!label || !s_native_customized)
    return;
  set_object_property(label, "X", s_native_base_x);
  set_object_property(label, "Width", s_native_base_width);
  set_object_property(label, "Height", s_native_base_height);
  set_object_property(label, "NumberOfLines", 1);
  set_object_property(label, "HorizontalAlignment", 2);
  set_object_property(label, "VerticalAlignment", 0);
  set_object_property(label, "FitWidthToText", false);
  set_object_property(label, "FitHeightToText", false);
  set_native_label_color(label, 1.0f, 1.0f, 1.0f);
  s_native_customized = false;
  s_visible_time_width = s_native_base_width;
  const int alias_index = find_clock_alias(label);
  if (alias_index >= 0)
    s_clock_aliases[static_cast<size_t>(alias_index)].customized = false;
}

void restore_all_native_labels() {
  struct SavedLayout {
    float x;
    float width;
    float height;
    float visible_width;
    bool customized;
  } saved{s_native_base_x, s_native_base_width, s_native_base_height,
          s_visible_time_width, s_native_customized};
  MonoObject *active = clock_target();
  if (active && !is_disposed(active))
    restore_native_label(active);
  for (NativeClockAlias &alias : s_clock_aliases) {
    if (!alias.handle || !mono_gchandle_get_target)
      continue;
    MonoObject *label = mono_gchandle_get_target(alias.handle);
    if (!label || label == active || is_disposed(label) ||
        !alias.customized)
      continue;
    s_native_base_x = alias.base_x;
    s_native_base_width = alias.base_width;
    s_native_base_height = alias.base_height;
    s_native_customized = alias.customized;
    restore_native_label(label);
    alias.customized = false;
  }
  s_native_base_x = saved.x;
  s_native_base_width = saved.width;
  s_native_base_height = saved.height;
  s_visible_time_width = saved.visible_width;
  s_native_customized = saved.customized && (AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled);
}

void layout_native_clock(MonoObject *label, const std::string &time_text) {
  if (!label || s_native_base_width <= 0.0f)
    return;
  const size_t stock_glyphs =
      std::max<size_t>(utf8_glyph_count(s_last_stock_time), 4);
  const size_t rendered_glyphs =
      std::max<size_t>(utf8_glyph_count(time_text), stock_glyphs);
  const float ratio = static_cast<float>(rendered_glyphs) /
                      static_cast<float>(stock_glyphs);
  const float font_scale =
      static_cast<float>(std::clamp(g_settings.shell_clock_time_font_size,
                                    24, 56)) /
      36.0f;
  // Size from the text being painted, not only from the old stock label.
  // Otherwise PUI wraps the longer custom value and renders the lower glyphs
  // as clipped dots on some ShellUI builds.
  float text_em = 0.0f;
  for (const unsigned char character : time_text) {
    if (character == ' ')
      text_em += 0.30f;
    else if (character == ':' || character == '.')
      text_em += 0.29f;
    else if (character >= '0' && character <= '9')
      text_em += 0.56f;
    else
      text_em += 0.58f;
  }
  const float measured = text_em *
      static_cast<float>(std::clamp(g_settings.shell_clock_time_font_size,
                                    24, 56));
  const float desired = std::clamp(
      std::max(s_native_base_width * ratio * font_scale * 1.16f,
               measured + 40.0f),
      s_native_base_width, 480.0f);
  // Keep the expanded label's right edge aligned to the native clock
  // container. Extending it beyond that edge clips the final seconds digit
  // when the Control Center applies its parent clip rectangle.
  // The edge comes from the container React lays out, which we never resize:
  // the label's own remembered geometry can be one we applied earlier (a
  // label first met after it was already widened), and then every further
  // widening pushed the time past the right edge of the screen.
  float right_edge = s_native_base_x + s_native_base_width;
  if (MonoObject *holder = parent_of(label)) {
    const float holder_width = label_dimension(holder, "Width");
    if (holder_width > 8.0f)
      right_edge = holder_width;
  }
  const float x = right_edge - desired;
  set_object_property(label, "RenderSync", true);
  set_object_property(label, "FitWidthToText", false);
  set_object_property(label, "FitHeightToText", false);
  set_object_property(label, "TextTrimming", 0);
  set_object_property(label, "MaxLineCount", 1);
  set_object_property(label, "HorizontalAlignment", 2);
  set_object_property(label, "VerticalAlignment", 0);
  set_object_property(label, "NumberOfLines", 1);
  set_object_property(label, "X", x);
  set_object_property(label, "Width", desired);
  if (clock_target() == label) {
    s_applied_x = x;
    s_applied_width = desired;
  }
  set_object_property(label, "Height",
                      std::max(s_native_base_height,
                               static_cast<float>(std::clamp(
                                   g_settings.shell_clock_time_font_size,
                                   24, 56)) * 1.75f));
  // Keep the ShellUI clock's proven native font. The custom font path was the
  // source of firmware-dependent glyph wrapping; color and text formatting
  // remain fully controlled by Aniketos.
  if (ensure_custom_time_color())
    set_object_property_invoke(label, "TextColor", s_custom_time_color_value);
  s_native_customized = true;
  // The native label is right-aligned and deliberately wider than its painted
  // text to prevent trimming.  The date must follow the painted time, not the
  // center of that safety box.
  float painted_em = 0.0f;
  for (const unsigned char character : time_text) {
    if (character == ' ')
      painted_em += 0.30f;
    else if (character == ':' || character == '.')
      painted_em += 0.29f;
    else if (character >= '0' && character <= '9')
      painted_em += 0.56f;
    else
      painted_em += 0.58f;
  }
  const float painted_width = std::clamp(
      painted_em * static_cast<float>(std::clamp(
          g_settings.shell_clock_time_font_size, 24, 56)),
      52.0f, desired);
  /* Ask the label how wide the digits really are; the estimate above is only
     a fallback. The date is centred on this, so a guess shows as an offset. */
  float measured_width = 0.0f;
  {
    static MonoMethod *text_width = nullptr;
    static MonoClass *text_width_class = nullptr;
    MonoClass *label_class = mono_object_get_class(label);
    if (text_width_class != label_class) {
      text_width_class = label_class;
      text_width = nullptr;
      for (MonoClass *klass = label_class; klass && !text_width;
           klass = mono_class_get_parent(klass))
        text_width = mono_class_get_method_from_name(klass, "GetTextWidth", 0);
    }
    if (text_width) {
      MonoObject *exception = nullptr;
      MonoObject *boxed =
          mono_runtime_invoke(text_width, label, nullptr, &exception);
      if (!exception && boxed && mono_object_unbox)
        measured_width = *static_cast<float *>(mono_object_unbox(boxed));
    }
  }
  /* Digits are not all the same width, so the measured value wobbles by a
     pixel or two every second and would make the date under it jitter. Keep
     the widest value seen for this format and size. */
  {
    static float widest = 0.0f;
    static size_t widest_glyphs = 0;
    static int widest_font = 0;
    const size_t glyphs = utf8_glyph_count(time_text);
    if (glyphs != widest_glyphs ||
        widest_font != g_settings.shell_clock_time_font_size) {
      widest_glyphs = glyphs;
      widest_font = g_settings.shell_clock_time_font_size;
      widest = 0.0f;
    }
    widest = std::max(widest, measured_width);
    if (measured_width > 8.0f)
      measured_width = widest;
  }
  s_measured_time_width = measured_width;
  s_visible_time_width =
      measured_width > 8.0f && measured_width <= desired ? measured_width
                                                          : painted_width;
  const float painted_width_final = s_visible_time_width;
  s_has_global_time_geometry = global_label_geometry(
      label, desired, s_native_base_height, s_visible_time_center_x,
      s_visible_time_bottom_y);
  if (s_has_global_time_geometry)
    s_visible_time_center_x += (desired - painted_width_final) * 0.5f;
  const int alias_index = find_clock_alias(label);
  if (alias_index >= 0)
    s_clock_aliases[static_cast<size_t>(alias_index)].customized = true;
}

/* A clock label is often first seen while its scene is still being built
 * (Home coming back from Settings or the Control Center builds a new one), and
 * at that point it has no size yet. The stock geometry used to be read once,
 * right then, so such a label kept a base width of zero for good: it was
 * never laid out, and the longer custom time was squeezed into Sony's box
 * where it wrapped out of sight. Pick the geometry up as soon as the label has
 * one, provided we have not resized it ourselves yet. */
void adopt_native_geometry(MonoObject *label, NativeClockAlias &alias,
                           bool active) {
  if (!label || alias.customized || alias.base_width > 0.0f)
    return;
  const float width = label_dimension(label, "Width");
  if (width <= 0.0f)
    return;
  alias.base_x = label_dimension(label, "X");
  alias.base_width = width;
  alias.base_height = label_dimension(label, "Height");
  if (!active)
    return;
  s_native_base_x = alias.base_x;
  s_native_base_width = alias.base_width;
  s_native_base_height = alias.base_height;
  s_visible_time_width = alias.base_width;
  s_force_refresh = true;
}

void update_clock_alias(MonoObject *label, size_t alias_index, time_t now) {
  if (!label || alias_index >= s_clock_aliases.size())
    return;
  NativeClockAlias &alias = s_clock_aliases[alias_index];
  const bool active = clock_target() == label;
  adopt_native_geometry(label, alias, active);
  struct SavedClockState {
    std::string stock_time;
    float base_x;
    float base_width;
    float base_height;
    float visible_width;
    float center_x;
    float bottom_y;
    float opacity;
    bool customized;
    bool visible;
    bool has_global_geometry;
  } saved{s_last_stock_time,
          s_native_base_x,
          s_native_base_width,
          s_native_base_height,
          s_visible_time_width,
          s_visible_time_center_x,
          s_visible_time_bottom_y,
          s_native_opacity,
          s_native_customized,
          s_native_visible,
          s_has_global_time_geometry};

  if (!active) {
    s_last_stock_time = alias.stock_time;
    s_native_base_x = alias.base_x;
    s_native_base_width = alias.base_width;
    s_native_base_height = alias.base_height;
    s_native_customized = alias.customized;
  }
  if (s_native_base_width <= 0.0f)
    goto restore_state;

  if (!(AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled)) {
    restore_native_label(label);
    goto restore_state;
  }

  {
    std::tm local{};
    if (!local_time(now, local))
      goto restore_state;
    const std::string rendered = format_time(local);
    if (raw_text(label) != rendered) {
      if (MonoString *replacement = ui_string(rendered)) {
        if (s_native_label_set_text_orig)
          s_native_label_set_text_orig(label, replacement);
      }
    }
    layout_native_clock(label, rendered);
    s_native_opacity = native_clock_opacity(label);
    s_native_visible = s_native_opacity > kShownOpacity;
    alias.customized = s_native_customized;
    if (active) {
      publish_date_overlay(now, true, rendered);
#ifndef NDEBUG
      trace_clock_update_once(label, now, rendered);
#endif
      s_last_rendered = rendered;
      s_last_second = now;
    }
  }

restore_state:
  if (!active) {
    s_last_stock_time = std::move(saved.stock_time);
    s_native_base_x = saved.base_x;
    s_native_base_width = saved.base_width;
    s_native_base_height = saved.base_height;
    s_visible_time_width = saved.visible_width;
    s_visible_time_center_x = saved.center_x;
    s_visible_time_bottom_y = saved.bottom_y;
    s_native_opacity = saved.opacity;
    s_native_customized = saved.customized;
    s_native_visible = saved.visible;
    s_has_global_time_geometry = saved.has_global_geometry;
  }
}

bool has_clock_test_id(MonoObject *label);

void release_clock(bool restore) {
  MonoObject *target = clock_target();
#ifndef NDEBUG
  record_clock_event("release", target,
                     restore ? "restore=true" : "restore=false");
  if (s_native_visible && (!target || is_disposed(target))) {
    dump_clock_tree(target, "clock-target-lost");
    dump_clock_history("clock-target-lost");
  }
#endif
  if (restore && target && !is_disposed(target))
    restore_native_label(target);
  release_handle(s_clock_handle);
  s_last_rendered.clear();
  s_last_second = static_cast<time_t>(-1);
  s_native_customized = false;
  s_native_visible = false;
  s_native_opacity = 0.0f;
  s_animation_started_ms = 0;
  s_last_animation_frame_ms = 0;
  s_native_base_x = 0.0f;
  s_native_base_width = 0.0f;
  s_native_base_height = 0.0f;
  s_visible_time_width = 0.0f;
  s_visible_time_center_x = 0.0f;
  s_visible_time_bottom_y = 0.0f;
  s_has_global_time_geometry = false;
  s_date_overlay = {};
}

void bind_native_clock(MonoObject *label, const std::string &stock) {
  if (!label || !mono_gchandle_new_weakref || !mono_gchandle_get_target)
    return;
  if (has_clock_test_id(label) && find_clock_alias(label) < 0)
    remember_clock_alias(label, stock);
  /* Home's clock is never given up for another scene's (Settings and the
     Control Center have their own): everything hung in Home follows the
     clock we follow. The other one stays a second, dressed clock. */
  {
    MonoObject *followed = clock_target();
    if (followed && followed != label && !is_disposed(followed) &&
        in_home_scene(followed) && !in_home_scene(label))
      return;
  }
  /* The size check tells Sony's clock from other labels that happen to show a
     time. A label we already know has been resized by us (a large font makes
     it taller than any stock clock), so it must not be asked again or it could
     never become the active clock a second time. */
  if (find_clock_alias(label) < 0 && !plausible_native_clock_geometry(label))
    return;
  const int alias_index = remember_clock_alias(label, stock);
  if (alias_index < 0)
    return;
  NativeClockAlias &alias = s_clock_aliases[static_cast<size_t>(alias_index)];
  if (clock_target() != label) {
    // A scene transition may create a second clock while the previous one is
    // still visible underneath it. Drop only the active reference; restoring
    // that old label to white causes the exact color flash seen in Control
    // Center. Its weak alias keeps the original geometry for its next update.
    release_clock(false);
    s_clock_handle = mono_gchandle_new_weakref(label, 0);
    if (!s_clock_handle)
      return;
    s_last_stock_time = alias.stock_time;
    s_native_base_x = alias.base_x;
    s_native_base_width = alias.base_width;
    s_native_base_height = alias.base_height;
    s_native_customized = alias.customized;
  } else {
    s_last_stock_time = stock;
    alias.stock_time = stock;
  }
  s_visible_time_width = s_native_base_width;
  s_force_refresh = true;
  alias.stock_time = stock;
#ifndef NDEBUG
  record_clock_event("bind", label, stock);
  dump_clock_tree(label, "clock-bind");
  LOG_INFO("[CLOCK:BIND] pid=%d label=%p parent=%p stock='%s' "
           "x=%.1f y=%.1f width=%.1f height=%.1f",
           getpid(), static_cast<void *>(label),
           static_cast<void *>(parent_of(label)), stock.c_str(),
           s_native_base_x, label_dimension(label, "Y"), s_native_base_width,
           s_native_base_height);
#endif
}


std::string label_test_id(MonoObject *label) {
  if (!label)
    return {};
  MonoMethod *getter = nullptr;
  for (MonoClass *klass = mono_object_get_class(label); klass && !getter;
       klass = mono_class_get_parent(klass))
    getter = mono_class_get_method_from_name(klass, "get_TestId", 0);
  if (!getter)
    return {};
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, label, nullptr, &exception);
  return exception ? std::string() : managed_string(value);
}

void native_label_set_text_hook(MonoObject *label, MonoString *text) {
  std::string stock;
  if (label && text)
    stock = managed_string(reinterpret_cast<MonoObject *>(text));
  const bool clock_like = looks_like_stock_clock(stock.c_str());
  /* Sony's clock label is recognised by its test id where it has one; the
     size check is only for builds whose clock carries none. A label already
     resized for a large font fails that check, and used to be left showing
     the stock time for good. */
  if (clock_like && clock_target() != label &&
      (has_clock_test_id(label) || plausible_native_clock_geometry(label)))
    bind_native_clock(label, stock);
  else if (clock_like && clock_target() == label)
    s_last_stock_time = stock;
  else if (clock_like && find_clock_alias(label) < 0)
    note_unbound_clock(label, stock);

  if (clock_target() != label) {
    if (AniketosTheme::HomeTiles::IsSpaceSwitcherTabLabel(stock))
      prepare_space_tab_label(
          label,
          AniketosTheme::HomeTiles::IsSpaceSwitcherMediaTabLabel(stock)
              ? "media"
              : "games");
    MonoString *display_text =
        AniketosTheme::HomeTiles::DecorateSpaceSwitcherLabel(label, text);
    if (s_native_label_set_text_orig)
      s_native_label_set_text_orig(label, display_text ? display_text : text);
    return;
  }
  const time_t now = std::time(nullptr);
  if (!(AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled)) {
    restore_all_native_labels();
    s_date_overlay = {};
    s_last_rendered = stock;
    if (s_native_label_set_text_orig)
      s_native_label_set_text_orig(label, text);
    return;
  }
  std::tm local{};
  if (!local_time(now, local)) {
    if (s_native_label_set_text_orig)
      s_native_label_set_text_orig(label, text);
    return;
  }
  const std::string rendered = format_time(local);
#ifndef NDEBUG
  if (clock_like)
    record_clock_event("native-format", label, stock);
#endif
  if (MonoString *replacement = ui_string(rendered)) {
    if (s_native_label_set_text_orig)
      s_native_label_set_text_orig(label, replacement);
    layout_native_clock(label, rendered);
    s_native_opacity = native_clock_opacity(label);
    s_native_visible = s_native_opacity > kShownOpacity;
    publish_date_overlay(now, true, rendered);
#ifndef NDEBUG
    trace_clock_update_once(label, now, rendered);
#endif
    s_last_rendered = rendered;
    s_last_second = now;
    /* React sets the text first and the rest of the label's style after it
       (colour, size, position), which undoes the layout just applied. That
       happens once a minute, and until the next second's redraw the clock
       stood there in Sony's colour and box. Redraw on the frames that
       follow instead. */
    s_redraw_frames = 4;
    return;
  }
  if (s_native_label_set_text_orig)
    s_native_label_set_text_orig(label, text);
}

bool has_clock_test_id(MonoObject *label) {
  return label_test_id(label) == "clock-text";
}

void native_label_on_update_hook(MonoObject *label) {
  if (s_native_label_on_update_orig)
    s_native_label_on_update_orig(label);
  if (!label)
    return;
  int alias_index = find_clock_alias(label);
  if (clock_target() == label || alias_index >= 0) {
    if (is_disposed(label)) {
#ifndef NDEBUG
      record_clock_event("label-disposed", label);
      dump_clock_tree(label, "label-disposed");
      dump_clock_history("label-disposed");
#endif
      if (clock_target() == label)
        release_clock(false);
      forget_clock_alias(label);
      return;
    }
    const time_t now = std::time(nullptr);
    if (!(AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled)) {
      restore_all_native_labels();
      s_date_overlay = {};
      return;
    }
    if (alias_index < 0) {
      alias_index = remember_clock_alias(label, raw_text(label));
      if (alias_index < 0)
        return;
    }
    const bool active = clock_target() == label;
    const float opacity = native_clock_opacity(label);
    if (!active && opacity > kShownOpacity && !s_native_visible) {
      const std::string stock =
          s_clock_aliases[static_cast<size_t>(alias_index)].stock_time;
      bind_native_clock(label, stock);
      alias_index = find_clock_alias(label);
      if (alias_index < 0)
        return;
    }
    update_clock_alias(label, static_cast<size_t>(alias_index), now);
    return;
  }
  const std::string stock = raw_text(label);
  AniketosTheme::HomeTiles::UpdateSpaceSwitcherFocusBounds(label, stock);
  // HomeUI may create the native tab labels before our setter hook is
  // installed. Recheck their live text during the runtime-resolved Label
  // update hook, then apply the same localized, semantic decorator once.
  // After decoration the exact-label check is false, so this does not allocate
  // or rewrite the label on every frame.
  if (stock.size() <= 24 &&
      AniketosTheme::HomeTiles::IsSpaceSwitcherTabLabel(stock)) {
    prepare_space_tab_label(
        label,
        AniketosTheme::HomeTiles::IsSpaceSwitcherMediaTabLabel(stock)
            ? "media"
            : "games");
    if (MonoString *current_text = ui_string(stock)) {
      MonoString *display_text =
          AniketosTheme::HomeTiles::DecorateSpaceSwitcherLabel(label,
                                                               current_text);
      if (display_text && display_text != current_text &&
          s_native_label_set_text_orig)
        s_native_label_set_text_orig(label, display_text);
    }
    return;
  }
  if (looks_like_stock_clock(stock.c_str()) &&
      plausible_native_clock_geometry(label))
    bind_native_clock(label, stock);
}

} // namespace

/* ---- Home's own scene ---------------------------------------------------
 * The clock being followed is not always Home's: Settings and the Control
 * Center have clocks of their own, and the root of the scene the followed
 * clock is in used to pass for "Home's root". Whatever was hung there (the
 * date, the OSD, the video, the icon menu) then went to a scene that is not
 * on screen. Home's root is found in the scene tree instead: from any scene
 * up to the top, into the layer Home is shown in (TopMenu), and there to the
 * React scene that draws. Names only; no title id, no offsets. */
MonoObject *ClockSceneRoot();
MonoObject *GetHomeRoot();

MonoObject *scene_field(MonoObject *object, const char *name) {
  if (!object)
    return nullptr;
  MonoClassField *field = nullptr;
  for (MonoClass *klass = mono_object_get_class(object); klass && !field;
       klass = mono_class_get_parent(klass))
    field = mono_class_get_field_from_name(klass, name);
  MonoObject *value = nullptr;
  if (field)
    mono_field_get_value(object, field, &value);
  return value;
}

MonoObject *scene_call(MonoObject *object, const char *name, int parameters,
                       void **arguments) {
  MonoMethod *method = nullptr;
  for (MonoClass *klass = object ? mono_object_get_class(object) : nullptr;
       klass && !method; klass = mono_class_get_parent(klass))
    method = mono_class_get_method_from_name(klass, name, parameters);
  if (!method)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(method, object, arguments, &exception);
  return exception ? nullptr : value;
}

template <typename Wanted>
MonoObject *find_sub_scene(MonoObject *scene, int depth, Wanted wanted) {
  if (!scene || depth > 8)
    return nullptr;
  if (wanted(scene))
    return scene;
  MonoObject *list = scene_field(scene, "subSceneList");
  MonoObject *boxed = scene_call(list, "get_Count", 0, nullptr);
  const int32_t count =
      boxed ? *static_cast<int32_t *>(mono_object_unbox(boxed)) : 0;
  for (int32_t index = 0; index < count && index < 40; ++index) {
    void *arguments[1] = {&index};
    if (MonoObject *found = find_sub_scene(
            scene_call(list, "get_Item", 1, arguments), depth + 1, wanted))
      return found;
  }
  return nullptr;
}

uint32_t s_home_scene_root = 0; /* weak */

MonoObject *home_scene_root() {
  MonoObject *cached = s_home_scene_root
                           ? mono_gchandle_get_target(s_home_scene_root)
                           : nullptr;
  if (cached && !is_disposed(cached))
    return cached;
  release_handle(s_home_scene_root);
  /* The tree is walked at most twice a second while Home is not there. */
  static uint64_t last_try = 0;
  static uint64_t tries = 0;
  if (++tries - last_try < 30 && last_try != 0)
    return nullptr;
  last_try = tries;
  MonoObject *scene =
      scene_call(ClockSceneRoot(), "get_ParentScene", 0, nullptr);
  for (int guard = 0; scene && guard < 16; ++guard) {
    MonoObject *up = scene_field(scene, "superScene");
    if (!up)
      break;
    scene = up;
  }
  MonoObject *layer = find_sub_scene(scene, 0, [](MonoObject *candidate) {
    MonoObject *exception = nullptr;
    MonoString *text = mono_object_to_string(candidate, &exception);
    return text && !exception &&
           managed_string(reinterpret_cast<MonoObject *>(text))
                   .rfind("TopMenu ", 0) == 0;
  });
  MonoObject *render = find_sub_scene(layer, 0, [](MonoObject *candidate) {
    return !std::strcmp(mono_class_get_name(mono_object_get_class(candidate)),
                        "AppScene") &&
           scene_field(candidate, "rootWidget");
  });
  MonoObject *root = scene_field(render, "rootWidget");
  if (!root || is_disposed(root) || !mono_gchandle_new_weakref)
    return nullptr;
  s_home_scene_root = mono_gchandle_new_weakref(root, 0);
  LOG_INFO("[CLOCK] Home scene root found (%p)", static_cast<void *>(root));
  return root;
}

/* Whether a widget lives in Home's scene. */
bool in_home_scene(MonoObject *widget) {
  MonoObject *home = home_scene_root();
  if (!home || !widget)
    return false;
  MonoObject *root = widget;
  for (int depth = 0; depth < 24; ++depth) {
    MonoObject *parent = parent_of(root);
    if (!parent || parent == root)
      break;
    root = parent;
  }
  return root == home;
}

/* May `label` replace `followed` as the clock we follow? Home's clock is
   never given up for another scene's, and is always preferred over one;
   otherwise the one on screen wins. */
bool should_follow(MonoObject *label, MonoObject *followed) {
  const bool followed_alive = followed && !is_disposed(followed);
  const bool label_home = in_home_scene(label);
  const bool followed_home = followed_alive && in_home_scene(followed);
  if (label_home != followed_home)
    return label_home;
  const bool followed_shown =
      followed_alive && native_clock_opacity(followed) > kShownOpacity;
  return !followed_shown && native_clock_opacity(label) > kShownOpacity;
}

static MonoImage *s_debug_pui_image = nullptr;

/* Diagnostic: write the loaded PUI assembly out so it can be decompiled. */
void DebugDumpPuiImage(const char *path) {
  if (!s_debug_pui_image || !path)
    return;
  struct ImageHead {
    int ref_count;
    void *raw_data_handle;
    const char *raw_data;
    uint32_t raw_data_len;
  };
  const ImageHead *head =
      reinterpret_cast<const ImageHead *>(s_debug_pui_image);
  LOG_INFO("[CLOCKDUMP] pui image data=%p len=%u",
           static_cast<const void *>(head->raw_data), head->raw_data_len);
  if (reinterpret_cast<uintptr_t>(head->raw_data) < 0x100000000ull ||
      head->raw_data_len < 0x1000 || head->raw_data_len > 64u * 1024 * 1024)
    return;
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    LOG_INFO("[CLOCKDUMP] pui image open errno=%d", errno);
    return;
  }
  size_t off = 0;
  while (off < head->raw_data_len) {
    const ssize_t n = write(fd, head->raw_data + off,
                            std::min<size_t>(head->raw_data_len - off, 1 << 20));
    if (n <= 0)
      break;
    off += static_cast<size_t>(n);
  }
  close(fd);
  LOG_INFO("[CLOCKDUMP] pui image wrote=%zu", off);
}

/* Label.set_SpannableText: the setter React really uses for the clock. At
   the turn of every minute it hands the label Sony's "HH:MM" this way, and
   the label draws it (white, no seconds) even though its plain Text still
   reads as ours, which is why the clock fell back to the stock look for the
   whole first second of each minute. For the clock label the request is
   answered with our own time instead; for any other label it goes through
   untouched. */
using SetSpannable = void (*)(MonoObject *, MonoObject *);
SetSpannable s_set_spannable_original = nullptr;

void set_spannable_hook(MonoObject *label, MonoObject *spannable) {
  std::string stock;
  if (label && spannable && mono_object_to_string) {
    MonoObject *exception = nullptr;
    MonoString *shown = mono_object_to_string(spannable, &exception);
    if (shown && !exception)
      stock = managed_string(reinterpret_cast<MonoObject *>(shown));
  }
  const bool clock_like = looks_like_stock_clock(stock.c_str());
  if (clock_like && clock_target() != label &&
      (has_clock_test_id(label) || plausible_native_clock_geometry(label))) {
    /* Another clock (the Control Center has its own). It becomes the one we
       follow if it is the one on screen; otherwise it is only remembered, so
       it is dressed the moment it shows. */
    if (should_follow(label, clock_target())) {
      bind_native_clock(label, stock);
    } else {
      /* The clock we follow is on screen and stays the one we follow. This
         one is dressed as a second clock, here and now. */
      const int alias = remember_clock_alias(label, stock);
      if (alias >= 0 && (AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled) &&
          s_native_label_set_text_orig) {
        update_clock_alias(label, static_cast<size_t>(alias), std::time(nullptr));
        return;
      }
    }
  } else if (clock_like && clock_target() != label &&
             find_clock_alias(label) < 0) {
    note_unbound_clock(label, stock);
  }
  if (clock_target() == label && (AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled) &&
      s_native_label_set_text_orig) {
    if (clock_like)
      s_last_stock_time = stock;
    const time_t now = std::time(nullptr);
    std::tm local{};
    if (local_time(now, local)) {
      const std::string rendered = format_time(local);
      if (MonoString *replacement = ui_string(rendered)) {
        s_native_label_set_text_orig(label, replacement);
        layout_native_clock(label, rendered);
        s_last_rendered = rendered;
        s_last_second = now;
        s_redraw_frames = 4;
        static int logged = 0;
        if (logged < 6) {
          ++logged;
          LOG_INFO("[CLOCK] React asked for '%s'; drawn as '%s'",
                   stock.c_str(), rendered.c_str());
        }
        return;
      }
    }
  }
  if (s_set_spannable_original)
    s_set_spannable_original(label, spannable);
}

void Install(MonoImage *pui) {
  s_debug_pui_image = pui;
  if (pui && mono_class_from_name) {
    MonoClass *ui3_label =
        mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Label");
    const uint64_t span = ui3_label ? Get_Address_of_Method(
                                          pui, ui3_label, "set_SpannableText", 1)
                                    : 0;
    const bool hooked =
        span && InstallDetour(span,
                              reinterpret_cast<void *>(&set_spannable_hook),
                              reinterpret_cast<void **>(&s_set_spannable_original));
    LOG_INFO("[CLOCK] SpannableText hook target=0x%lx installed=%d",
             static_cast<unsigned long>(span), hooked ? 1 : 0);
    if (!hooked)
      s_set_spannable_original = nullptr;
  }
  if (!pui || !mono_class_from_name) {
    LOG_WARN("[HOME-SPACES:NATIVE] UI3.Label.Text hook skipped pui=%p "
             "class_resolver=%d",
             static_cast<void *>(pui), mono_class_from_name ? 1 : 0);
    return;
  }
  MonoClass *native_label =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Label");
  for (MonoClass *klass = native_label;
       klass && !s_is_disposed_getter && mono_class_get_parent;
       klass = mono_class_get_parent(klass)) {
    MonoProperty *disposed = mono_class_get_property_from_name
                                 ? mono_class_get_property_from_name(
                                       klass, "IsDisposed")
                                 : nullptr;
    s_is_disposed_getter =
        disposed && mono_property_get_get_method
            ? mono_property_get_get_method(disposed)
            : nullptr;
  }
  MonoProperty *native_text =
      native_label && mono_class_get_property_from_name
          ? mono_class_get_property_from_name(native_label, "Text")
          : nullptr;
  MonoMethod *native_text_setter =
      native_text && mono_property_get_set_method
          ? mono_property_get_set_method(native_text)
          : nullptr;
  const uint64_t native_text_address =
      native_text_setter && mono_compile_method
          ? mono_compile_method(native_text_setter)
          : 0;
  bool native_text_hook_installed = false;
  if (native_text_address)
    native_text_hook_installed = InstallDetour(
        native_text_address,
        reinterpret_cast<void *>(&native_label_set_text_hook),
        reinterpret_cast<void **>(&s_native_label_set_text_orig));
  LOG_INFO("[HOME-SPACES:NATIVE] UI3.Label.Text class=%p property=%p "
           "setter=%p target=0x%lx installed=%d original=%p",
           static_cast<void *>(native_label), static_cast<void *>(native_text),
           static_cast<void *>(native_text_setter),
           static_cast<unsigned long>(native_text_address),
           native_text_hook_installed ? 1 : 0,
           reinterpret_cast<void *>(s_native_label_set_text_orig));
  const uint64_t native_update_address =
      native_label
          ? Get_Address_of_Method(pui, native_label, "OnUpdate", 0)
          : 0;
  bool native_update_hook_installed = false;
  if (native_update_address)
    native_update_hook_installed = InstallDetour(
        native_update_address,
        reinterpret_cast<void *>(&native_label_on_update_hook),
        reinterpret_cast<void **>(&s_native_label_on_update_orig));
  LOG_INFO("[HOME-SPACES:NATIVE] UI3.Label.OnUpdate class=%p "
           "target=0x%lx installed=%d original=%p",
           static_cast<void *>(native_label),
           static_cast<unsigned long>(native_update_address),
           native_update_hook_installed ? 1 : 0,
           reinterpret_cast<void *>(s_native_label_on_update_orig));
#ifndef NDEBUG
  clock_logf("hooks pid=%d process=SceShellUI module=PUI namespace="
             "Sce.PlayStation.PUI.UI3 class=Label setter=0x%lx "
             "setter_installed=%d setter_original=%p on_update=0x%lx "
             "update_installed=%d update_original=%p firmware_runtime="
             "resolved-at-injection",
             getpid(), static_cast<unsigned long>(native_text_address),
             native_text_hook_installed ? 1 : 0,
             reinterpret_cast<void *>(s_native_label_set_text_orig),
             static_cast<unsigned long>(native_update_address),
             native_update_hook_installed ? 1 : 0,
             reinterpret_cast<void *>(s_native_label_on_update_orig));
  LOG_INFO("[CLOCK:HOOKS] pid=%d PUI.UI3.Label.Text=0x%lx installed=%d "
           "OnUpdate=0x%lx installed=%d",
           getpid(), static_cast<unsigned long>(native_text_address),
           native_text_hook_installed ? 1 : 0,
           static_cast<unsigned long>(native_update_address),
           native_update_hook_installed ? 1 : 0);
#endif

}

MonoObject *debug_prop_object(MonoObject *node, const char *name);
std::string debug_prop_text(MonoObject *node, const char *name,
                            size_t max_len);
MonoObject *ClockSceneRoot();

/* Root widgets of the scenes a clock has been seen in. They outlive the
   label, which React replaces when it rebuilds the bar. */
uint32_t s_known_roots[2] = {};

MonoObject *find_clock_label(MonoObject *widget, int depth, int &budget) {
  if (!widget || depth > 14 || budget-- <= 0 || is_disposed(widget))
    return nullptr;
  if (has_clock_test_id(widget))
    return widget;
  MonoObject *child = debug_prop_object(widget, "FirstChild");
  for (int guard = 0; child && guard < 48; ++guard) {
    if (MonoObject *found = find_clock_label(child, depth + 1, budget))
      return found;
    child = debug_prop_object(child, "NextSibling");
  }
  return nullptr;
}

/* No clock is being followed. The hooks pick a new label up when its text
   is set; if that moment was missed (the label was made before the hook
   could see it, or failed the checks), look for it by its test id in the
   scenes where the clock used to be. */
void recover_clock() {
  for (uint32_t handle : s_known_roots) {
    MonoObject *root = handle ? mono_gchandle_get_target(handle) : nullptr;
    if (!root || is_disposed(root))
      continue;
    int budget = 600;
    MonoObject *label = find_clock_label(root, 0, budget);
    if (!label || native_clock_opacity(label) <= kShownOpacity)
      continue;
    const std::string stock = raw_text(label);
    bind_native_clock(label, stock);
    LOG_INFO("[CLOCK] clock label found again by its test id (%p '%s')",
             static_cast<void *>(label), stock.c_str());
    return;
  }
}

/* Once a second every clock label in the scenes we know is looked at, not
   only the one being followed. One that is on screen showing Sony's own
   time, while the custom clock is enabled, is taken over there and then, and
   the circumstances are logged: whichever route let a stock clock through
   (React rebuilding the bar, a label the text hook judged not to be the
   clock, two clocks on screen at once), this is where it ends. */
uint32_t s_known_labels[2] = {};

void sweep_clock_labels(time_t now) {
  if (!(AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled))
    return;
  static int rescan = 0;
  const bool may_rescan = --rescan <= 0;
  if (may_rescan)
    rescan = 3;
  std::tm local{};
  if (!local_time(now, local))
    return;
  /* The clock followed should be Home's whenever Home has one. */
  if (may_rescan) {
    MonoObject *followed = clock_target();
    MonoObject *home = home_scene_root();
    if (home && !(followed && !is_disposed(followed) &&
                  in_home_scene(followed))) {
      int budget = 600;
      MonoObject *label = find_clock_label(home, 0, budget);
      if (label && label != followed) {
        bind_native_clock(label, raw_text(label));
        LOG_INFO("[CLOCK] following Home's clock again (%p)",
                 static_cast<void *>(label));
      }
    }
  }
  for (size_t i = 0; i < 2; ++i) {
    MonoObject *root = s_known_roots[i]
                           ? mono_gchandle_get_target(s_known_roots[i])
                           : nullptr;
    if (!root || is_disposed(root))
      continue;
    MonoObject *label = s_known_labels[i]
                            ? mono_gchandle_get_target(s_known_labels[i])
                            : nullptr;
    if ((!label || is_disposed(label)) && may_rescan) {
      int budget = 600;
      label = find_clock_label(root, 0, budget);
      release_handle(s_known_labels[i]);
      if (label)
        s_known_labels[i] = mono_gchandle_new_weakref(label, 0);
    }
    if (!label || is_disposed(label) ||
        native_clock_opacity(label) <= kShownOpacity)
      continue;
    const std::string shown = raw_text(label);
    if (!looks_like_stock_clock(shown.c_str()) || shown == format_time(local))
      continue;
    MonoObject *target = clock_target();
    LOG_INFO("[CLOCK] stock time '%s' on screen: label=%p target=%p "
             "known=%d target_opacity=%.2f; taking it over",
             shown.c_str(), static_cast<void *>(label),
             static_cast<void *>(target), find_clock_alias(label) >= 0 ? 1 : 0,
             target ? native_clock_opacity(target) : -1.0f);
    if (target == label) {
      s_force_refresh = true; /* ours already: the next tick redraws it */
      s_last_rendered.clear();
    } else if (should_follow(label, target)) {
      bind_native_clock(label, shown);
    } else {
      /* Another clock is being followed and is on screen too: dress this
         one as well, as a second clock. */
      const int alias = remember_clock_alias(label, shown);
      if (alias >= 0)
        update_clock_alias(label, static_cast<size_t>(alias), now);
    }
  }
}

void Tick() {
  const time_t now = std::time(nullptr);
  sync_clock_language(now);
  {
    static time_t swept = 0;
    if (now != swept) {
      swept = now;
      sweep_clock_labels(now);
    }
  }
  MonoObject *target = clock_target();
  if (!target) {
    static int recover_countdown = 0;
    if (--recover_countdown <= 0) {
      recover_countdown = 60;
      recover_clock();
    }
    if (s_clock_handle) {
#ifndef NDEBUG
      record_clock_event("weak-target-null", nullptr);
      if (s_native_visible)
        dump_clock_history("weak-target-null");
#endif
      release_clock(false);
    }
    s_date_overlay = {};
    return;
  }
  /* Whether Sony's clock label is alive and how opaque it is means walking
     its parent chain. Its fades last hundreds of milliseconds, so sampling
     at 30 Hz tracks them just as well as doing it on every frame. */
  static uint64_t s_last_probe_ms = 0;
  const uint64_t probe_ms = monotonic_millis();
  const bool probe = s_force_refresh || probe_ms - s_last_probe_ms >= 33;
  if (probe)
    s_last_probe_ms = probe_ms;
  if (probe && is_disposed(target)) {
#ifndef NDEBUG
    record_clock_event("tick-disposed", target);
    dump_clock_tree(target, "tick-disposed");
    if (s_native_visible)
      dump_clock_history("tick-disposed");
#endif
    release_clock(false);
    forget_clock_alias(target);
    return;
  }
  if (probe) {
    const int target_alias = find_clock_alias(target);
    if (target_alias >= 0)
      adopt_native_geometry(
          target, s_clock_aliases[static_cast<size_t>(target_alias)], true);
  }
  /* React put its own geometry back (it re-renders the bar now and then):
     redraw at once rather than at the next second. Two plain property reads
     a frame. */
  if (s_native_customized && s_applied_width > 0.0f &&
      (std::fabs(label_dimension(target, "Width") - s_applied_width) > 0.5f ||
       std::fabs(label_dimension(target, "X") - s_applied_x) > 0.5f))
  {
    s_redraw_frames = std::max(s_redraw_frames, 2);
    static int logged = 0;
    if (logged < 20) {
      ++logged;
      LOG_INFO("[CLOCK] React restyled the clock (width %.0f, ours %.0f); "
               "redrawn the same frame",
               label_dimension(target, "Width"), s_applied_width);
    }
  }
  /* The same for the text, thirty times a second: at the turn of the minute
     React writes Sony's time, and not always by the setter the hook is on. */
  if (probe && s_native_customized && (AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled) &&
      !s_last_rendered.empty() && s_native_opacity > kShownOpacity) {
    const std::string shown = raw_text(target);
    if (shown != s_last_rendered) {
      static int logged = 0;
      if (logged < 20) {
        ++logged;
        LOG_INFO("[CLOCK] the clock showed '%s' instead of '%s'; redrawn",
                 shown.c_str(), s_last_rendered.c_str());
      }
      s_last_rendered.clear();
      s_redraw_frames = std::max(s_redraw_frames, 2);
    }
  }
  if (s_redraw_frames > 0) {
    --s_redraw_frames;
    s_force_refresh = true;
  }
  const float opacity =
      probe ? native_clock_opacity(target) : s_native_opacity;
  /* The clock we follow is not on screen. If another known clock is (Home
     again after the Control Center closed, for instance), follow that one.
     This used to depend on that label's own update callback, which PUI only
     runs when the label changes. */
  if (probe && opacity <= kShownOpacity) {
    for (NativeClockAlias &alias : s_clock_aliases) {
      MonoObject *other = alias.handle && mono_gchandle_get_target
                              ? mono_gchandle_get_target(alias.handle)
                              : nullptr;
      if (!other || other == target || is_disposed(other) ||
          native_clock_opacity(other) <= kShownOpacity)
        continue;
      const std::string stock = alias.stock_time;
      bind_native_clock(other, stock);
      return;
    }
  }
  const bool opacity_changed =
      std::abs(opacity - s_native_opacity) > 0.001f;
  s_native_opacity = opacity;
  const bool visible = opacity > kShownOpacity;
  const bool visibility_changed = visible != s_native_visible;
#ifndef NDEBUG
  if (visibility_changed) {
    record_clock_event(visible ? "visible" : "hidden", target);
    dump_clock_tree(target, visible ? "visible" : "hidden");
    LOG_INFO("[CLOCK:VISIBILITY] pid=%d label=%p visible=%d opacity=%.3f "
             "text='%s' x=%.1f y=%.1f width=%.1f height=%.1f",
             getpid(), static_cast<void *>(target), visible ? 1 : 0, opacity,
             raw_text(target).c_str(), label_dimension(target, "X"),
             label_dimension(target, "Y"), label_dimension(target, "Width"),
             label_dimension(target, "Height"));
    if (!visible)
      dump_clock_history("native-visible-to-hidden");
  }
#endif
  s_native_visible = visible;
  /* Leave a clock that is not on screen alone. PUI renders a label's text
     asynchronously and only for scenes that are being drawn; a text or size
     change made behind Settings or the Control Center left that render
     pending for good (IsReadyToRenderInternal stayed false), and the time was
     never painted again once Home came back. Everything is brought up to date
     the moment the clock is shown again. */
  if (!visible && (AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled)) {
    if (visibility_changed)
      publish_date_overlay(now, true, s_last_rendered);
    s_force_refresh = true;
    return;
  }
  const uint64_t signature = settings_signature();
  const bool settings_changed = signature != s_settings_signature;
  const uint64_t frame_ms = monotonic_millis();
  const bool animation_frame =
      clock_animation_active() &&
      frame_ms - s_last_animation_frame_ms >= kAnimationFrameMs;
  if (!s_force_refresh && now == s_last_second && !settings_changed &&
      !visibility_changed && !opacity_changed && !animation_frame)
    return;
  /* An animation frame on its own only moves the colour. Text and geometry
     are refreshed once a second and whenever something actually changed;
     re-stating a dozen layout properties of Sony's clock label twenty times a
     second invalidated its layout for nothing. */
  const bool colour_only = animation_frame && !s_force_refresh &&
                           now == s_last_second && !settings_changed &&
                           !visibility_changed && !opacity_changed &&
                           (AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled) &&
                           s_native_customized;
  s_force_refresh = false;
  s_settings_signature = signature;
  if (!clock_animation_active())
    s_animation_started_ms = 0;
  if (animation_frame)
    s_last_animation_frame_ms = frame_ms;

  if (!(AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled)) {
    publish_date_overlay(now, false, {});
    restore_all_native_labels();
    if (s_last_rendered != s_last_stock_time || settings_changed) {
      if (MonoString *stock = ui_string(s_last_stock_time)) {
        if (s_native_label_set_text_orig)
          s_native_label_set_text_orig(target, stock);
      }
    }
    s_last_rendered = s_last_stock_time;
    s_last_second = now;
    return;
  }

  if (colour_only) {
    if (ensure_custom_time_color())
      set_object_property_invoke(target, "TextColor",
                                 s_custom_time_color_value);
    publish_date_overlay(now, true, s_last_rendered);
    return;
  }

  std::tm local{};
  if (!local_time(now, local))
    return;
  const std::string rendered = format_time(local);
  /* Checked against what the label really shows, not only against what was
     last written: React can put its own time back by a route the text hook
     does not see, and with seconds hidden nothing else would notice for up
     to a minute. */
  const bool overwritten = raw_text(target) != rendered;
  if (overwritten && rendered == s_last_rendered)
    LOG_INFO("[CLOCK] label text had been replaced; custom time restored");
  if (rendered != s_last_rendered || settings_changed || overwritten) {
    if (MonoString *text = ui_string(rendered)) {
      if (s_native_label_set_text_orig)
        s_native_label_set_text_orig(target, text);
    }
    s_last_rendered = rendered;
  }
  layout_native_clock(target, rendered);
  s_native_opacity = native_clock_opacity(target);
  s_native_visible = s_native_opacity > kShownOpacity;
  publish_date_overlay(now, true, rendered);
#ifndef NDEBUG
  trace_clock_update_once(target, now, rendered);
#endif
  s_last_second = now;
}

bool GetNativeClockOpacity(float *opacity);

void DebugSnapshot(char *out, size_t out_size) {
  if (!out || !out_size)
    return;
  /* Deliberately free of the time text, so the line only changes when the
     clock's state does. */
  MonoObject *target = clock_target();
  size_t used = static_cast<size_t>(std::snprintf(
      out, out_size,
      "target=%p disp=%d op=%.2f vis=%d cust=%d base(x=%.0f w=%.0f h=%.0f) "
      "rdy=%d label(x=%.0f y=%.0f w=%.0f h=%.0f len=%zu) date(vis=%d op=%.2f geo=%d)",
      static_cast<void *>(target), target ? (is_disposed(target) ? 1 : 0) : -1,
      target ? native_clock_opacity(target) : -1.0f, s_native_visible ? 1 : 0,
      s_native_customized ? 1 : 0, s_native_base_x, s_native_base_width,
      s_native_base_height,
      target && property_class(target, "IsReadyToRenderInternal")
          ? (Get_Property<bool>(
                 property_class(target, "IsReadyToRenderInternal"), target,
                 "IsReadyToRenderInternal")
                 ? 1
                 : 0)
          : -1,
      target ? label_dimension(target, "X") : -1.0f,
      target ? label_dimension(target, "Y") : -1.0f,
      target ? label_dimension(target, "Width") : -1.0f,
      target ? label_dimension(target, "Height") : -1.0f,
      target ? raw_text(target).size() : 0, s_date_overlay.visible ? 1 : 0,
      s_date_overlay.opacity, s_date_overlay.has_global_time_geometry ? 1 : 0));
  float mirror = -1.0f;
  GetNativeClockOpacity(&mirror);
  if (used < out_size)
    used += static_cast<size_t>(std::snprintf(out + used, out_size - used,
                                              " mirror=%.2f tw=%.1f/%.1f cx=%.1f "
                                              "text='%s'",
                                              mirror, s_measured_time_width,
                                              s_visible_time_width,
                                              s_visible_time_center_x,
                                              s_date_overlay.text.c_str()));
  for (size_t i = 0; i < s_clock_aliases.size() && used < out_size; ++i) {
    NativeClockAlias &alias = s_clock_aliases[i];
    MonoObject *label = alias.handle && mono_gchandle_get_target
                            ? mono_gchandle_get_target(alias.handle)
                            : nullptr;
    if (!label)
      continue;
    used += static_cast<size_t>(std::snprintf(
        out + used, out_size - used,
        " | a%zu=%p%s op=%.2f base(x=%.0f w=%.0f) label(x=%.0f w=%.0f len=%zu) "
        "cust=%d",
        i, static_cast<void *>(label), label == target ? "*" : "",
        native_clock_opacity(label), alias.base_x, alias.base_width,
        label_dimension(label, "X"), label_dimension(label, "Width"),
        raw_text(label).size(), alias.customized ? 1 : 0));
  }
}

/* Diagnostic: every readable property of the clock label and of its first
 * parents, one line per object. Only reached from the profiler script. */
void DebugDumpObject(MonoObject *node, const char *tag);

std::string debug_prop_text(MonoObject *node, const char *name,
                            size_t max_len) {
  if (!node)
    return "-";
  MonoMethod *getter = nullptr;
  char method[64];
  std::snprintf(method, sizeof(method), "get_%s", name);
  for (MonoClass *k = mono_object_get_class(node); k && !getter;
       k = mono_class_get_parent(k))
    getter = mono_class_get_method_from_name(k, method, 0);
  if (!getter)
    return "-";
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, node, nullptr, &exception);
  if (exception || !value)
    return exception ? "!" : "null";
  MonoObject *exc2 = nullptr;
  MonoString *str = mono_object_to_string(value, &exc2);
  std::string out = exc2 || !str
                        ? "?"
                        : managed_string(reinterpret_cast<MonoObject *>(str));
  if (out.size() > max_len)
    out.resize(max_len);
  return out;
}

MonoObject *debug_prop_object(MonoObject *node, const char *name) {
  if (!node)
    return nullptr;
  MonoMethod *getter = nullptr;
  char method[64];
  std::snprintf(method, sizeof(method), "get_%s", name);
  for (MonoClass *k = mono_object_get_class(node); k && !getter;
       k = mono_class_get_parent(k))
    getter = mono_class_get_method_from_name(k, method, 0);
  if (!getter)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, node, nullptr, &exception);
  return exception ? nullptr : value;
}

MonoObject *debug_root_widget() {
  MonoObject *node = clock_target();
  for (int depth = 0; node && depth < 24; ++depth) {
    MonoObject *parent = parent_of(node);
    if (!parent || parent == node)
      break;
    node = parent;
  }
  return node;
}

void DebugDumpChain(const char *tag) {
  MonoObject *node = clock_target();
  for (int depth = 0; node && depth < 24; ++depth) {
    if (is_disposed(node))
      break;
    MonoClass *klass = mono_object_get_class(node);
    const auto text = [&](const char *name) -> std::string {
      MonoClass *owner = property_class(node, name);
      MonoMethod *getter = nullptr;
      for (MonoClass *k = klass; k && !getter; k = mono_class_get_parent(k)) {
        char method[64];
        std::snprintf(method, sizeof(method), "get_%s", name);
        getter = mono_class_get_method_from_name(k, method, 0);
      }
      if (!owner || !getter)
        return "-";
      MonoObject *exception = nullptr;
      MonoObject *value = mono_runtime_invoke(getter, node, nullptr, &exception);
      if (exception || !value)
        return exception ? "!" : "null";
      MonoObject *exc2 = nullptr;
      MonoString *str = mono_object_to_string(value, &exc2);
      std::string out = exc2 || !str ? "?" : managed_string(reinterpret_cast<MonoObject *>(str));
      if (out.size() > 40)
        out.resize(40);
      return out;
    };
    LOG_INFO("[CLOCKCHAIN] %s d%d %p %s.%s name=%s x=%s y=%s w=%s h=%s op=%s "
             "fop=%s vis=%s clip=%s children=%s",
             tag ? tag : "", depth, static_cast<void *>(node),
             mono_class_get_namespace(klass), mono_class_get_name(klass),
             text("Name").c_str(), text("X").c_str(), text("Y").c_str(),
             text("Width").c_str(), text("Height").c_str(),
             text("Opacity").c_str(), text("FinalOpacity").c_str(),
             text("Visible").c_str(), text("ClipChildren").c_str(),
             text("ChildCount").c_str());
    MonoObject *parent = parent_of(node);
    if (!parent || parent == node)
      break;
    node = parent;
  }
}

void DebugDumpObject(MonoObject *node, const char *tag) {
  if (!node)
    return;
  std::string line;
  for (MonoClass *klass = mono_object_get_class(node); klass;
       klass = mono_class_get_parent(klass)) {
    const char *class_name = mono_class_get_name(klass);
    if (!class_name || !std::strcmp(class_name, "Object"))
      break;
    line += "#";
    line += class_name;
    line += ";";
    void *iter = nullptr;
    while (MonoMethod *method = mono_class_get_methods(klass, &iter)) {
      const char *name = mono_method_get_name(method);
      if (!name || std::strncmp(name, "get_", 4) != 0)
        continue;
      uint32_t iflags = 0;
      if (mono_method_get_flags(method, &iflags) & 0x0010) /* static */
        continue;
      MonoMethodSignature *sig = mono_method_signature(method);
      if (!sig || mono_signature_get_param_count(sig) != 0)
        continue;
      MonoObject *exception = nullptr;
      MonoObject *value = mono_runtime_invoke(method, node, nullptr, &exception);
      std::string text = "null";
      if (exception)
        text = "!exc";
      else if (value) {
        MonoObject *exc2 = nullptr;
        MonoString *str = mono_object_to_string(value, &exc2);
        text = exc2 || !str
                   ? "?"
                   : managed_string(reinterpret_cast<MonoObject *>(str));
      }
      if (text.size() > 120)
        text.resize(120);
      line += name + 4;
      line += '=';
      line += text;
      line += ';';
    }
  }
  for (size_t off = 0; off < line.size(); off += 400)
    LOG_INFO("[CLOCKDUMP] %s %p %s", tag ? tag : "",
             static_cast<void *>(node), line.substr(off, 400).c_str());
}

void DebugDumpProperties(const char *tag) {
  (void)tag;
  DebugDumpObject(debug_prop_object(debug_root_widget(), "ParentScene"),
                  "scene-steady");
}

/* The root widget of the scene the system clock is in (Home's root, or the
 * Control Center's while its clock is the one on screen). Scene fades and
 * zooms are applied above it, so whatever is parented to it takes part in
 * them exactly like the clock does. */
MonoObject *ClockSceneRoot() {
  MonoObject *target = clock_target();
  if (!target || !mono_gchandle_new_weakref || is_disposed(target))
    return nullptr;
  static uint32_t root_handle = 0;
  static uint32_t root_for_handle = 0;
  MonoObject *root_for =
      root_for_handle ? mono_gchandle_get_target(root_for_handle) : nullptr;
  MonoObject *root =
      root_handle && root_for == target ? mono_gchandle_get_target(root_handle)
                                        : nullptr;
  if (!root) {
    release_handle(root_handle);
    release_handle(root_for_handle);
    root = target;
    for (int depth = 0; depth < 24; ++depth) {
      MonoObject *parent = parent_of(root);
      if (!parent || parent == root)
        break;
      root = parent;
    }
    if (root == target)
      return nullptr;
    root_handle = mono_gchandle_new_weakref(root, 0);
    /* Remembered for recover_clock(), two scenes at most (Home and the
       Control Center). */
    bool known = false;
    for (uint32_t handle : s_known_roots)
      known = known || (handle && mono_gchandle_get_target(handle) == root);
    if (!known) {
      static int next = 0;
      release_handle(s_known_roots[next]);
      s_known_roots[next] = mono_gchandle_new_weakref(root, 0);
      next = (next + 1) % 2;
    }
    root_for_handle = mono_gchandle_new_weakref(target, 0);
  }
  return is_disposed(root) ? nullptr : root;
}

namespace {
void visit_scene(MonoObject *scene, int depth,
                 void (*visit)(MonoObject *root, void *context),
                 void *context) {
  if (!scene || depth > 10)
    return;
  if (MonoObject *root = scene_field(scene, "rootWidget"))
    if (!is_disposed(root))
      visit(root, context);
  for (const char *list_name : {"subSceneList", "modalSceneList"}) {
    MonoObject *list = scene_field(scene, list_name);
    MonoObject *boxed = scene_call(list, "get_Count", 0, nullptr);
    const int32_t count =
        boxed ? *static_cast<int32_t *>(mono_object_unbox(boxed)) : 0;
    for (int32_t index = 0; index < count && index < 40; ++index) {
      void *arguments[1] = {&index};
      visit_scene(scene_call(list, "get_Item", 1, arguments), depth + 1, visit,
                  context);
    }
  }
}
} // namespace

void VisitSceneRoots(void (*visit)(MonoObject *root, void *context),
                     void *context) {
  MonoObject *scene =
      scene_call(GetHomeRoot(), "get_ParentScene", 0, nullptr);
  for (int guard = 0; scene && guard < 16; ++guard) {
    MonoObject *up = scene_field(scene, "superScene");
    if (!up)
      break;
    scene = up;
  }
  visit_scene(scene, 0, visit, context);
}

MonoObject *GetHomeRoot() {
  /* Keeps the clock's scene known to recover_clock() as before. */
  MonoObject *clock_root = ClockSceneRoot();
  MonoObject *home = home_scene_root();
  return home ? home : clock_root;
}

bool IsWidgetDisposed(MonoObject *widget) { return is_disposed(widget); }

/* ShellUI outlives a reload of this module, and with it whatever an earlier
 * instance left under Home's root. Drop those before adding ours. */
void RemoveStaleHomeChildren(MonoObject *root) {
  MonoObject *parents[2] = {root, debug_prop_object(root, "FirstChild")};
  for (MonoObject *parent : parents) {
    MonoObject *child = debug_prop_object(parent, "FirstChild");
    for (int guard = 0; child && guard < 64; ++guard) {
      MonoObject *next = debug_prop_object(child, "NextSibling");
      const std::string name = debug_prop_text(child, "Name", 40);
      const bool ours = name.rfind("id_anivsh_", 0) == 0 ||
                        raw_text(child) == "ANIKETOS UI3 TEST";
      if (ours) {
        MonoMethod *remove = nullptr;
        for (MonoClass *klass = mono_object_get_class(child);
             klass && !remove; klass = mono_class_get_parent(klass))
          remove =
              mono_class_get_method_from_name(klass, "RemoveFromParent", 0);
        MonoObject *exception = nullptr;
        if (remove)
          mono_runtime_invoke(remove, child, nullptr, &exception);
      }
      child = next;
    }
  }
}

std::string DebugPropText(MonoObject *object, const char *name) {
  return object ? debug_prop_text(object, name, 60) : std::string("-");
}

bool GetNativeClockOpacity(float *opacity) {
  MonoObject *target = clock_target();
  if (!opacity || !target)
    return false;
  /* s_native_opacity is only sampled at 30 Hz and says whether the clock is
     shown at all (parent chain, disposal). The value itself is read here on
     every call: one property of the label, which already includes every
     ancestor and the scene fade. */
  float live = s_native_opacity;
  if (s_native_opacity > 0.0f && !is_disposed(target)) {
    if (MonoClass *klass = property_class(target, "FinalOpacity"))
      live = Get_Property<float>(klass, target, "FinalOpacity");
  }
  /* A Home that is merely covered is parked at about 0.02, not at 0; our
     labels sit on a layer above everything, so map that floor to nothing. */
  constexpr float kParked = 0.03f;
  *opacity = std::clamp((live - kParked) / (1.0f - kParked), 0.0f, 1.0f);
  return true;
}

bool GetDateOverlayState(DateOverlayState *state) {
  if (!state)
    return false;
  *state = s_date_overlay;
  return state->visible;
}

void RequestRefresh() { s_force_refresh = true; }

} // namespace ShellClock
