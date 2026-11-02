#include "home_video_internal.hpp"

#include "aniketos_shell/core/host_scene.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_ui/pui_primitives.hpp"
#include "aniketos_ui/ui_color.hpp"
#include "hooked_funcs.hpp"
#include "shellui_types.hpp"

#include <onion/log.h>

#include <algorithm>
#include <cstring>

namespace HomeVideo {

/* The overload whose parameter types mention `type_text`. */
MonoMethod *overload_of(MonoClass *klass, const char *name, int parameters,
                        const char *type_text) {
  void *iter = nullptr;
  while (MonoMethod *method = mono_class_get_methods(klass, &iter)) {
    if (std::strcmp(mono_method_get_name(method), name))
      continue;
    MonoMethodSignature *signature = mono_method_signature(method);
    if (!signature ||
        static_cast<int>(mono_signature_get_param_count(signature)) !=
            parameters)
      continue;
    void *param_iter = nullptr;
    while (MonoType *type = mono_signature_get_params(signature, &param_iter)) {
      char *text = mono_type_get_name(type);
      const bool match = text && std::strstr(text, type_text);
      if (text && mono_free)
        mono_free(text);
      if (match)
        return method;
    }
  }
  return nullptr;
}

bool invoke(MonoObject *object, MonoMethod *method, void **arguments,
            MonoObject **result) {
  if (!object || !method)
    return false;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(method, object, arguments, &exception);
  if (result)
    *result = value;
  return !exception;
}

bool call(MonoObject *object, const char *name, int parameters,
          void **arguments, MonoObject **result) {
  if (!object)
    return false;
  MonoMethod *method = Reflect::MethodOf(object, name, parameters);
  if (!method) {
    LOG_ERROR("[HOMEVIDEO] no method %s/%d", name, parameters);
    return false;
  }
  if (!invoke(object, method, arguments, result)) {
    LOG_ERROR("[HOMEVIDEO] %s threw", name);
    return false;
  }
  return true;
}

MonoObject *object_property(MonoObject *object, const char *getter) {
  MonoObject *value = nullptr;
  return invoke(object, Reflect::MethodOf(object, getter, 0), nullptr, &value)
             ? value
             : nullptr;
}

/* An object with a parameterless constructor. */
MonoObject *make(MonoClass *klass) {
  if (!klass || !mono_class_get_method_from_name(klass, ".ctor", 0))
    return nullptr;
  MonoObject *object = mono_object_new(mono_domain_get(), klass);
  if (object)
    mono_runtime_object_init(object);
  return object;
}

MonoClass *ui3_class(const char *name) {
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  return pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", name)
             : nullptr;
}

/* `size` stands in for a side that cannot be read. */
ScreenSize size_of(MonoObject *root, ScreenSize size) {
  Reflect::FloatProperty(root, "get_Width", &size.width);
  Reflect::FloatProperty(root, "get_Height", &size.height);
  if (size.width < 1.0f || size.height < 1.0f) /* a layer may not say */
    return {1920.0f, 1080.0f};
  return size;
}

/* What every full-screen widget starts with: sized, named so it can be found,
   scaled if `scale` is given, never focused. Goes through them all; false if
   one did not take. */
bool prepare_layer(MonoObject *widget, ScreenSize size, const char *name,
                   const char *scale) {
  void *width_arguments[1] = {&size.width};
  void *height_arguments[1] = {&size.height};
  void *name_arguments[1] = {mono_string_new(mono_domain_get(), name)};
  bool no = false;
  void *focus_arguments[1] = {&no};
  bool ok = call(widget, "set_Width", 1, width_arguments);
  ok &= call(widget, "set_Height", 1, height_arguments);
  ok &= call(widget, "set_Name", 1, name_arguments);
  if (scale)
    set_scale(widget, scale);
  if (Reflect::MethodOf(widget, "set_Focusable", 1))
    ok &= call(widget, "set_Focusable", 1, focus_arguments);
  return ok;
}

/* An ImageScaleType member on a video view or an image box. */
void set_scale(MonoObject *widget, const char *member) {
  int32_t scale = 0;
  if (Reflect::EnumValue(ui3_class("ImageScaleType"), member, &scale)) {
    void *arguments[1] = {&scale};
    call(widget, "set_ImageScaleType", 1, arguments);
  }
}

/* The video or the picture goes under everything else on the stage. */
bool put_first(MonoObject *root, MonoObject *widget) {
  MonoObject *first = object_property(root, "get_FirstChild");
  if (first == widget)
    return true;
  if (object_property(widget, "get_Parent"))
    call(widget, "RemoveFromParent", 0);
  first = object_property(root, "get_FirstChild");
  if (!first)
    return call(root, "AppendChild", 1, reinterpret_cast<void **>(&widget));
  void *arguments[2] = {widget, first};
  return call(root, "InsertChildBelow", 2, arguments);
}

void remove_widget(MonoObject *widget) {
  if (!widget || HomeScene::IsDisposed(widget))
    return;
  call(widget, "RemoveFromParent", 0);
  call(widget, "Dispose", 0);
}

namespace {

using AniketosShell::FocusLayer;

/* ---- the stage ---------------------------------------------------------
   The video and the picture are not in Home's scene. The system draws the
   background of Home and of the system apps (Settings and the others) in a
   scene of its own, BGLayerPlugin (Sce.Vsh.ShellUI.BGLayer), which stays
   up while those screens fade and slide over it. A panel of ours appended
   to its root holds the video, the picture and the effects, above the
   system's own images, so the video plays on through every change of
   screen instead of going with Home. (That is where LiveShell puts its
   video too.) ShellUI outlives a reload of this module, so a panel an
   earlier instance left is removed first. */
constexpr const char *kStageName = "id_aniketos_home_stage";
uint32_t s_stage = 0; /* strong: the panel */
int s_stage_retry_at = 0;

bool s_shown = true; /* the stage is (or is on its way to be) on screen */
float s_stage_opacity = 1.0f;
bool s_stage_visible = true;
FocusLayer s_focus = FocusLayer::Unknown;
int s_focus_frame = 0; /* frame the focus last changed */

MonoObject *background_root() {
  MonoImage *image = getDLLimage("Sce.Vsh.ShellUI.BGLayer.dll");
  MonoClass *plugin_class =
      image ? mono_class_from_name(image, "Sce.Vsh.ShellUI.BGLayer",
                                   "BGLayerPlugin")
            : nullptr;
  MonoObject *plugin =
      plugin_class ? Get_Property<MonoObject *>(plugin_class, nullptr, "Instance")
                   : nullptr;
  MonoObject *root = object_property(plugin, "get_RootWidget");
  return root && !HomeScene::IsDisposed(root) ? root : nullptr;
}

void remove_stale(MonoObject *root) {
  MonoObject *child = object_property(root, "get_FirstChild");
  for (int guard = 0; child && guard < 64; ++guard) {
    MonoObject *next = object_property(child, "get_NextSibling");
    if (HomeScene::PropertyText(child, "Name") == kStageName) {
      remove_widget(child);
      LOG_INFO("[HOMEVIDEO] a stage of an earlier instance removed");
    }
    child = next;
  }
}

/* Without the focus field all there is to go by is Home's own opacity. */
bool home_in_sight() {
  MonoObject *home = HomeScene::Root();
  float opacity = 1.0f;
  return home && (!Reflect::FloatProperty(home, "get_FinalOpacity", &opacity) ||
                  opacity > 0.05f);
}

} // namespace

MonoObject *stage() {
  MonoObject *panel = Reflect::Target(s_stage);
  if (panel && !HomeScene::IsDisposed(panel))
    return panel;
  AniketosUI::Pui::Disown(panel);
  Reflect::Release(s_stage);
  if (frame_count < s_stage_retry_at)
    return nullptr;
  MonoObject *root = background_root();
  MonoClass *panel_class = ui3_class("Panel");
  if (!root || !panel_class)
    return nullptr; /* ShellUI still starting */
  remove_stale(root);
  panel = make(panel_class);
  const ScreenSize size = size_of(root, {0.0f, 0.0f});
  void *panel_arguments[1] = {panel};
  if (!panel || !prepare_layer(panel, size, kStageName) ||
      !call(root, "AppendChild", 1, panel_arguments)) {
    LOG_ERROR("[HOMEVIDEO] no stage in the background layer");
    s_stage_retry_at = frame_count + 600;
    return nullptr;
  }
  /* nothing of its own on screen: the video and the picture are its
     children */
  AniketosUI::UIColor clear{0.0f, 0.0f, 0.0f, 0.0f, 0};
  void *color_arguments[1] = {&clear};
  call(panel, "set_BackgroundColor", 1, color_arguments);
  AniketosUI::Pui::Adopt(panel); /* ours: not for the system-panel theme */
  s_stage = mono_gchandle_new(panel, /*pinned=*/1);
  /* a new panel is on screen, whatever was left of the last one's fade */
  s_stage_opacity = 1.0f;
  s_stage_visible = true;
  LOG_INFO("[HOMEVIDEO] stage added to the background layer (%.0fx%.0f)",
           size.width, size.height);
  return panel;
}

bool stage_wanted() { return s_shown; }

/* Every frame. The stage shows while the focus is on Home (the TopMenu layer),
   and with home_background_global on a system app (the ShellApp layer: Settings
   and the others) as well; a game in front (the Game layer, or none) must never
   have our video over it. The stage fades: out over about a second (quicker
   for a game), in over half a second, as Home's own screens do, so nothing
   cuts. Hidden all the way it is made invisible as well, which pauses the
   player with it. */
void tick_stage() {
  MonoObject *panel = Reflect::Target(s_stage);
  if (!panel || HomeScene::IsDisposed(panel))
    return;
  const FocusLayer focus = AniketosShell::CurrentFocusLayer();
  if (focus != s_focus) {
    LOG_INFO("[HOMEVIDEO] focus layer %d -> %d", static_cast<int>(s_focus),
             static_cast<int>(focus));
    s_focus = focus;
    s_focus_frame = frame_count;
  }
  const bool wanted =
      focus == FocusLayer::TopMenu ||
      (focus == FocusLayer::ShellApp && AniketosTheme::CustomizationOn() &&
       g_settings.home_background_global) ||
      (focus == FocusLayer::Unknown && home_in_sight());
  s_shown = wanted;
  float opacity = s_stage_opacity;
  if (wanted) {
    opacity = std::min(1.0f, opacity + 1.0f / 30.0f);
  } else if (focus == FocusLayer::ShellApp) {
    /* Settings is drawn a few frames after the focus moves */
    if (frame_count - s_focus_frame >= 9)
      opacity = std::max(0.0f, opacity - 1.0f / 60.0f);
  } else {
    opacity = std::max(0.0f, opacity - 1.0f / 45.0f);
  }
  if (opacity == s_stage_opacity && (opacity > 0.0f) == s_stage_visible)
    return;
  bool visible = opacity > 0.0f;
  if (visible != s_stage_visible) {
    void *arguments[1] = {&visible};
    call(panel, "set_Visible", 1, arguments);
    s_stage_visible = visible;
    LOG_INFO("[HOMEVIDEO] stage %s", visible ? "shown" : "hidden");
  }
  void *arguments[1] = {&opacity};
  call(panel, "set_Opacity", 1, arguments);
  s_stage_opacity = opacity;
}

} // namespace HomeVideo
