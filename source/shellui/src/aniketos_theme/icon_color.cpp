#include "aniketos_theme/icon_color.hpp"

#include "aniketos_theme/color.hpp"
#include "aniketos_theme/customization.hpp"
#include "external_symbols.hpp"
#include "aniketos_theme/clock/shell_clock.hpp"
#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/monotonic_clock.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <time.h>
#include <vector>

extern onion::Settings g_settings;

namespace AniketosTheme::IconColor {
namespace {

constexpr const char *kLogPath = ONION_DIST_USER_DATA_ROOT "/icon_color.log";
constexpr uint32_t kTypeDefTable = 2;
constexpr uint32_t kTypeDefTokenBase = 0x02000000u;
constexpr size_t kMaxLoggedTokens = 256;
constexpr size_t kMaxLoggedImages = 512;
constexpr size_t kMaxLoggedImageSources = 192;
constexpr size_t kMaxTracked = 4096;
constexpr size_t kMaxHookedSetters = 8;
constexpr size_t kMaxImageSourceSetters = 6;
constexpr size_t kMaxMenuIconSetters = 6;
constexpr size_t kMaxLoggedMenuIcons = 96;
constexpr size_t kMaxLoggedAttach = 192;
constexpr size_t kMaxVisibilityCache = 64;
constexpr size_t kMaxSceneNodes = 1200;
constexpr size_t kMaxSceneIdentities = 128;
constexpr int kMaxSceneDepth = 40;

struct Uicolor {
  float r;
  float g;
  float b;
  float a;
  int32_t color_space;
};
static_assert(sizeof(Uicolor) == 20);

using IconSetter = void (*)(MonoObject *, Uicolor);

struct TrackedIcon {
  MonoObject *pinned = nullptr;
  uint32_t handle = 0;
  MonoMethod *method = nullptr;
  Uicolor restore{};
  std::string class_name;
  std::string test_id;
  std::string name;
  bool image = false;
};

struct VisibilityProbe {
  MonoClass *klass = nullptr;
  MonoMethod *getter = nullptr;
};

IconSetter s_originals[kMaxHookedSetters] = {};
MonoMethod *s_methods[kMaxHookedSetters] = {};
uint64_t s_addresses[kMaxHookedSetters] = {};
size_t s_hooked_count = 0;
IconSetter s_image_originals[kMaxHookedSetters] = {};
MonoMethod *s_image_methods[kMaxHookedSetters] = {};
uint64_t s_image_addresses[kMaxHookedSetters] = {};
size_t s_image_hooked_count = 0;
using SourceSetter = void (*)(MonoObject *, MonoObject *);
SourceSetter s_image_source_originals[kMaxImageSourceSetters] = {};
uint64_t s_image_source_addresses[kMaxImageSourceSetters] = {};
size_t s_image_source_hooked_count = 0;
SourceSetter s_menu_icon_originals[kMaxMenuIconSetters] = {};
uint64_t s_menu_icon_addresses[kMaxMenuIconSetters] = {};
const char *s_menu_icon_properties[kMaxMenuIconSetters] = {};
size_t s_menu_icon_hooked_count = 0;
SourceSetter s_attach_original = nullptr;
uint64_t s_attach_address = 0;
bool s_attach_installed = false;
std::vector<TrackedIcon> s_tracked;
std::vector<std::string> s_logged_tokens;
std::vector<std::string> s_logged_images;
std::vector<std::string> s_logged_image_sources;
std::vector<std::string> s_logged_menu_icons;
std::vector<std::string> s_logged_menu_props;
std::vector<std::string> s_logged_menu_prop_none;
std::vector<std::string> s_logged_menu_elements;
std::vector<std::string> s_logged_menu_element_no_color;
std::vector<std::string> s_logged_menu_element_value;
std::vector<std::string> s_logged_menu_lazy;
std::vector<std::string> s_logged_attach;
std::vector<std::string> s_logged_menu_button;
std::vector<std::string> s_logged_menu_button_missing;
std::vector<std::string> s_logged_restore_unknown;
std::vector<std::string> s_logged_scene_nodes;
std::vector<VisibilityProbe> s_visibility_cache;
MonoClass *s_mode_class = nullptr;
MonoClass *s_image_box_class = nullptr;
MonoMethod *s_image_filter_setter = nullptr;
MonoMethod *s_image_filter_getter = nullptr;
MonoMethod *s_disposed_getter = nullptr;
uint64_t s_rainbow_started_ms = 0;
Uicolor s_last_rainbow{};
bool s_has_last_rainbow = false;
bool s_weak = false;
bool s_logged_apply = false;
bool s_logged_capacity = false;
bool s_logged_disposed_probe = false;
bool s_scene_scan_active = false;
bool s_image_source_active = false;
bool s_menu_icon_active = false;
bool s_attach_active = false;
uint64_t s_last_white_ms = 0;
uint64_t s_last_white_log_ms = 0;

void diag(const char *format, ...) {
  FILE *file = std::fopen(kLogPath, "a");
  if (!file)
    return;
  va_list args;
  va_start(args, format);
  std::vfprintf(file, format, args);
  va_end(args);
  std::fputc('\n', file);
  std::fflush(file);
  std::fclose(file);
}

std::string ascii_lower(std::string value) {
  for (char &c : value) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return value;
}

bool contains(const std::string &value, const char *needle) {
  return value.find(needle) != std::string::npos;
}

bool feature_on() {
  return AniketosTheme::CustomizationOn() &&
         g_settings.system_icon_color_enabled;
}

std::string type_name(MonoType *type) {
  if (!type || !mono_type_get_name)
    return {};
  char *name = mono_type_get_name(type);
  if (!name)
    return {};
  std::string result(name);
  if (mono_free)
    mono_free(name);
  return result;
}

const char *class_name(MonoClass *klass) {
  if (!klass || !mono_class_get_name)
    return nullptr;
  return mono_class_get_name(klass);
}

bool is_void(const std::string &name) {
  return name == "System.Void" || name == "void";
}

bool is_uicolor(const std::string &name) {
  return name.find("UIColor") != std::string::npos;
}

MonoMethodSignature *signature_of(MonoMethod *method) {
  return method && mono_method_signature ? mono_method_signature(method)
                                         : nullptr;
}

bool uicolor_setter(MonoMethod *method) {
  MonoMethodSignature *signature = signature_of(method);
  if (!signature || !mono_signature_get_param_count ||
      !mono_signature_get_params || !mono_signature_get_return_type)
    return false;
  if (!is_void(type_name(mono_signature_get_return_type(signature))))
    return false;
  if (mono_signature_get_param_count(signature) != 1)
    return false;
  void *iter = nullptr;
  MonoType *param = mono_signature_get_params(signature, &iter);
  return param && is_uicolor(type_name(param));
}

std::string signature_text(MonoMethod *method) {
  MonoMethodSignature *signature = signature_of(method);
  if (!signature || !mono_signature_get_return_type ||
      !mono_signature_get_param_count || !mono_signature_get_params)
    return {};
  std::string result = type_name(mono_signature_get_return_type(signature));
  result += '(';
  void *iter = nullptr;
  const uint32_t count = mono_signature_get_param_count(signature);
  for (uint32_t i = 0; i < count; ++i) {
    if (i)
      result += ',';
    MonoType *param = mono_signature_get_params(signature, &iter);
    result += param ? type_name(param) : std::string("?");
  }
  result += ')';
  return result;
}

MonoMethod *find_method(MonoClass *klass, const char *wanted) {
  if (!klass || !mono_class_get_methods || !mono_method_get_name)
    return nullptr;
  void *iter = nullptr;
  while (MonoMethod *method = mono_class_get_methods(klass, &iter)) {
    const char *name = mono_method_get_name(method);
    if (name && std::strcmp(name, wanted) == 0)
      return method;
  }
  return nullptr;
}

MonoMethod *find_method_in_hierarchy(MonoClass *klass, const char *wanted) {
  int guard = 0;
  for (MonoClass *current = klass; current && guard < 64;
       current = mono_class_get_parent ? mono_class_get_parent(current)
                                       : nullptr, ++guard) {
    if (MonoMethod *method = find_method(current, wanted))
      return method;
  }
  return nullptr;
}

MonoProperty *find_property_in_hierarchy(MonoClass *klass, const char *wanted) {
  int guard = 0;
  for (MonoClass *current = klass; current && guard < 64;
       current = mono_class_get_parent ? mono_class_get_parent(current)
                                       : nullptr, ++guard) {
    if (!mono_class_get_property_from_name)
      return nullptr;
    if (MonoProperty *property =
            mono_class_get_property_from_name(current, wanted))
      return property;
  }
  return nullptr;
}

MonoMethod *find_property_getter_in_hierarchy(MonoClass *klass,
                                              const char *wanted) {
  MonoProperty *property = find_property_in_hierarchy(klass, wanted);
  if (!property || !mono_property_get_get_method)
    return nullptr;
  return mono_property_get_get_method(property);
}

bool invoke_bool(MonoMethod *method, MonoObject *object, bool *value) {
  if (!method || !object || !value || !mono_runtime_invoke ||
      !mono_object_unbox)
    return false;
  MonoObject *exception = nullptr;
  MonoObject *result = mono_runtime_invoke(method, object, nullptr, &exception);
  if (exception || !result)
    return false;
  void *unboxed = mono_object_unbox(result);
  if (!unboxed)
    return false;
  *value = *static_cast<uint8_t *>(unboxed) != 0;
  return true;
}

void log_disposed_probe(bool ok) {
  if (s_logged_disposed_probe)
    return;
  s_logged_disposed_probe = true;
  diag("[ICONCOLOR] disposed probe=%s", ok ? "ok" : "missing");
}

bool object_disposed(MonoObject *object) {
  if (!object)
    return false;
  if (!s_disposed_getter) {
    log_disposed_probe(false);
    return false;
  }
  log_disposed_probe(true);
  bool disposed = false;
  return invoke_bool(s_disposed_getter, object, &disposed) && disposed;
}

MonoMethod *visibility_getter(MonoClass *klass) {
  if (!klass)
    return nullptr;
  for (const VisibilityProbe &probe : s_visibility_cache) {
    if (probe.klass == klass)
      return probe.getter;
  }
  MonoMethod *getter = find_property_getter_in_hierarchy(klass, "IsVisible");
  if (!getter)
    getter = find_method_in_hierarchy(klass, "get_IsVisible");
  if (!getter)
    getter = find_property_getter_in_hierarchy(klass, "Shown");
  if (!getter)
    getter = find_method_in_hierarchy(klass, "get_Shown");
  if (s_visibility_cache.size() < kMaxVisibilityCache)
    s_visibility_cache.push_back({klass, getter});
  return getter;
}

bool object_visible(MonoObject *object) {
  if (!object || !mono_object_get_class)
    return false;
  MonoMethod *getter = visibility_getter(mono_object_get_class(object));
  if (!getter)
    return true;
  bool visible = false;
  if (!invoke_bool(getter, object, &visible))
    return false;
  return visible;
}

bool interesting_method(const char *name) {
  if (!name)
    return false;
  static const char *const kNeedles[] = {"FilterColor", "TintColor", "Color",
                                         "Visibility", "Icon"};
  for (const char *needle : kNeedles) {
    if (std::strstr(name, needle))
      return true;
  }
  return false;
}

void log_class_methods(MonoClass *klass, int maximum) {
  if (!klass || !mono_class_get_methods || !mono_method_get_name ||
      !mono_method_signature)
    return;
  const char *type = class_name(klass);
  void *iter = nullptr;
  int logged = 0;
  while (MonoMethod *method = mono_class_get_methods(klass, &iter)) {
    const char *name = mono_method_get_name(method);
    if (!interesting_method(name))
      continue;
    diag("[ICONCOLOR] method type=%s name=%s signature=%s",
         type ? type : "?", name, signature_text(method).c_str());
    if (++logged >= maximum)
      break;
  }
}

bool menulist_interesting(const char *name) {
  if (!name)
    return false;
  static const char *const kTokens[] = {"icon",  "image", "element",
                                        "render", "filter", "color"};
  std::string lower = ascii_lower(name);
  for (const char *token : kTokens) {
    if (contains(lower, token))
      return true;
  }
  return false;
}

void log_menulist_class(MonoClass *klass) {
  if (!klass)
    return;
  const char *type = class_name(klass);
  void *method_iter = nullptr;
  int methods = 0;
  while (mono_class_get_methods) {
    MonoMethod *method = mono_class_get_methods(klass, &method_iter);
    if (!method)
      break;
    const char *name =
        mono_method_get_name ? mono_method_get_name(method) : nullptr;
    if (!menulist_interesting(name))
      continue;
    diag("[ICONCOLOR] menulist method type=%s name=%s signature=%s",
         type ? type : "?", name ? name : "?",
         signature_text(method).c_str());
    if (++methods >= 40)
      break;
  }
  void *field_iter = nullptr;
  int fields = 0;
  while (mono_class_get_fields) {
    MonoClassField *field = mono_class_get_fields(klass, &field_iter);
    if (!field)
      break;
    const char *name =
        mono_field_get_name ? mono_field_get_name(field) : nullptr;
    if (!menulist_interesting(name))
      continue;
    MonoType *field_type =
        mono_field_get_type ? mono_field_get_type(field) : nullptr;
    diag("[ICONCOLOR] menulist field type=%s name=%s fieldtype=%s",
         type ? type : "?", name ? name : "?",
         type_name(field_type).c_str());
    if (++fields >= 40)
      break;
  }
}

std::string object_text(MonoObject *object, const char *getter_name) {
  if (!object || !mono_object_get_class || !mono_class_get_method_from_name ||
      !mono_runtime_invoke || !mono_string_to_utf8)
    return {};
  MonoMethod *getter =
      find_method_in_hierarchy(mono_object_get_class(object), getter_name);
  if (!getter)
    return {};
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, object, nullptr, &exception);
  if (exception || !value)
    return {};
  const char *utf =
      mono_string_to_utf8(reinterpret_cast<MonoString *>(value));
  if (!utf)
    return {};
  std::string result(utf);
  if (mono_free)
    mono_free(const_cast<char *>(utf));
  return result;
}

MonoObject *object_property_object(MonoObject *object,
                                   const char *getter_name) {
  if (!object || !mono_object_get_class || !mono_runtime_invoke)
    return nullptr;
  MonoMethod *getter =
      find_method_in_hierarchy(mono_object_get_class(object), getter_name);
  if (!getter)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, object, nullptr, &exception);
  return exception ? nullptr : value;
}

