#include "aniketos_ui/pui_primitives.hpp"

#include "aniketos_ui/mono_reflect.hpp"
#include "hooked_funcs.hpp"
#include "aniketos_ui/mono_property_cache.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace AniketosUI::Pui {

/* Per-frame code: each managed property goes through the accessor cache, not
 * the by-name lookup of the hooked_funcs.hpp helpers. */
using MonoPropertyCache::Set;
using MonoPropertyCache::SetInvoke;

/* Shadow state.
 * The menu repaints by re-stating the geometry, colour and text of every
 * widget it owns. Most of those values did not change since the previous
 * frame, and each managed setter invalidates layout or allocates. Remember
 * the last value written through this module and skip identical writes.
 *
 * Widgets are pinned while the menu owns them, so the object address is a
 * stable key. Forget()/ForgetAll() must be called when a widget is created
 * or released, and ForgetAll() periodically so that a value changed behind
 * our back (a skin re-applied by PUI) is healed on the next repaint. */
namespace {

struct ColorShadow {
  uint32_t property = 0;
  Color color{};
};

struct WidgetShadow {
  bool has_geometry = false;
  float x = 0, y = 0, width = 0, height = 0;
  int color_count = 0;
  ColorShadow colors[3];
  bool has_text = false;
  std::string text;
};

std::unordered_map<MonoObject *, WidgetShadow> s_shadow;
std::unordered_set<MonoObject *> s_own;

uint32_t property_id(const char *name) {
  uint32_t hash = 2166136261u;
  for (; *name; ++name) {
    hash ^= static_cast<unsigned char>(*name);
    hash *= 16777619u;
  }
  return hash ? hash : 1u;
}

bool same(float a, float b) { return std::fabs(a - b) < 0.001f; }

bool same(const Color &a, const Color &b) {
  return std::fabs(a.r - b.r) < 0.0005f && std::fabs(a.g - b.g) < 0.0005f &&
         std::fabs(a.b - b.b) < 0.0005f && std::fabs(a.a - b.a) < 0.0005f;
}

/* True when (x, y, width, height) is already what the widget holds. */
bool geometry_unchanged(MonoObject *object, float x, float y, float width,
                        float height) {
  WidgetShadow &shadow = s_shadow[object];
  if (shadow.has_geometry && same(shadow.x, x) && same(shadow.y, y) &&
      same(shadow.width, width) && same(shadow.height, height))
    return true;
  shadow.has_geometry = true;
  shadow.x = x;
  shadow.y = y;
  shadow.width = width;
  shadow.height = height;
  return false;
}

bool color_unchanged(MonoObject *object, const char *property,
                     const Color &color) {
  WidgetShadow &shadow = s_shadow[object];
  const uint32_t id = property_id(property);
  for (int i = 0; i < shadow.color_count; ++i) {
    if (shadow.colors[i].property != id)
      continue;
    if (same(shadow.colors[i].color, color))
      return true;
    shadow.colors[i].color = color;
    return false;
  }
  if (shadow.color_count < 3)
    shadow.colors[shadow.color_count++] = {id, color};
  return false;
}

} // namespace

void Forget(MonoObject *object) {
  if (object)
    s_shadow.erase(object);
}

void ForgetAll() { s_shadow.clear(); }

bool Resolve(Runtime &runtime, MonoImage *image, RetainCallback retain,
             void *retain_context) {
  runtime = {};
  runtime.image = image;
  runtime.retain = retain;
  runtime.retain_context = retain_context;
  if (!image)
    return false;
  runtime.widget_base =
      mono_class_from_name(image, "Sce.PlayStation.PUI", "WidgetBase");
  runtime.widget =
      mono_class_from_name(image, "Sce.PlayStation.PUI.UI2", "Widget");
  runtime.panel =
      mono_class_from_name(image, "Sce.PlayStation.PUI.UI2", "Panel");
  runtime.label =
      mono_class_from_name(image, "Sce.PlayStation.PUI.UI2", "Label");
  runtime.image_box =
      mono_class_from_name(image, "Sce.PlayStation.PUI.UI2", "ImageBox");
  return runtime.widget && runtime.panel && runtime.label;
}

