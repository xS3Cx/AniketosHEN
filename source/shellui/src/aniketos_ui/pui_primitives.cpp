#include "aniketos_ui/pui_primitives.hpp"

#include "hooked_funcs.hpp"
#include "aniketos_ui/mono_fast.hpp"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

/* Per-frame code: resolve each managed property once and reuse the compiled
 * accessor (see aniketos_ui/mono_fast.hpp) instead of looking it up by name on
 * every call. Same call syntax and semantics as the hooked_funcs.hpp helpers. */
#include "aniketos_ui/mono_fast.hpp"
#define Get_Property AniketosUI::MonoFast::Get
#define Set_Property AniketosUI::MonoFast::Set
#define Set_Property_Invoke AniketosUI::MonoFast::SetInvoke

#include <algorithm>
#include <cstring>

MonoObject *CreateUIColor(float r, float g, float b, float a);
void Widget_Append_Child(MonoObject *widget, MonoObject *child);

namespace AniketosUI::Pui {

/* ---- Shadow state -------------------------------------------------------
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

MonoString *String(const std::string &value) {
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  if (!domain)
    domain = Root_Domain;
  return domain ? mono_string_new(domain, value.c_str()) : nullptr;
}

MonoObject *Find(const Runtime &runtime, MonoObject *root, const char *name) {
  if (!root || !runtime.widget || !runtime.image || !name)
    return nullptr;
  return Invoke<MonoObject *>(runtime.image, runtime.widget, root,
                              "FindWidgetByName", String(name));
}

void RemoveNamed(const Runtime &runtime, MonoObject *root, const char *name) {
  MonoObject *object = Find(runtime, root, name);
  if (object)
    Invoke<void>(runtime.image, runtime.widget, object, "RemoveFromParent");
}

bool IsOwn(MonoObject *object) { return object && s_own.count(object) != 0; }

void Remove(const Runtime &, MonoObject *object) {
  /* Works for UI2 and UI3 widgets alike: take the method from the object. */
  if (!object || !mono_object_get_class || !mono_class_get_parent ||
      !mono_class_get_method_from_name || !mono_runtime_invoke)
    return;
  MonoMethod *remove = nullptr;
  for (MonoClass *klass = mono_object_get_class(object); klass && !remove;
       klass = mono_class_get_parent(klass))
    remove = mono_class_get_method_from_name(klass, "RemoveFromParent", 0);
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
      MonoFast::ScratchColor(color.r, color.g, color.b, color.a);
  if (managed)
    MonoFast::SetInvoke(klass, object, property, managed);
}

bool AppendChild(const Runtime &runtime, MonoObject *parent,
                 MonoObject *child) {
  if (!parent || !child)
    return false;

  // UI2.Widget::AppendChild is the proven path for the ordinary menu tree.
  // Calling the generic base method on a UI2 object is not equivalent on all
  // PUI builds and can throw, leaving CreatePanel() with no parent. Detect the
  // actual managed parent before selecting the mixed UI2/UI3 path.
  const MonoClass *parent_class =
      mono_object_get_class ? mono_object_get_class(parent) : nullptr;
  const char *parent_namespace =
      parent_class && mono_class_get_namespace
          ? mono_class_get_namespace(const_cast<MonoClass *>(parent_class))
          : nullptr;
  const bool ui3_parent =
      parent_namespace && std::strcmp(parent_namespace,
                                     "Sce.PlayStation.PUI.UI3") == 0;
  if (!ui3_parent) {
    if (!runtime.widget)
      return false;
    Widget_Append_Child(parent, child);
    return true;
  }

  // WidgetBase::AppendChild accepts both UI2.Widget and UI3.Widget.  This is
  // the only supported way to put the existing UI2 controls under the native
  // UI3 rounded surface.
  if (mono_class_get_method_from_name && mono_runtime_invoke) {
    /* A UI3 child goes in through the parent's own AppendChild (the one a
       scene root accepts); a UI2 child needs the mixed WidgetBase one. */
    const MonoClass *child_class =
        mono_object_get_class ? mono_object_get_class(child) : nullptr;
    const char *child_namespace =
        child_class && mono_class_get_namespace
            ? mono_class_get_namespace(const_cast<MonoClass *>(child_class))
            : nullptr;
    const bool ui3_child =
        child_namespace &&
        std::strcmp(child_namespace, "Sce.PlayStation.PUI.UI3") == 0;
    MonoMethod *append = nullptr;
    if (ui3_child && mono_class_get_parent)
      for (MonoClass *klass = const_cast<MonoClass *>(parent_class);
           klass && !append; klass = mono_class_get_parent(klass))
        append = mono_class_get_method_from_name(klass, "AppendChild", 1);
    if (!append && runtime.widget_base)
      append = mono_class_get_method_from_name(runtime.widget_base,
                                               "AppendChild", 1);
    if (append) {
      void *args[1] = {child};
      MonoObject *exception = nullptr;
      mono_runtime_invoke(append, parent, args, &exception);
      return exception == nullptr;
    }
  }

  return false;
}