bool hierarchy_name_contains(MonoClass *klass, const char *needle) {
  if (!klass || !needle)
    return false;
  int guard = 0;
  for (MonoClass *current = klass; current && guard < 64;
       current = mono_class_get_parent ? mono_class_get_parent(current)
                                       : nullptr, ++guard) {
    const char *name = class_name(current);
    if (name && contains(ascii_lower(name), needle))
      return true;
  }
  return false;
}

MonoObject *parent_object(MonoObject *object) {
  return object_property_object(object, "get_Parent");
}

bool widget_excluded(const std::string &type, const std::string &test_id,
                     const std::string &name) {
  const std::string all = ascii_lower(type + " " + test_id + " " + name);
  return contains(all, "background") || contains(all, "avatar") ||
         contains(all, "tile") || contains(all, "artwork");
}

struct WidgetIdentity {
  bool icon = false;
  bool settings = false;
  bool excluded = false;
};

WidgetIdentity classify_widget(MonoObject *object, const std::string &type,
                               const std::string &test_id,
                               const std::string &name) {
  WidgetIdentity identity{};
  MonoClass *klass = object && mono_object_get_class
                         ? mono_object_get_class(object)
                         : nullptr;
  identity.icon = hierarchy_name_contains(klass, "icon") ||
                  contains(ascii_lower(test_id + " " + name), "icon");
  identity.settings = hierarchy_name_contains(klass, "settings") ||
                      hierarchy_name_contains(klass, "ipdsettings");
  if (!identity.settings) {
    MonoObject *parent = object;
    for (int depth = 0; parent && depth < 10; ++depth) {
      parent = parent_object(parent);
      if (!parent)
        break;
      MonoClass *parent_class = mono_object_get_class
                                    ? mono_object_get_class(parent)
                                    : nullptr;
      if (hierarchy_name_contains(parent_class, "settings") ||
          hierarchy_name_contains(parent_class, "ipdsettings")) {
        identity.settings = true;
        break;
      }
    }
  }
  identity.excluded = widget_excluded(type, test_id, name);
  return identity;
}

bool widget_should_tint(const WidgetIdentity &identity) {
  return (identity.icon || identity.settings) && !identity.excluded;
}

Uicolor static_color() {
  return {std::clamp(g_settings.system_icon_color_r, 0, 255) / 255.0f,
          std::clamp(g_settings.system_icon_color_g, 0, 255) / 255.0f,
          std::clamp(g_settings.system_icon_color_b, 0, 255) / 255.0f, 1.0f, 0};
}

Uicolor white_color() { return {1.0f, 1.0f, 1.0f, 1.0f, 0}; }

Uicolor current_color() {
  if (g_settings.system_icon_color_animation != 1)
    return static_color();
  const uint64_t now = AniketosUI::MonotonicMs();
  if (!s_rainbow_started_ms)
    s_rainbow_started_ms = now;
  const float speed =
      std::clamp(g_settings.system_icon_color_animation_speed, 25, 400) /
      100.0f;
  const float elapsed =
      static_cast<float>(now - s_rainbow_started_ms) / 1000.0f;
  float hue = std::fmod(elapsed * 70.0f * speed, 360.0f);
  if (hue < 0.0f)
    hue += 360.0f;
  const RGB rgb = HSVToRGB({hue, 1.0f, 1.0f});
  return {rgb.r, rgb.g, rgb.b, 1.0f, 0};
}

void free_handle(uint32_t handle) {
  if (handle && mono_gchandle_free)
    mono_gchandle_free(handle);
}

MonoObject *tracked_object(const TrackedIcon &entry) {
  if (s_weak)
    return mono_gchandle_get_target ? mono_gchandle_get_target(entry.handle)
                                    : nullptr;
  return entry.pinned;
}

TrackedIcon *find_tracked(MonoObject *object) {
  for (TrackedIcon &entry : s_tracked) {
    if (tracked_object(entry) == object)
      return &entry;
  }
  return nullptr;
}

void prune_tracked() {
  std::vector<size_t> drop;
  for (size_t i = 0; i < s_tracked.size(); ++i) {
    MonoObject *object = tracked_object(s_tracked[i]);
    if (!object || object_disposed(object))
      drop.push_back(i);
  }
  for (size_t i = drop.size(); i > 0; --i)
    free_handle(s_tracked[drop[i - 1]].handle);
  for (size_t i = drop.size(); i > 0; --i)
    s_tracked.erase(s_tracked.begin() +
                    static_cast<std::ptrdiff_t>(drop[i - 1]));
}

void track_object(MonoObject *object, MonoMethod *method,
                  const Uicolor &restore, const char *classname,
                  const std::string &test_id, const std::string &name,
                  bool image) {
  if (!object || !method)
    return;
  if (TrackedIcon *existing = find_tracked(object)) {
    (void)existing;
    return;
  }
  if (s_tracked.size() >= kMaxTracked) {
    if (!s_logged_capacity) {
      s_logged_capacity = true;
      diag("[ICONCOLOR] tracked capacity reached: %zu", kMaxTracked);
    }
    return;
  }
  TrackedIcon entry{};
  entry.method = method;
  entry.restore = restore;
  entry.class_name = classname ? classname : "?";
  entry.test_id = test_id;
  entry.name = name;
  entry.image = image;
  if (s_weak) {
    entry.handle = mono_gchandle_new_weakref(object, 0);
    if (!entry.handle)
      return;
  } else if (mono_gchandle_new) {
    entry.pinned = object;
    entry.handle = mono_gchandle_new(object, 1);
    if (!entry.handle)
      return;
  } else {
    return;
  }
  s_tracked.push_back(entry);
}