namespace {

MonoString *managed_string(const std::string &value) {
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  if (!domain)
    domain = Root_Domain;
  return domain ? mono_string_new(domain, value.c_str()) : nullptr;
}

} // namespace

void RemoveNamed(const Runtime &runtime, MonoObject *root, const char *name) {
  if (!root || !runtime.widget || !runtime.image || !name)
    return;
  MonoObject *object =
      Invoke<MonoObject *>(runtime.image, runtime.widget, root,
                           "FindWidgetByName", managed_string(name));
  if (object)
    Invoke<void>(runtime.image, runtime.widget, object, "RemoveFromParent");
}

bool IsOwn(MonoObject *object) { return object && s_own.count(object) != 0; }

void Adopt(MonoObject *object) {
  if (object)
    s_own.insert(object);
}

void Disown(MonoObject *object) {
  if (object)
    s_own.erase(object);
}

void Remove(const Runtime &, MonoObject *object) {
  /* Works for UI2 and UI3 widgets alike: take the method from the object. */
  if (!object || !mono_object_get_class || !mono_class_get_parent ||
      !mono_class_get_method_from_name || !mono_runtime_invoke)
    return;
  MonoMethod *remove = Reflect::MethodOf(object, "RemoveFromParent", 0);
  if (!remove)
    return;
  MonoObject *exception = nullptr;
  mono_runtime_invoke(remove, object, nullptr, &exception);
  Forget(object);
}

void SetColor(MonoClass *klass, MonoObject *object, const char *property,
              Color color) {
  if (!klass || !object || !property)
    return;
  if (color_unchanged(object, property, color))
    return;
  MonoObject *managed =
      MonoPropertyCache::ScratchColor(color.r, color.g, color.b, color.a);
  if (managed)
    SetInvoke(klass, object, property, managed);
}

namespace {

bool is_ui3(MonoObject *object) {
  MonoClass *klass =
      mono_object_get_class ? mono_object_get_class(object) : nullptr;
  const char *name_space = klass && mono_class_get_namespace
                               ? mono_class_get_namespace(klass)
                               : nullptr;
  return name_space && std::strcmp(name_space, "Sce.PlayStation.PUI.UI3") == 0;
}

bool append_child(const Runtime &runtime, MonoObject *parent,
                  MonoObject *child) {
  if (!parent || !child)
    return false;

  // UI2.Widget::AppendChild is the proven path for the ordinary menu tree.
  // Calling the generic base method on a UI2 object is not equivalent on all
  // PUI builds and can throw, leaving CreatePanel() with no parent. Detect the
  // actual managed parent before selecting the mixed UI2/UI3 path.
  if (!is_ui3(parent)) {
    if (!runtime.widget)
      return false;
    Widget_Append_Child(parent, child);
    return true;
  }

  // WidgetBase::AppendChild accepts both UI2.Widget and UI3.Widget.  This is
  // the only supported way to put the existing UI2 controls under the native
  // UI3 rounded surface.
  if (!mono_class_get_method_from_name || !mono_runtime_invoke)
    return false;
  /* A UI3 child goes in through the parent's own AppendChild (the one a
     scene root accepts); a UI2 child needs the mixed WidgetBase one. */
  MonoMethod *append = is_ui3(child) && mono_class_get_parent
                           ? Reflect::MethodOf(parent, "AppendChild", 1)
                           : nullptr;
  if (!append && runtime.widget_base)
    append = mono_class_get_method_from_name(runtime.widget_base, "AppendChild",
                                             1);
  if (!append)
    return false;
  void *args[1] = {child};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(append, parent, args, &exception);
  return exception == nullptr;
}

/* `own` is decided before the constructor runs: the theme hooks may see the
   widget from then on. */
MonoObject *new_widget(const Runtime &runtime, MonoClass *klass, bool own) {
  MonoObject *widget = New_Object(klass);
  if (!widget)
    return nullptr;
  Forget(widget); /* a freed widget's address may be reused */
  if (own)
    s_own.insert(widget);
  mono_runtime_object_init(widget);
  if (runtime.retain && !runtime.retain(widget, runtime.retain_context))
    return nullptr;
  return widget;
}

void place(MonoClass *klass, MonoObject *widget, const char *name, float size) {
  Set(klass, widget, "Name", managed_string(name ? name : ""));
  Set(klass, widget, "PositionType", 0);
  Set(klass, widget, "X", 0.0f);
  Set(klass, widget, "Y", 0.0f);
  Set(klass, widget, "Width", size);
  Set(klass, widget, "Height", size);
}

} // namespace

