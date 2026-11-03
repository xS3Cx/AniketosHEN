#include "home_video_internal.hpp"

#include "aniketos_shell/core/embedded_assets.hpp"
#include "aniketos_ui/pui_primitives.hpp"
#include "aniketos_ui/ui_color.hpp"
#include "shellui_types.hpp"

#include <onion/log.h>

#include <algorithm>
#include <cstdio>

/* Full-screen layers right above the video or picture and under the rest of
 * Home (the particles, then Home's React tree): dimming and haze, black
 * panels (the panels are counted as ours, or the system-panel theme would
 * paint them its colour); a vignette and shading at the top and bottom,
 * embedded black gradients stretched over the screen. Each follows its
 * Shell Menu slider live, from 0 (no layer at all) to 100. */
namespace HomeVideo {
namespace {

enum Effect { kDim, kHaze, kVignette, kEdges, kEffectCount };
constexpr const char *kEffectNames[kEffectCount] = {"dim", "haze", "vignette",
                                                    "edges"};
/* What each gets at 100. */
constexpr float kDimOpacity = 0.85f;
constexpr float kHazeOpacity = 0.5f;

struct EffectLayer {
  uint32_t widget = 0; /* strong handle */
  int applied = -1;    /* the strength on screen */
  int failed = -1;     /* a strength the layer could not be made at */
};
EffectLayer s_effect_layers[kEffectCount];
/* put the layers in again above the video (see raise_effects()) */
bool s_raise_effects = false;

int effect_strength(int effect) {
  int strength = 0;
  switch (effect) {
  case kDim: strength = g_settings.home_bg_dim; break;
  case kHaze: strength = g_settings.home_bg_haze; break;
  case kVignette: strength = g_settings.home_bg_vignette; break;
  case kEdges: strength = g_settings.home_bg_edges; break;
  default: break;
  }
  return std::clamp(strength, 0, 100);
}

void drop_effect(int effect) {
  EffectLayer &layer = s_effect_layers[effect];
  MonoObject *widget = Reflect::Target(layer.widget);
  remove_widget(widget);
  AniketosUI::Pui::Disown(widget);
  Reflect::Release(layer.widget);
  layer.applied = -1;
}

MonoObject *build_effect(MonoObject *root, int effect) {
  const bool picture = effect == kVignette || effect == kEdges;
  MonoObject *widget = make(ui3_class(picture ? "ImageBox" : "Panel"));
  if (!widget)
    return nullptr;
  s_effect_layers[effect].widget = mono_gchandle_new(widget, /*pinned=*/1);
  char name[48];
  std::snprintf(name, sizeof(name), "id_aniketos_home_%s",
                kEffectNames[effect]);
  prepare_layer(widget, size_of(root), name);

  if (picture) {
    const char *path = HomeEffectImagePath(effect == kVignette
                                               ? HomeEffectImage::Vignette
                                               : HomeEffectImage::Edges);
    if (!path || !path[0] || !load_image(widget, path))
      return nullptr;
    /* stretched over the whole screen, whatever its shape */
    set_scale(widget, "Stretch");
    return widget;
  }

  AniketosUI::Pui::Adopt(widget);
  AniketosUI::UIColor color{0.0f, 0.0f, 0.0f, 1.0f, 0};
  void *color_arguments[1] = {&color};
  if (!call(widget, "set_BackgroundColor", 1, color_arguments))
    return nullptr;
  return widget;
}

void apply_effect(MonoObject *widget, int effect, int strength) {
  const float amount = strength / 100.0f;
  float opacity = amount * (effect == kDim    ? kDimOpacity
                            : effect == kHaze ? kHazeOpacity
                                              : 1.0f);
  void *opacity_arguments[1] = {&opacity};
  call(widget, "set_Opacity", 1, opacity_arguments);
}

/* `widget` right above `below` on the stage. */
void place_above(MonoObject *root, MonoObject *below, MonoObject *widget) {
  if (object_property(widget, "get_Parent") == root &&
      object_property(below, "get_NextSibling") == widget)
    return;
  if (object_property(widget, "get_Parent"))
    call(widget, "RemoveFromParent", 0);
  MonoObject *next = object_property(below, "get_NextSibling");
  if (!next) {
    call(root, "AppendChild", 1, reinterpret_cast<void **>(&widget));
    return;
  }
  void *arguments[2] = {widget, next};
  call(root, "InsertChildBelow", 2, arguments);
}

} // namespace

/* Once the picture is on, the view is drawn on top of its siblings until they
   are put in again (LiveShell's dim panel is raised the same way): the effects
   go back above it. */
void raise_effects() { s_raise_effects = true; }

/* Twice a second: a layer for each effect turned up, in order right above
   `base`, the video or the picture; none without one. */
void tick_effects(MonoObject *base) {
  MonoObject *root = base ? object_property(base, "get_Parent") : nullptr;
  const bool shown = base && root && !HomeScene::IsDisposed(root) &&
                     !HomeScene::IsDisposed(base) &&
                     object_property(base, "get_Parent") == root;
  MonoObject *below = base;
  for (int effect = 0; effect < kEffectCount; ++effect) {
    EffectLayer &layer = s_effect_layers[effect];
    const int strength = shown ? effect_strength(effect) : 0;
    MonoObject *widget = Reflect::Target(layer.widget);
    if (widget && HomeScene::IsDisposed(widget)) {
      /* went with Home's scene */
      AniketosUI::Pui::Disown(widget);
      Reflect::Release(layer.widget);
      layer.applied = -1;
      widget = nullptr;
    }
    if (strength == 0) {
      if (layer.widget)
        drop_effect(effect);
      layer.failed = -1;
      continue;
    }
    if (strength == layer.failed)
      continue;
    if (!widget) {
      widget = build_effect(root, effect);
      if (!widget) {
        LOG_ERROR("[HOMEFX] the %s layer could not be made",
                  kEffectNames[effect]);
        drop_effect(effect);
        layer.failed = strength;
        continue;
      }
    }
    if (layer.applied != strength) {
      apply_effect(widget, effect, strength);
      LOG_INFO("[HOMEFX] %s %d%%", kEffectNames[effect], strength);
      layer.applied = strength;
    }
    if (s_raise_effects && object_property(widget, "get_Parent"))
      call(widget, "RemoveFromParent", 0);
    place_above(root, below, widget);
    below = widget;
  }
  if (s_raise_effects)
    LOG_INFO("[HOMEFX] layers put back above the video");
  s_raise_effects = false;
}

} // namespace HomeVideo