bool apply_entry(TrackedIcon &entry, const Uicolor &color) {
  MonoObject *object = tracked_object(entry);
  if (!object || !entry.method || !mono_runtime_invoke)
    return false;
  Uicolor applied = color;
  applied.a = entry.restore.a;
  applied.color_space = entry.restore.color_space;
  void *args[1] = {&applied};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(entry.method, object, args, &exception);
  return exception == nullptr;
}

void erase_dropped(const std::vector<size_t> &drop) {
  for (size_t i = drop.size(); i > 0; --i)
    free_handle(s_tracked[drop[i - 1]].handle);
  for (size_t i = drop.size(); i > 0; --i)
    s_tracked.erase(s_tracked.begin() +
                    static_cast<std::ptrdiff_t>(drop[i - 1]));
}

size_t apply_all(const Uicolor &color) {
  prune_tracked();
  size_t applied = 0;
  size_t skipped = 0;
  std::vector<size_t> drop;
  for (size_t i = 0; i < s_tracked.size(); ++i) {
    TrackedIcon &entry = s_tracked[i];
    MonoObject *object = tracked_object(entry);
    if (!object || object_disposed(object) || !object_visible(object)) {
      ++skipped;
      continue;
    }
    if (apply_entry(entry, color))
      ++applied;
    else {
      ++skipped;
      drop.push_back(i);
    }
  }
  if (!drop.empty())
    erase_dropped(drop);
  diag("[ICONCOLOR] live apply count=%zu skipped=%zu", applied, skipped);
  LOG_INFO("[ICONCOLOR] live apply count=%zu skipped=%zu", applied, skipped);
  return applied;
}

size_t apply_white_hold() {
  prune_tracked();
  const Uicolor white = white_color();
  size_t applied = 0;
  std::vector<size_t> drop;
  for (size_t i = 0; i < s_tracked.size(); ++i) {
    TrackedIcon &entry = s_tracked[i];
    MonoObject *object = tracked_object(entry);
    if (!object || object_disposed(object) || !object_visible(object))
      continue;
    if (apply_entry(entry, white))
      ++applied;
    else
      drop.push_back(i);
  }
  if (!drop.empty())
    erase_dropped(drop);
  return applied;
}

void apply_button_setter(size_t index, MonoObject *object, Uicolor color) {
  if (index >= s_hooked_count)
    return;
  IconSetter original = s_originals[index];
  if (!original || !object || object_disposed(object))
    return;
  TrackedIcon *existing = find_tracked(object);
  if (!existing) {
    const char *classname = class_name(mono_object_get_class(object));
    track_object(object, s_methods[index], color, classname,
                 object_text(object, "get_TestId"),
                 object_text(object, "get_Name"), false);
  }
  Uicolor applied = color;
  if (feature_on()) {
    const Uicolor custom = current_color();
    applied.r = custom.r;
    applied.g = custom.g;
    applied.b = custom.b;
    applied.a = color.a;
    applied.color_space = color.color_space;
    if (!s_logged_apply) {
      s_logged_apply = true;
      diag("[ICONCOLOR] apply mode=%s rgba=%.3f,%.3f,%.3f,%.3f",
           g_settings.system_icon_color_animation == 1 ? "rainbow" : "static",
           applied.r, applied.g, applied.b, applied.a);
    }
  }
  original(object, applied);
}

template <int N>
void button_setter_hook(MonoObject *object, Uicolor color) {
  apply_button_setter(static_cast<size_t>(N), object, color);
}

using SetterHook = void (*)(MonoObject *, Uicolor);
constexpr SetterHook kButtonHooks[kMaxHookedSetters] = {
    &button_setter_hook<0>, &button_setter_hook<1>, &button_setter_hook<2>,
    &button_setter_hook<3>, &button_setter_hook<4>, &button_setter_hook<5>,
    &button_setter_hook<6>, &button_setter_hook<7>};

void log_image_once(const char *classname, const std::string &test_id,
                    const std::string &name) {
  if (s_logged_images.size() >= kMaxLoggedImages)
    return;
  const std::string key = std::string(classname ? classname : "?") + "|" +
                          test_id + "|" + name;
  if (std::find(s_logged_images.begin(), s_logged_images.end(), key) !=
      s_logged_images.end())
    return;
  s_logged_images.push_back(key);
  diag("[ICONCOLOR] image class=%s testid=%s name=%s",
       classname ? classname : "?", test_id.c_str(), name.c_str());
}

void apply_image_setter(size_t index, MonoObject *object, Uicolor color) {
  if (index >= s_image_hooked_count)
    return;
  IconSetter original = s_image_originals[index];
  if (!original || !object || object_disposed(object))
    return;
  const char *classname = class_name(mono_object_get_class(object));
  const std::string test_id = object_text(object, "get_TestId");
  const std::string name = object_text(object, "get_Name");
  log_image_once(classname, test_id, name);
  const WidgetIdentity identity =
      classify_widget(object, classname ? classname : "", test_id, name);
  const bool target = widget_should_tint(identity);
  if (target) {
    TrackedIcon *existing = find_tracked(object);
    if (!existing) {
      track_object(object, s_image_methods[index], color, classname,
                   test_id, name, true);
    }
  }
  Uicolor applied = color;
  if (target && feature_on()) {
    const Uicolor custom = current_color();
    applied.r = custom.r;
    applied.g = custom.g;
    applied.b = custom.b;
    applied.a = color.a;
    applied.color_space = color.color_space;
  }
  original(object, applied);
}

template <int N>
void image_setter_hook(MonoObject *object, Uicolor color) {
  apply_image_setter(static_cast<size_t>(N), object, color);
}

constexpr SetterHook kImageHooks[kMaxHookedSetters] = {
    &image_setter_hook<0>, &image_setter_hook<1>, &image_setter_hook<2>,
    &image_setter_hook<3>, &image_setter_hook<4>, &image_setter_hook<5>,
    &image_setter_hook<6>, &image_setter_hook<7>};

void log_image_set_once(const char *type, const char *property,
                        const std::string &test_id, const std::string &name,
                        bool icon, bool settings, bool applied) {
  if (s_logged_image_sources.size() >= kMaxLoggedImageSources)
    return;
  const std::string key = std::string(type ? type : "?") + "|" +
                          (property ? property : "-") + "|" + test_id + "|" +
                          name;
  if (std::find(s_logged_image_sources.begin(), s_logged_image_sources.end(),
                key) != s_logged_image_sources.end())
    return;
  s_logged_image_sources.push_back(key);
  diag("[ICONCOLOR] image-set type=%s property=%s testid=%s name=%s "
       "icon=%d settings=%d applied=%d",
       type ? type : "?", property ? property : "-", test_id.c_str(),
       name.c_str(), icon ? 1 : 0, settings ? 1 : 0, applied ? 1 : 0);
}

bool read_filter_color(MonoObject *object, Uicolor *color);

void log_restore_unknown_once(const std::string &type) {
  if (s_logged_restore_unknown.size() >= 64)
    return;
  if (std::find(s_logged_restore_unknown.begin(),
                s_logged_restore_unknown.end(),
                type) != s_logged_restore_unknown.end())
    return;
  s_logged_restore_unknown.push_back(type);
  diag("[ICONCOLOR] restore=unknown type=%s", type.c_str());
}

bool apply_image_custom(MonoObject *object, MonoMethod *method,
                        const char *type, const std::string &test_id,
                        const std::string &name) {
  Uicolor original{1.0f, 1.0f, 1.0f, 1.0f, 0};
  const bool known = read_filter_color(object, &original);
  Uicolor color = current_color();
  color.a = 1.0f;
  color.color_space = 0;
  void *args[1] = {&color};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, object, args, &exception);
  if (exception)
    return false;
  if (known) {
    track_object(object, method, original, type, test_id, name, true);
  } else {
    log_restore_unknown_once(type ? type : "?");
  }
  return true;
}

void apply_image_source(size_t index, MonoObject *object, MonoObject *value) {
  if (index >= s_image_source_hooked_count)
    return;
  SourceSetter original = s_image_source_originals[index];
  if (!original || !object || object_disposed(object))
    return;
  if (s_image_source_active) {
    original(object, value);
    return;
  }
  s_image_source_active = true;
  original(object, value);
  if (!feature_on() || object_disposed(object)) {
    s_image_source_active = false;
    return;
  }
  const char *type = class_name(mono_object_get_class(object));
  const std::string test_id = object_text(object, "get_TestId");
  const std::string name = object_text(object, "get_Name");
  const WidgetIdentity identity =
      classify_widget(object, type ? type : "", test_id, name);
  bool applied = false;
  if (widget_should_tint(identity) && s_image_filter_setter)
    applied = apply_image_custom(object, s_image_filter_setter, type, test_id,
                                 name);
  log_image_set_once(type, "Image", test_id, name, identity.icon,
                     identity.settings, applied);
  s_image_source_active = false;
}

template <int N>
void image_source_hook(MonoObject *object, MonoObject *value) {
  apply_image_source(static_cast<size_t>(N), object, value);
}

constexpr SourceSetter kImageSourceHooks[kMaxImageSourceSetters] = {
    &image_source_hook<0>, &image_source_hook<1>, &image_source_hook<2>,
    &image_source_hook<3>, &image_source_hook<4>, &image_source_hook<5>};

struct SceneScanState {
  size_t nodes = 0;
  size_t images = 0;
  size_t applied = 0;
  std::vector<MonoObject *> visited;
};

bool is_image_box_descendant(MonoClass *klass) {
  if (!klass || !s_image_box_class || !mono_class_get_parent)
    return false;
  int guard = 0;
  for (MonoClass *current = klass; current && guard < 64;
       current = mono_class_get_parent(current), ++guard) {
    if (current == s_image_box_class)
      return true;
  }
  return false;
}

