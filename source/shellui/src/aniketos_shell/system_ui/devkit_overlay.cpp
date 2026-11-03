#include "aniketos_shell/system_ui/devkit_overlay.hpp"

#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/pui_primitives.hpp"
#include "aniketos_ui/ui_color.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>

#include <string>

namespace DevkitOverlay {
namespace {

namespace Reflect = AniketosUI::Reflect;

constexpr const char *kName = "id_anivsh_devkit_overlay";
/* The banner's text and colour, as the widget's own update sets them when
   the kit's activation has run out (<updateDevKitPanel>b__1). */
constexpr const char *kExpiredText =
    "This DevKit/TestKit is Expired. See Activation Guide on DevNet.";
constexpr float kExpiredRed = 0.86f;
constexpr float kExpiredInset = 24.0f;
/* The widget's own update timer is compiled out on retail; its job reads
   the values again this often. */
constexpr int kUpdateFrames = 60 * 30;

MonoClass *s_view_class = nullptr;
MonoMethod *s_view_ctor = nullptr;
MonoMethod *s_create_devkit = nullptr;
MonoMethod *s_create_expired = nullptr;
MonoMethod *s_update = nullptr;
MonoClassField *s_panel_info = nullptr;
MonoClassField *s_panel_expired = nullptr;
MonoClassField *s_panel_trial = nullptr;
MonoClassField *s_label_expired = nullptr;

uint32_t s_view = 0; /* strong: our DebugInfoView */
uint32_t s_root = 0; /* weak: the Home root it was put under */
int s_frame = 0;
int s_next_update = 0;
int s_retry_at = 0;
bool s_refresh = true;

bool invoke(MonoObject *object, MonoMethod *method, const char *what) {
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, object, nullptr, &exception);
  if (exception)
    LOG_ERROR("[DEVKIT] %s threw", what);
  return !exception;
}

MonoObject *field(MonoObject *object, MonoClassField *which) {
  MonoObject *value = nullptr;
  if (object && which)
    mono_field_get_value(object, which, &value);
  return value && !AniketosUI::HomeScene::IsDisposed(value) ? value : nullptr;
}

void set_visible(MonoObject *widget, bool visible) {
  void *arguments[1] = {&visible};
  Reflect::Call(widget, "set_Visible", 1, arguments);
}

void set_opacity(MonoObject *widget, float opacity) {
  void *arguments[1] = {&opacity};
  Reflect::Call(widget, "set_Opacity", 1, arguments);
}

std::string label_text(MonoObject *label) {
  MonoObject *value = nullptr;
  if (!Reflect::Call(label, "get_Text", 0, nullptr, &value))
    return {};
  return Mono_to_String(reinterpret_cast<MonoString *>(value));
}

bool wanted() {
  return g_settings.devkit_overlay_info || g_settings.devkit_overlay_expired;
}

void drop() {
  if (MonoObject *view = Reflect::Target(s_view)) {
    for (MonoClassField *which :
         {s_panel_info, s_panel_expired, s_panel_trial})
      if (MonoObject *panel = field(view, which))
        AniketosUI::Pui::Disown(panel);
    if (!AniketosUI::HomeScene::IsDisposed(view)) {
      Reflect::Call(view, "RemoveFromParent", 0);
      Reflect::Call(view, "Dispose", 0);
    }
    LOG_INFO("[DEVKIT] overlay removed");
  }
  Reflect::Release(s_view);
  Reflect::Release(s_root);
}

/* Our widget under Home's root (beside React, never inside it), with the
   panels a DevKit builds. */
MonoObject *view() {
  MonoObject *root = AniketosUI::HomeScene::Root();
  MonoObject *view = Reflect::Target(s_view);
  if (view &&
      (AniketosUI::HomeScene::IsDisposed(view) || Reflect::Target(s_root) != root)) {
    drop();
    view = nullptr;
  }
  if (view || !root || s_frame < s_retry_at)
    return view;
  s_retry_at = s_frame + 600;
  view = mono_object_new(mono_domain_get(), s_view_class);
  if (!view)
    return nullptr;
  if (!invoke(view, s_view_ctor, "DebugInfoView()"))
    return nullptr;
  MonoString *name = mono_string_new(mono_domain_get(), kName);
  Reflect::Call(view, "set_Name", 1, reinterpret_cast<void **>(&name));
  if (!invoke(view, s_create_devkit, "createDevKitPanel") ||
      !invoke(view, s_create_expired, "createExpiredPanel") ||
      !Reflect::Call(root, "AppendChild", 1, reinterpret_cast<void **>(&view))) {
    Reflect::Call(view, "Dispose", 0);
    return nullptr;
  }
  /* the system's look, not the theme's */
  for (MonoClassField *which : {s_panel_info, s_panel_expired, s_panel_trial})
    if (MonoObject *panel = field(view, which))
      AniketosUI::Pui::Adopt(panel);
  s_view = mono_gchandle_new(view, /*pinned=*/1);
  Reflect::Hold(s_root, root);
  s_next_update = s_frame;
  s_refresh = true;
  LOG_INFO("[DEVKIT] overlay added info=%d expired=%d",
           g_settings.devkit_overlay_info ? 1 : 0,
           g_settings.devkit_overlay_expired ? 1 : 0);
  return view;
}

/* What the widget's update does for an expired kit: the banner in red,
   opaque, with its text. Kept every frame, as the update (which on a
   retail console cannot read the activation) hides it again. */
void show_expired(MonoObject *panel, MonoObject *label) {
  set_opacity(panel, 1.0f);
  if (!label)
    return;
  if (label_text(label) != kExpiredText) {
    AniketosUI::UIColor red{kExpiredRed, 0.0f, 0.0f, kExpiredRed, 0};
    void *color[1] = {&red};
    Reflect::Call(panel, "set_BackgroundColor", 1, color);
    MonoString *text = mono_string_new(mono_domain_get(), kExpiredText);
    Reflect::Call(label, "set_Text", 1, reinterpret_cast<void **>(&text));
  }
  /* centred at the top: the text and 24 px each side */
  float text_width = 0.0f, width = 0.0f, screen = 1920.0f;
  if (!Reflect::FloatProperty(label, "get_Width", &text_width) ||
      text_width <= 0.0f)
    return;
  Reflect::FloatProperty(panel, "get_Width", &width);
  Reflect::FloatProperty(AniketosUI::HomeScene::Root(), "get_Width", &screen);
  float wanted_width = text_width + 2.0f * kExpiredInset;
  if (width != wanted_width) {
    float x = (screen - wanted_width) * 0.5f;
    void *width_argument[1] = {&wanted_width};
    void *x_argument[1] = {&x};
    Reflect::Call(panel, "set_Width", 1, width_argument);
    Reflect::Call(panel, "set_X", 1, x_argument);
  }
}

} // namespace