MonoObject *CreatePanel(const Runtime &runtime, MonoObject *parent,
                        const char *name) {
  if (!parent || !runtime.panel)
    return nullptr;
  MonoObject *panel = new_widget(runtime, runtime.panel, /*own=*/true);
  if (!panel)
    return nullptr;
  place(runtime.panel, panel, name, 1.0f);
  SetColor(runtime.panel, panel, "BackgroundColor", {0, 0, 0, 0});
  if (!append_child(runtime, parent, panel))
    return nullptr;
  // Applying a PUI skin during append may replace a transparent default with
  // the stock dark panel colour. These panels are custom-painted (or serve as
  // geometry-only roots), so opt out and reassert the supported transparent
  // UI2 background after parenting.
  if (runtime.widget)
    Set(runtime.widget, panel, "FollowSkinBackgroundColor", false);
  SetColor(runtime.panel, panel, "BackgroundColor", {0, 0, 0, 0});
  return panel;
}

MonoObject *CreateLabel(const Runtime &runtime, MonoObject *parent,
                        const char *name, int horizontal, int lines) {
  if (!parent || !runtime.label)
    return nullptr;
  MonoObject *label = new_widget(runtime, runtime.label, /*own=*/true);
  if (!label)
    return nullptr;
  place(runtime.label, label, name, 1.0f);
  Set(runtime.label, label, "Text", managed_string(""));
  if (runtime.font)
    SetInvoke(runtime.label, label, "Font", runtime.font);
  Set(runtime.label, label, "HorizontalAlignment", horizontal);
  Set(runtime.label, label, "VerticalAlignment", 2); // Center
  Set(runtime.label, label, "FitWidthToText", false);
  Set(runtime.label, label, "FitHeightToText", false);
  // UI2.Label on 11.40 exposes MaxLineCount, not NumberOfLines. The latter
  // silently did nothing and allowed one-line HUD labels to wrap/ellipsize.
  Set(runtime.label, label, "MaxLineCount", std::max(lines, 1));
  Set(runtime.label, label, "TextTrimming", 0); // None
  Set(runtime.label, label, "EnableThemedColor", false);
  Set(runtime.label, label, "EnableThemedShadowColor", false);
  SetColor(runtime.label, label, "TextColor", {1, 1, 1, 1});
  if (!append_child(runtime, parent, label))
    return nullptr;
  // Parenting may apply the host skin once. Keep menu labels under the
  // explicit per-theme colours used by the Shell renderer.
  Set(runtime.label, label, "EnableThemedColor", false);
  Set(runtime.label, label, "EnableThemedShadowColor", false);
  SetColor(runtime.label, label, "TextColor", {1, 1, 1, 1});
  return label;
}

MonoObject *CreateSystemImage(const Runtime &runtime, MonoObject *parent,
                              const char *name, const char *uri) {
  if (!parent || !runtime.image_box || !runtime.image || !uri)
    return nullptr;
  MonoObject *image = new_widget(runtime, runtime.image_box, /*own=*/false);
  if (!image)
    return nullptr;
  place(runtime.image_box, image, name, 0.0f);
  Set(runtime.image_box, image, "Opacity", 0.0f);
  Set(runtime.image_box, image, "ImageScaleType", 2);
  SetColor(runtime.image_box, image, "FilterColor", {1, 1, 1, 1});
  if (!append_child(runtime, parent, image))
    return nullptr;
  LoadSystemImage(runtime, image, uri);
  return image;
}