void log_menu_icon_once(const char *type, const char *property,
                        const std::string &test_id, const std::string &name,
                        size_t found, size_t applied) {
  if (s_logged_menu_icons.size() >= kMaxLoggedMenuIcons)
    return;
  const std::string key = std::string(type ? type : "?") + "|" +
                          (property ? property : "-") + "|" + test_id + "|" +
                          name;
  if (std::find(s_logged_menu_icons.begin(), s_logged_menu_icons.end(), key) !=
      s_logged_menu_icons.end())
    return;
  s_logged_menu_icons.push_back(key);
  diag("[ICONCOLOR] menu-icon type=%s property=%s testid=%s name=%s "
       "found=%zu applied=%zu",
       type ? type : "?", property ? property : "-", test_id.c_str(),
       name.c_str(), found, applied);
}

struct MenuIconScanState {
  size_t nodes = 0;
  size_t found = 0;
  size_t applied = 0;
};

void scan_menu_icon_node(MonoObject *node, void *context, int depth) {
  MenuIconScanState *state = static_cast<MenuIconScanState *>(context);
  if (!state || !node || depth > 6 || state->nodes >= 64)
    return;
  if (object_disposed(node))
    return;
  ++state->nodes;
  if (is_image_box_descendant(mono_object_get_class(node))) {
    ++state->found;
    if (s_image_filter_setter) {
      const char *type = class_name(mono_object_get_class(node));
      const std::string test_id = object_text(node, "get_TestId");
      const std::string name = object_text(node, "get_Name");
      if (apply_image_custom(node, s_image_filter_setter, type, test_id, name))
        ++state->applied;
    }
  }
  MonoObject *child = object_property_object(node, "get_FirstChild");
  MonoObject *current = child;
  while (current && state->nodes < 64) {
    if (object_disposed(current))
      break;
    MonoObject *next = object_property_object(current, "get_NextSibling");
    scan_menu_icon_node(current, context, depth + 1);
    current = next;
  }
}

bool apply_menu_button_color(MonoObject *object, const char *type,
                             const std::string &test_id,
                             const std::string &name);
bool apply_menu_icon_widget(MonoObject *object, const char *type,
                            const std::string &test_id,
                            const std::string &name);
bool apply_menu_element(MonoObject *button, const char *button_type,
                        const std::string &test_id,
                        const std::string &name);

void apply_menu_icon(size_t index, MonoObject *object, MonoObject *value) {
  if (index >= s_menu_icon_hooked_count)
    return;
  SourceSetter original = s_menu_icon_originals[index];
  if (!original || !object || object_disposed(object))
    return;
  if (s_menu_icon_active) {
    original(object, value);
    return;
  }
  s_menu_icon_active = true;
  original(object, value);
  if (!feature_on() || object_disposed(object)) {
    s_menu_icon_active = false;
    return;
  }
  const char *type = class_name(mono_object_get_class(object));
  const std::string test_id = object_text(object, "get_TestId");
  const std::string name = object_text(object, "get_Name");
  (void)apply_menu_button_color(object, type, test_id, name);
  if (!apply_menu_element(object, type, test_id, name))
    (void)apply_menu_icon_widget(object, type, test_id, name);
  MenuIconScanState state;
  scan_menu_icon_node(object, &state, 0);
  log_menu_icon_once(type, s_menu_icon_properties[index], test_id, name,
                     state.found, state.applied);
  s_menu_icon_active = false;
}

template <int N>
void menu_icon_hook(MonoObject *object, MonoObject *value) {
  apply_menu_icon(static_cast<size_t>(N), object, value);
}

constexpr SourceSetter kMenuIconHooks[kMaxMenuIconSetters] = {
    &menu_icon_hook<0>, &menu_icon_hook<1>, &menu_icon_hook<2>,
    &menu_icon_hook<3>, &menu_icon_hook<4>, &menu_icon_hook<5>};

void log_attach_once(const char *type, const std::string &test_id,
                     const std::string &name, bool icon, bool settings,
                     bool image, bool applied) {
  if (s_logged_attach.size() >= kMaxLoggedAttach)
    return;
  const std::string key =
      std::string(type ? type : "?") + "|" + test_id + "|" + name;
  if (std::find(s_logged_attach.begin(), s_logged_attach.end(), key) !=
      s_logged_attach.end())
    return;
  s_logged_attach.push_back(key);
  diag("[ICONCOLOR] attach type=%s testid=%s name=%s icon=%d settings=%d "
       "image=%d applied=%d",
       type ? type : "?", test_id.c_str(), name.c_str(), icon ? 1 : 0,
       settings ? 1 : 0, image ? 1 : 0, applied ? 1 : 0);
}

void apply_attach(MonoObject *object, MonoObject *value) {
  if (!s_attach_original || !object)
    return;
  if (object_disposed(object))
    return;
  if (s_attach_active) {
    s_attach_original(object, value);
    return;
  }
  s_attach_active = true;
  s_attach_original(object, value);
  if (!feature_on() || object_disposed(object)) {
    s_attach_active = false;
    return;
  }
  MonoClass *klass = mono_object_get_class(object);
  const bool image = is_image_box_descendant(klass);
  const bool cheap = image || hierarchy_name_contains(klass, "icon") ||
                     hierarchy_name_contains(klass, "image") ||
                     hierarchy_name_contains(klass, "picture") ||
                     hierarchy_name_contains(klass, "glyph");
  if (!cheap) {
    s_attach_active = false;
    return;
  }
  const char *type = class_name(klass);
  const std::string test_id = object_text(object, "get_TestId");
  const std::string name = object_text(object, "get_Name");
  const WidgetIdentity identity =
      classify_widget(object, type ? type : "", test_id, name);
  bool applied = false;
  if (image && widget_should_tint(identity) && s_image_filter_setter)
    applied = apply_image_custom(object, s_image_filter_setter, type, test_id,
                                 name);
  log_attach_once(type, test_id, name, identity.icon, identity.settings, image,
                  applied);
  s_attach_active = false;
}

bool scene_seen(SceneScanState &state, MonoObject *node) {
  for (MonoObject *visited : state.visited) {
    if (visited == node)
      return true;
  }
  if (state.visited.size() >= kMaxSceneNodes)
    return true;
  state.visited.push_back(node);
  return false;
}

void log_scene_node(const char *type, const std::string &test_id,
                    const std::string &name) {
  if (s_logged_scene_nodes.size() >= kMaxSceneIdentities)
    return;
  const std::string key =
      std::string(type ? type : "?") + "|" + test_id + "|" + name;
  if (std::find(s_logged_scene_nodes.begin(), s_logged_scene_nodes.end(),
                key) != s_logged_scene_nodes.end())
    return;
  s_logged_scene_nodes.push_back(key);
  diag("[ICONCOLOR] scene node type=%s testid=%s name=%s",
       type ? type : "?", test_id.c_str(), name.c_str());
}

bool read_filter_color(MonoObject *object, Uicolor *color) {
  if (!object || !color || !s_image_filter_getter || !mono_runtime_invoke ||
      !mono_object_unbox)
    return false;
  MonoObject *exception = nullptr;
  MonoObject *value =
      mono_runtime_invoke(s_image_filter_getter, object, nullptr, &exception);
  if (exception || !value)
    return false;
  const void *unboxed = mono_object_unbox(value);
  if (!unboxed)
    return false;
  *color = *static_cast<const Uicolor *>(unboxed);
  return true;
}

bool read_color_value(MonoMethod *getter, MonoObject *object, Uicolor *color) {
  if (!getter || !object || !color || !mono_runtime_invoke ||
      !mono_object_unbox)
    return false;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, object, nullptr, &exception);
  if (exception || !value)
    return false;
  const void *unboxed = mono_object_unbox(value);
  if (!unboxed)
    return false;
  *color = *static_cast<const Uicolor *>(unboxed);
  return true;
}

bool menu_button_color_property(MonoClass *klass, MonoMethod **setter,
                                MonoMethod **getter,
                                const char **property_name) {
  static const char *const kNames[] = {"IconFilterColor", "FilterColor"};
  if (!mono_property_get_set_method)
    return false;
  for (const char *name : kNames) {
    MonoProperty *property = find_property_in_hierarchy(klass, name);
    if (!property)
      continue;
    MonoMethod *method = mono_property_get_set_method(property);
    if (!method || !uicolor_setter(method))
      continue;
    MonoClass *declaring = method->klass;
    const char *declaring_name = declaring ? class_name(declaring) : nullptr;
    if (declaring_name && std::strchr(declaring_name, '`'))
      continue;
    if (setter)
      *setter = method;
    if (getter)
      *getter = mono_property_get_get_method
                    ? mono_property_get_get_method(property)
                    : nullptr;
    if (property_name)
      *property_name = name;
    return true;
  }
  return false;
}

void log_menu_button_once(const char *type, const char *property,
                          const std::string &test_id,
                          const std::string &name, bool applied) {
  if (s_logged_menu_button.size() >= 96)
    return;
  const std::string key = std::string(type ? type : "?") + "|" +
                          (property ? property : "-") + "|" + test_id + "|" +
                          name;
  if (std::find(s_logged_menu_button.begin(), s_logged_menu_button.end(),
                key) != s_logged_menu_button.end())
    return;
  s_logged_menu_button.push_back(key);
  diag("[ICONCOLOR] menu-button type=%s property=%s testid=%s name=%s "
       "applied=%d",
       type ? type : "?", property ? property : "-", test_id.c_str(),
       name.c_str(), applied ? 1 : 0);
}

void log_menu_button_missing_once(const char *type) {
  const std::string text = type ? type : "?";
  if (s_logged_menu_button_missing.size() >= 64 ||
      std::find(s_logged_menu_button_missing.begin(),
                s_logged_menu_button_missing.end(),
                text) != s_logged_menu_button_missing.end())
    return;
  s_logged_menu_button_missing.push_back(text);
  diag("[ICONCOLOR] menu-button no color property type=%s", text.c_str());
}

