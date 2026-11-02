/* Copyright (C) 2026 AniketosHEN — native HomeUI tile focus theming. */

#include "aniketos_theme/tiles/home_tiles.hpp"
#include "home_tiles_internal.hpp"

#include "aniketos_theme/animation.hpp"
#include "aniketos_theme/color.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/panel_tint.hpp"
#include "aniketos_theme/theme_config.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "aniketos_ui/ui_color.hpp"
#include "external_symbols.hpp"
#include "homeui_top_nav_patch.hpp"
#include "hooked_funcs.hpp"

#include <onion/detour.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace AniketosTheme::HomeTiles {

MonoClass *s_pui_widget_class = nullptr;
MonoClass *s_focus_settings_class = nullptr;
StringGetter s_get_test_id = nullptr;

namespace {

using AniketosUI::MonotonicMs;
using AniketosUI::NullableUIColor;
using AniketosUI::UIColor;

constexpr const char *kReactNamespace = "ReactNative.Views.UI3.View";
constexpr const char *kTileFocusTestId = "tile-item-focus-layer";
constexpr uint64_t kFrameIntervalMs = 50;
constexpr size_t kMaxTrackedLayers = 1024;

struct NullableFloat {
  uint8_t has_value;
  uint8_t padding[3];
  float value;
};
static_assert(sizeof(NullableFloat) == 8);

using ObjectGetter = MonoObject *(*)(MonoObject *);
using FocusColorSetter = void (*)(MonoObject *, NullableUIColor);
using ThemeColorSetter = void (*)(MonoObject *, UIColor);
using RadiusSetter = void (*)(MonoObject *, NullableFloat);
using OnUpdate = void (*)(MonoObject *);
using SyncRenderParam = void (*)(MonoObject *);

struct TrackedLayer {
  uint32_t handle = 0; // weak, to the layer's FocusCustomSettings
  bool tile = false;
};

ObjectGetter s_get_focus_settings = nullptr;
FocusColorSetter s_set_focus_image_color = nullptr;
FocusColorSetter s_set_line_color = nullptr;
MonoClassField *s_render_line_element_field = nullptr;
MonoClassField *s_render_area_element_field = nullptr;
ThemeColorSetter s_set_theme_color = nullptr;
RadiusSetter s_set_rounded_radius = nullptr;
OnUpdate s_on_update_original = nullptr;
SyncRenderParam s_sync_render_param_original = nullptr;
std::vector<TrackedLayer> s_layers;
uint64_t s_animation_started_ms = 0;
AnimationMode s_outline_animation_mode = AnimationMode::Count;
uint64_t s_last_frame_ms = 0;
uint64_t s_last_config_signature = UINT64_MAX;
bool s_last_enabled = false;
bool s_installed = false;

template <typename Fn>
Fn method_thunk(MonoImage *image, MonoClass *klass, const char *name,
                int argc) {
  return reinterpret_cast<Fn>(Get_Address_of_Method(image, klass, name, argc));
}

bool outline_enabled() {
  if (!(CustomizationOn() && g_settings.shell_tiles_outline_enabled))
    return false;
  // In synchronized mode, Original PS5 restores Sony's native focus color.
  // Independent mode works even when the custom ShellUI background is off.
  return !g_settings.shell_tiles_outline_sync_background ||
         g_settings.shell_background_enabled;
}

ThemeConfig outline_config() {
  return g_settings.shell_tiles_outline_sync_background
             ? ConfigFromSettings(g_settings)
             : OutlineConfigFromSettings(g_settings);
}

RGB current_outline_color() {
  const uint64_t now = MonotonicMs();
  if (GlobalTheme::Active())
    return GlobalTheme::Color(now);
  const ThemeConfig config = outline_config();
  if (!s_animation_started_ms ||
      config.animation != s_outline_animation_mode) {
    s_animation_started_ms = now;
    s_outline_animation_mode = config.animation;
  }
  const float elapsed =
      static_cast<float>(now - s_animation_started_ms) / 1000.0f;
  return EvaluateAnimation(config, elapsed).glow;
}

UIColor opaque(RGB color) {
  return {std::clamp(color.r, 0.0f, 1.0f), std::clamp(color.g, 0.0f, 1.0f),
          std::clamp(color.b, 0.0f, 1.0f), 1.0f, 0};
}

void sync_render_param_hook(MonoObject *instance) {
  if (s_sync_render_param_original)
    s_sync_render_param_original(instance);
  if (!instance || !shellui_hooks_are_ready() || !outline_enabled() ||
      !mono_field_get_value)
    return;

  MonoObject *line = nullptr;
  MonoObject *area = nullptr;
  mono_field_get_value(instance, s_render_line_element_field, &line);
  mono_field_get_value(instance, s_render_area_element_field, &area);
  if (!line && !area)
    return;

  const UIColor color = opaque(current_outline_color());
  if (line)
    s_set_theme_color(line, color);
  if (area)
    s_set_theme_color(area, color);
}

/* The tile focus outline's corner: the focus frame the HomeUI patch left
   the tiles with (homeui_tile_patch.inc), so the outline also stays Sony's
   when a HomeUI could not be rounded. Before a Home bundle was seen, the
   setting's: the 106px icon's whole-pixel radius scaled to the 168px focus
   frame. */
void apply_radius(MonoObject *settings) {
  if (!settings || !s_set_rounded_radius)
    return;
  float focus_radius = homeui_tile_focus_radius();
  if (focus_radius < 0.0f) {
    constexpr float kFocusScale = 168.0f / 106.0f;
    const int rounding = TileRoundingPercent();
    const float icon_radius =
        rounding < 0 ? 16.0f : std::round(rounding * 53 / 100.0f);
    focus_radius = icon_radius * kFocusScale;
  }
  const NullableFloat radius{1, {0, 0, 0}, focus_radius};
  s_set_rounded_radius(settings, radius);
}

// ReactFocusLayer directly derives from Sce.PlayStation.PUI.UI3.FocusLayer,
// so the hooked instance itself is the native PUI Widget.
void register_layer(MonoObject *widget) {
  if (!widget || !s_get_test_id || !s_get_focus_settings)
    return;
  const std::string test_id = Mono_to_String(s_get_test_id(widget));
  SpaceSwitcherLabels::RememberFocusLayer(widget, test_id);
  PanelTint::ApplyFocusHierarchy(widget, test_id);
  MonoObject *settings = s_get_focus_settings(widget);
  SpaceSwitcherLabels::RememberFocusPadding(settings, test_id);
  if (!settings || find_live(s_layers, settings) ||
      s_layers.size() >= kMaxTrackedLayers)
    return;
  const uint32_t handle = weak_handle(settings);
  if (!handle)
    return;
  const bool tile = test_id == kTileFocusTestId;
  s_layers.push_back({handle, tile});
  if (tile)
    apply_radius(settings);
  if (s_layers.size() <= 8) {
    LOG_INFO("[SHELL-FOCUS:THEME] tracked test_id='%s' layer=%p count=%zu",
             test_id.c_str(), static_cast<void *>(settings), s_layers.size());
  }
}

void on_update_hook(MonoObject *instance) {
  if (s_on_update_original)
    s_on_update_original(instance);
  if (shellui_hooks_are_ready())
    register_layer(instance);
}

void apply_color(MonoObject *settings, RGB color, bool enabled) {
  if (!settings || !s_set_focus_image_color || !s_set_line_color)
    return;
  NullableUIColor value{};
  if (enabled) {
    value.has_value = 1;
    value.value = opaque(color);
  }
  // FocusImageFilterColor tints the visible animated focus asset.  The
  // separate line filter only affects its thin line component, so both must
  // be updated to recolor the complete Home tile outline.
  s_set_focus_image_color(settings, value);
  s_set_line_color(settings, value);
}

} // namespace