void LoadSystemImage(const Runtime &runtime, MonoObject *image,
                     const char *uri) {
  if (!image || !runtime.image_box || !runtime.image || !uri || !*uri)
    return;
  Invoke<void>(runtime.image, runtime.image_box, image, "LoadAsync",
               managed_string(uri));
}

void PanelGeometry(const Runtime &runtime, MonoObject *panel, float x, float y,
                   float width, float height) {
  if (!panel || !runtime.panel)
    return;
  width = std::max(width, 0.0f);
  height = std::max(height, 0.0f);
  if (geometry_unchanged(panel, x, y, width, height))
    return;
  Set(runtime.panel, panel, "X", x);
  Set(runtime.panel, panel, "Y", y);
  Set(runtime.panel, panel, "Width", width);
  Set(runtime.panel, panel, "Height", height);
}

void PanelY(const Runtime &runtime, MonoObject *panel, float y) {
  if (!panel || !runtime.panel)
    return;
  auto found = s_shadow.find(panel);
  if (found != s_shadow.end() && found->second.has_geometry) {
    if (same(found->second.y, y))
      return;
    found->second.y = y;
  }
  Set(runtime.panel, panel, "Y", y);
}

void PanelYHeight(const Runtime &runtime, MonoObject *panel, float y,
                  float height) {
  if (!panel || !runtime.panel)
    return;
  height = std::max(height, 0.0f);
  auto found = s_shadow.find(panel);
  if (found != s_shadow.end() && found->second.has_geometry) {
    if (same(found->second.y, y) && same(found->second.height, height))
      return;
    found->second.y = y;
    found->second.height = height;
  }
  Set(runtime.panel, panel, "Y", y);
  Set(runtime.panel, panel, "Height", height);
}

void PanelColor(const Runtime &runtime, MonoObject *panel, Color color) {
  if (!panel || !runtime.panel)
    return;
  SetColor(runtime.panel, panel, "BackgroundColor", color);
}

void PanelHide(const Runtime &runtime, MonoObject *panel) {
  if (!panel || !runtime.panel)
    return;
  // UI2.Panel on 11.40 has BackgroundColor, but no BackgroundVisibility or
  // BackgroundOpacity properties: a transparent UIColor is the supported hide.
  SetColor(runtime.panel, panel, "BackgroundColor", {0, 0, 0, 0});
}

void LabelGeometry(const Runtime &runtime, MonoObject *label, float x, float y,
                   float width, float height) {
  if (!label || !runtime.label)
    return;
  /* Not shadowed: labels may resize themselves (FitWidthToText and friends)
     and several callers position them directly. The cached thunks alone make
     this cheap. */
  Set(runtime.label, label, "X", x);
  Set(runtime.label, label, "Y", y);
  Set(runtime.label, label, "Width", std::max(width, 0.0f));
  Set(runtime.label, label, "Height", std::max(height, 0.0f));
}

void ImageGeometry(const Runtime &runtime, MonoObject *image, float x, float y,
                   float width, float height, bool visible) {
  if (!image || !runtime.image_box)
    return;
  /* Opacity is not shadowed: a theme writes it directly (the header glyph). */
  Set(runtime.image_box, image, "Opacity", visible ? 1.0f : 0.0f);
  width = visible ? std::max(width, 0.0f) : 0.0f;
  height = visible ? std::max(height, 0.0f) : 0.0f;
  if (geometry_unchanged(image, x, y, width, height))
    return;
  Set(runtime.image_box, image, "X", x);
  Set(runtime.image_box, image, "Y", y);
  Set(runtime.image_box, image, "Width", width);
  Set(runtime.image_box, image, "Height", height);
}

void LabelText(const Runtime &runtime, MonoObject *label,
               const std::string &text) {
  if (!label || !runtime.label)
    return;
  WidgetShadow &shadow = s_shadow[label];
  if (shadow.has_text && shadow.text == text)
    return;
  shadow.has_text = true;
  shadow.text = text;
  Set(runtime.label, label, "Text", managed_string(text));
}

void LabelFont(const Runtime &runtime, MonoObject *label) {
  if (label && runtime.label && runtime.font)
    SetInvoke(runtime.label, label, "Font", runtime.font);
}

} // namespace AniketosUI::Pui