bool apply_menu_button_color(MonoObject *object, const char *type,
                             const std::string &test_id,
                             const std::string &name) {
  if (!object || object_disposed(object))
    return false;
  MonoClass *klass = mono_object_get_class ? mono_object_get_class(object)
                                           : nullptr;
  MonoMethod *setter = nullptr;
  MonoMethod *getter = nullptr;
  const char *property = nullptr;
  if (!menu_button_color_property(klass, &setter, &getter, &property)) {
    log_menu_button_missing_once(type);
    return false;
  }
  Uicolor original{1.0f, 1.0f, 1.0f, 1.0f, 0};
  if (!read_color_value(getter, object, &original)) {
    log_menu_button_once(type, property, test_id, name, false);
    return false;
  }
  Uicolor color = current_color();
  color.a = 1.0f;
  color.color_space = 0;
  void *args[1] = {&color};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(setter, object, args, &exception);
  const bool applied = exception == nullptr;
  if (applied)
    track_object(object, setter, original, type, test_id, name, true);
  log_menu_button_once(type, property, test_id, name, applied);
  return applied;
}

void log_menu_prop_once(const char *button, const char *property,
                        const std::string &test_id, const std::string &name,
                        const char *type) {
  if (s_logged_menu_props.size() >= 96)
    return;
  const std::string key = std::string(button ? button : "?") + "|" +
                          (property ? property : "-") + "|" + test_id + "|" +
                          name;
  if (std::find(s_logged_menu_props.begin(), s_logged_menu_props.end(), key) !=
      s_logged_menu_props.end())
    return;
  s_logged_menu_props.push_back(key);
  diag("[ICONCOLOR] menu-prop button=%s property=%s testid=%s name=%s "
       "type=%s",
       button ? button : "?", property ? property : "-", test_id.c_str(),
       name.c_str(), type ? type : "?");
}

void log_menu_prop_none_once(const char *type, const std::string &test_id) {
  const std::string text = type ? type : "?";
  const std::string key = text + "|" + test_id;
  if (s_logged_menu_prop_none.size() >= 64 ||
      std::find(s_logged_menu_prop_none.begin(),
                s_logged_menu_prop_none.end(),
                key) != s_logged_menu_prop_none.end())
    return;
  s_logged_menu_prop_none.push_back(key);
  diag("[ICONCOLOR] menu-prop none type=%s testid=%s", text.c_str(),
       test_id.c_str());
}

MonoObject *menu_button_icon_widget(MonoObject *object,
                                    const char **property_name) {
  static const char *const kGetters[] = {
      "get_IconImageBox",       "get_IconWidget",
      "get_IconImageBoxPostEffect", "get_IconImage",
      "get_Icon",               "get_ImageBox"};
  for (const char *getter : kGetters) {
    if (MonoObject *value = object_property_object(object, getter)) {
      if (property_name)
        *property_name = getter + 4;
      return value;
    }
  }
  return nullptr;
}

bool icon_widget_color_property(MonoClass *klass, MonoMethod **setter,
                                MonoMethod **getter,
                                const char **property_name) {
  static const char *const kNames[] = {"FilterColor", "IconFilterColor",
                                       "TintColor"};
  if (!mono_property_get_set_method)
    return false;
  for (const char *name : kNames) {
    MonoProperty *property = find_property_in_hierarchy(klass, name);
    if (!property)
      continue;
    MonoMethod *method = mono_property_get_set_method(property);
    if (!method || !uicolor_setter(method))
      continue;
    MonoClass *declaring = method->klass;
    const char *declaring_name = declaring ? class_name(declaring) : nullptr;
    if (declaring_name && std::strchr(declaring_name, '`'))
      continue;
    if (setter)
      *setter = method;
    if (getter)
      *getter = mono_property_get_get_method
                    ? mono_property_get_get_method(property)
                    : nullptr;
    if (property_name)
      *property_name = name;
    return true;
  }
  return false;
}

bool apply_menu_icon_widget(MonoObject *object, const char *type,
                            const std::string &test_id,
                            const std::string &name) {
  if (!object || object_disposed(object))
    return false;
  const char *property = nullptr;
  MonoObject *icon = menu_button_icon_widget(object, &property);
  if (!icon) {
    log_menu_prop_none_once(type, test_id);
    return false;
  }
  MonoClass *icon_class = mono_object_get_class ? mono_object_get_class(icon)
                                                : nullptr;
  const char *icon_type = class_name(icon_class);
  log_menu_prop_once(type, property, test_id, name, icon_type);
  if (is_image_box_descendant(icon_class) && s_image_filter_setter)
    return apply_image_custom(icon, s_image_filter_setter, icon_type, test_id,
                              name);
  MonoMethod *setter = nullptr;
  MonoMethod *getter = nullptr;
  const char *color_property = nullptr;
  if (!icon_widget_color_property(icon_class, &setter, &getter,
                                  &color_property))
    return false;
  Uicolor original{1.0f, 1.0f, 1.0f, 1.0f, 0};
  if (!read_color_value(getter, icon, &original))
    return false;
  Uicolor color = current_color();
  color.a = 1.0f;
  color.color_space = 0;
  void *args[1] = {&color};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(setter, icon, args, &exception);
  if (exception)
    return false;
  track_object(icon, setter, original, icon_type, test_id, name, true);
  return true;
}

std::string full_class_name(MonoClass *klass) {
  const char *name = class_name(klass);
  const char *space =
      klass && mono_class_get_namespace ? mono_class_get_namespace(klass)
                                        : nullptr;
  if (space && space[0])
    return std::string(space) + "." + (name ? name : "?");
  return name ? name : "?";
}

MonoObject *menu_button_icon_element(MonoObject *object,
                                     const char **field_name,
                                     MonoObject **lazy_out) {
  static const char *const kFields[] = {"iconElement", "checkIconElement"};
  if (lazy_out)
    *lazy_out = nullptr;
  if (!object || !mono_object_get_class || !mono_class_get_field_from_name ||
      !mono_field_get_type || !mono_field_get_value)
    return nullptr;
  MonoClass *klass = mono_object_get_class(object);
  for (const char *wanted : kFields) {
    int guard = 0;
    for (MonoClass *current = klass; current && guard < 64;
         current = mono_class_get_parent ? mono_class_get_parent(current)
                                         : nullptr, ++guard) {
      MonoClassField *field =
          mono_class_get_field_from_name(current, wanted);
      if (!field)
        continue;
      if (mono_field_get_flags) {
        uint32_t flags = mono_field_get_flags(field);
        if ((flags & 0x0010u) != 0)
          continue;
      }
      MonoType *field_type = mono_field_get_type(field);
      if (!field_type)
        continue;
      const std::string type_text = type_name(field_type);
      const bool reference =
          field_type->type == 0x12 ||
          (field_type->type == 0x15 &&
           type_text.rfind("System.Lazy", 0) == 0);
      if (!reference)
        continue;
      MonoObject *value = nullptr;
      mono_field_get_value(object, field, &value);
      if (!value) {
        if (field_name)
          *field_name = wanted;
        return nullptr;
      }
      MonoClass *value_class = mono_object_get_class
                                   ? mono_object_get_class(value)
                                   : nullptr;
      const std::string value_type = full_class_name(value_class);
      if (value_type.rfind("System.Lazy", 0) == 0) {
        if (lazy_out)
          *lazy_out = value;
        MonoMethod *get_value =
            find_method_in_hierarchy(value_class, "get_Value");
        if (!get_value) {
          if (field_name)
            *field_name = wanted;
          return nullptr;
        }
        MonoObject *exception = nullptr;
        MonoObject *result =
            mono_runtime_invoke(get_value, value, nullptr, &exception);
        if (field_name)
          *field_name = wanted;
        return exception ? nullptr : result;
      }
      if (field_name)
        *field_name = wanted;
      return value;
    }
  }
  return nullptr;
}

void log_menu_element_once(const char *button, const char *field,
                           const char *type, const std::string &test_id,
                           const std::string &name, bool applied) {
  if (s_logged_menu_elements.size() >= 96)
    return;
  const std::string key = std::string(button ? button : "?") + "|" +
                          (field ? field : "-") + "|" + test_id + "|" + name;
  if (std::find(s_logged_menu_elements.begin(), s_logged_menu_elements.end(),
                key) != s_logged_menu_elements.end())
    return;
  s_logged_menu_elements.push_back(key);
  diag("[ICONCOLOR] menu-element button=%s field=%s type=%s testid=%s "
       "name=%s applied=%d",
       button ? button : "?", field ? field : "-", type ? type : "?",
       test_id.c_str(), name.c_str(), applied ? 1 : 0);
}

void log_menu_element_no_color_once(const char *type) {
  const std::string text = type ? type : "?";
  if (s_logged_menu_element_no_color.size() >= 64 ||
      std::find(s_logged_menu_element_no_color.begin(),
                s_logged_menu_element_no_color.end(),
                text) != s_logged_menu_element_no_color.end())
    return;
  s_logged_menu_element_no_color.push_back(text);
  diag("[ICONCOLOR] menu-element no color type=%s", text.c_str());
}

bool class_is_value_type(MonoClass *klass) {
  if (!klass || !mono_class_get_parent)
    return false;
  int guard = 0;
  for (MonoClass *parent = mono_class_get_parent(klass);
       parent && guard < 64; parent = mono_class_get_parent(parent),
               ++guard) {
    const char *name = class_name(parent);
    if (name && std::strcmp(name, "ValueType") == 0)
      return true;
    if (full_class_name(parent) == "System.ValueType")
      return true;
  }
  return false;
}

void log_menu_element_value_once(const char *type, bool value) {
  const std::string text = type ? type : "?";
  if (s_logged_menu_element_value.size() >= 64 ||
      std::find(s_logged_menu_element_value.begin(),
                s_logged_menu_element_value.end(),
                text) != s_logged_menu_element_value.end())
    return;
  s_logged_menu_element_value.push_back(text);
  diag("[ICONCOLOR] menu-element type=%s value=%d", text.c_str(),
       value ? 1 : 0);
}