namespace {

bool retain(const Runtime &runtime, MonoObject *object) {
  return object && (!runtime.retain ||
                    runtime.retain(object, runtime.retain_context));
}

} // namespace

MonoObject *CreatePanel(const Runtime &runtime, MonoObject *parent,
                        const char *name) {
  if (!parent || !runtime.panel)
    return nullptr;
  MonoObject *panel = New_Object(runtime.panel);
  if (!panel)
    return nullptr;
  Forget(panel); /* a freed widget's address may be reused */
  s_own.insert(panel);
  mono_runtime_object_init(panel);
  if (!retain(runtime, panel))
    return nullptr;
  Set_Property(runtime.panel, panel, "Name", String(name ? name : ""));
  Set_Property(runtime.panel, panel, "PositionType", 0);
  Set_Property(runtime.panel, panel, "X", 0.0f);
  Set_Property(runtime.panel, panel, "Y", 0.0f);
  Set_Property(runtime.panel, panel, "Width", 1.0f);
  Set_Property(runtime.panel, panel, "Height", 1.0f);
  SetColor(runtime.panel, panel, "BackgroundColor", {0, 0, 0, 0});
  if (!AppendChild(runtime, parent, panel))
    return nullptr;
  // Applying a PUI skin during append may replace a transparent default with
  // the stock dark panel colour. These panels are custom-painted (or serve as
  // geometry-only roots), so opt out and reassert the supported transparent
  // UI2 background after parenting.
  if (runtime.widget)
    Set_Property(runtime.widget, panel, "FollowSkinBackgroundColor", false);
  SetColor(runtime.panel, panel, "BackgroundColor", {0, 0, 0, 0});
  return panel;
}

MonoObject *CreateLabel(const Runtime &runtime, MonoObject *parent,
                        const char *name, int horizontal, int lines) {
  if (!parent || !runtime.label)
    return nullptr;
  MonoObject *label = New_Object(runtime.label);
  if (!label)
    return nullptr;
  Forget(label); /* a freed widget's address may be reused */
  s_own.insert(label);
  mono_runtime_object_init(label);
  if (!retain(runtime, label))
    return nullptr;
  Set_Property(runtime.label, label, "Name", String(name ? name : ""));
  Set_Property(runtime.label, label, "PositionType", 0);
  Set_Property(runtime.label, label, "X", 0.0f);
  Set_Property(runtime.label, label, "Y", 0.0f);
  Set_Property(runtime.label, label, "Width", 1.0f);
  Set_Property(runtime.label, label, "Height", 1.0f);
  Set_Property(runtime.label, label, "Text", String(""));
  if (runtime.font)
    Set_Property_Invoke(runtime.label, label, "Font", runtime.font);
  Set_Property(runtime.label, label, "HorizontalAlignment", horizontal);
  Set_Property(runtime.label, label, "VerticalAlignment", 2); // Center
  Set_Property(runtime.label, label, "FitWidthToText", false);
  Set_Property(runtime.label, label, "FitHeightToText", false);
  // UI2.Label on 11.40 exposes MaxLineCount, not NumberOfLines. The latter
  // silently did nothing and allowed one-line HUD labels to wrap/ellipsize.
  Set_Property(runtime.label, label, "MaxLineCount", std::max(lines, 1));
  Set_Property(runtime.label, label, "TextTrimming", 0); // None
  Set_Property(runtime.label, label, "EnableThemedColor", false);
  Set_Property(runtime.label, label, "EnableThemedShadowColor", false);
  SetColor(runtime.label, label, "TextColor", {1, 1, 1, 1});
  if (!AppendChild(runtime, parent, label))
    return nullptr;
  // Parenting may apply the host skin once. Keep menu labels under the
  // explicit per-theme colours used by the Shell renderer.
  Set_Property(runtime.label, label, "EnableThemedColor", false);
  Set_Property(runtime.label, label, "EnableThemedShadowColor", false);
  SetColor(runtime.label, label, "TextColor", {1, 1, 1, 1});
  return label;
}