void Install() {
  MonoImage *image = getDLLimage("Sce.Vsh.ShellUI.ReactNativeShellApp.dll");
  s_view_class = image ? mono_class_from_name(
                             image, "ReactNative.Components.ShellUI.HomeUI",
                             "DebugInfoView")
                       : nullptr;
  if (s_view_class) {
    s_view_ctor = mono_class_get_method_from_name(s_view_class, ".ctor", 0);
    s_create_devkit =
        mono_class_get_method_from_name(s_view_class, "createDevKitPanel", 0);
    s_create_expired =
        mono_class_get_method_from_name(s_view_class, "createExpiredPanel", 0);
    s_update =
        mono_class_get_method_from_name(s_view_class, "updateDevKitPanel", 0);
    s_panel_info = mono_class_get_field_from_name(s_view_class, "m_panel");
    s_panel_expired =
        mono_class_get_field_from_name(s_view_class, "m_panel_expired");
    s_panel_trial =
        mono_class_get_field_from_name(s_view_class, "m_panel_expiredpup");
    s_label_expired =
        mono_class_get_field_from_name(s_view_class, "m_label_expiredtext");
  }
  const bool ready = s_view_ctor && s_create_devkit && s_create_expired &&
                     s_update && s_panel_info && s_panel_expired &&
                     s_label_expired;
  LOG_INFO("[DEVKIT] DebugInfoView=%p ready=%d",
           static_cast<void *>(s_view_class), ready ? 1 : 0);
  if (!ready)
    s_view_class = nullptr;
}

void Refresh() { s_refresh = true; }

void Tick() {
  ++s_frame;
  if (!s_view_class)
    return;
  if (!wanted()) {
    if (s_view)
      drop();
    return;
  }
  MonoObject *widget = view();
  if (!widget)
    return;
  if (s_frame >= s_next_update) {
    s_next_update = s_frame + kUpdateFrames;
    invoke(widget, s_update, "updateDevKitPanel");
  }
  MonoObject *info = field(widget, s_panel_info);
  MonoObject *expired = field(widget, s_panel_expired);
  if (s_refresh) {
    s_refresh = false;
    set_visible(info, g_settings.devkit_overlay_info);
    set_visible(expired, g_settings.devkit_overlay_expired);
    set_visible(field(widget, s_panel_trial), false);
  }
  if (g_settings.devkit_overlay_expired && expired)
    show_expired(expired, field(widget, s_label_expired));
}

} // namespace DevkitOverlay