void log_menu_lazy_fields_once(MonoClass *lazy_class) {
  if (!lazy_class)
    return;
  const std::string key = full_class_name(lazy_class);
  if (s_logged_menu_lazy.size() >= 64 ||
      std::find(s_logged_menu_lazy.begin(), s_logged_menu_lazy.end(), key) !=
          s_logged_menu_lazy.end())
    return;
  s_logged_menu_lazy.push_back(key);
  void *iter = nullptr;
  int logged = 0;
  while (mono_class_get_fields) {
    MonoClassField *field = mono_class_get_fields(lazy_class, &iter);
    if (!field)
      break;
    const char *name =
        mono_field_get_name ? mono_field_get_name(field) : nullptr;
    MonoType *field_type =
        mono_field_get_type ? mono_field_get_type(field) : nullptr;
    diag("[ICONCOLOR] menu-lazy field=%s type=%s", name ? name : "?",
         type_name(field_type).c_str());
    if (++logged >= 20)
      break;
  }
}

bool apply_menu_element(MonoObject *button, const char *button_type,
                        const std::string &test_id,
                        const std::string &name) {
  if (!button || object_disposed(button))
    return false;
  const char *field_name = nullptr;
  MonoObject *lazy = nullptr;
  MonoObject *element =
      menu_button_icon_element(button, &field_name, &lazy);
  if (!field_name || !element)
    return false;
  if (object_disposed(element))
    return false;
  MonoClass *element_class = mono_object_get_class
                                 ? mono_object_get_class(element)
                                 : nullptr;
  const char *element_type = class_name(element_class);
  const bool value_type = class_is_value_type(element_class);
  log_menu_element_value_once(element_type, value_type);
  MonoMethod *setter = nullptr;
  MonoMethod *getter = nullptr;
  const char *property = nullptr;
  if (!icon_widget_color_property(element_class, &setter, &getter,
                                  &property)) {
    log_menu_element_no_color_once(element_type);
    log_menu_element_once(button_type, field_name, element_type, test_id, name,
                          false);
    return false;
  }
  Uicolor original{1.0f, 1.0f, 1.0f, 1.0f, 0};
  if (!read_color_value(getter, element, &original)) {
    log_menu_element_once(button_type, field_name, element_type, test_id, name,
                          false);
    return false;
  }
  Uicolor color = current_color();
  color.a = 1.0f;
  color.color_space = 0;
  void *args[1] = {&color};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(setter, element, args, &exception);
  const bool applied = exception == nullptr;
  if (applied) {
    track_object(element, setter, original, element_type, test_id, name, true);
    if (value_type && lazy)
      log_menu_lazy_fields_once(mono_object_get_class(lazy));
  }
  log_menu_element_once(button_type, field_name, element_type, test_id, name,
                        applied);
  return applied;
}

void scan_scene_node(MonoObject *node, void *context, int depth) {
  SceneScanState *state = static_cast<SceneScanState *>(context);
  if (!state || !node || depth > kMaxSceneDepth ||
      state->nodes >= kMaxSceneNodes)
    return;
  if (object_disposed(node))
    return;
  if (scene_seen(*state, node))
    return;
  ++state->nodes;
  MonoClass *klass = mono_object_get_class ? mono_object_get_class(node)
                                           : nullptr;
  const char *type = class_name(klass);
  if (is_image_box_descendant(klass)) {
    ++state->images;
    const std::string test_id = object_text(node, "get_TestId");
    const std::string name = object_text(node, "get_Name");
    log_scene_node(type, test_id, name);
    if (s_image_filter_setter) {
      const WidgetIdentity identity =
          classify_widget(node, type ? type : "", test_id, name);
      if (widget_should_tint(identity) &&
          apply_image_custom(node, s_image_filter_setter, type, test_id, name))
        ++state->applied;
    }
  }
  MonoObject *child = object_property_object(node, "get_FirstChild");
  MonoObject *current = child;
  while (current && state->nodes < kMaxSceneNodes) {
    if (object_disposed(current))
      break;
    MonoObject *next = object_property_object(current, "get_NextSibling");
    scan_scene_node(current, context, depth + 1);
    current = next;
  }
}

void scan_scene_root(MonoObject *root, void *context) {
  SceneScanState *state = static_cast<SceneScanState *>(context);
  if (!state)
    return;
  MonoObject *current = root;
  while (current && state->nodes < kMaxSceneNodes) {
    if (object_disposed(current))
      break;
    MonoObject *next = object_property_object(current, "get_NextSibling");
    scan_scene_node(current, context, 0);
    current = next;
  }
}

void run_scene_scan() {
  if (s_scene_scan_active || !feature_on() || !s_image_box_class ||
      !s_image_filter_setter)
    return;
  s_scene_scan_active = true;
  SceneScanState state;
  AniketosUI::HomeScene::VisitSceneRoots(&scan_scene_root, &state);
  s_scene_scan_active = false;
  diag("[ICONCOLOR] scene scan nodes=%zu images=%zu applied=%zu",
       state.nodes, state.images, state.applied);
  LOG_INFO("[ICONCOLOR] scene scan nodes=%zu images=%zu applied=%zu",
           state.nodes, state.images, state.applied);
}

bool button_address_hooked(uint64_t address) {
  for (size_t i = 0; i < s_hooked_count; ++i) {
    if (s_addresses[i] == address)
      return true;
  }
  return false;
}

bool image_address_hooked(uint64_t address) {
  for (size_t i = 0; i < s_image_hooked_count; ++i) {
    if (s_image_addresses[i] == address)
      return true;
  }
  return false;
}

bool image_source_address_hooked(uint64_t address) {
  for (size_t i = 0; i < s_image_source_hooked_count; ++i) {
    if (s_image_source_addresses[i] == address)
      return true;
  }
  return false;
}

MonoClass *find_pui_class(MonoImage *pui, int rows, const char *name) {
  MonoClass *exact =
      mono_class_from_name
          ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", name)
          : nullptr;
  if (exact)
    return exact;
  for (int row = 1; row <= rows; ++row) {
    MonoClass *klass =
        mono_class_get(pui, kTypeDefTokenBase | static_cast<uint32_t>(row));
    const char *candidate = class_name(klass);
    if (candidate && std::strcmp(candidate, name) == 0)
      return klass;
  }
  return nullptr;
}

bool image_source_setter(MonoMethod *method, MonoClass *klass) {
  if (!method || !klass)
    return false;
  const char *declaring = class_name(klass);
  if (!declaring || std::strchr(declaring, '`'))
    return false;
  if (mono_method_get_flags) {
    uint32_t iflags = 0;
    if ((mono_method_get_flags(method, &iflags) & 0x0010u) != 0)
      return false;
  }
  MonoMethodSignature *signature = signature_of(method);
  if (!signature || !mono_signature_get_param_count ||
      !mono_signature_get_params || !mono_signature_get_return_type)
    return false;
  if (!is_void(type_name(mono_signature_get_return_type(signature))))
    return false;
  if (mono_signature_get_param_count(signature) != 1)
    return false;
  void *iter = nullptr;
  MonoType *param = mono_signature_get_params(signature, &iter);
  if (!param)
    return false;
  return param->type == 0x12 || param->type == 0x0e;
}

bool attach_setter(MonoMethod *method, MonoClass *klass) {
  if (!method || !klass)
    return false;
  const char *declaring = class_name(klass);
  if (!declaring || std::strchr(declaring, '`'))
    return false;
  if (mono_method_get_flags) {
    uint32_t iflags = 0;
    if ((mono_method_get_flags(method, &iflags) & 0x0010u) != 0)
      return false;
  }
  MonoMethodSignature *signature = signature_of(method);
  if (!signature || !mono_signature_get_param_count ||
      !mono_signature_get_params || !mono_signature_get_return_type)
    return false;
  if (!is_void(type_name(mono_signature_get_return_type(signature))))
    return false;
  if (mono_signature_get_param_count(signature) != 1)
    return false;
  void *iter = nullptr;
  MonoType *param = mono_signature_get_params(signature, &iter);
  if (!param)
    return false;
  return param->type == 0x12 || param->type == 0x0e || param->type == 0x1c;
}

bool menu_icon_address_hooked(uint64_t address) {
  for (size_t i = 0; i < s_menu_icon_hooked_count; ++i) {
    if (s_menu_icon_addresses[i] == address)
      return true;
  }
  return false;
}

bool any_address_hooked(uint64_t address) {
  return button_address_hooked(address) || image_address_hooked(address) ||
         image_source_address_hooked(address) ||
         menu_icon_address_hooked(address) || s_attach_address == address;
}

void log_filter_color_mode(MonoClass *klass) {
  MonoProperty *property = nullptr;
  int guard = 0;
  for (MonoClass *current = klass; current && !property && guard < 64;
       current = mono_class_get_parent ? mono_class_get_parent(current)
                                       : nullptr, ++guard)
    property = mono_class_get_property_from_name(current, "FilterColorMode");
  if (!property || !mono_property_get_get_method ||
      !mono_signature_get_return_type || !mono_type_get_name)
    return;
  MonoMethod *getter = mono_property_get_get_method(property);
  MonoMethodSignature *signature = signature_of(getter);
  if (!signature)
    return;
  MonoType *type = mono_signature_get_return_type(signature);
  if (!type)
    return;
  diag("[ICONCOLOR] mode type=%s", type_name(type).c_str());
  MonoClass *enum_class = type->data.klass;
  if (!enum_class || !mono_class_get_fields || !mono_field_get_name ||
      !mono_field_get_value)
    return;
  void *iter = nullptr;
  while (MonoClassField *field = mono_class_get_fields(enum_class, &iter)) {
    const char *name = mono_field_get_name(field);
    int64_t value = 0;
    mono_field_get_value(nullptr, field, &value);
    diag("[ICONCOLOR] mode field=%s value=%lld", name ? name : "?",
         static_cast<long long>(value));
  }
}

} // namespace

bool Active() { return feature_on(); }

