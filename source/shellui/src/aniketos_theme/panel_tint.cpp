/* Copyright (C) 2026 AniketosHEN — shared native PS5 UI background theming. */

#include "aniketos_ui/monotonic_clock.hpp"
#include "aniketos_ui/pui_primitives.hpp"
#include "aniketos_ui/ui_color.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_theme/panel_tint.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "aniketos_theme/animation.hpp"
#include "aniketos_theme/color.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/theme_config.hpp"

#include <onion/detour.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "aniketos_theme/toggle_styles.hpp"

namespace AniketosTheme::PanelTint {
namespace {

using AniketosUI::MonotonicMs;
using AniketosUI::UIColor;

constexpr uint64_t kFrameIntervalMs = 50;
constexpr size_t kMaxTrackedPanels = 2048;
constexpr size_t kMaxLoggedTokenKeys = 256;

template <typename Value> struct Tracked {
  // Only used by the compatibility fallback when weak gchandles are absent.
  MonoObject *fallback_object = nullptr;
  uint32_t handle = 0;
  Value original{};
};

using ColorSetter = void (*)(MonoObject *, UIColor);
using OpacitySetter = void (*)(MonoObject *, float);
using ObjectGetter = MonoObject *(*)(MonoObject *);
using StringGetter = MonoString *(*)(MonoObject *);
using TryTokenColor = bool (*)(MonoObject *, MonoString *, UIColor *, int32_t);

ColorSetter s_set_background_original = nullptr;
OpacitySetter s_set_basemat_opacity_original = nullptr;
ColorSetter s_set_fill_color_original = nullptr;
ObjectGetter s_get_widget_parent = nullptr;
StringGetter s_get_test_id = nullptr;
TryTokenColor s_try_token_color_original = nullptr;
std::vector<Tracked<UIColor>> s_panels;
// PanelBasemat keeps its glass/gradation opacity outside BackgroundColor.
std::vector<Tracked<float>> s_basemat_opacities;
std::vector<std::string> s_logged_token_keys;
uint64_t s_animation_started_ms = 0;
uint64_t s_last_frame_ms = 0;
uint64_t s_last_config = 0;
bool s_installed = false;
bool s_last_enabled = false;
bool s_logged_first_apply = false;
bool s_logged_capacity = false;
bool s_weak_handles = false;

bool panels_on() {
  return CustomizationOn() && g_settings.shell_ui_panels_enabled;
}

std::string ascii_lower(std::string value) {
  for (char &c : value) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return value;
}

bool contains_any(const std::string &value,
                  std::initializer_list<const char *> needles) {
  return std::any_of(needles.begin(), needles.end(),
                     [&value](const char *needle) {
                       return value.find(needle) != std::string::npos;
                     });
}

bool surface_token(const std::string &key) {
  const std::string lower = ascii_lower(key);
  if (!contains_any(lower, {"background", "basemat", "surface", "panel",
                            "dialog", "modal", "popup", "menu", "card",
                            "container"}))
    return false;
  return !contains_any(lower, {"text", "icon", "focus", "border",
                               "separator", "shadow", "button",
                               "disabled", "progress", "slider"});
}

void log_token_once(const std::string &key, const UIColor &color,
                    bool selected) {
  if (key.empty() || s_logged_token_keys.size() >= kMaxLoggedTokenKeys ||
      std::find(s_logged_token_keys.begin(), s_logged_token_keys.end(), key) !=
          s_logged_token_keys.end())
    return;
  s_logged_token_keys.push_back(key);
  LOG_INFO("[SHELL-UI:TOKEN] key='%s' rgba=(%.3f,%.3f,%.3f,%.3f) "
           "surface=%d",
           key.c_str(), color.r, color.g, color.b, color.a, selected ? 1 : 0);
}

const char *object_class_name(MonoObject *object) {
  if (!object || !mono_object_get_class || !mono_class_get_name)
    return nullptr;
  MonoClass *klass = mono_object_get_class(object);
  return klass ? mono_class_get_name(klass) : nullptr;
}

bool excluded_panel(MonoObject *object) {
  if (!object || !mono_object_get_class || !mono_class_get_name)
    return true;
  /* Our own UI3 panels (the OSD and legend in Home's scene) are painted
     with the colours we give them, not with the system panel theme. */
  if (AniketosUI::Pui::IsOwn(object))
    return true;
  const char *name = object_class_name(object);
  if (!name)
    return false;

  // Full-screen basemats and BGLayer are owned by the separate native ShellUI
  // background implementation. PanelBasemat and ReactPanel render through a
  // private RectangleBasematElement that BackgroundColor does not reach.
  if (std::strstr(name, "BGLayer") ||
      std::strcmp(name, "FullScreenBasemat") == 0 ||
      std::strcmp(name, "ImageBasemat") == 0 ||
      std::strcmp(name, "PanelBasemat") == 0 ||
      std::strcmp(name, "ReactPanel") == 0 ||
      std::strstr(name, "BaseMatLayer"))
    return true;
  if (!std::strstr(name, "FocusLayer") || !s_get_test_id)
    return false;
  MonoString *managed = s_get_test_id(object);
  const std::string test_id = managed ? Mono_to_String(managed) : std::string();
  const bool notification = test_id.find("toast") != std::string::npos ||
                            test_id.find("notification") != std::string::npos ||
                            test_id.find("banner") != std::string::npos;
  const bool tile = test_id == "tile-item-focus-layer";
  const bool viewport = test_id.find("overlay-screen") != std::string::npos;
  return notification || tile || viewport;
}

template <typename Value> MonoObject *target_of(const Tracked<Value> &entry) {
  if (s_weak_handles && entry.handle && mono_gchandle_get_target)
    return mono_gchandle_get_target(entry.handle);
  return entry.fallback_object;
}

template <typename Value> void remove_dead(std::vector<Tracked<Value>> &list) {
  if (!s_weak_handles || !mono_gchandle_get_target)
    return;
  list.erase(std::remove_if(list.begin(), list.end(),
                            [](const Tracked<Value> &entry) {
                              if (target_of(entry))
                                return false;
                              if (entry.handle && mono_gchandle_free)
                                mono_gchandle_free(entry.handle);
                              return true;
                            }),
             list.end());
}

// An object already listed only takes the new original. Null when the list is
// full or no handle could be made.
template <typename Value>
Tracked<Value> *track(std::vector<Tracked<Value>> &list, MonoObject *object,
                      Value original) {
  const auto found = std::find_if(list.begin(), list.end(),
                                  [object](const Tracked<Value> &entry) {
                                    return target_of(entry) == object;
                                  });
  if (found != list.end()) {
    found->original = original;
    return &*found;
  }
  remove_dead(list);
  if (list.size() >= kMaxTrackedPanels)
    return nullptr;
  const uint32_t handle =
      s_weak_handles
          ? mono_gchandle_new_weakref(object, /*track_resurrection=*/0)
          : (mono_gchandle_new ? mono_gchandle_new(object, /*pinned=*/1) : 0);
  if ((s_weak_handles || mono_gchandle_new) && !handle)
    return nullptr;
  list.push_back({s_weak_handles ? nullptr : object, handle, original});
  return &list.back();
}

Tracked<UIColor> *track_panel(MonoObject *object, UIColor original) {
  Tracked<UIColor> *tracked = track(s_panels, object, original);
  if (!tracked && s_panels.size() >= kMaxTrackedPanels && !s_logged_capacity) {
    s_logged_capacity = true;
    LOG_WARN("[SHELL-UI:THEME] panel limit reached=%zu", s_panels.size());
  }
  return tracked;
}

RGB animated_theme_color(uint64_t now) {
  if (GlobalTheme::Active())
    return GlobalTheme::Color(now);
  if (!s_animation_started_ms)
    s_animation_started_ms = now;
  ThemeConfig config{};
  config.base = {g_settings.shell_ui_panels_r / 255.0f,
                 g_settings.shell_ui_panels_g / 255.0f,
                 g_settings.shell_ui_panels_b / 255.0f};
  config.glow = config.base;
  config.animation = static_cast<AnimationMode>(std::clamp(
      g_settings.shell_ui_panels_animation, 0, AnimationModeCount() - 1));
  config.animation_speed = g_settings.shell_ui_panels_animation_speed / 100.0f;
  const float elapsed =
      static_cast<float>(now - s_animation_started_ms) / 1000.0f;
  return EvaluateAnimation(config, elapsed).base;
}

float opacity_scale() {
  return std::clamp(g_settings.shell_ui_panels_opacity / 100.0f, 0.0f,
                    2.0f);
}

UIColor tint(UIColor original, RGB theme) {
  const RGB source{std::clamp(original.r, 0.0f, 1.0f),
                   std::clamp(original.g, 0.0f, 1.0f),
                   std::clamp(original.b, 0.0f, 1.0f)};
  const float amount =
      std::clamp(g_settings.shell_ui_panels_strength / 100.0f, 0.0f, 1.0f);
  const RGB result = TintPreservingBrightness(source, theme, amount);
  return {result.r, result.g, result.b,
          std::clamp(original.a * opacity_scale(), 0.0f, 1.0f),
          original.color_space};
}

uint64_t config_signature() {
  uint64_t value = 0;
  const auto add = [&value](uint64_t radix, int setting, int low, int high) {
    value = value * radix +
            static_cast<uint64_t>(std::clamp(setting, low, high));
  };
  add(1, g_settings.shell_ui_panels_animation, 0, AnimationModeCount() - 1);
  add(401, g_settings.shell_ui_panels_animation_speed, 25, 400);
  add(256, g_settings.shell_ui_panels_r, 0, 255);
  add(256, g_settings.shell_ui_panels_g, 0, 255);
  add(256, g_settings.shell_ui_panels_b, 0, 255);
  add(101, g_settings.shell_ui_panels_strength, 0, 100);
  add(201, g_settings.shell_ui_panels_opacity, 0, 200);
  // The Global Theme replaces the panel colour, so its colour counts too.
  add(2, GlobalTheme::Active() ? 1 : 0, 0, 1);
  add(256, static_cast<int>(g_settings.vsh_left_r * 255.0f), 0, 255);
  add(256, static_cast<int>(g_settings.vsh_left_g * 255.0f), 0, 255);
  add(256, static_cast<int>(g_settings.vsh_left_b * 255.0f), 0, 255);
  return value;
}

void set_background_hook(MonoObject *object, UIColor color) {
  if (!s_set_background_original)
    return;
  /* a switch drawn in another style (toggle_styles.cpp): its colours are
     the style's, not to be tinted with the panel theme */
  if (ToggleStyles::OnPanelBackground(object, color) || !object ||
      color.a <= 0.01f || excluded_panel(object) ||
      !track_panel(object, color) || !panels_on()) {
    s_set_background_original(object, color);
    return;
  }
  s_set_background_original(object,
                            tint(color, animated_theme_color(MonotonicMs())));
  if (!s_logged_first_apply) {
    s_logged_first_apply = true;
    const char *name = object_class_name(object);
    LOG_INFO("[SHELL-UI:THEME] first native panel styled class=%s",
             name ? name : "unknown");
  }
}

// Nothing tints fills any more; LAYOUT section 6 keeps this hook installed.
void set_fill_color_hook(MonoObject *object, UIColor color) {
  if (!s_set_fill_color_original)
    return;
  s_set_fill_color_original(object, color);
}

void set_basemat_opacity_hook(MonoObject *object, float opacity) {
  if (!s_set_basemat_opacity_original)
    return;
  if (object)
    track(s_basemat_opacities, object, opacity);
  s_set_basemat_opacity_original(object, opacity);
}

bool try_token_color_hook(MonoObject *manager, MonoString *managed_key,
                          UIColor *color, int32_t theme_override) {
  if (!s_try_token_color_original)
    return false;
  const bool found = s_try_token_color_original(manager, managed_key, color,
                                                 theme_override);
  if (!found || !color)
    return found;
  const std::string key = managed_key ? Mono_to_String(managed_key)
                                      : std::string();
  const bool selected = surface_token(key);
  log_token_once(key, *color, selected);
  if (selected && panels_on())
    *color = tint(*color, animated_theme_color(MonotonicMs()));
  return found;
}

void reapply_basemat_opacities() {
  if (!s_set_basemat_opacity_original)
    return;
  for (const Tracked<float> &basemat : s_basemat_opacities) {
    if (MonoObject *object = target_of(basemat))
      s_set_basemat_opacity_original(object, basemat.original);
  }
}

void restore_all() {
  for (const Tracked<UIColor> &panel : s_panels) {
    if (MonoObject *object = target_of(panel))
      s_set_background_original(object, panel.original);
  }
  reapply_basemat_opacities();
}

} // namespace

void Install(MonoImage *pui) {
  if (s_installed || !pui)
    return;
  s_weak_handles = mono_gchandle_new_weakref && mono_gchandle_get_target &&
                   mono_gchandle_free;
  MonoClass *panel =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Panel");
  MonoClass *panel_basemat =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "PanelBasemat");
  MonoClass *rectangle =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "RectangleElement");
  MonoClass *style_manager =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "StyleManager");
  MonoClass *widget =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Widget");
  s_get_test_id = reinterpret_cast<StringGetter>(
      Get_Address_of_Method(pui, widget, "get_TestId", 0));
  s_get_widget_parent = reinterpret_cast<ObjectGetter>(
      Get_Address_of_Method(pui, widget, "get_Parent", 0));
  const uint64_t setter =
      Get_Address_of_Method(pui, panel, "set_BackgroundColor", 1);
  s_installed =
      setter &&
      InstallDetour(setter, reinterpret_cast<void *>(&set_background_hook),
                    reinterpret_cast<void **>(&s_set_background_original));
  const uint64_t basemat_opacity_setter = Get_Address_of_Method(
      pui, panel_basemat, "set_BasematOpacity", 1);
  const bool basemat_installed =
      basemat_opacity_setter &&
      InstallDetour(basemat_opacity_setter,
                    reinterpret_cast<void *>(&set_basemat_opacity_hook),
                    reinterpret_cast<void **>(&s_set_basemat_opacity_original));
  const uint64_t fill_color_setter =
      Get_Address_of_Method(pui, rectangle, "set_FillColor", 1);
  const bool fill_color_installed =
      fill_color_setter &&
      InstallDetour(fill_color_setter,
                    reinterpret_cast<void *>(&set_fill_color_hook),
                    reinterpret_cast<void **>(&s_set_fill_color_original));
  const uint64_t try_token_color = Get_Address_of_Method(
      pui, style_manager, "TryGetComponentTokenColor", 3);
  const bool token_installed =
      try_token_color &&
      InstallDetour(try_token_color,
                    reinterpret_cast<void *>(&try_token_color_hook),
                    reinterpret_cast<void **>(&s_try_token_color_original));
  LOG_INFO("[SHELL-UI:THEME] native hooks panel=%d basematOpacity=%d "
           "basematFill=%d styleToken=%d "
           "panelAddr=0x%llx basematAddr=0x%llx fillAddr=0x%llx "
           "tokenAddr=0x%llx handles=%s",
           s_installed ? 1 : 0, basemat_installed ? 1 : 0,
           fill_color_installed ? 1 : 0, token_installed ? 1 : 0,
           static_cast<unsigned long long>(setter),
           static_cast<unsigned long long>(basemat_opacity_setter),
           static_cast<unsigned long long>(fill_color_setter),
           static_cast<unsigned long long>(try_token_color),
           s_weak_handles ? "weak" : "pinned-fallback");
}