MonoObject *CreateSystemImage(const Runtime &runtime, MonoObject *parent,
                              const char *name, const char *uri) {
  if (!parent || !runtime.image_box || !runtime.image || !uri)
    return nullptr;
  MonoObject *image = New_Object(runtime.image_box);
  if (!image)
    return nullptr;
  Forget(image); /* a freed widget's address may be reused */
  mono_runtime_object_init(image);
  if (!retain(runtime, image))
    return nullptr;
  Set_Property(runtime.image_box, image, "Name", String(name ? name : ""));
  Set_Property(runtime.image_box, image, "PositionType", 0);
  Set_Property(runtime.image_box, image, "X", 0.0f);
  Set_Property(runtime.image_box, image, "Y", 0.0f);
  Set_Property(runtime.image_box, image, "Width", 0.0f);
  Set_Property(runtime.image_box, image, "Height", 0.0f);
  Set_Property(runtime.image_box, image, "Opacity", 0.0f);
  Set_Property(runtime.image_box, image, "ImageScaleType", 2);
  SetColor(runtime.image_box, image, "FilterColor", {1, 1, 1, 1});
  if (!AppendChild(runtime, parent, image))
    return nullptr;
  LoadSystemImage(runtime, image, uri);
  return image;
}

void LoadSystemImage(const Runtime &runtime, MonoObject *image,
                     const char *uri) {
  if (!image || !runtime.image_box || !runtime.image || !uri || !*uri)
    return;
  Invoke<void>(runtime.image, runtime.image_box, image, "LoadAsync",
               String(uri));
}

void PanelGeometry(const Runtime &runtime, MonoObject *panel, float x, float y,
                   float width, float height) {
  if (!panel || !runtime.panel)
    return;
  width = std::max(width, 0.0f);
  height = std::max(height, 0.0f);
  if (geometry_unchanged(panel, x, y, width, height))
    return;
  MonoFast::Set(runtime.panel, panel, "X", x);
  MonoFast::Set(runtime.panel, panel, "Y", y);
  MonoFast::Set(runtime.panel, panel, "Width", width);
  MonoFast::Set(runtime.panel, panel, "Height", height);
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
  MonoFast::Set(runtime.panel, panel, "Y", y);
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
  MonoFast::Set(runtime.panel, panel, "Y", y);
  MonoFast::Set(runtime.panel, panel, "Height", height);
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
  // BackgroundOpacity properties. The former implementation therefore did
  // nothing and could leave an old black backing visible after switching from
  // A transparent UIColor is the real, supported hide path.
  SetColor(runtime.panel, panel, "BackgroundColor", {0, 0, 0, 0});
}

void LabelGeometry(const Runtime &runtime, MonoObject *label, float x, float y,
                   float width, float height) {
  if (!label || !runtime.label)
    return;
  /* Not shadowed: labels may resize themselves (FitWidthToText and friends)
     and several callers position them directly. The cached thunks alone make
     this cheap. */
  MonoFast::Set(runtime.label, label, "X", x);
  MonoFast::Set(runtime.label, label, "Y", y);
  MonoFast::Set(runtime.label, label, "Width", std::max(width, 0.0f));
  MonoFast::Set(runtime.label, label, "Height", std::max(height, 0.0f));
}

void ImageGeometry(const Runtime &runtime, MonoObject *image, float x, float y,
                   float width, float height, bool visible) {
  if (!image || !runtime.image_box)
    return;
  MonoFast::Set(runtime.image_box, image, "X", x);
  MonoFast::Set(runtime.image_box, image, "Y", y);
  MonoFast::Set(runtime.image_box, image, "Width",
                visible ? std::max(width, 0.0f) : 0.0f);
  MonoFast::Set(runtime.image_box, image, "Height",
                visible ? std::max(height, 0.0f) : 0.0f);
  MonoFast::Set(runtime.image_box, image, "Opacity", visible ? 1.0f : 0.0f);
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
  MonoFast::Set(runtime.label, label, "Text", String(text));
}

void LabelFont(const Runtime &runtime, MonoObject *label) {
  if (label && runtime.label && runtime.font)
    MonoFast::SetInvoke(runtime.label, label, "Font", runtime.font);
}

} // namespace AniketosUI::Pui