bool TokenIsIcon(const std::string &key) {
  const std::string lower = ascii_lower(key);
  if (!contains(lower, "icon"))
    return false;
  return !contains(lower, "background") && !contains(lower, "surface") &&
         !contains(lower, "basemat");
}

void LogTokenOnce(const std::string &key, float r, float g, float b, float a,
                  bool icon) {
  if (key.empty() || s_logged_tokens.size() >= kMaxLoggedTokens)
    return;
  const std::string lower = ascii_lower(key);
  if (std::find(s_logged_tokens.begin(), s_logged_tokens.end(), lower) !=
      s_logged_tokens.end())
    return;
  s_logged_tokens.push_back(lower);
  diag("[ICONCOLOR] token=%s selected=%d rgba=%.3f,%.3f,%.3f,%.3f",
       key.c_str(), icon ? 1 : 0, r, g, b, a);
}

bool TintToken(const std::string &key, float *r, float *g, float *b) {
  if (!r || !g || !b || !Active() || !TokenIsIcon(key))
    return false;
  const Uicolor color = current_color();
  *r = color.r;
  *g = color.g;
  *b = color.b;
  return true;
}

void Tick() {
  if (!feature_on()) {
    if (s_tracked.empty())
      return;
    const uint64_t now = AniketosUI::MonotonicMs();
    if (s_last_white_ms && now - s_last_white_ms < 250)
      return;
    s_last_white_ms = now;
    const size_t count = apply_white_hold();
    if (now - s_last_white_log_ms >= 1000) {
      s_last_white_log_ms = now;
      diag("[ICONCOLOR] white hold count=%zu", count);
      LOG_INFO("[ICONCOLOR] white hold count=%zu", count);
    }
    return;
  }
  if (g_settings.system_icon_color_animation != 1 || s_tracked.empty())
    return;
  prune_tracked();
  bool any_visible = false;
  for (const TrackedIcon &entry : s_tracked) {
    MonoObject *object = tracked_object(entry);
    if (object && !object_disposed(object) && object_visible(object)) {
      any_visible = true;
      break;
    }
  }
  if (!any_visible)
    return;
  const Uicolor color = current_color();
  if (s_has_last_rainbow &&
      std::fabs(color.r - s_last_rainbow.r) < 0.001f &&
      std::fabs(color.g - s_last_rainbow.g) < 0.001f &&
      std::fabs(color.b - s_last_rainbow.b) < 0.001f)
    return;
  s_last_rainbow = color;
  s_has_last_rainbow = true;
  std::vector<size_t> drop;
  for (size_t i = 0; i < s_tracked.size(); ++i) {
    TrackedIcon &entry = s_tracked[i];
    MonoObject *object = tracked_object(entry);
    if (!object || object_disposed(object) || !object_visible(object))
      continue;
    if (!apply_entry(entry, color))
      drop.push_back(i);
  }
  if (!drop.empty())
    erase_dropped(drop);
}

void RequestApply() {
  s_rainbow_started_ms = 0;
  s_has_last_rainbow = false;
  s_logged_apply = false;
  if (!feature_on()) {
    const size_t count = apply_all(white_color());
    diag("[ICONCOLOR] live white count=%zu", count);
    LOG_INFO("[ICONCOLOR] live white count=%zu", count);
    return;
  }
  apply_all(current_color());
  run_scene_scan();
}

