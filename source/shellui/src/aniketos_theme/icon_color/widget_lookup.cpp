#include "icon_color_internal.hpp"

#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "hooked_funcs.hpp"

#include <onion/distribution.h>

#include <unordered_map>

namespace AniketosTheme::IconColor {
namespace {

namespace HomeScene = AniketosUI::HomeScene;
namespace Reflect = AniketosUI::Reflect;

constexpr size_t kMaxVisibilityCache = 64;
constexpr int kMaxSettingsParents = 10;

struct VisibilityProbe {
  MonoClass *klass = nullptr;
  MonoMethod *getter = nullptr;
};

std::vector<VisibilityProbe> s_visibility_cache;
std::unordered_map<MonoClass *, MonoMethod *> s_disposed_getters;

MonoMethod *find_getter(MonoClass *klass, const char *property,
                        const char *method) {
  MonoProperty *found = find_property_in_hierarchy(klass, property);
  MonoMethod *getter = found ? mono_property_get_get_method(found) : nullptr;
  return getter ? getter : Reflect::MethodOf(klass, method, 0);
}

/* The result of a getter of a value type, unboxed, or null. */
const void *invoke_unboxed(MonoMethod *getter, MonoObject *object) {
  if (!getter || !object)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, object, nullptr, &exception);
  return exception || !value ? nullptr : mono_object_unbox(value);
}

bool invoke_bool(MonoMethod *getter, MonoObject *object, bool *value) {
  const void *unboxed = invoke_unboxed(getter, object);
  if (!unboxed)
    return false;
  *value = *static_cast<const uint8_t *>(unboxed) != 0;
  return true;
}

MonoMethod *visibility_getter(MonoClass *klass) {
  for (const VisibilityProbe &probe : s_visibility_cache) {
    if (probe.klass == klass)
      return probe.getter;
  }
  MonoMethod *getter = find_getter(klass, "IsVisible", "get_IsVisible");
  if (!getter)
    getter = find_getter(klass, "Shown", "get_Shown");
  if (s_visibility_cache.size() < kMaxVisibilityCache)
    s_visibility_cache.push_back({klass, getter});
  return getter;
}

/* Widget and Element each declare their own IsDisposed, with the flag in a
   different place, so a Widget's getter cannot be used on an ImageElement. */
MonoMethod *disposed_getter(MonoClass *klass) {
  const auto known = s_disposed_getters.find(klass);
  if (known != s_disposed_getters.end())
    return known->second;
  MonoMethod *getter = find_getter(klass, "IsDisposed", "get_IsDisposed");
  s_disposed_getters.emplace(klass, getter);
  return getter;
}

std::string object_text(MonoObject *object, const char *property) {
  MonoObject *value = HomeScene::PropertyObject(object, property);
  return Mono_to_String(reinterpret_cast<MonoString *>(value));
}

bool hierarchy_name_contains(MonoClass *klass, const char *needle) {
  for (; klass; klass = mono_class_get_parent(klass)) {
    const char *name = class_name(klass);
    if (name && contains(ascii_lower(name), needle))
      return true;
  }
  return false;
}

/* The URIs of what the widget shows: a row's or button's icon, an image's
   picture. */
std::string picture_uris(MonoObject *object) {
  std::string uris = object_text(object, "IconUri");
  for (const char *property : {"Image", "IconImage"}) {
    if (MonoObject *asset = HomeScene::PropertyObject(object, property))
      uris += " " + object_text(asset, "Uri");
  }
  return uris;
}

/* Our pictures live in our data folder; the system's emoji are
   cxml://ui2/unicode_emoji_*, cxml://ui2/emoji_* and cxml://ui3/emoji_*. */
bool own_colours(const std::string &uris) {
  return contains(uris, "/" ONION_DIST_DIR_NAME "/") ||
         contains(ascii_lower(uris), "emoji");
}

bool inside_settings(MonoObject *object) {
  if (hierarchy_name_contains(mono_object_get_class(object), "settings"))
    return true;
  MonoObject *parent = object;
  for (int depth = 0; depth < kMaxSettingsParents; ++depth) {
    parent = HomeScene::PropertyObject(parent, "Parent");
    if (!parent)
      return false;
    if (hierarchy_name_contains(mono_object_get_class(parent), "settings"))
      return true;
  }
  return false;
}

} // namespace

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

std::string type_name(MonoType *type) {
  char *name = type ? mono_type_get_name(type) : nullptr;
  if (!name)
    return {};
  std::string result(name);
  mono_free(name);
  return result;
}

const char *class_name(MonoClass *klass) {
  return klass ? mono_class_get_name(klass) : nullptr;
}

bool derives_from(MonoClass *klass, MonoClass *ancestor) {
  if (!ancestor)
    return false;
  for (; klass; klass = mono_class_get_parent(klass)) {
    if (klass == ancestor)
      return true;
  }
  return false;
}

MonoProperty *find_property_in_hierarchy(MonoClass *klass, const char *wanted) {
  for (; klass; klass = mono_class_get_parent(klass)) {
    if (MonoProperty *property =
            mono_class_get_property_from_name(klass, wanted))
      return property;
  }
  return nullptr;
}

MonoType *setter_parameter(MonoMethod *method) {
  MonoMethodSignature *signature =
      method ? mono_method_signature(method) : nullptr;
  if (!signature)
    return nullptr;
  const std::string result =
      type_name(mono_signature_get_return_type(signature));
  if ((result != "System.Void" && result != "void") ||
      mono_signature_get_param_count(signature) != 1)
    return nullptr;
  void *iter = nullptr;
  return mono_signature_get_params(signature, &iter);
}

bool uicolor_setter(MonoMethod *method) {
  MonoType *param = setter_parameter(method);
  return param && contains(type_name(param), "UIColor");
}

bool read_color(MonoMethod *getter, MonoObject *object, UIColor *color) {
  const void *unboxed = invoke_unboxed(getter, object);
  if (!unboxed)
    return false;
  *color = *static_cast<const UIColor *>(unboxed);
  return true;
}

bool object_disposed(MonoObject *object) {
  if (!object)
    return false;
  bool disposed = false;
  return invoke_bool(disposed_getter(mono_object_get_class(object)), object,
                     &disposed) &&
         disposed;
}

bool object_visible(MonoObject *object) {
  if (!object)
    return false;
  MonoMethod *getter = visibility_getter(mono_object_get_class(object));
  if (!getter)
    return true;
  bool visible = false;
  return invoke_bool(getter, object, &visible) && visible;
}

bool is_image_box_descendant(MonoClass *klass) {
  return derives_from(klass, s_image_filter.box_class);
}

bool keeps_own_colours(MonoObject *object) {
  return own_colours(picture_uris(object));
}

bool widget_should_tint(MonoObject *object) {
  const std::string pictures = picture_uris(object);
  if (own_colours(pictures))
    return false;
  MonoClass *klass = mono_object_get_class(object);
  const char *type = class_name(klass);
  const std::string text = ascii_lower(object_text(object, "TestId") + " " +
                                       object_text(object, "Name"));
  const std::string all = ascii_lower(type ? type : "") + " " + text;
  if (contains(all, "background") || contains(all, "avatar") ||
      contains(all, "tile") || contains(all, "artwork"))
    return false;
  /* a system icon by its picture (cxml://.../iconid_*), whatever the widget
     is called: React's images have no name */
  return hierarchy_name_contains(klass, "icon") || contains(text, "icon") ||
         contains(pictures, "/iconid_") || inside_settings(object);
}

} // namespace AniketosTheme::IconColor
