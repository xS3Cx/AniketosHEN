#include "icon_color_internal.hpp"

#include "aniketos_ui/home_scene.hpp"

#include <cstring>
#include <initializer_list>

namespace AniketosTheme::IconColor {
namespace {

namespace HomeScene = AniketosUI::HomeScene;

constexpr int kMonoTypeGenericInstance = 0x15;
constexpr const char *kLazyPrefix = "System.Lazy";

/* Tints through the first of `names` that is a UIColor property of a class
   that is not generic, and tracks the object with the colour it had. */
bool tint_by_property(MonoObject *object,
                      std::initializer_list<const char *> names) {
  MonoClass *klass = mono_object_get_class(object);
  for (const char *name : names) {
    MonoProperty *property = find_property_in_hierarchy(klass, name);
    if (!property)
      continue;
    MonoMethod *setter = mono_property_get_set_method(property);
    if (!setter || !uicolor_setter(setter))
      continue;
    const char *declaring = class_name(setter->klass);
    if (declaring && std::strchr(declaring, '`'))
      continue;
    MonoMethod *getter = mono_property_get_get_method(property);
    UIColor original{1.0f, 1.0f, 1.0f, 1.0f, 0};
    if (!read_color(getter, object, &original) ||
        !set_color(setter, object, current_color()))
      return false;
    track_object(object, setter, original);
    return true;
  }
  return false;
}

bool tint_widget(MonoObject *widget) {
  return tint_by_property(widget, {"FilterColor", "IconFilterColor",
                                   "TintColor"});
}

MonoObject *row_icon_widget(MonoObject *row) {
  for (const char *property : {"IconImageBox", "IconWidget",
                               "IconImageBoxPostEffect", "IconImage", "Icon",
                               "ImageBox"}) {
    if (MonoObject *value = HomeScene::PropertyObject(row, property))
      return value;
  }
  return nullptr;
}

bool tint_row_icon_widget(MonoObject *row) {
  if (object_disposed(row))
    return false;
  MonoObject *icon = row_icon_widget(row);
  if (!icon)
    return false;
  if (is_image_box_descendant(mono_object_get_class(icon)))
    return tint_image(icon);
  return tint_widget(icon);
}

std::string full_class_name(MonoClass *klass) {
  const char *name = class_name(klass);
  const char *space =
      klass && mono_class_get_namespace ? mono_class_get_namespace(klass)
                                        : nullptr;
  if (space && space[0])
    return std::string(space) + "." + (name ? name : "?");
  return name ? name : "?";
}

bool is_lazy(MonoObject *value) {
  return full_class_name(mono_object_get_class(value)).rfind(kLazyPrefix, 0) ==
         0;
}

bool element_field(MonoClassField *field) {
  if (mono_field_get_flags &&
      (mono_field_get_flags(field) & kStaticAttribute) != 0)
    return false;
  MonoType *type = mono_field_get_type(field);
  return type &&
         (type->type == kMonoTypeClass ||
          (type->type == kMonoTypeGenericInstance &&
           type_name(type).rfind(kLazyPrefix, 0) == 0));
}

/* The value of the first field found. A Lazy creates its element here if
   nothing has shown it yet. */
MonoObject *row_icon_element(MonoObject *row) {
  if (!mono_field_get_type || !mono_field_get_value)
    return nullptr;
  MonoClass *klass = mono_object_get_class(row);
  for (const char *wanted : {"iconElement", "checkIconElement"}) {
    for (MonoClass *current = klass; current;
         current = mono_class_get_parent(current)) {
      MonoClassField *field = mono_class_get_field_from_name(current, wanted);
      if (!field || !element_field(field))
        continue;
      MonoObject *value = nullptr;
      mono_field_get_value(row, field, &value);
      return value && is_lazy(value) ? HomeScene::PropertyObject(value, "Value")
                                     : value;
    }
  }
  return nullptr;
}

bool tint_row_element(MonoObject *row) {
  if (object_disposed(row))
    return false;
  MonoObject *element = row_icon_element(row);
  return element && !object_disposed(element) && tint_widget(element);
}

} // namespace

void tint_menu_row(MonoObject *row) {
  if (keeps_own_colours(row)) {
    leave_untinted(row);
    for (MonoObject *icon : {row_icon_element(row), row_icon_widget(row)}) {
      if (icon)
        leave_untinted(icon);
    }
    return;
  }
  if (!object_disposed(row))
    tint_by_property(row, {"IconFilterColor", "FilterColor"});
  if (!tint_row_element(row))
    tint_row_icon_widget(row);
  scan_menu_icons(row);
}

void tint_button_icon(MonoObject *button) {
  if (keeps_own_colours(button))
    leave_untinted(button);
  else
    tint_by_property(button, {"IconFilterColor"});
}

} // namespace AniketosTheme::IconColor