void Install(MonoImage *pui) {
  diag("[ICONCOLOR] install begin image=%p", static_cast<void *>(pui));
  if (!pui)
    return;
  if (!mono_image_get_table_info || !mono_table_info_get_rows ||
      !mono_class_get || !mono_class_get_property_from_name ||
      !mono_property_get_set_method || !mono_compile_method ||
      !mono_method_signature) {
    diag("[ICONCOLOR] not found: mono table APIs unavailable");
    LOG_INFO("[ICONCOLOR] not found: mono table APIs unavailable");
    return;
  }
  s_weak = mono_gchandle_new_weakref && mono_gchandle_get_target &&
           mono_gchandle_free;
  MonoClass *widget =
      mono_class_from_name
          ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Widget")
          : nullptr;
  s_disposed_getter =
      find_property_getter_in_hierarchy(widget, "IsDisposed");
  if (!s_disposed_getter)
    s_disposed_getter = find_method_in_hierarchy(widget, "get_IsDisposed");
  log_disposed_probe(s_disposed_getter != nullptr);
  MonoClass *button_base =
      mono_class_from_name
          ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "ButtonBase")
          : nullptr;
  MonoClass *image_box = mono_class_from_name
                             ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3",
                                                    "ImageBox")
                             : nullptr;
  const MonoTableInfo *table = mono_image_get_table_info(pui, kTypeDefTable);
  const int rows = table ? mono_table_info_get_rows(table) : 0;
  struct Candidate {
    int row;
    MonoClass *klass;
    MonoMethod *setter;
    uint64_t address;
    bool has_filter;
    MonoMethod *compute;
    MonoMethod *update;
  };
  std::vector<Candidate> candidates;
  struct ImageCandidate {
    MonoClass *klass;
    MonoMethod *setter;
    uint64_t address;
    MonoMethod *getter;
  };
  std::vector<ImageCandidate> image_candidates;
  for (int row = 1; row <= rows; ++row) {
    MonoClass *klass =
        mono_class_get(pui, kTypeDefTokenBase | static_cast<uint32_t>(row));
    if (!klass)
      continue;
    MonoProperty *property =
        mono_class_get_property_from_name(klass, "IconFilterColor");
    if (property) {
      MonoMethod *setter = mono_property_get_set_method(property);
      if (setter && uicolor_setter(setter)) {
        const uint64_t address = mono_compile_method(setter);
        if (address) {
          Candidate candidate{};
          candidate.row = row;
          candidate.klass = klass;
          candidate.setter = setter;
          candidate.address = address;
          candidate.has_filter =
              mono_class_get_property_from_name(klass,
                                                "EnableIconFilterColor") !=
              nullptr;
          candidate.compute = find_method(klass, "ComputeIconColor");
          candidate.update = find_method(klass, "UpdateIconColor");
          const char *name = class_name(klass);
          diag("[ICONCOLOR] candidate row=%d type=%s signature=%s "
               "setter=0x%llx filter=%d compute=%d update=%d",
               row, name ? name : "?", signature_text(setter).c_str(),
               static_cast<unsigned long long>(address),
               candidate.has_filter ? 1 : 0, candidate.compute ? 1 : 0,
               candidate.update ? 1 : 0);
          candidates.push_back(candidate);
        }
      }
    }
    if (!image_box)
      continue;
    bool is_image = false;
    int guard = 0;
    for (MonoClass *current = klass; current && guard < 64;
         current = mono_class_get_parent ? mono_class_get_parent(current)
                                         : nullptr, ++guard) {
      if (current == image_box) {
        is_image = true;
        break;
      }
    }
    if (!is_image)
      continue;
    MonoProperty *filter =
        mono_class_get_property_from_name(klass, "FilterColor");
    if (!filter)
      continue;
    MonoMethod *setter = mono_property_get_set_method(filter);
    if (!setter || !uicolor_setter(setter))
      continue;
    const uint64_t address = mono_compile_method(setter);
    if (!address)
      continue;
    const char *name = class_name(klass);
    diag("[ICONCOLOR] image candidate type=%s signature=%s setter=0x%llx",
         name ? name : "?", signature_text(setter).c_str(),
         static_cast<unsigned long long>(address));
    MonoMethod *getter = mono_property_get_get_method
                             ? mono_property_get_get_method(filter)
                             : nullptr;
    image_candidates.push_back({klass, setter, address, getter});
  }
  if (image_box) {
    MonoProperty *scene_filter =
        find_property_in_hierarchy(image_box, "FilterColor");
    if (scene_filter) {
      MonoMethod *scene_setter =
          mono_property_get_set_method(scene_filter);
      if (scene_setter && uicolor_setter(scene_setter)) {
        s_image_box_class = image_box;
        s_image_filter_setter = scene_setter;
        s_image_filter_getter =
            mono_property_get_get_method
                ? mono_property_get_get_method(scene_filter)
                : nullptr;
      }
    }
  }
  if (!s_image_filter_setter && !image_candidates.empty()) {
    s_image_box_class = s_image_box_class ? s_image_box_class : image_box;
    s_image_filter_setter = image_candidates[0].setter;
    s_image_filter_getter = image_candidates[0].getter;
  }
  if (s_image_filter_setter) {
    const char *name = class_name(s_image_box_class);
    diag("[ICONCOLOR] scene image type=%s setter=0x%llx getter=%d",
         name ? name : "?",
         static_cast<unsigned long long>(mono_compile_method(
             s_image_filter_setter)),
         s_image_filter_getter ? 1 : 0);
  }
  if (candidates.empty()) {
    diag("[ICONCOLOR] not found: no IconFilterColor setter");
    LOG_INFO("[ICONCOLOR] not found: no IconFilterColor setter");
  }
  for (const Candidate &candidate : candidates) {
    if (button_address_hooked(candidate.address) ||
        image_address_hooked(candidate.address))
      continue;
    if (s_hooked_count >= kMaxHookedSetters) {
      diag("[ICONCOLOR] hook capacity reached: %zu", kMaxHookedSetters);
      LOG_INFO("[ICONCOLOR] hook capacity reached: %zu", kMaxHookedSetters);
      break;
    }
    const size_t index = s_hooked_count;
    if (!InstallDetour(candidate.address,
                       reinterpret_cast<void *>(kButtonHooks[index]),
                       reinterpret_cast<void **>(&s_originals[index]))) {
      const char *name = class_name(candidate.klass);
      diag("[ICONCOLOR] hook failed type=%s setter=0x%llx",
           name ? name : "?",
           static_cast<unsigned long long>(candidate.address));
      continue;
    }
    s_addresses[index] = candidate.address;
    s_methods[index] = candidate.setter;
    s_hooked_count = index + 1;
    const char *name = class_name(candidate.klass);
    diag("[ICONCOLOR] hooked type=%s setter=0x%llx", name ? name : "?",
         static_cast<unsigned long long>(candidate.address));
    LOG_INFO("[ICONCOLOR] hooked type=%s setter=0x%llx", name ? name : "?",
             static_cast<unsigned long long>(candidate.address));
    if (!s_mode_class)
      s_mode_class = candidate.klass;
  }
  for (const ImageCandidate &candidate : image_candidates) {
    if (button_address_hooked(candidate.address) ||
        image_address_hooked(candidate.address))
      continue;
    if (s_image_hooked_count >= kMaxHookedSetters) {
      diag("[ICONCOLOR] image hook capacity reached: %zu",
           kMaxHookedSetters);
      break;
    }
    const size_t index = s_image_hooked_count;
    if (!InstallDetour(candidate.address,
                       reinterpret_cast<void *>(kImageHooks[index]),
                       reinterpret_cast<void **>(&s_image_originals[index]))) {
      const char *name = class_name(candidate.klass);
      diag("[ICONCOLOR] image hook failed type=%s setter=0x%llx",
           name ? name : "?",
           static_cast<unsigned long long>(candidate.address));
      continue;
    }
    s_image_addresses[index] = candidate.address;
    s_image_methods[index] = candidate.setter;
    s_image_hooked_count = index + 1;
    const char *name = class_name(candidate.klass);
    diag("[ICONCOLOR] hooked image type=%s setter=0x%llx",
         name ? name : "?",
         static_cast<unsigned long long>(candidate.address));
    LOG_INFO("[ICONCOLOR] hooked image type=%s setter=0x%llx",
             name ? name : "?",
             static_cast<unsigned long long>(candidate.address));
  }
  if (!s_image_hooked_count) {
    diag("[ICONCOLOR] image hook not installed");
    LOG_INFO("[ICONCOLOR] image hook not installed");
  }
  struct ImageSourceCandidate {
    const char *type;
    MonoClass *klass;
    MonoMethod *setter;
    uint64_t address;
  };
  std::vector<ImageSourceCandidate> image_source_candidates;
  static const char *const kImageSourceClasses[] = {
      "ImageBox",  "ImageQuad",     "ImageBasemat",
      "AnimationImageBox", "IconImageBox"};
  for (const char *class_name_wanted : kImageSourceClasses) {
    MonoClass *klass = find_pui_class(pui, rows, class_name_wanted);
    if (!klass)
      continue;
    MonoProperty *property =
        mono_class_get_property_from_name(klass, "Image");
    if (!property)
      continue;
    MonoMethod *setter = mono_property_get_set_method(property);
    if (!image_source_setter(setter, setter && setter->klass ? setter->klass
                                                             : klass))
      continue;
    const uint64_t address = mono_compile_method(setter);
    if (!address)
      continue;
    diag("[ICONCOLOR] image-source candidate type=%s property=Image "
         "setter=0x%llx",
         class_name_wanted, static_cast<unsigned long long>(address));
    image_source_candidates.push_back(
        {class_name_wanted, klass, setter, address});
  }
  for (const ImageSourceCandidate &candidate : image_source_candidates) {
    if (button_address_hooked(candidate.address) ||
        image_address_hooked(candidate.address) ||
        image_source_address_hooked(candidate.address))
      continue;
    if (s_image_source_hooked_count >= kMaxImageSourceSetters) {
      diag("[ICONCOLOR] image-source capacity reached: %zu",
           kMaxImageSourceSetters);
      break;
    }
    const size_t index = s_image_source_hooked_count;
    if (!InstallDetour(
            candidate.address,
            reinterpret_cast<void *>(kImageSourceHooks[index]),
            reinterpret_cast<void **>(&s_image_source_originals[index]))) {
      diag("[ICONCOLOR] image-source hook failed type=%s setter=0x%llx",
           candidate.type,
           static_cast<unsigned long long>(candidate.address));
      continue;
    }
    s_image_source_addresses[index] = candidate.address;
    s_image_source_hooked_count = index + 1;
    diag("[ICONCOLOR] hooked image-source type=%s setter=0x%llx",
         candidate.type,
         static_cast<unsigned long long>(candidate.address));
    LOG_INFO("[ICONCOLOR] hooked image-source type=%s setter=0x%llx",
             candidate.type,
             static_cast<unsigned long long>(candidate.address));
  }
  if (!s_image_source_hooked_count) {
    diag("[ICONCOLOR] image-source hook not installed");
    LOG_INFO("[ICONCOLOR] image-source hook not installed");
  }
  struct MenuIconCandidate {
    const char *type;
    const char *property;
    MonoClass *klass;
    MonoMethod *setter;
    uint64_t address;
  };
  std::vector<MenuIconCandidate> menu_icon_candidates;
  static const char *const kMenuIconClasses[] = {
      "MenuListButton",    "MenuListSubmenu", "MenuListSelectable",
      "MenuListDropdown", "MenuListCustom",  "MenuListItem"};
  static const char *const kMenuIconProperties[] = {"IconUri", "IconImage"};
  for (const char *class_name_wanted : kMenuIconClasses) {
    MonoClass *klass = find_pui_class(pui, rows, class_name_wanted);
    if (!klass)
      continue;
    log_menulist_class(klass);
    for (const char *property_name : kMenuIconProperties) {
      MonoProperty *property =
          mono_class_get_property_from_name(klass, property_name);
      if (!property)
        continue;
      MonoMethod *setter = mono_property_get_set_method(property);
      if (!image_source_setter(setter, setter && setter->klass ? setter->klass
                                                               : klass))
        continue;
      const uint64_t address = mono_compile_method(setter);
      if (!address)
        continue;
      diag("[ICONCOLOR] menu-icon candidate type=%s property=%s "
           "setter=0x%llx",
           class_name_wanted, property_name,
           static_cast<unsigned long long>(address));
      menu_icon_candidates.push_back(
          {class_name_wanted, property_name, klass, setter, address});
    }
  }
  for (const MenuIconCandidate &candidate : menu_icon_candidates) {
    if (any_address_hooked(candidate.address))
      continue;
    if (s_menu_icon_hooked_count >= kMaxMenuIconSetters) {
      diag("[ICONCOLOR] menu-icon capacity reached: %zu",
           kMaxMenuIconSetters);
      break;
    }
    const size_t index = s_menu_icon_hooked_count;
    if (!InstallDetour(
            candidate.address,
            reinterpret_cast<void *>(kMenuIconHooks[index]),
            reinterpret_cast<void **>(&s_menu_icon_originals[index]))) {
      diag("[ICONCOLOR] menu-icon hook failed type=%s property=%s "
           "setter=0x%llx",
           candidate.type, candidate.property,
           static_cast<unsigned long long>(candidate.address));
      continue;
    }
    s_menu_icon_addresses[index] = candidate.address;
    s_menu_icon_properties[index] = candidate.property;
    s_menu_icon_hooked_count = index + 1;
    diag("[ICONCOLOR] hooked menu-icon type=%s property=%s setter=0x%llx",
         candidate.type, candidate.property,
         static_cast<unsigned long long>(candidate.address));
    LOG_INFO("[ICONCOLOR] hooked menu-icon type=%s property=%s setter=0x%llx",
             candidate.type, candidate.property,
             static_cast<unsigned long long>(candidate.address));
  }
  if (!s_menu_icon_hooked_count) {
    diag("[ICONCOLOR] menu-icon hook not installed");
    LOG_INFO("[ICONCOLOR] menu-icon hook not installed");
  }
  const char *attach_property_name = "Parent";
  MonoProperty *attach_property =
      find_property_in_hierarchy(widget, attach_property_name);
  if (!attach_property) {
    attach_property_name = "ParentWidget";
    attach_property = find_property_in_hierarchy(widget, attach_property_name);
  }
  if (attach_property) {
    MonoMethod *attach_setter_method =
        mono_property_get_set_method(attach_property);
    MonoClass *attach_declaring =
        attach_setter_method && attach_setter_method->klass
            ? attach_setter_method->klass
            : widget;
    if (attach_setter_method &&
        attach_setter(attach_setter_method, attach_declaring)) {
      const char *name = class_name(attach_declaring);
      const uint64_t address = mono_compile_method(attach_setter_method);
      if (address && !any_address_hooked(address)) {
        if (InstallDetour(address, reinterpret_cast<void *>(&apply_attach),
                          reinterpret_cast<void **>(&s_attach_original))) {
          s_attach_address = address;
          s_attach_installed = true;
          diag("[ICONCOLOR] hooked attach type=%s property=%s setter=0x%llx",
               name ? name : "?", attach_property_name,
               static_cast<unsigned long long>(address));
          LOG_INFO(
              "[ICONCOLOR] hooked attach type=%s property=%s setter=0x%llx",
              name ? name : "?", attach_property_name,
              static_cast<unsigned long long>(address));
        }
      }
    }
  }
  if (!s_attach_installed) {
    diag("[ICONCOLOR] attach hook not installed");
    LOG_INFO("[ICONCOLOR] attach hook not installed");
  }
  if (!s_hooked_count) {
    diag("[ICONCOLOR] not found: no button setter hooked");
    LOG_INFO("[ICONCOLOR] not found: no button setter hooked");
  }
  MonoClass *chosen_button = s_mode_class ? s_mode_class : button_base;
  if (chosen_button) {
    const char *name = class_name(chosen_button);
    diag("[ICONCOLOR] button class=%s enableFilter=%d filterMode=%d",
         name ? name : "?",
         find_property_in_hierarchy(chosen_button, "EnableIconFilterColor")
             ? 1
             : 0,
         find_property_in_hierarchy(chosen_button, "FilterColorMode") ? 1 : 0);
  }
  if (button_base)
    log_class_methods(button_base, 40);
  MonoClass *first_image =
      !image_candidates.empty() ? image_candidates[0].klass : image_box;
  if (first_image)
    log_class_methods(first_image, 40);
  if (s_mode_class)
    log_filter_color_mode(s_mode_class);
}

} // namespace AniketosTheme::IconColor
