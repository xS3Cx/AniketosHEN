/* Copyright (C) 2026 AniketosHEN — shared native PS5 UI background theming. */

#include "aniketos_ui/pui_primitives.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_theme/shared_ui.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "aniketos_theme/animation.hpp"
#include "aniketos_theme/color.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/icon_color.hpp"
#include "aniketos_theme/theme_config.hpp"

#include <onion/detour.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <time.h>
#include <vector>

#include "aniketos_theme/toggle_styles.hpp"

extern onion::Settings g_settings;

namespace AniketosTheme::SharedUI {
namespace {

constexpr uint64_t kFrameIntervalMs = 50;
constexpr size_t kMaxTrackedPanels = 2048;

struct UIColor {
  float r, g, b, a;
  int32_t color_space;
};
static_assert(sizeof(UIColor) == 20);

struct TrackedPanel {
  // Only used by the compatibility fallback when weak gchandles are absent.
  MonoObject *fallback_object = nullptr;
  uint32_t handle = 0;
  UIColor original{};
};

struct TrackedBasematOpacity {
  // PanelBasemat keeps its glass/gradation opacity outside BackgroundColor.
  MonoObject *fallback_object = nullptr;
  uint32_t handle = 0;
  float original = 1.0f;
};

struct TrackedBasematElement {
  MonoObject *fallback_object = nullptr;
  uint32_t handle = 0;
  UIColor original{};
};

struct TrackedGradationElement {
  MonoObject *fallback_object = nullptr;
  uint32_t handle = 0;
  UIColor original{};
};

struct DeferredModalTarget {
  MonoObject *fallback_object = nullptr;
  uint32_t handle = 0;
  uint64_t queued_ms = 0;
  bool apply_requested = false;
};

struct ModalRenderCandidate {
  MonoObject *fallback_object = nullptr;
  uint32_t handle = 0;
};

using BackgroundSetter = void (*)(MonoObject *, UIColor);
using BasematOpacitySetter = void (*)(MonoObject *, float);
using ElementColorSetter = void (*)(MonoObject *, UIColor);
using EnumSetter = void (*)(MonoObject *, int32_t);
using FloatSetter = void (*)(MonoObject *, float);
using ElementUpdate = void (*)(MonoObject *);
using ObjectGetter = MonoObject *(*)(MonoObject *);
using StringGetter = MonoString *(*)(MonoObject *);
using BasematRender = void (*)(MonoObject *, MonoObject *, void *);
using TryTokenColor = bool (*)(MonoObject *, MonoString *, UIColor *, int32_t);

BackgroundSetter s_set_background_original = nullptr;
BasematOpacitySetter s_set_basemat_opacity_original = nullptr;
ElementColorSetter s_set_fill_color_original = nullptr;
ElementColorSetter s_set_gradation_color = nullptr;
EnumSetter s_set_basemat_position = nullptr;
EnumSetter s_set_basemat_type = nullptr;
FloatSetter s_set_element_basemat_opacity = nullptr;
ElementUpdate s_update_rectangle_basemat = nullptr;
BasematRender s_on_basemat_render_original = nullptr;
BasematRender s_on_rectangle_render_original = nullptr;
ObjectGetter s_get_element_parent_widget = nullptr;
ObjectGetter s_get_widget_parent = nullptr;
ObjectGetter s_get_background_element = nullptr;
StringGetter s_get_test_id = nullptr;
TryTokenColor s_try_token_color_original = nullptr;
MonoClassField *s_gradation_color_field = nullptr;
MonoClassField *s_fill_color_field = nullptr;
MonoClassField *s_panel_basemat_element_field = nullptr;
MonoClassField *s_round_opt_fill_field = nullptr;
MonoClassField *s_round_opt_basemat_field = nullptr;
MonoClassField *s_round_opt_radius_field = nullptr;
std::vector<TrackedPanel> s_panels;
std::vector<TrackedBasematOpacity> s_basemat_opacities;
std::vector<TrackedBasematElement> s_basemat_elements;
std::vector<TrackedGradationElement> s_gradation_elements;
DeferredModalTarget s_modal_target;
std::vector<ModalRenderCandidate> s_modal_render_candidates;
std::vector<std::string> s_logged_token_keys;
uint64_t s_animation_started_ms = 0;
uint64_t s_last_frame_ms = 0;
uint64_t s_last_config = 0;
bool s_installed = false;
bool s_last_enabled = false;
bool s_logged_first_apply = false;
bool s_logged_capacity = false;
bool s_weak_handles = false;

constexpr size_t kMaxLoggedTokenKeys = 256;

uint64_t monotonic_millis() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000ULL +
         static_cast<uint64_t>(now.tv_nsec) / 1000000ULL;
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
  // background implementation. PanelBasemat is intentionally NOT excluded:
  // it is the rounded gray/blue glass surface used by profile menus, popup
  // menus, dialogs, and many other shared system windows.
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

MonoObject *panel_target(const TrackedPanel &panel) {
  if (s_weak_handles && panel.handle && mono_gchandle_get_target)
    return mono_gchandle_get_target(panel.handle);
  return panel.fallback_object;
}

MonoObject *basemat_target(const TrackedBasematOpacity &basemat) {
  if (s_weak_handles && basemat.handle && mono_gchandle_get_target)
    return mono_gchandle_get_target(basemat.handle);
  return basemat.fallback_object;
}

MonoObject *basemat_element_target(const TrackedBasematElement &element) {
  if (s_weak_handles && element.handle && mono_gchandle_get_target)
    return mono_gchandle_get_target(element.handle);
  return element.fallback_object;
}

MonoObject *gradation_element_target(const TrackedGradationElement &element) {
  if (s_weak_handles && element.handle && mono_gchandle_get_target)
    return mono_gchandle_get_target(element.handle);
  return element.fallback_object;
}

MonoObject *modal_target() {
  if (s_weak_handles && s_modal_target.handle && mono_gchandle_get_target)
    return mono_gchandle_get_target(s_modal_target.handle);
  return s_modal_target.fallback_object;
}

void clear_modal_target() {
  if (s_modal_target.handle && mono_gchandle_free)
    mono_gchandle_free(s_modal_target.handle);
  s_modal_target = {};
}

MonoObject *modal_render_candidate_target(
    const ModalRenderCandidate &candidate) {
  if (s_weak_handles && candidate.handle && mono_gchandle_get_target)
    return mono_gchandle_get_target(candidate.handle);
  return candidate.fallback_object;
}

void remove_dead_modal_render_candidates() {
  if (!s_weak_handles || !mono_gchandle_get_target)
    return;
  s_modal_render_candidates.erase(
      std::remove_if(
          s_modal_render_candidates.begin(), s_modal_render_candidates.end(),
          [](const ModalRenderCandidate &candidate) {
            if (modal_render_candidate_target(candidate))
              return false;
            if (candidate.handle && mono_gchandle_free)
              mono_gchandle_free(candidate.handle);
            return true;
          }),
      s_modal_render_candidates.end());
}

bool queue_modal_render_candidate(MonoObject *panel) {
  if (!panel)
    return false;
  for (const ModalRenderCandidate &candidate : s_modal_render_candidates) {
    if (modal_render_candidate_target(candidate) == panel)
      return true;
  }
  remove_dead_modal_render_candidates();
  if (s_modal_render_candidates.size() >= 24)
    return false;
  const uint32_t handle =
      s_weak_handles
          ? mono_gchandle_new_weakref(panel, /*track_resurrection=*/0)
          : (mono_gchandle_new ? mono_gchandle_new(panel, /*pinned=*/1) : 0);
  if ((s_weak_handles || mono_gchandle_new) && !handle)
    return false;
  s_modal_render_candidates.push_back(
      {s_weak_handles ? nullptr : panel, handle});
  return true;
}

[[maybe_unused]] bool is_modal_gradation_element(MonoObject *element) {
  if (!element || !s_panel_basemat_element_field || !mono_field_get_value)
    return false;
  for (const ModalRenderCandidate &candidate : s_modal_render_candidates) {
    MonoObject *panel = modal_render_candidate_target(candidate);
    if (!panel)
      continue;
    MonoObject *panel_element = nullptr;
    mono_field_get_value(panel, s_panel_basemat_element_field, &panel_element);
    if (panel_element == element)
      return true;
  }
  return false;
}

[[maybe_unused]] bool queue_modal_target(MonoObject *object) {
  if (!object)
    return false;
  if (modal_target() == object)
    return true;

  clear_modal_target();
  const uint32_t handle =
      s_weak_handles
          ? mono_gchandle_new_weakref(object, /*track_resurrection=*/0)
          : (mono_gchandle_new ? mono_gchandle_new(object, /*pinned=*/1) : 0);
  if ((s_weak_handles || mono_gchandle_new) && !handle)
    return false;
  s_modal_target = {s_weak_handles ? nullptr : object, handle,
                    monotonic_millis(), true};
  return true;
}

void remove_dead_panels() {
  if (!s_weak_handles || !mono_gchandle_get_target)
    return;
  s_panels.erase(std::remove_if(s_panels.begin(), s_panels.end(),
                                [](const TrackedPanel &panel) {
                                  if (panel_target(panel))
                                    return false;
                                  if (panel.handle && mono_gchandle_free)
                                    mono_gchandle_free(panel.handle);
                                  return true;
                                }),
                 s_panels.end());
}

void remove_dead_basemats() {
  if (!s_weak_handles || !mono_gchandle_get_target)
    return;
  s_basemat_opacities.erase(
      std::remove_if(s_basemat_opacities.begin(), s_basemat_opacities.end(),
                     [](const TrackedBasematOpacity &basemat) {
                       if (basemat_target(basemat))
                         return false;
                       if (basemat.handle && mono_gchandle_free)
                         mono_gchandle_free(basemat.handle);
                       return true;
                     }),
      s_basemat_opacities.end());
}

void remove_dead_basemat_elements() {
  if (!s_weak_handles || !mono_gchandle_get_target)
    return;
  s_basemat_elements.erase(
      std::remove_if(s_basemat_elements.begin(), s_basemat_elements.end(),
                     [](const TrackedBasematElement &element) {
                       if (basemat_element_target(element))
                         return false;
                       if (element.handle && mono_gchandle_free)
                         mono_gchandle_free(element.handle);
                       return true;
                     }),
      s_basemat_elements.end());
}

void remove_dead_gradation_elements() {
  if (!s_weak_handles || !mono_gchandle_get_target)
    return;
  s_gradation_elements.erase(
      std::remove_if(s_gradation_elements.begin(), s_gradation_elements.end(),
                     [](const TrackedGradationElement &element) {
                       if (gradation_element_target(element))
                         return false;
                       if (element.handle && mono_gchandle_free)
                         mono_gchandle_free(element.handle);
                       return true;
                     }),
      s_gradation_elements.end());
}

TrackedPanel *find_panel(MonoObject *object) {
  const auto found = std::find_if(s_panels.begin(), s_panels.end(),
                                  [object](const TrackedPanel &panel) {
                                    return panel_target(panel) == object;
                                  });
  return found == s_panels.end() ? nullptr : &*found;
}

TrackedPanel *track_panel(MonoObject *object, UIColor original) {
  if (TrackedPanel *tracked = find_panel(object)) {
    tracked->original = original;
    return tracked;
  }
  remove_dead_panels();
  if (s_panels.size() >= kMaxTrackedPanels) {
    if (!s_logged_capacity) {
      s_logged_capacity = true;
      LOG_WARN("[SHELL-UI:THEME] panel limit reached=%zu", s_panels.size());
    }
    return nullptr;
  }
  const uint32_t handle =
      s_weak_handles
          ? mono_gchandle_new_weakref(object, /*track_resurrection=*/0)
          : (mono_gchandle_new ? mono_gchandle_new(object, /*pinned=*/1) : 0);
  if ((s_weak_handles || mono_gchandle_new) && !handle)
    return nullptr;
  s_panels.push_back({s_weak_handles ? nullptr : object, handle, original});
  return &s_panels.back();
}

[[maybe_unused]] TrackedGradationElement *
track_gradation_element(MonoObject *object, UIColor original) {
  const auto found = std::find_if(
      s_gradation_elements.begin(), s_gradation_elements.end(),
      [object](const TrackedGradationElement &element) {
        return gradation_element_target(element) == object;
      });
  if (found != s_gradation_elements.end())
    return &*found;
  remove_dead_gradation_elements();
  if (s_gradation_elements.size() >= kMaxTrackedPanels)
    return nullptr;
  const uint32_t handle =
      s_weak_handles
          ? mono_gchandle_new_weakref(object, /*track_resurrection=*/0)
          : (mono_gchandle_new ? mono_gchandle_new(object, /*pinned=*/1) : 0);
  if ((s_weak_handles || mono_gchandle_new) && !handle)
    return nullptr;
  s_gradation_elements.push_back(
      {s_weak_handles ? nullptr : object, handle, original});
  return &s_gradation_elements.back();
}

TrackedBasematOpacity *track_basemat_opacity(MonoObject *object,
                                             float original) {
  const auto found = std::find_if(
      s_basemat_opacities.begin(), s_basemat_opacities.end(),
      [object](const TrackedBasematOpacity &basemat) {
        return basemat_target(basemat) == object;
      });
  if (found != s_basemat_opacities.end()) {
    found->original = original;
    return &*found;
  }
  remove_dead_basemats();
  if (s_basemat_opacities.size() >= kMaxTrackedPanels)
    return nullptr;
  const uint32_t handle =
      s_weak_handles
          ? mono_gchandle_new_weakref(object, /*track_resurrection=*/0)
          : (mono_gchandle_new ? mono_gchandle_new(object, /*pinned=*/1) : 0);
  if ((s_weak_handles || mono_gchandle_new) && !handle)
    return nullptr;
  s_basemat_opacities.push_back(
      {s_weak_handles ? nullptr : object, handle, original});
  return &s_basemat_opacities.back();
}

[[maybe_unused]] TrackedBasematElement *
track_basemat_element(MonoObject *object, UIColor original) {
  const auto found = std::find_if(
      s_basemat_elements.begin(), s_basemat_elements.end(),
      [object](const TrackedBasematElement &element) {
        return basemat_element_target(element) == object;
      });
  if (found != s_basemat_elements.end()) {
    return &*found;
  }
  remove_dead_basemat_elements();
  if (s_basemat_elements.size() >= kMaxTrackedPanels)
    return nullptr;
  const uint32_t handle =
      s_weak_handles
          ? mono_gchandle_new_weakref(object, /*track_resurrection=*/0)
          : (mono_gchandle_new ? mono_gchandle_new(object, /*pinned=*/1) : 0);
  if ((s_weak_handles || mono_gchandle_new) && !handle)
    return nullptr;
  s_basemat_elements.push_back(
      {s_weak_handles ? nullptr : object, handle, original});
  MonoObject *parent = s_get_element_parent_widget
                           ? s_get_element_parent_widget(object)
                           : nullptr;
  const char *parent_name = object_class_name(parent);
  LOG_INFO("[SHELL-UI:SURFACE] rectangle=%zu parent=%s "
           "rgba=(%.3f,%.3f,%.3f,%.3f)",
           s_basemat_elements.size(), parent_name ? parent_name : "null",
           original.r, original.g, original.b, original.a);
  return &s_basemat_elements.back();
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

UIColor tint(UIColor original, RGB theme, bool scale_alpha = true) {
  const RGB source{std::clamp(original.r, 0.0f, 1.0f),
                   std::clamp(original.g, 0.0f, 1.0f),
                   std::clamp(original.b, 0.0f, 1.0f)};
  const float amount =
      std::clamp(g_settings.shell_ui_panels_strength / 100.0f, 0.0f, 1.0f);
  const RGB result = TintPreservingBrightness(source, theme, amount);
  // PanelBasemat has a separate BasematOpacity multiplier. Scaling both would
  // square the requested alpha, so its UIColor alpha remains native.
  const float alpha = scale_alpha
                          ? std::clamp(original.a * opacity_scale(), 0.0f, 1.0f)
                          : original.a;
  return {result.r, result.g, result.b, alpha, original.color_space};
}

// PanelBasemat/ReactPanel overrides BackgroundColor and renders through a
// private RectangleBasematElement. Updating Panel::BackgroundColor alone does
// not reach that shader-backed surface, so apply its GradationColor directly.
// This also lets Tick recolor panels which were created before our hooks.
uint64_t config_signature() {
  uint64_t value = static_cast<uint64_t>(
      std::clamp(g_settings.shell_ui_panels_animation, 0,
                 AnimationModeCount() - 1));
  value = value * 401ULL +
          static_cast<uint64_t>(
              std::clamp(g_settings.shell_ui_panels_animation_speed, 25, 400));
  value =
      value * 256ULL +
      static_cast<uint64_t>(std::clamp(g_settings.shell_ui_panels_r, 0, 255));
  value =
      value * 256ULL +
      static_cast<uint64_t>(std::clamp(g_settings.shell_ui_panels_g, 0, 255));
  value =
      value * 256ULL +
      static_cast<uint64_t>(std::clamp(g_settings.shell_ui_panels_b, 0, 255));
  value = value * 101ULL + static_cast<uint64_t>(std::clamp(
                               g_settings.shell_ui_panels_strength, 0, 100));
  value = value * 201ULL + static_cast<uint64_t>(std::clamp(
                               g_settings.shell_ui_panels_opacity, 0, 200));
  return value;
}

void set_background_hook(MonoObject *object, UIColor color) {
  if (!s_set_background_original)
    return;
  /* a switch drawn in another style (toggle_styles.cpp): its colours are
     the style's, not to be tinted with the panel theme */
  if (ToggleStyles::OnPanelBackground(object, &color.r)) {
    s_set_background_original(object, color);
    return;
  }
  if (!object || color.a <= 0.01f || excluded_panel(object)) {
    s_set_background_original(object, color);
    return;
  }
  TrackedPanel *tracked = track_panel(object, color);
  if (!tracked || !(AniketosTheme::CustomizationOn() && g_settings.shell_ui_panels_enabled)) {
    s_set_background_original(object, color);
    return;
  }
  const uint64_t now = monotonic_millis();
  const RGB theme = animated_theme_color(now);
  s_set_background_original(object, tint(color, theme, true));
  if (!s_logged_first_apply) {
    s_logged_first_apply = true;
    MonoClass *klass =
        mono_object_get_class ? mono_object_get_class(object) : nullptr;
    const char *name =
        klass && mono_class_get_name ? mono_class_get_name(klass) : "unknown";
    LOG_INFO("[SHELL-UI:THEME] first native panel styled class=%s",
             name ? name : "unknown");
  }
}

void set_fill_color_hook(MonoObject *object, UIColor color) {
  if (!s_set_fill_color_original)
    return;
  s_set_fill_color_original(object, color);
}

void set_basemat_opacity_hook(MonoObject *object, float opacity) {
  if (!s_set_basemat_opacity_original)
    return;
  if (!object) {
    s_set_basemat_opacity_original(object, opacity);
    return;
  }
  TrackedBasematOpacity *tracked = track_basemat_opacity(object, opacity);
  if (!tracked || !(AniketosTheme::CustomizationOn() && g_settings.shell_ui_panels_enabled)) {
    s_set_basemat_opacity_original(object, opacity);
    return;
  }
  s_set_basemat_opacity_original(object, opacity);
}

void on_basemat_render_hook(MonoObject *object, MonoObject *renderer,
                            void *matrix) {
  if (!s_on_basemat_render_original)
    return;

  if (!object || !(AniketosTheme::CustomizationOn() && g_settings.shell_ui_panels_enabled) || !s_fill_color_field ||
      !s_gradation_color_field || !mono_field_get_value ||
      !mono_field_set_value) {
    s_on_basemat_render_original(object, renderer, matrix);
    return;
  }

  UIColor native_fill{};
  UIColor native_gradation{};
  mono_field_get_value(object, s_fill_color_field, &native_fill);
  mono_field_get_value(object, s_gradation_color_field, &native_gradation);

  if (!is_modal_gradation_element(object)) {
    s_on_basemat_render_original(object, renderer, matrix);
    return;
  }

  const RGB theme = animated_theme_color(monotonic_millis());
  const float strength = std::clamp(
      g_settings.shell_ui_panels_strength / 100.0f, 0.0f, 1.0f);
  constexpr RGB kBlackFloor{0.006f, 0.006f, 0.008f};
  constexpr float kRedGradientGain = 0.26f;
  const RGB gradient_target{
      std::clamp(kBlackFloor.r + theme.r * kRedGradientGain, 0.0f, 1.0f),
      std::clamp(kBlackFloor.g + theme.g * kRedGradientGain, 0.0f, 1.0f),
      std::clamp(kBlackFloor.b + theme.b * kRedGradientGain, 0.0f, 1.0f)};

  UIColor black_fill = native_fill;
  black_fill.r = native_fill.r + (kBlackFloor.r - native_fill.r) * strength;
  black_fill.g = native_fill.g + (kBlackFloor.g - native_fill.g) * strength;
  black_fill.b = native_fill.b + (kBlackFloor.b - native_fill.b) * strength;
  // Some popup containers keep FillColor transparent and only configure the
  // gradation path.  If this exact popup falls back to RectangleElement, use
  // the native gradation alpha instead of making the whole card disappear.
  if (black_fill.a <= 0.01f && native_gradation.a > 0.01f) {
    black_fill.a = std::clamp(native_gradation.a * opacity_scale(), 0.0f, 1.0f);
  }

  UIColor subtle_gradation = native_gradation;
  subtle_gradation.r =
      native_gradation.r + (gradient_target.r - native_gradation.r) * strength;
  subtle_gradation.g =
      native_gradation.g + (gradient_target.g - native_gradation.g) * strength;
  subtle_gradation.b =
      native_gradation.b + (gradient_target.b - native_gradation.b) * strength;

  // Swap only the two RGB endpoints for this draw.  Sony's shader still owns
  // the direction, length, curve, opacity and rounded geometry, and the
  // managed object is restored immediately after it has submitted the frame.
  mono_field_set_value(object, s_fill_color_field, &black_fill);
  mono_field_set_value(object, s_gradation_color_field, &subtle_gradation);
  s_on_basemat_render_original(object, renderer, matrix);
  mono_field_set_value(object, s_gradation_color_field, &native_gradation);
  mono_field_set_value(object, s_fill_color_field, &native_fill);

  static bool logged = false;
  if (!logged) {
    logged = true;
    LOG_INFO("[SHELL-UI:MODAL] exact fc-popup gradient rendered "
             "target=(%.3f,%.3f,%.3f) nativeFillA=%.3f "
             "drawFillA=%.3f nativeGradA=%.3f",
             subtle_gradation.r, subtle_gradation.g, subtle_gradation.b,
             native_fill.a, black_fill.a, native_gradation.a);
  }
}

void on_rectangle_render_hook(MonoObject *object, MonoObject *renderer,
                              void *matrix) {
  if (!s_on_rectangle_render_original)
    return;
  // Kept installed as a harmless pass-through while the exact modal surface
  // is identified.  Generic RectangleElement tinting also catches unrelated
  // Control Center layers and therefore must not style anything here.
  s_on_rectangle_render_original(object, renderer, matrix);
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
  const bool icon = AniketosTheme::IconColor::TokenIsIcon(key);
  AniketosTheme::IconColor::LogTokenOnce(key, color->r, color->g, color->b,
                                         color->a, icon);
  if (icon)
    (void)AniketosTheme::IconColor::TintToken(key, &color->r, &color->g,
                                              &color->b);
  if (selected && (AniketosTheme::CustomizationOn() && g_settings.shell_ui_panels_enabled))
    *color = tint(*color, animated_theme_color(monotonic_millis()), true);
  return found;
}

void restore_all() {
  if (!s_set_background_original)
    return;
  for (const TrackedPanel &panel : s_panels) {
    if (MonoObject *object = panel_target(panel))
      s_set_background_original(object, panel.original);
  }
  if (s_set_basemat_opacity_original) {
    for (const TrackedBasematOpacity &basemat : s_basemat_opacities) {
      if (MonoObject *object = basemat_target(basemat))
        s_set_basemat_opacity_original(object, basemat.original);
    }
  }
  if (s_set_fill_color_original) {
    for (const TrackedBasematElement &element : s_basemat_elements) {
      if (MonoObject *object = basemat_element_target(element))
        s_set_fill_color_original(object, element.original);
    }
  }
  if (s_set_gradation_color) {
    for (const TrackedGradationElement &element : s_gradation_elements) {
      if (MonoObject *object = gradation_element_target(element))
        s_set_gradation_color(object, element.original);
    }
  }
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
  MonoClass *element =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Element");
  MonoClass *rectangle =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "RectangleElement");
  MonoClass *rectangle_basemat = mono_class_from_name(
      pui, "Sce.PlayStation.PUI.UI3", "RectangleBasematElement");
  MonoClass *style_manager =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "StyleManager");
  MonoClass *round_corner_optimization = mono_class_from_name(
      pui, "Sce.PlayStation.PUI", "RoundCornerOptimization");
  MonoClass *widget =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Widget");
  s_get_test_id = reinterpret_cast<StringGetter>(
      Get_Address_of_Method(pui, widget, "get_TestId", 0));
  s_get_widget_parent = reinterpret_cast<ObjectGetter>(
      Get_Address_of_Method(pui, widget, "get_Parent", 0));
  const uint64_t setter =
      Get_Address_of_Method(pui, panel, "set_BackgroundColor", 1);
  s_get_background_element = reinterpret_cast<ObjectGetter>(
      Get_Address_of_Method(pui, panel, "get_BackgroundElement", 0));
  const uint64_t gradation_color_setter = Get_Address_of_Method(
      pui, rectangle_basemat, "set_GradationColor", 1);
  s_set_gradation_color =
      reinterpret_cast<ElementColorSetter>(gradation_color_setter);
  s_set_basemat_position = reinterpret_cast<EnumSetter>(Get_Address_of_Method(
      pui, rectangle_basemat, "set_BasematPosition", 1));
  s_set_basemat_type = reinterpret_cast<EnumSetter>(Get_Address_of_Method(
      pui, rectangle_basemat, "set_BasematType", 1));
  s_set_element_basemat_opacity =
      reinterpret_cast<FloatSetter>(Get_Address_of_Method(
          pui, rectangle_basemat, "set_BasematOpacity", 1));
  s_update_rectangle_basemat = reinterpret_cast<ElementUpdate>(
      Get_Address_of_Method(pui, rectangle_basemat, "Update", 0));
  s_gradation_color_field = mono_class_get_field_from_name
                                ? mono_class_get_field_from_name(
                                      rectangle_basemat, "gradationColor")
                                : nullptr;
  s_panel_basemat_element_field =
      mono_class_get_field_from_name
          ? mono_class_get_field_from_name(panel_basemat, "elem")
          : nullptr;
  s_round_opt_fill_field =
      mono_class_get_field_from_name
          ? mono_class_get_field_from_name(rectangle_basemat, "roundOptFill")
          : nullptr;
  s_round_opt_basemat_field =
      mono_class_get_field_from_name
          ? mono_class_get_field_from_name(rectangle_basemat,
                                           "roundOptBasemat")
          : nullptr;
  s_round_opt_radius_field =
      mono_class_get_field_from_name && round_corner_optimization
          ? mono_class_get_field_from_name(round_corner_optimization, "Radius")
          : nullptr;
  s_fill_color_field = mono_class_get_field_from_name
                           ? mono_class_get_field_from_name(rectangle,
                                                           "fillColor")
                           : nullptr;
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
  s_get_element_parent_widget = reinterpret_cast<ObjectGetter>(
      Get_Address_of_Method(pui, element, "get_ParentWidget", 0));
  const uint64_t fill_color_setter =
      Get_Address_of_Method(pui, rectangle, "set_FillColor", 1);
  const bool fill_color_installed =
      fill_color_setter && s_get_element_parent_widget &&
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
  const uint64_t basemat_render =
      Get_Address_of_Method(pui, rectangle_basemat, "OnRender", 2);
  const bool render_installed =
      basemat_render && s_get_element_parent_widget &&
      s_fill_color_field && s_gradation_color_field &&
      mono_field_get_value && mono_field_set_value &&
      s_set_gradation_color &&
      InstallDetour(basemat_render,
                    reinterpret_cast<void *>(&on_basemat_render_hook),
                    reinterpret_cast<void **>(&s_on_basemat_render_original));
  const uint64_t rectangle_render =
      Get_Address_of_Method(pui, rectangle, "OnRender", 2);
  const bool rectangle_render_installed =
      rectangle_render && s_get_element_parent_widget && s_fill_color_field &&
      mono_field_get_value &&
      InstallDetour(rectangle_render,
                    reinterpret_cast<void *>(&on_rectangle_render_hook),
                    reinterpret_cast<void **>(&s_on_rectangle_render_original));
  LOG_INFO("[SHELL-UI:THEME] native hooks panel=%d basematOpacity=%d "
           "basematFill=%d basematGradation=%d basematRender=%d "
           "surfaceRender=%d styleToken=%d "
           "panelAddr=0x%llx basematAddr=0x%llx fillAddr=0x%llx "
           "gradationAddr=0x%llx tokenAddr=0x%llx handles=%s",
           s_installed ? 1 : 0, basemat_installed ? 1 : 0,
           fill_color_installed ? 1 : 0,
           (s_get_background_element && s_set_gradation_color) ? 1 : 0,
           render_installed ? 1 : 0, rectangle_render_installed ? 1 : 0,
           token_installed ? 1 : 0,
           static_cast<unsigned long long>(setter),
           static_cast<unsigned long long>(basemat_opacity_setter),
           static_cast<unsigned long long>(fill_color_setter),
           static_cast<unsigned long long>(gradation_color_setter),
           static_cast<unsigned long long>(try_token_color),
           s_weak_handles ? "weak" : "pinned-fallback");
}

void ApplyFocusHierarchy(MonoObject *focus_layer,
                         const std::string &test_id) {
  if (!focus_layer || !(AniketosTheme::CustomizationOn() && g_settings.shell_ui_panels_enabled) ||
      test_id.rfind("fc-list-item-", 0) != 0 || !s_get_widget_parent ||
      !s_set_background_original)
    return;

  MonoObject *node = s_get_widget_parent(focus_layer);
  for (unsigned depth = 1; node && depth < 18; ++depth) {
    const char *name = object_class_name(node);
    if (name && std::strcmp(name, "ReactFocusLayer") == 0) {
      const RGB theme = animated_theme_color(monotonic_millis());
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

void QueueModalPanel(MonoObject *panel, const std::string &anchor_test_id) {
  static bool logged_seen = false;
  static bool logged_queued = false;
  if (!panel)
    return;
  if (!logged_seen) {
    logged_seen = true;
    LOG_INFO("[SHELL-UI:MODAL] exact fc-popup received anchor='%s' enabled=%d",
             anchor_test_id.c_str(),
             (AniketosTheme::CustomizationOn() && g_settings.shell_ui_panels_enabled) ? 1 : 0);
  }
  if (!(AniketosTheme::CustomizationOn() && g_settings.shell_ui_panels_enabled))
    return;
  if (queue_modal_render_candidate(panel) && !logged_queued) {
    logged_queued = true;
    LOG_INFO("[SHELL-UI:MODAL] profile basemat render candidate queued");
  }
}

void Tick() {
  if (!s_installed || !s_set_background_original)
    return;

  const bool enabled = (AniketosTheme::CustomizationOn() && g_settings.shell_ui_panels_enabled);
  if (!enabled) {
    if (s_last_enabled)
      restore_all();
    s_last_enabled = false;
    return;
  }

  const uint64_t now = monotonic_millis();
  const uint64_t signature = config_signature();
  const bool config_changed = !s_last_enabled || signature != s_last_config;
  const bool animated = g_settings.shell_ui_panels_animation != 0 ||
                        GlobalTheme::Animated();
  MonoObject *deferred_modal = modal_target();
  if (!deferred_modal && s_modal_target.handle)
    clear_modal_target();
  const bool modal_ready =
      deferred_modal && s_modal_target.apply_requested &&
      now >= s_modal_target.queued_ms + 100;
  if (animated && s_last_frame_ms && now - s_last_frame_ms < kFrameIntervalMs)
    return;
  if (!animated && s_last_enabled && signature == s_last_config &&
      !modal_ready)
    return;

  const RGB theme = animated_theme_color(now);
  if (deferred_modal && (modal_ready || config_changed) &&
      s_panel_basemat_element_field && s_gradation_color_field &&
      s_set_gradation_color && mono_field_get_value) {
    MonoObject *element = nullptr;
    mono_field_get_value(deferred_modal, s_panel_basemat_element_field,
                         &element);
    if (element) {
      UIColor original{};
      mono_field_get_value(element, s_gradation_color_field, &original);
      const float alpha =
          std::clamp(original.a * opacity_scale(), 0.0f, 1.0f);
      s_set_gradation_color(
          element,
          {std::clamp(theme.r, 0.0f, 1.0f),
           std::clamp(theme.g, 0.0f, 1.0f),
           std::clamp(theme.b, 0.0f, 1.0f), alpha,
           original.color_space});
      s_modal_target.apply_requested = false;
      LOG_INFO("[SHELL-UI:MODAL] fc-popup deferred gradation applied "
               "original=(%.3f,%.3f,%.3f,%.3f)",
               original.r, original.g, original.b, original.a);
    }
  }
  remove_dead_panels();
  remove_dead_basemats();
  remove_dead_basemat_elements();
  remove_dead_gradation_elements();
  for (const TrackedPanel &panel : s_panels) {
    if (MonoObject *object = panel_target(panel))
      s_set_background_original(object, tint(panel.original, theme, true));
  }
  if (s_set_basemat_opacity_original) {
    for (const TrackedBasematOpacity &basemat : s_basemat_opacities) {
      if (MonoObject *object = basemat_target(basemat)) {
        s_set_basemat_opacity_original(object, basemat.original);
      }
    }
  }
  if (s_set_fill_color_original) {
    for (const TrackedBasematElement &element : s_basemat_elements) {
      if (MonoObject *object = basemat_element_target(element))
        s_set_fill_color_original(object, tint(element.original, theme, false));
    }
  }
  if (s_set_gradation_color) {
    for (const TrackedGradationElement &element : s_gradation_elements) {
      if (MonoObject *object = gradation_element_target(element))
        s_set_gradation_color(object, tint(element.original, theme, true));
    }
  }
  if (config_changed) {
    LOG_INFO("[SHELL-UI:THEME] applied panels=%zu basemats=%zu "
             "basematRenderers=%zu liveGradations=%zu "
             "rgb=(%d,%d,%d) strength=%d opacity=%d%%",
             s_panels.size(), s_basemat_opacities.size(),
             s_basemat_elements.size(), s_gradation_elements.size(),
             g_settings.shell_ui_panels_r,
             g_settings.shell_ui_panels_g, g_settings.shell_ui_panels_b,
             g_settings.shell_ui_panels_strength,
             g_settings.shell_ui_panels_opacity);
  }
  s_last_frame_ms = now;
  s_last_config = signature;
  s_last_enabled = true;
}

void ClearTrackedPanels() {
  if (s_last_enabled)
    restore_all();
  if (mono_gchandle_free) {
    for (const TrackedPanel &panel : s_panels) {
      if (panel.handle)
        mono_gchandle_free(panel.handle);
    }
    for (const TrackedBasematOpacity &basemat : s_basemat_opacities) {
      if (basemat.handle)
        mono_gchandle_free(basemat.handle);
    }
    for (const TrackedBasematElement &element : s_basemat_elements) {
      if (element.handle)
        mono_gchandle_free(element.handle);
    }
    for (const TrackedGradationElement &element : s_gradation_elements) {
      if (element.handle)
        mono_gchandle_free(element.handle);
    }
  }
  s_panels.clear();
  s_basemat_opacities.clear();
  s_basemat_elements.clear();
  s_gradation_elements.clear();
  clear_modal_target();
  if (mono_gchandle_free) {
    for (const ModalRenderCandidate &candidate : s_modal_render_candidates) {
      if (candidate.handle)
        mono_gchandle_free(candidate.handle);
    }
  }
  s_modal_render_candidates.clear();
  s_logged_token_keys.clear();
  s_last_enabled = false;
  s_last_frame_ms = 0;
  s_last_config = 0;
}

} // namespace AniketosTheme::SharedUI