void Install(MonoImage *react_pui, MonoImage *pui) {
  if (s_installed || !react_pui || !pui)
    return;
  MonoClass *react_focus_class =
      mono_class_from_name(react_pui, kReactNamespace, "ReactFocusLayer");
  s_pui_widget_class =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Widget");
  s_focus_settings_class = mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3",
                                                "FocusCustomSettings");
  MonoClass *focus_element_class =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "FocusElement");
  MonoClass *focus_render_widget_class = mono_class_from_name(
      pui, "Sce.PlayStation.PUI.UI3", "FocusRenderWidget");
  s_get_test_id =
      method_thunk<StringGetter>(pui, s_pui_widget_class, "get_TestId", 0);
  s_get_focus_settings = method_thunk<ObjectGetter>(
      pui, s_pui_widget_class, "get_FocusCustomSettings", 0);
  s_set_theme_color = method_thunk<ThemeColorSetter>(
      pui, focus_element_class, "set_ThemeColor", 1);
  s_set_focus_image_color = method_thunk<FocusColorSetter>(
      pui, s_focus_settings_class, "set_FocusImageFilterColor", 1);
  s_set_line_color = method_thunk<FocusColorSetter>(
      pui, s_focus_settings_class, "set_LineFilterColor", 1);
  if (mono_class_get_field_from_name && focus_render_widget_class) {
    s_render_line_element_field = mono_class_get_field_from_name(
        focus_render_widget_class, "lineFocusElement");
    s_render_area_element_field = mono_class_get_field_from_name(
        focus_render_widget_class, "areaFocusElement");
  }
  s_set_rounded_radius = method_thunk<RadiusSetter>(
      pui, s_focus_settings_class, "set_RoundedCornerRadius", 1);
  const uint64_t update =
      Get_Address_of_Method(react_pui, react_focus_class, "OnUpdate", 0);
  const uint64_t sync_render_param = Get_Address_of_Method(
      pui, focus_render_widget_class, "SyncRenderParam", 0);
  const bool resolved = react_focus_class && s_pui_widget_class &&
                        s_focus_settings_class && s_get_test_id &&
                        s_get_focus_settings && s_set_focus_image_color &&
                        s_set_line_color && focus_element_class &&
                        focus_render_widget_class && s_set_theme_color &&
                        s_render_line_element_field &&
                        s_render_area_element_field && s_set_rounded_radius &&
                        update && sync_render_param;
  if (!resolved) {
    LOG_ERROR("[HOME-TILES:FOCUS] runtime resolve failed react=%d widget=%d "
              "settings=%d testId=%d getSettings=%d focusColor=%d "
              "lineColor=%d renderWidget=%d themeColor=%d elements=%d "
              "radius=%d update=%d sync=%d",
              react_focus_class ? 1 : 0, s_pui_widget_class ? 1 : 0,
              s_focus_settings_class ? 1 : 0, s_get_test_id ? 1 : 0,
              s_get_focus_settings ? 1 : 0,
              s_set_focus_image_color ? 1 : 0, s_set_line_color ? 1 : 0,
              focus_render_widget_class ? 1 : 0,
              s_set_theme_color ? 1 : 0,
              (s_render_line_element_field && s_render_area_element_field)
                  ? 1
                  : 0,
              s_set_rounded_radius ? 1 : 0, update ? 1 : 0,
              sync_render_param ? 1 : 0);
    return;
  }
  const bool update_installed =
      InstallDetour(update, reinterpret_cast<void *>(&on_update_hook),
                    reinterpret_cast<void **>(&s_on_update_original));
  const bool shader_installed = InstallDetour(
      sync_render_param, reinterpret_cast<void *>(&sync_render_param_hook),
      reinterpret_cast<void **>(&s_sync_render_param_original));
  s_installed = update_installed && shader_installed;
  LOG_INFO("[HOME-TILES:FOCUS] hooks update=%d shader=%d update_addr=0x%llx "
           "sync_addr=0x%llx",
           update_installed ? 1 : 0, shader_installed ? 1 : 0,
           static_cast<unsigned long long>(update),
           static_cast<unsigned long long>(sync_render_param));
}