void ApplyFocusHierarchy(MonoObject *focus_layer,
                         const std::string &test_id) {
  if (!focus_layer || !panels_on() ||
      test_id.rfind("fc-list-item-", 0) != 0 || !s_get_widget_parent ||
      !s_set_background_original)
    return;

  MonoObject *node = s_get_widget_parent(focus_layer);
  for (unsigned depth = 1; node && depth < 18; ++depth) {
    const char *name = object_class_name(node);
    if (name && std::strcmp(name, "ReactFocusLayer") == 0) {
      const RGB theme = animated_theme_color(MonotonicMs());
      const float strength = std::clamp(
          g_settings.shell_ui_panels_strength / 100.0f, 0.0f, 1.0f);
      constexpr RGB kBlackFloor{0.006f, 0.004f, 0.006f};
      constexpr float kGlobalThemeGain = 0.10f;
      const RGB dark_theme{
          std::clamp(kBlackFloor.r + theme.r * kGlobalThemeGain, 0.0f, 1.0f),
          std::clamp(kBlackFloor.g + theme.g * kGlobalThemeGain, 0.0f, 1.0f),
          std::clamp(kBlackFloor.b + theme.b * kGlobalThemeGain, 0.0f, 1.0f)};
      // A dark translucent wash leaves Sony's native gray luminance gradient
      // visible underneath.  Only its hue follows the animated Global Theme,
      // so the original direction, rounding and falloff remain untouched.
      const float profile_wash_alpha = std::clamp(
          0.75f * opacity_scale() * strength, 0.0f, 0.92f);
      s_set_background_original(
          node, {dark_theme.r, dark_theme.g, dark_theme.b,
                 profile_wash_alpha, 0});

      static bool logged = false;
      if (!logged) {
        logged = true;
        LOG_INFO("[SHELL-UI:MODAL] black global-theme gradient styled=1 "
                 "anchor='%s' rgb=(%.3f,%.3f,%.3f) alpha=%.3f",
                 test_id.c_str(), dark_theme.r, dark_theme.g, dark_theme.b,
                 profile_wash_alpha);
      }
      return;
    }
    MonoObject *parent = s_get_widget_parent(node);
    if (!parent || parent == node)
      break;
    node = parent;
  }
}

