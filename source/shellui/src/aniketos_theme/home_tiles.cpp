/* Copyright (C) 2026 AniketosHEN — native HomeUI tile focus theming. */

#include "aniketos_shell/home_bookmark.hpp"
#include "aniketos_shell/home_tabs.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_theme/home_tiles.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "homeui_top_nav_patch.hpp"
#include "aniketos_theme/animation.hpp"
#include "aniketos_theme/color.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/shared_ui.hpp"
#include "aniketos_theme/theme_config.hpp"

#include <onion/detour.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <time.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

extern onion::Settings g_settings;

namespace AniketosTheme::HomeTiles {
namespace {

constexpr const char *kReactNamespace = "ReactNative.Views.UI3.View";
constexpr const char *kTileFocusTestId = "tile-item-focus-layer";
constexpr uint64_t kFrameIntervalMs = 50;
constexpr size_t kMaxTrackedLayers = 1024;
constexpr size_t kMaxArcNodes = 4096;

struct ArcBadge {
  std::string title_id;
  std::string version;
};

struct UIColor {
  float r, g, b, a;
  int32_t color_space;
};
struct NullableUIColor {
  uint8_t has_value;
  uint8_t padding[3];
  UIColor value;
};
struct NullableFloat {
  uint8_t has_value;
  uint8_t padding[3];
  float value;
};
static_assert(sizeof(UIColor) == 20);
static_assert(sizeof(NullableUIColor) == 24);
static_assert(sizeof(NullableFloat) == 8);

using StringGetter = MonoString *(*)(MonoObject *);
using ObjectGetter = MonoObject *(*)(MonoObject *);
using FocusColorSetter = void (*)(MonoObject *, NullableUIColor);
using ThemeColorSetter = void (*)(MonoObject *, UIColor);
using RadiusSetter = void (*)(MonoObject *, NullableFloat);
using OnUpdate = void (*)(MonoObject *);
using SyncRenderParam = void (*)(MonoObject *);

struct SpaceTabLabelLayout {
  uint32_t handle = 0;
  float stock_x = 0.0f;
  float stock_width = 0.0f;
  bool decorated = false;
};

struct SpaceTabFocusPadding {
  uint32_t handle = 0;
  int stock_left = 0;
  int stock_right = 0;
  int applied_left = -1;
  int applied_right = -1;
};

MonoClass *s_react_focus_class = nullptr;
MonoClass *s_pui_widget_class = nullptr;
MonoClass *s_focus_settings_class = nullptr;
MonoClass *s_focus_element_class = nullptr;
MonoClass *s_focus_render_widget_class = nullptr;
MonoClass *s_label_class = nullptr;
StringGetter s_get_test_id = nullptr;
ObjectGetter s_get_focus_settings = nullptr;
ObjectGetter s_get_parent_widget = nullptr;
FocusColorSetter s_set_focus_image_color = nullptr;
FocusColorSetter s_set_line_color = nullptr;
MonoClassField *s_focus_image_color_field = nullptr;
MonoClassField *s_line_color_field = nullptr;
MonoClassField *s_render_line_element_field = nullptr;
MonoClassField *s_render_area_element_field = nullptr;
MonoClassField *s_render_target_field = nullptr;
ObjectGetter s_get_render_custom_settings = nullptr;
ObjectGetter s_get_render_target = nullptr;
ThemeColorSetter s_set_theme_color = nullptr;
RadiusSetter s_set_rounded_radius = nullptr;
OnUpdate s_on_update_original = nullptr;
SyncRenderParam s_sync_render_param_original = nullptr;
std::vector<MonoObject *> s_layers;
std::vector<uint32_t> s_handles;
std::vector<uint8_t> s_tile_layers;
uint64_t s_animation_started_ms = 0;
AnimationMode s_outline_animation_mode = AnimationMode::Count;
uint64_t s_last_frame_ms = 0;
uint64_t s_last_config_signature = UINT64_MAX;
uint64_t s_last_verified_signature = UINT64_MAX;
bool s_last_enabled = false;
bool s_installed = false;
unsigned s_sync_probe_count = 0;
std::unordered_set<std::string> s_logged_shader_targets;
std::unordered_set<std::string> s_logged_focus_chains;
MonoObject *s_arc_font = nullptr;
uint32_t s_arc_font_handle = 0;
const char *s_arc_rotation_property = nullptr;
MonoClass *s_arc_rotation_class = nullptr;
std::unordered_map<MonoObject *, ArcBadge> s_arc_badges;
std::unordered_set<MonoObject *> s_arc_rendered;
std::deque<ArcBadge> s_pending_arc_badges;
std::vector<uint32_t> s_arc_glyph_handles;
std::unordered_set<std::string> s_logged_space_switchers;
std::vector<uint32_t> s_space_switcher_root_handles;
std::vector<uint32_t> s_space_switcher_focus_handles;
std::vector<uint32_t> s_space_switcher_label_handles;
std::vector<SpaceTabLabelLayout> s_space_tab_label_layouts;
std::vector<SpaceTabFocusPadding> s_space_tab_focus_paddings;

MonoObject *parent_of(MonoObject *object) {
  if (!object || !mono_object_get_class || !mono_class_get_method_from_name ||
      !mono_runtime_invoke)
    return nullptr;
  MonoClass *klass = mono_object_get_class(object);
  if (!klass)
    return nullptr;
  // React/PUI can attach a node through its layout/base relationship while
  // the public Parent is still null.  Resolve semantically and accept the
  // first live relationship instead of binding to one firmware's storage.
  constexpr const char *kParentGetters[] = {
      "get_Parent", "get_ParentBase", "get_LayoutParent",
      "get_NativeParent"};
  for (const char *name : kParentGetters) {
    MonoMethod *getter = mono_class_get_method_from_name(klass, name, 0);
    if (!getter)
      continue;
    if (MonoObject *parent =
            mono_runtime_invoke(getter, object, nullptr, nullptr))
      return parent;
  }
  return nullptr;
}

bool derives_from(MonoObject *object, MonoClass *base) {
  if (!object || !base || !mono_object_get_class || !mono_class_get_parent)
    return false;
  for (MonoClass *klass = mono_object_get_class(object); klass;
       klass = mono_class_get_parent(klass)) {
    if (klass == base)
      return true;
  }
  return false;
}

bool is_home_space_switcher(const std::string &test_id) {
  return test_id.find("experience-switcher-focus-layer") !=
             std::string::npos ||
         test_id.find("space-switcher-game") != std::string::npos ||
         test_id.find("space-switcher-media") != std::string::npos;
}

bool is_space_switcher_root(MonoObject *object) {
  if (!object || !mono_gchandle_get_target)
    return false;
  for (uint32_t handle : s_space_switcher_root_handles) {
    if (handle && mono_gchandle_get_target(handle) == object)
      return true;
  }
  return false;
}

void remember_space_switcher_root(MonoObject *widget) {
  if (!widget || !mono_gchandle_new_weakref || !mono_gchandle_get_target)
    return;
  bool focus_known = false;
  for (uint32_t handle : s_space_switcher_focus_handles) {
    if (handle && mono_gchandle_get_target(handle) == widget) {
      focus_known = true;
      break;
    }
  }
  if (!focus_known) {
    const uint32_t focus_handle = mono_gchandle_new_weakref(widget, 0);
    if (focus_handle)
      s_space_switcher_focus_handles.push_back(focus_handle);
  }
  MonoObject *root = parent_of(widget);
  if (!root || is_space_switcher_root(root))
    return;
  const uint32_t handle = mono_gchandle_new_weakref(root, 0);
  if (!handle)
    return;
  s_space_switcher_root_handles.push_back(handle);
  MonoClass *root_class = mono_object_get_class ? mono_object_get_class(root)
                                                 : nullptr;
  const char *class_name = root_class && mono_class_get_name
                               ? mono_class_get_name(root_class)
                               : "?";
  LOG_INFO("[HOME-SPACES:NATIVE] owner class='%s' root=%p count=%zu",
           class_name ? class_name : "?", static_cast<void *>(root),
           s_space_switcher_root_handles.size());
}

bool bind_space_switcher_label(MonoObject *label,
                               const std::string &text) {
  if (!label || !s_pui_widget_class || s_space_switcher_focus_handles.empty() ||
      !mono_gchandle_get_target || !mono_gchandle_new_weakref)
    return false;
  for (auto it = s_space_switcher_label_handles.begin();
       it != s_space_switcher_label_handles.end();) {
    MonoObject *target = *it ? mono_gchandle_get_target(*it) : nullptr;
    if (!target) {
      if (*it && mono_gchandle_free)
        mono_gchandle_free(*it);
      it = s_space_switcher_label_handles.erase(it);
      continue;
    }
    if (target == label)
      return true;
    ++it;
  }
  // This fallback is called only after the ancestor resolves to the native
  // shared space-switcher focus layer. Do not use pixel dimensions here:
  // those vary by HomeUI layout/firmware. Its first two text children are the
  // stock Games and Media tabs, in that order.
  if (s_space_switcher_label_handles.size() >= 2)
    return false;
  const uint32_t handle = mono_gchandle_new_weakref(label, 0);
  if (!handle)
    return false;
  s_space_switcher_label_handles.push_back(handle);
  LOG_INFO("[HOME-SPACES:NATIVE] bound top-tab index=%zu text='%s'",
           s_space_switcher_label_handles.size() - 1, text.c_str());
  return true;
}

int space_switcher_label_index(MonoObject *label) {
  if (!label || !mono_gchandle_get_target)
    return -1;
  for (size_t index = 0; index < s_space_switcher_label_handles.size();
       ++index) {
    const uint32_t handle = s_space_switcher_label_handles[index];
    if (handle && mono_gchandle_get_target(handle) == label)
      return static_cast<int>(index);
  }
  return -1;
}

std::string space_switcher_ancestor(MonoObject *object) {
  for (unsigned depth = 0; object && depth < 20; ++depth) {
    if (is_space_switcher_root(object))
      return "experience-switcher-focus-layer";
    if (derives_from(object, s_pui_widget_class) && s_get_test_id) {
      MonoString *managed = s_get_test_id(object);
      const std::string test_id =
          managed ? Mono_to_String(managed) : std::string();
      if (is_home_space_switcher(test_id))
        return test_id;
    }
    MonoObject *parent = parent_of(object);
    if (!parent || parent == object)
      break;
    object = parent;
  }
  return {};
}

int localized_space_tab(const std::string &text) {
  // The HomeUI space labels are localized by the system, not by Aniketos.
  // Resolve exact visible labels; never infer a tab from its position,
  // dimensions, firmware version, or a guessed native address. These labels
  // are unique category names in the ShellUI header, unlike app titles.
  static constexpr const char *kGamesLabels[] = {
      "Games", "Gry", "Spiele", "Jeux", "Juegos", "Giochi", "Jogos",
      "Игры", "ゲーム", "遊戲", "游戏", "게임", "Oyunlar", "الألعاب"};
  static constexpr const char *kMediaLabels[] = {
      "Media", "Multimedia", "Média", "Médias", "Medien", "Multimédia",
      "Мультимедиа", "メディア", "媒體", "媒体", "미디어", "Medya",
      "الوسائط"};
  for (const char *label : kGamesLabels)
    if (text == label)
      return 0;
  for (const char *label : kMediaLabels)
    if (text == label)
      return 1;
  return -1;
}

void remember_space_tab_label_layout(MonoObject *label) {
  if (!label || !s_pui_widget_class || !mono_gchandle_get_target ||
      !mono_gchandle_new_weakref)
    return;
  const float x = Get_Property<float>(s_pui_widget_class, label, "X");
  const float width = Get_Property<float>(s_pui_widget_class, label, "Width");
  for (auto it = s_space_tab_label_layouts.begin();
       it != s_space_tab_label_layouts.end();) {
    MonoObject *target = it->handle ? mono_gchandle_get_target(it->handle)
                                    : nullptr;
    if (!target) {
      if (it->handle && mono_gchandle_free)
        mono_gchandle_free(it->handle);
      it = s_space_tab_label_layouts.erase(it);
      continue;
    }
    if (target == label) {
      // Called only while the label still contains the stock, undecorated
      // text. Refresh the baseline if HomeUI rebuilds or relocalizes it.
      it->stock_x = x;
      it->stock_width = width;
      it->decorated = true;
      return;
    }
    ++it;
  }
  if (s_space_tab_label_layouts.size() >= 4)
    return;
  const uint32_t handle = mono_gchandle_new_weakref(label, 0);
  if (handle)
    s_space_tab_label_layouts.push_back({handle, x, width, true});
}

void remember_space_tab_focus_padding(MonoObject *settings) {
  if (!settings || !s_focus_settings_class || !mono_gchandle_get_target ||
      !mono_gchandle_new_weakref || !mono_class_get_property_from_name ||
      !mono_class_get_property_from_name(s_focus_settings_class,
                                         "FocusImagePaddingLeft") ||
      !mono_class_get_property_from_name(s_focus_settings_class,
                                         "FocusImagePaddingRight"))
    return;
  for (auto it = s_space_tab_focus_paddings.begin();
       it != s_space_tab_focus_paddings.end();) {
    MonoObject *target = it->handle ? mono_gchandle_get_target(it->handle)
                                    : nullptr;
    if (!target) {
      if (it->handle && mono_gchandle_free)
        mono_gchandle_free(it->handle);
      it = s_space_tab_focus_paddings.erase(it);
      continue;
    }
    if (target == settings)
      return;
    ++it;
  }
  const uint32_t handle = mono_gchandle_new_weakref(settings, 0);
  if (!handle)
    return;
  const int left = Get_Property<int>(s_focus_settings_class, settings,
                                     "FocusImagePaddingLeft");
  const int right = Get_Property<int>(s_focus_settings_class, settings,
                                      "FocusImagePaddingRight");
  s_space_tab_focus_paddings.push_back({handle, left, right});
  LOG_INFO("[HOME-SPACES:FOCUS] padding baseline settings=%p left=%d right=%d",
           static_cast<void *>(settings), left, right);
}

const ArcBadge *find_arc_badge(MonoObject *object) {
  for (unsigned depth = 0; object && depth < 20; ++depth) {
    const auto found = s_arc_badges.find(object);
    if (found != s_arc_badges.end())
      return &found->second;
    MonoObject *parent = parent_of(object);
    if (!parent || parent == object)
      break;
    object = parent;
  }
  return nullptr;
}

void discard_pending_badge(const ArcBadge &badge) {
  const auto found = std::find_if(
      s_pending_arc_badges.begin(), s_pending_arc_badges.end(),
      [&badge](const ArcBadge &candidate) {
        return candidate.title_id == badge.title_id &&
               candidate.version == badge.version;
      });
  if (found != s_pending_arc_badges.end())
    s_pending_arc_badges.erase(found);
}

MonoString *managed_string(const std::string &value) {
  return Root_Domain && mono_string_new
             ? mono_string_new(Root_Domain, value.c_str())
             : nullptr;
}

MonoObject *create_arc_glyph(MonoObject *parent, char glyph, float x, float y,
                             float size, float rotation, unsigned index,
                             bool title_row) {
  if (!parent || !s_label_class || !mono_runtime_object_init)
    return nullptr;
  MonoObject *label = New_Object(s_label_class);
  if (!label)
    return nullptr;
  mono_runtime_object_init(label);
  // Mono does not scan native C++ locals as GC roots.  Pin immediately, before
  // creating Name/Text/Color managed objects; otherwise a collection can move
  // or reclaim the Label while this function still holds its old pointer.
  const uint32_t handle = mono_gchandle_new
                              ? mono_gchandle_new(label, /*pinned=*/1)
                              : 0;
  if (mono_gchandle_new && !handle)
    return nullptr;
  if (handle)
    s_arc_glyph_handles.push_back(handle);
  const std::string name = std::string("AniketosTileArc") +
                           (title_row ? "Title" : "Version") +
                           std::to_string(index);
  const char text[2] = {glyph, '\0'};
  Set_Property(s_label_class, label, "Name", managed_string(name));
  // Match the proven Shell Menu UI2 path used elsewhere in this process.
  Set_Property(s_label_class, label, "PositionType", 0);
  Set_Property(s_label_class, label, "X", x);
  Set_Property(s_label_class, label, "Y", y);
  Set_Property(s_label_class, label, "Width", size * 1.45f);
  Set_Property(s_label_class, label, "Height", size * 1.7f);
  Set_Property(s_label_class, label, "Text",
               managed_string(std::string(text)));
  if (s_arc_font)
    Set_Property_Invoke(s_label_class, label, "Font", s_arc_font);
  Set_Property(s_label_class, label, "HorizontalAlignment", 1);
  Set_Property(s_label_class, label, "VerticalAlignment", 1);
  Set_Property(s_label_class, label, "FitWidthToText", false);
  Set_Property(s_label_class, label, "FitHeightToText", false);
  Set_Property(s_label_class, label, "NumberOfLines", 1);
  Set_Property(s_label_class, label, "EnableThemedTextShadow", true);
  Set_Property(s_label_class, label, "Opacity", 1.0f);
  if (MonoObject *white = CreateUIColor(1.0f, 1.0f, 1.0f, 1.0f))
    Set_Property_Invoke(s_label_class, label, "TextColor", white);
  if (s_arc_rotation_property && s_arc_rotation_class)
    Set_Property(s_arc_rotation_class, label, s_arc_rotation_property,
                 rotation);
  Widget_Append_Child(parent, label);
  return label;
}

bool draw_arc(MonoObject *parent, const std::string &text, float diameter,
              bool title_row) {
  if (text.empty())
    return true;
  const size_t count = std::min<size_t>(text.size(), 24);
  const float center = diameter * 0.5f;
  // Preserve Sony's original ordering (Title ID above version), but bend both
  // rows along concentric upper arcs of the circular tile.
  const float radius = diameter * (title_row ? 0.40f : 0.275f);
  const float font_size = std::clamp(diameter * (title_row ? 0.075f : 0.068f),
                                     9.0f, 14.0f);
  const float glyph_width = font_size * 1.45f;
  const float glyph_height = font_size * 1.7f;
  const float natural_span = 0.105f * static_cast<float>(count - 1);
  // A five-character version such as "v1.00" otherwise bends by only ~2.5
  // pixels at 106px and still looks straight on a television.  Give the
  // shorter inner row a visible minimum sweep while keeping it inside circle.
  const float half_span = title_row
                              ? std::min(0.88f, natural_span)
                              : std::min(0.72f, std::max(0.58f, natural_span));
  bool ok = true;
  for (size_t i = 0; i < count; ++i) {
    const float t = count <= 1
                        ? 0.0f
                        : -half_span + 2.0f * half_span *
                                           static_cast<float>(i) /
                                           static_cast<float>(count - 1);
    const float x = center + radius * std::sin(t) - glyph_width * 0.5f;
    const float y = center - radius * std::cos(t) - glyph_height * 0.5f;
    constexpr float kRadiansToDegrees = 57.2957795f;
    const float rotation = t * kRadiansToDegrees;
    ok = create_arc_glyph(parent, text[i], x, y, font_size, rotation,
                          static_cast<unsigned>(i), title_row) &&
         ok;
  }
  return ok;
}

void render_arc_badge(MonoObject *react_layer) {
  // The arcs follow a full circle; any other rounding keeps Sony's labels.
  // What the patch gave the tiles counts, not just the setting.
  if (!react_layer || AniketosTheme::TileRoundingPercent() != 100 ||
      homeui_tile_rounding() != 100 ||
      s_arc_rendered.find(react_layer) != s_arc_rendered.end())
    return;
  ArcBadge resolved_badge;
  const ArcBadge *badge = find_arc_badge(react_layer);
  const char *binding = "ancestry";
  if (badge) {
    resolved_badge = *badge;
    discard_pending_badge(resolved_badge);
  } else if (!s_pending_arc_badges.empty()) {
    resolved_badge = std::move(s_pending_arc_badges.front());
    s_pending_arc_badges.pop_front();
    s_arc_badges[react_layer] = resolved_badge;
    binding = "render-order";
  } else {
    return;
  }
  float width = Get_Property<float>(s_pui_widget_class, react_layer, "Width");
  float height =
      Get_Property<float>(s_pui_widget_class, react_layer, "Height");
  float diameter = std::min(width, height);
  if (diameter < 80.0f || diameter > 400.0f)
    diameter = 168.0f;
  const bool title_ok = draw_arc(react_layer, resolved_badge.title_id, diameter,
                                 /*title_row=*/true);
  const bool version_ok = draw_arc(react_layer, resolved_badge.version, diameter,
                                   /*title_row=*/false);
  if (title_ok && version_ok) {
    s_arc_rendered.insert(react_layer);
    LOG_INFO("[HOME-TILES:ARC] rendered title='%s' version='%s' layer=%p "
             "diameter=%.1f glyphs=%zu binding=%s",
             resolved_badge.title_id.c_str(), resolved_badge.version.c_str(),
             static_cast<void *>(react_layer), diameter,
             resolved_badge.title_id.size() + resolved_badge.version.size(),
             binding);
  } else {
    LOG_ERROR("[HOME-TILES:ARC] native glyph creation failed layer=%p",
              static_cast<void *>(react_layer));
  }
}

uint64_t monotonic_millis() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000ULL +
         static_cast<uint64_t>(now.tv_nsec) / 1000000ULL;
}

template <typename Fn>
Fn method_thunk(MonoImage *image, MonoClass *klass, const char *name,
                int argc) {
  return reinterpret_cast<Fn>(Get_Address_of_Method(image, klass, name, argc));
}

bool tracked(MonoObject *settings) {
  for (size_t i = 0; i < s_layers.size(); ++i) {
    MonoObject *target = s_handles[i] && mono_gchandle_get_target
                             ? mono_gchandle_get_target(s_handles[i])
                             : s_layers[i];
    if (target == settings)
      return true;
  }
  return false;
}

bool outline_enabled() {
  if (!(AniketosTheme::CustomizationOn() && g_settings.shell_tiles_outline_enabled))
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
  const uint64_t now = monotonic_millis();
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

void sync_render_param_hook(MonoObject *instance) {
  if (s_sync_render_param_original)
    s_sync_render_param_original(instance);
  if (!instance || !shellui_hooks_are_ready() ||
      !outline_enabled() ||
      !s_get_render_custom_settings || !s_set_theme_color ||
      !mono_field_get_value)
    return;

  MonoObject *settings = s_get_render_custom_settings(instance);
  MonoObject *target = s_get_render_target ? s_get_render_target(instance)
                                           : nullptr;
  if (!target && s_render_target_field)
    mono_field_get_value(instance, s_render_target_field, &target);
  MonoObject *line = nullptr;
  MonoObject *area = nullptr;
  if (s_render_line_element_field)
    mono_field_get_value(instance, s_render_line_element_field, &line);
  if (s_render_area_element_field)
    mono_field_get_value(instance, s_render_area_element_field, &area);
  if (!line && !area)
    return;

  MonoString *managed_test_id =
      target && s_get_test_id ? s_get_test_id(target) : nullptr;
  const std::string test_id =
      managed_test_id ? Mono_to_String(managed_test_id) : std::string();
  if (s_sync_probe_count < 12) {
    LOG_INFO("[HOME-TILES:SHADER-PROBE] target=%p test_id='%s' settings=%p "
             "global=1",
             static_cast<void *>(target), test_id.c_str(),
             static_cast<void *>(settings));
    ++s_sync_probe_count;
  }
  const RGB line_color = current_outline_color();
  const RGB area_color = line_color;
  const UIColor line_value{std::clamp(line_color.r, 0.0f, 1.0f),
                           std::clamp(line_color.g, 0.0f, 1.0f),
                           std::clamp(line_color.b, 0.0f, 1.0f), 1.0f, 0};
  const UIColor area_value{std::clamp(area_color.r, 0.0f, 1.0f),
                           std::clamp(area_color.g, 0.0f, 1.0f),
                           std::clamp(area_color.b, 0.0f, 1.0f), 1.0f, 0};
  if (line)
    s_set_theme_color(line, line_value);
  if (area)
    s_set_theme_color(area, area_value);
  const std::string log_key = test_id.empty() ? "<unnamed>" : test_id;
  if (s_logged_shader_targets.size() < 24 &&
      s_logged_shader_targets.insert(log_key).second) {
    LOG_INFO("[SHELL-FOCUS:SHADER] global=1 test_id='%s' "
             "rgb=(%.3f,%.3f,%.3f) line=%p area=%p settings=%p",
             test_id.c_str(),
             line_value.r, line_value.g, line_value.b, static_cast<void *>(line),
             static_cast<void *>(area), static_cast<void *>(settings));
  }
}

/* The tile focus outline's corner: the focus frame the HomeUI patch left
   the tiles with (homeui_home_tiles.inc), so the outline also stays Sony's
   when a HomeUI could not be rounded. Before a Home bundle was seen, the
   setting's: the 106px icon's whole-pixel radius scaled to the 168px focus
   frame. */
void apply_radius(MonoObject *settings) {
  if (!settings || !s_set_rounded_radius)
    return;
  float focus_radius = homeui_tile_focus_radius();
  if (focus_radius < 0.0f) {
    constexpr float kFocusScale = 168.0f / 106.0f;
    const int rounding = AniketosTheme::TileRoundingPercent();
    const float icon_radius =
        rounding < 0 ? 16.0f : std::round(rounding * 53 / 100.0f);
    focus_radius = icon_radius * kFocusScale;
  }
  const NullableFloat radius{1, {0, 0, 0}, focus_radius};
  s_set_rounded_radius(settings, radius);
}

void register_layer(MonoObject *react_layer) {
  if (!react_layer || !s_get_test_id || !s_get_focus_settings)
    return;
  // ReactFocusLayer directly derives from Sce.PlayStation.PUI.UI3.FocusLayer,
  // so the hooked instance itself is the native PUI Widget.
  MonoObject *widget = react_layer;
  MonoString *managed_test_id = s_get_test_id(widget);
  const std::string test_id =
      managed_test_id ? Mono_to_String(managed_test_id) : std::string();
  if (s_get_parent_widget && mono_object_get_class && mono_class_get_name &&
      s_logged_focus_chains.size() < 256) {
    std::string chain;
    MonoObject *node = widget;
    for (unsigned depth = 0; node && depth < 10; ++depth) {
      MonoClass *klass = mono_object_get_class(node);
      const char *name = klass ? mono_class_get_name(klass) : nullptr;
      MonoString *node_test_id = s_get_test_id(node);
      const std::string node_id =
          node_test_id ? Mono_to_String(node_test_id) : std::string();
      if (!chain.empty())
        chain += " <- ";
      chain += name ? name : "?";
      if (!node_id.empty()) {
        chain += "#";
        chain += node_id;
      }
      MonoObject *parent = s_get_parent_widget(node);
      if (parent == node)
        break;
      node = parent;
    }
    if (!chain.empty() && s_logged_focus_chains.insert(chain).second) {
      LOG_INFO("[SHELL-UI:FOCUS-PROBE] %s", chain.c_str());
    }
  }
  if (is_home_space_switcher(test_id) &&
      s_logged_space_switchers.insert(test_id).second) {
    LOG_INFO("[HOME-SPACES:NATIVE] focus-layer test_id='%s' widget=%p",
             test_id.c_str(), static_cast<void *>(widget));
  }
  if (is_home_space_switcher(test_id))
    remember_space_switcher_root(widget);
  SharedUI::ApplyFocusHierarchy(widget, test_id);
  const bool tile = test_id == kTileFocusTestId;
  if (tile)
    render_arc_badge(react_layer);
  MonoObject *settings = s_get_focus_settings(widget);
  if (test_id == "experience-switcher-focus-layer")
    remember_space_tab_focus_padding(settings);
  if (!settings || tracked(settings) || s_layers.size() >= kMaxTrackedLayers)
    return;
  const bool weak = mono_gchandle_new_weakref && mono_gchandle_get_target;
  const uint32_t handle =
      weak
          ? mono_gchandle_new_weakref(settings, /*track_resurrection=*/0)
          : (mono_gchandle_new ? mono_gchandle_new(settings, /*pinned=*/1) : 0);
  if ((weak || mono_gchandle_new) && !handle)
    return;
  s_layers.push_back(weak ? nullptr : settings);
  s_handles.push_back(handle);
  s_tile_layers.push_back(tile ? 1 : 0);
  if (s_layers.size() == 1)
    s_sync_probe_count = 0;
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

void apply_color(MonoObject *settings, RGB image_color, RGB line_color,
                 bool enabled) {
  if (!settings || !s_set_focus_image_color || !s_set_line_color)
    return;
  NullableUIColor value{};
  if (enabled) {
    value.has_value = 1;
    value.value = {std::clamp(image_color.r, 0.0f, 1.0f),
                   std::clamp(image_color.g, 0.0f, 1.0f),
                   std::clamp(image_color.b, 0.0f, 1.0f), 1.0f, 0};
  }
  // FocusImageFilterColor tints the visible animated focus asset.  The
  // separate line filter only affects its thin line component, so both must
  // be updated to recolor the complete Home tile outline.
  s_set_focus_image_color(settings, value);
  if (enabled) {
    value.value = {std::clamp(line_color.r, 0.0f, 1.0f),
                   std::clamp(line_color.g, 0.0f, 1.0f),
                   std::clamp(line_color.b, 0.0f, 1.0f), 1.0f, 0};
  }
  s_set_line_color(settings, value);
}

void verify_color(MonoObject *settings, uint64_t signature) {
  if (!settings || signature == s_last_verified_signature ||
      !mono_field_get_value || !s_focus_image_color_field ||
      !s_line_color_field)
    return;
  NullableUIColor focus{};
  NullableUIColor line{};
  mono_field_get_value(settings, s_focus_image_color_field, &focus);
  mono_field_get_value(settings, s_line_color_field, &line);
  LOG_INFO("[HOME-TILES:COLOR-VERIFY] focus=(%d %.3f,%.3f,%.3f,%.3f) "
           "line=(%d %.3f,%.3f,%.3f,%.3f)",
           focus.has_value ? 1 : 0, focus.value.r, focus.value.g,
           focus.value.b, focus.value.a, line.has_value ? 1 : 0,
           line.value.r, line.value.g, line.value.b, line.value.a);
  s_last_verified_signature = signature;
}

} // namespace

void Install(MonoImage *react_pui, MonoImage *pui) {
  if (s_installed || !react_pui || !pui)
    return;
  s_react_focus_class =
      mono_class_from_name(react_pui, kReactNamespace, "ReactFocusLayer");
  s_pui_widget_class =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Widget");
  s_focus_settings_class = mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3",
                                                "FocusCustomSettings");
  s_focus_element_class =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "FocusElement");
  s_focus_render_widget_class = mono_class_from_name(
      pui, "Sce.PlayStation.PUI.UI3", "FocusRenderWidget");
  s_label_class =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI2", "Label");
  if (s_label_class && mono_class_get_property_from_name) {
    constexpr const char *kRotationCandidates[] = {
        "Rotation", "RotationZ", "Angle", "TransformAngle",
        "RotationAngle", "TransformRotation"};
    for (const char *candidate : kRotationCandidates) {
      for (MonoClass *klass : {s_label_class, s_pui_widget_class}) {
        if (klass && mono_class_get_property_from_name(klass, candidate)) {
          s_arc_rotation_property = candidate;
          s_arc_rotation_class = klass;
          break;
        }
      }
      if (s_arc_rotation_property)
        break;
    }
  }
  if (!s_arc_rotation_property && mono_class_get_methods &&
      mono_method_get_name) {
    for (MonoClass *klass : {s_label_class, s_pui_widget_class}) {
      void *iterator = nullptr;
      unsigned logged = 0;
      while (klass && logged < 24) {
        MonoMethod *method = mono_class_get_methods(klass, &iterator);
        if (!method)
          break;
        const char *name = mono_method_get_name(method);
        if (name && (std::strstr(name, "Rot") ||
                     std::strstr(name, "rot") ||
                     std::strstr(name, "Angle") ||
                     std::strstr(name, "Transform") ||
                     std::strstr(name, "Matrix"))) {
          LOG_INFO("[HOME-TILES:ARC-PROBE] class=%s method=%s",
                   mono_class_get_name ? mono_class_get_name(klass) : "?",
                   name);
          ++logged;
        }
      }
    }
  }
  if (!s_arc_font) {
    s_arc_font = CreateUIFont(12, 0, 700);
    if (s_arc_font && mono_gchandle_new)
      s_arc_font_handle =
          mono_gchandle_new(s_arc_font, /*pinned=*/1);
  }
  s_get_test_id =
      method_thunk<StringGetter>(pui, s_pui_widget_class, "get_TestId", 0);
  s_get_focus_settings = method_thunk<ObjectGetter>(
      pui, s_pui_widget_class, "get_FocusCustomSettings", 0);
  s_get_parent_widget = method_thunk<ObjectGetter>(
      pui, s_pui_widget_class, "get_Parent", 0);
  s_get_render_custom_settings = method_thunk<ObjectGetter>(
      pui, s_focus_render_widget_class, "get_RenderCustomSetting", 0);
  s_get_render_target = method_thunk<ObjectGetter>(
      pui, s_focus_render_widget_class, "get_Target", 0);
  s_set_theme_color = method_thunk<ThemeColorSetter>(
      pui, s_focus_element_class, "set_ThemeColor", 1);
  s_set_focus_image_color = method_thunk<FocusColorSetter>(
      pui, s_focus_settings_class, "set_FocusImageFilterColor", 1);
  s_set_line_color = method_thunk<FocusColorSetter>(
      pui, s_focus_settings_class, "set_LineFilterColor", 1);
  if (mono_class_get_field_from_name) {
    s_focus_image_color_field = mono_class_get_field_from_name(
        s_focus_settings_class, "<FocusImageFilterColor>k__BackingField");
    s_line_color_field = mono_class_get_field_from_name(
        s_focus_settings_class, "<LineFilterColor>k__BackingField");
    s_render_line_element_field = mono_class_get_field_from_name(
        s_focus_render_widget_class, "lineFocusElement");
    s_render_area_element_field = mono_class_get_field_from_name(
        s_focus_render_widget_class, "areaFocusElement");
    s_render_target_field = mono_class_get_field_from_name(
        s_focus_render_widget_class, "<Target>k__BackingField");
  }
  s_set_rounded_radius = method_thunk<RadiusSetter>(
      pui, s_focus_settings_class, "set_RoundedCornerRadius", 1);
  const uint64_t update =
      Get_Address_of_Method(react_pui, s_react_focus_class, "OnUpdate", 0);
  const uint64_t sync_render_param = Get_Address_of_Method(
      pui, s_focus_render_widget_class, "SyncRenderParam", 0);
  const bool resolved = s_react_focus_class && s_pui_widget_class &&
                        s_focus_settings_class && s_get_test_id &&
                        s_get_parent_widget &&
                        s_get_focus_settings && s_set_focus_image_color &&
                        s_set_line_color &&
                        s_focus_element_class && s_focus_render_widget_class &&
                        s_get_render_custom_settings && s_get_render_target &&
                        s_set_theme_color &&
                        s_render_line_element_field &&
                        s_render_area_element_field && s_render_target_field &&
                        s_set_rounded_radius && update && sync_render_param;
  if (!resolved) {
    LOG_ERROR("[HOME-TILES:FOCUS] runtime resolve failed react=%d widget=%d "
              "settings=%d testId=%d parent=%d getSettings=%d focusColor=%d "
              "lineColor=%d renderWidget=%d renderSettings=%d renderTarget=%d "
              "themeColor=%d elements=%d target=%d radius=%d update=%d "
              "sync=%d",
              s_react_focus_class ? 1 : 0, s_pui_widget_class ? 1 : 0,
              s_focus_settings_class ? 1 : 0, s_get_test_id ? 1 : 0,
              s_get_parent_widget ? 1 : 0,
              s_get_focus_settings ? 1 : 0,
              s_set_focus_image_color ? 1 : 0, s_set_line_color ? 1 : 0,
              s_focus_render_widget_class ? 1 : 0,
              s_get_render_custom_settings ? 1 : 0,
              s_get_render_target ? 1 : 0,
              s_set_theme_color ? 1 : 0,
              (s_render_line_element_field && s_render_area_element_field)
                  ? 1
                  : 0,
              s_render_target_field ? 1 : 0,
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
  LOG_INFO("[HOME-TILES:ARC] label positioning=absolute-xy pinned=1 rotation=%s",
           s_arc_rotation_property ? s_arc_rotation_property : "baseline-only");
}

void Tick() {
  if (!s_installed || s_layers.empty())
    return;
  const uint64_t now = monotonic_millis();
  if (s_last_frame_ms && now - s_last_frame_ms < kFrameIntervalMs)
    return;
  s_last_frame_ms = now;
  const bool enabled = outline_enabled();
  if (!enabled && !s_last_enabled)
    return;
  if (!s_animation_started_ms)
    s_animation_started_ms = now;

  uint64_t signature = 1469598103934665603ULL;
  const ThemeConfig config = outline_config();
  const int signature_values[] = {
      enabled ? 1 : 0,
      g_settings.shell_tiles_outline_sync_background ? 1 : 0,
      static_cast<int>(config.preset),
      static_cast<int>(config.animation),
      static_cast<int>(std::lround(config.animation_speed * 100.0f)),
      static_cast<int>(std::lround(config.glow.r * 255.0f)),
      static_cast<int>(std::lround(config.glow.g * 255.0f)),
      static_cast<int>(std::lround(config.glow.b * 255.0f)),
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
             g_settings.shell_tiles_outline_sync_background
                 ? "shell-background"
                 : "independent-outline",
             static_cast<int>(config.preset),
             static_cast<int>(config.animation),
             static_cast<int>(std::lround(config.animation_speed * 100.0f)),
             static_cast<int>(std::lround(config.glow.r * 255.0f)),
             static_cast<int>(std::lround(config.glow.g * 255.0f)),
             static_cast<int>(std::lround(config.glow.b * 255.0f)),
             s_layers.size());
    s_last_config_signature = signature;
  }
  const RGB color = current_outline_color();
  for (size_t i = 0; i < s_layers.size();) {
    MonoObject *settings = s_handles[i] && mono_gchandle_get_target
                               ? mono_gchandle_get_target(s_handles[i])
                               : s_layers[i];
    if (!settings) {
      if (s_handles[i] && mono_gchandle_free)
        mono_gchandle_free(s_handles[i]);
      s_layers.erase(s_layers.begin() + static_cast<long>(i));
      s_handles.erase(s_handles.begin() + static_cast<long>(i));
      s_tile_layers.erase(s_tile_layers.begin() + static_cast<long>(i));
      continue;
    }
    if (s_tile_layers[i])
      apply_radius(settings);
    apply_color(settings, color, color, enabled);
    verify_color(settings, signature);
    ++i;
  }
  s_last_enabled = enabled;
}

void RememberArcBadge(MonoObject *raw_text, const char *title_id,
                      const char *version, bool show_title_id,
                      bool show_version) {
  if (!raw_text || s_arc_badges.size() >= kMaxArcNodes)
    return;
  ArcBadge badge;
  if (show_title_id && title_id)
    badge.title_id = title_id;
  if (show_version && version)
    badge.version = version;
  const bool first_seen = s_arc_badges.find(raw_text) == s_arc_badges.end();
  s_arc_badges[raw_text] = badge;
  if (first_seen)
    s_pending_arc_badges.push_back(badge);

  // SetText is often called after AddChildAt has already built the shadow
  // tree, so propagating only from AddChildAt misses the owning tile.  Walk
  // the already-existing ancestry immediately and tag every reachable node.
  MonoObject *node = raw_text;
  unsigned mapped_depth = 0;
  for (; node && mapped_depth < 20 && s_arc_badges.size() < kMaxArcNodes;
       ++mapped_depth) {
    s_arc_badges[node] = badge;
    MonoObject *parent = parent_of(node);
    if (!parent || parent == node)
      break;
    node = parent;
  }
  if (first_seen) {
    LOG_INFO("[HOME-TILES:ARC] remembered title='%s' version='%s' raw=%p "
             "ancestry=%u pending=%zu",
             badge.title_id.c_str(), badge.version.c_str(),
             static_cast<void *>(raw_text), mapped_depth + 1,
             s_pending_arc_badges.size());
  }
}

void PropagateArcBadge(MonoObject *parent, MonoObject *child) {
  if (!parent || !child || s_arc_badges.size() >= kMaxArcNodes)
    return;
  const auto badge = s_arc_badges.find(child);
  if (badge != s_arc_badges.end())
    s_arc_badges[parent] = badge->second;
}

MonoString *DecorateSpaceSwitcherLabel(MonoObject *label, MonoString *text) {
  if (!label || !text)
    return text;
  const std::string original = Mono_to_String(text);
  static constexpr auto &kGamesSymbol = HomeTabs::kGamesGlyph; // U+EFCE
  static constexpr auto &kMediaSymbol = HomeTabs::kMediaGlyph; // U+F871
  /* Already carrying a glyph: ours, or the Emulators caption that stands in
     on the Games tab while that tab is showing. */
  if (original.empty() ||
      original.compare(0, sizeof(kGamesSymbol) - 1, kGamesSymbol) == 0 ||
      original.compare(0, sizeof(kMediaSymbol) - 1, kMediaSymbol) == 0 ||
      original.compare(0, sizeof(HomeTabs::kEmulatorsGlyph) - 1,
                       HomeTabs::kEmulatorsGlyph) == 0)
    return text;

  const std::string test_id = space_switcher_ancestor(label);
  const char *symbol = nullptr;
  if (test_id.find("space-switcher-game") != std::string::npos) {
    symbol = kGamesSymbol;
  } else if (test_id.find("space-switcher-media") != std::string::npos) {
    symbol = kMediaSymbol;
  } else if (test_id == "experience-switcher-focus-layer" &&
             bind_space_switcher_label(label, original)) {
    // Some HomeUI builds expose only the shared switcher focus-layer ID.
    // Bind its two child labels at runtime; stock order is Games then Media.
    const int index = space_switcher_label_index(label);
    if (index == 0)
      symbol = kGamesSymbol;
    else if (index == 1)
      symbol = kMediaSymbol;
  }
  if (!symbol) {
    int tab = localized_space_tab(original);
    /* the bookmark's caption is the user's name as typed, which may be a
       stock tab's word ("Games", "Gry", "Media"): it is not that tab */
    if (tab >= 0 && original == HomeBookmark::Name())
      tab = -1;
    if (tab == 0)
      symbol = kGamesSymbol;
    else if (tab == 1)
      symbol = kMediaSymbol;
    if (symbol) {
      LOG_INFO("[HOME-SPACES:NATIVE] semantic text-label tab=%s text='%s' "
               "label=%p",
               tab == 0 ? "games" : "media", original.c_str(),
               static_cast<void *>(label));
    }
  }
  if (!symbol)
    return text;

  remember_space_tab_label_layout(label);
  /* A name of the user's for the tab, in a label found in Home's tab bar
     (home_tabs; Search shows the same words): a new label gets it as soon
     as it is found there, a known one at once. */
  const std::string named = HomeTabs::TabLabelText(
      label,
      symbol == kGamesSymbol ? HomeTabs::StockTab::Games
                             : HomeTabs::StockTab::Media,
      original);
  /* tab icons switched off: the system's caption as it is */
  if (named.empty() && !HomeTabs::IconsShown())
    return text;
  MonoString *replacement = managed_string(
      named.empty() ? HomeTabs::Caption(symbol, original) : named);
  if (!replacement)
    return text;
  LOG_INFO("[HOME-SPACES:NATIVE] decorated match='%s' symbol=U+%04X "
           "text='%s' label=%p",
           test_id.c_str(),
           static_cast<unsigned>(symbol == kGamesSymbol ? 0xefce : 0xf871),
           original.c_str(), static_cast<void *>(label));
  return replacement;
}

bool IsSpaceSwitcherTabLabel(const std::string &text) {
  /* without the icons there is nothing to fit or put back on the label */
  return HomeTabs::IconsShown() && localized_space_tab(text) >= 0;
}

bool IsSpaceSwitcherMediaTabLabel(const std::string &text) {
  if (localized_space_tab(text) == 1)
    return true;
  static constexpr auto &kMediaSymbol = HomeTabs::kMediaGlyph; // U+F871
  if (text.compare(0, sizeof(kMediaSymbol) - 1, kMediaSymbol) != 0)
    return false;
  size_t label_start = sizeof(kMediaSymbol) - 1;
  while (label_start < text.size() &&
         (text[label_start] == ' ' || text[label_start] == '\t'))
    ++label_start;
  return localized_space_tab(text.substr(label_start)) == 1;
}

void UpdateSpaceSwitcherFocusBounds(MonoObject *label,
                                    const std::string &text) {
  if (!label || !s_pui_widget_class || !s_focus_settings_class ||
      !mono_gchandle_get_target || s_space_tab_label_layouts.empty() ||
      s_space_tab_focus_paddings.empty())
    return;

  static constexpr auto &kGamesSymbol = HomeTabs::kGamesGlyph; // U+EFCE
  static constexpr auto &kMediaSymbol = HomeTabs::kMediaGlyph; // U+F871
  const char *symbol = nullptr;
  int expected_tab = -1;
  if (text.compare(0, sizeof(kGamesSymbol) - 1, kGamesSymbol) == 0) {
    symbol = kGamesSymbol;
    expected_tab = 0;
  } else if (text.compare(0, sizeof(kMediaSymbol) - 1, kMediaSymbol) == 0) {
    symbol = kMediaSymbol;
    expected_tab = 1;
  }
  if (!symbol)
    return;
  size_t stock_start = sizeof(kGamesSymbol) - 1;
  if (expected_tab == 1)
    stock_start = sizeof(kMediaSymbol) - 1;
  if (stock_start < text.size() &&
      (text[stock_start] == ' ' || text[stock_start] == '\t'))
    ++stock_start;
  if (localized_space_tab(text.substr(stock_start)) != expected_tab)
    return;

  bool tracked_label = false;
  for (const SpaceTabLabelLayout &layout : s_space_tab_label_layouts) {
    if (layout.handle && mono_gchandle_get_target(layout.handle) == label) {
      tracked_label = layout.decorated;
      break;
    }
  }
  if (!tracked_label)
    return;

  int extra_left = 0;
  int extra_right = 0;
  for (auto it = s_space_tab_label_layouts.begin();
       it != s_space_tab_label_layouts.end();) {
    MonoObject *target = it->handle ? mono_gchandle_get_target(it->handle)
                                    : nullptr;
    if (!target) {
      if (it->handle && mono_gchandle_free)
        mono_gchandle_free(it->handle);
      it = s_space_tab_label_layouts.erase(it);
      continue;
    }
    if (it->decorated) {
      const float x = Get_Property<float>(s_pui_widget_class, target, "X");
      const float width =
          Get_Property<float>(s_pui_widget_class, target, "Width");
      const float stock_right = it->stock_x + it->stock_width;
      const float current_right = x + width;
      const float left_growth = std::clamp(it->stock_x - x, 0.0f, 512.0f);
      const float right_growth =
          std::clamp(current_right - stock_right, 0.0f, 512.0f);
      extra_left = std::max(extra_left,
                            static_cast<int>(std::ceil(left_growth)));
      extra_right = std::max(extra_right,
                             static_cast<int>(std::ceil(right_growth)));
    }
    ++it;
  }

  for (auto it = s_space_tab_focus_paddings.begin();
       it != s_space_tab_focus_paddings.end();) {
    MonoObject *settings = it->handle ? mono_gchandle_get_target(it->handle)
                                      : nullptr;
    if (!settings) {
      if (it->handle && mono_gchandle_free)
        mono_gchandle_free(it->handle);
      it = s_space_tab_focus_paddings.erase(it);
      continue;
    }
    const int left = it->stock_left + extra_left;
    const int right = it->stock_right + extra_right;
    if (left != it->applied_left || right != it->applied_right) {
      Set_Property(s_focus_settings_class, settings, "FocusImagePaddingLeft",
                   left);
      Set_Property(s_focus_settings_class, settings, "FocusImagePaddingRight",
                   right);
      it->applied_left = left;
      it->applied_right = right;
      LOG_INFO("[HOME-SPACES:FOCUS] symbol bounds label=%p settings=%p "
               "growth-left=%d growth-right=%d padding-left=%d "
               "padding-right=%d",
               static_cast<void *>(label), static_cast<void *>(settings),
               extra_left, extra_right, left, right);
    }
    ++it;
  }
}

void ClearTrackedLayers() {
  if (mono_gchandle_free) {
    for (uint32_t handle : s_handles)
      if (handle)
        mono_gchandle_free(handle);
    for (uint32_t handle : s_arc_glyph_handles)
      if (handle)
        mono_gchandle_free(handle);
    for (uint32_t handle : s_space_switcher_root_handles)
      if (handle)
        mono_gchandle_free(handle);
    for (uint32_t handle : s_space_switcher_focus_handles)
      if (handle)
        mono_gchandle_free(handle);
    for (uint32_t handle : s_space_switcher_label_handles)
      if (handle)
        mono_gchandle_free(handle);
    for (const SpaceTabLabelLayout &layout : s_space_tab_label_layouts)
      if (layout.handle)
        mono_gchandle_free(layout.handle);
    for (const SpaceTabFocusPadding &padding : s_space_tab_focus_paddings)
      if (padding.handle)
        mono_gchandle_free(padding.handle);
  }
  s_handles.clear();
  s_layers.clear();
  s_tile_layers.clear();
  s_arc_badges.clear();
  s_arc_rendered.clear();
  s_pending_arc_badges.clear();
  s_arc_glyph_handles.clear();
  s_space_switcher_root_handles.clear();
  s_space_switcher_focus_handles.clear();
  s_space_switcher_label_handles.clear();
  s_space_tab_label_layouts.clear();
  s_space_tab_focus_paddings.clear();
  s_logged_space_switchers.clear();
  s_last_frame_ms = 0;
  s_last_config_signature = UINT64_MAX;
  s_last_verified_signature = UINT64_MAX;
  s_animation_started_ms = 0;
  s_outline_animation_mode = AnimationMode::Count;
  s_sync_probe_count = 0;
  s_logged_shader_targets.clear();
}

} // namespace AniketosTheme::HomeTiles