void Tick() {
  if (!s_installed || s_layers.empty())
    return;
  const uint64_t now = MonotonicMs();
  if (s_last_frame_ms && now - s_last_frame_ms < kFrameIntervalMs)
    return;
  s_last_frame_ms = now;
  const bool enabled = outline_enabled();
  if (!enabled && !s_last_enabled)
    return;

  uint64_t signature = 1469598103934665603ULL;
  const ThemeConfig config = outline_config();
  const bool synced = g_settings.shell_tiles_outline_sync_background;
  const int preset = static_cast<int>(config.preset);
  const int animation = static_cast<int>(config.animation);
  const int speed =
      static_cast<int>(std::lround(config.animation_speed * 100.0f));
  const int red = static_cast<int>(std::lround(config.glow.r * 255.0f));
  const int green = static_cast<int>(std::lround(config.glow.g * 255.0f));
  const int blue = static_cast<int>(std::lround(config.glow.b * 255.0f));
  const int signature_values[] = {
      enabled ? 1 : 0, synced ? 1 : 0, preset, animation,
      speed,           red,            green,  blue,
  };
  for (int value : signature_values) {
    signature ^= static_cast<uint32_t>(value);
    signature *= 1099511628211ULL;
  }
  if (signature != s_last_config_signature) {
    LOG_INFO("[HOME-TILES:COLOR] enabled=%d source=%s "
             "preset=%d animation=%d speed=%d rgb=(%d,%d,%d) "
             "phase=shader-sync layers=%zu",
             enabled ? 1 : 0,
             synced ? "shell-background" : "independent-outline", preset,
             animation, speed, red, green, blue, s_layers.size());
    s_last_config_signature = signature;
  }
  const RGB color = current_outline_color();
  visit_live(s_layers, [&](const TrackedLayer &layer, MonoObject *settings) {
    if (layer.tile)
      apply_radius(settings);
    apply_color(settings, color, enabled);
    return false;
  });
  s_last_enabled = enabled;
}

} // namespace AniketosTheme::HomeTiles
