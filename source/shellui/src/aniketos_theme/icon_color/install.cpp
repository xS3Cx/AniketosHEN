#include "icon_color_internal.hpp"

#include "aniketos_theme/icon_color.hpp"

#include <onion/log.h>

#include <cstring>

namespace AniketosTheme::IconColor {

ImageFilter s_image_filter;

namespace {

constexpr const char *kUi3Namespace = "Sce.PlayStation.PUI.UI3";
constexpr uint32_t kTypeDefTable = 2;
constexpr uint32_t kTypeDefTokenBase = 0x02000000u;
constexpr int kMonoTypeString = 0x0e;
constexpr int kMonoTypeObject = 0x1c;

/* Without one of these no setter can be recognised, so nothing gets hooked. */
bool reflection_available() {
  return mono_image_get_table_info && mono_table_info_get_rows &&
         mono_class_get && mono_method_signature &&
         mono_signature_get_param_count && mono_signature_get_params &&
         mono_signature_get_return_type && mono_type_get_name &&
         mono_class_get_name && mono_class_get_parent &&
         mono_class_get_property_from_name && mono_property_get_set_method &&
         mono_compile_method;
}

MonoClass *ui3_class(MonoImage *pui, const char *name) {
  return mono_class_from_name(pui, kUi3Namespace, name);
}

MonoClass *type_def(MonoImage *pui, int row) {
  return mono_class_get(pui, kTypeDefTokenBase | static_cast<uint32_t>(row));
}

MonoClass *find_pui_class(MonoImage *pui, int rows, const char *name) {
  if (MonoClass *exact = ui3_class(pui, name))
    return exact;
  for (int row = 1; row <= rows; ++row) {
    MonoClass *klass = type_def(pui, row);
    const char *candidate = class_name(klass);
    if (candidate && std::strcmp(candidate, name) == 0)
      return klass;
  }
  return nullptr;
}

/* A void instance method taking one reference, on a class that is not generic.
   A setter of a Widget's parent may also take a plain object. */
bool reference_setter(MonoMethod *method, MonoClass *klass, bool plain_object) {
  if (!method)
    return false;
  const char *declaring = class_name(method->klass ? method->klass : klass);
  if (!declaring || std::strchr(declaring, '`'))
    return false;
  if (mono_method_get_flags) {
    uint32_t iflags = 0;
    if ((mono_method_get_flags(method, &iflags) & kStaticAttribute) != 0)
      return false;
  }
  MonoType *param = setter_parameter(method);
  if (!param)
    return false;
  return param->type == kMonoTypeClass || param->type == kMonoTypeString ||
         (plain_object && param->type == kMonoTypeObject);
}

void add_color_setter(MonoClass *klass, const char *property_name,
                      std::vector<SetterCandidate> &candidates) {
  MonoProperty *property =
      mono_class_get_property_from_name(klass, property_name);
  MonoMethod *setter =
      property ? mono_property_get_set_method(property) : nullptr;
  if (!setter || !uicolor_setter(setter))
    return;
  if (const uint64_t address = mono_compile_method(setter)) {
    const char *type = class_name(klass);
    candidates.push_back({type ? type : "?", property_name, setter, address,
                          mono_property_get_get_method(property)});
  }
}

void add_object_setter(MonoClass *klass, const char *type,
                       const char *property_name,
                       std::vector<SetterCandidate> &candidates) {
  MonoProperty *property =
      mono_class_get_property_from_name(klass, property_name);
  if (!property)
    return;
  MonoMethod *setter = mono_property_get_set_method(property);
  if (!reference_setter(setter, klass, false))
    return;
  if (const uint64_t address = mono_compile_method(setter))
    candidates.push_back({type, property_name, setter, address, nullptr});
}

void choose_image_filter(MonoClass *image_box,
                         const std::vector<SetterCandidate> &images) {
  MonoProperty *filter = find_property_in_hierarchy(image_box, "FilterColor");
  MonoMethod *setter = filter ? mono_property_get_set_method(filter) : nullptr;
  if (setter && uicolor_setter(setter))
    s_image_filter = {image_box, setter, mono_property_get_get_method(filter)};
  else if (!images.empty())
    s_image_filter = {image_box, images[0].setter, images[0].getter};
  if (s_image_filter.setter && !s_image_filter.getter)
    LOG_INFO("[ICONCOLOR] not found: FilterColor getter, images not tracked");
}

void hook_image_sources(MonoImage *pui, int rows) {
  std::vector<SetterCandidate> candidates;
  for (const char *type : {"ImageBox", "ImageQuad", "ImageBasemat",
                           "AnimationImageBox", "IconImageBox"}) {
    if (MonoClass *klass = find_pui_class(pui, rows, type))
      add_object_setter(klass, type, "Image", candidates);
  }
  hook_image_source_setters(candidates);
}

void hook_menu_icons(MonoImage *pui, int rows) {
  std::vector<SetterCandidate> candidates;
  for (const char *type : {"MenuListButton", "MenuListSubmenu",
                           "MenuListSelectable", "MenuListDropdown",
                           "MenuListCustom", "MenuListItem"}) {
    MonoClass *klass = find_pui_class(pui, rows, type);
    if (!klass)
      continue;
    for (const char *property : {"IconUri", "IconImage"})
      add_object_setter(klass, type, property, candidates);
  }
  hook_menu_icon_setters(candidates);
}

bool hook_widget_attach(MonoClass *widget) {
  const char *property_name = "Parent";
  MonoProperty *property = find_property_in_hierarchy(widget, property_name);
  if (!property) {
    property_name = "ParentWidget";
    property = find_property_in_hierarchy(widget, property_name);
  }
  MonoMethod *setter =
      property ? mono_property_get_set_method(property) : nullptr;
  if (!reference_setter(setter, widget, true))
    return false;
  const char *type = class_name(setter->klass ? setter->klass : widget);
  const uint64_t address = mono_compile_method(setter);
  return address &&
         hook_attach_setter({type, property_name, setter, address, nullptr});
}

} // namespace

void Install(MonoImage *pui) {
  if (!pui)
    return;
  if (!reflection_available()) {
    LOG_INFO("[ICONCOLOR] not found: mono table APIs unavailable");
    return;
  }
  MonoClass *widget = ui3_class(pui, "Widget");
  resolve_disposed_getter(widget);
  MonoClass *image_box = ui3_class(pui, "ImageBox");
  const MonoTableInfo *table = mono_image_get_table_info(pui, kTypeDefTable);
  const int rows = table ? mono_table_info_get_rows(table) : 0;
  std::vector<SetterCandidate> buttons;
  std::vector<SetterCandidate> images;
  for (int row = 1; row <= rows; ++row) {
    MonoClass *klass = type_def(pui, row);
    if (!klass)
      continue;
    add_color_setter(klass, "IconFilterColor", buttons);
    if (derives_from(klass, image_box))
      add_color_setter(klass, "FilterColor", images);
  }
  choose_image_filter(image_box, images);
  hook_button_setters(buttons);
  hook_image_setters(images);
  hook_image_sources(pui, rows);
  hook_menu_icons(pui, rows);
  if (!hook_widget_attach(widget))
    LOG_INFO("[ICONCOLOR] attach hook not installed");
}

} // namespace AniketosTheme::IconColor