void Tick() {
  if (!s_installed || !s_set_background_original)
    return;

  if (!panels_on()) {
    if (s_last_enabled)
      restore_all();
    s_last_enabled = false;
    return;
  }

  const uint64_t now = MonotonicMs();
  const uint64_t signature = config_signature();
  const bool config_changed = !s_last_enabled || signature != s_last_config;
  const bool animated = g_settings.shell_ui_panels_animation != 0 ||
                        GlobalTheme::Animated();
  if (animated && s_last_frame_ms && now - s_last_frame_ms < kFrameIntervalMs)
    return;
  if (!animated && !config_changed)
    return;

  const RGB theme = animated_theme_color(now);
  remove_dead(s_panels);
  remove_dead(s_basemat_opacities);
  for (const Tracked<UIColor> &panel : s_panels) {
    if (MonoObject *object = target_of(panel))
      s_set_background_original(object, tint(panel.original, theme));
  }
  reapply_basemat_opacities();
  if (config_changed) {
    LOG_INFO("[SHELL-UI:THEME] applied panels=%zu basemats=%zu "
             "rgb=(%d,%d,%d) strength=%d opacity=%d%%",
             s_panels.size(), s_basemat_opacities.size(),
             g_settings.shell_ui_panels_r,
             g_settings.shell_ui_panels_g, g_settings.shell_ui_panels_b,
             g_settings.shell_ui_panels_strength,
             g_settings.shell_ui_panels_opacity);
  }
  s_last_frame_ms = now;
  s_last_config = signature;
  s_last_enabled = true;
}

} // namespace AniketosTheme::PanelTint
