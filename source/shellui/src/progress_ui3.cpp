#include "progress_ui3.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <cmath>
#include <cstring>

namespace progress_ui3 {
namespace {

constexpr const char *kPuiUi3Namespace = "Sce.PlayStation.PUI.UI3";
constexpr const char *kUpdatePanelNamespace =
    "Sce.Vsh.ShellUI.Settings.Peripherals.MorpheusUpdateUI3";
constexpr const char *kUpdatePanelClass = "UpdateProgressPanel";

MonoDomain *current_domain() {
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  return domain ? domain : Root_Domain;
}

MonoMethod *ui3_property_setter(const char *class_name, const char *name) {
  if (!pui_img || !class_name || !name || !mono_class_from_name ||
      !mono_class_get_property_from_name || !mono_property_get_set_method)
    return nullptr;
  MonoClass *klass =
      mono_class_from_name(pui_img, kPuiUi3Namespace, class_name);
  MonoProperty *property =
      klass ? mono_class_get_property_from_name(klass, name) : nullptr;
  return property ? mono_property_get_set_method(property) : nullptr;
}

MonoMethod *ui3_property_getter(const char *class_name, const char *name) {
  if (!pui_img || !class_name || !name || !mono_class_from_name ||
      !mono_class_get_property_from_name || !mono_property_get_get_method)
    return nullptr;
  MonoClass *klass =
      mono_class_from_name(pui_img, kPuiUi3Namespace, class_name);
  MonoProperty *property =
      klass ? mono_class_get_property_from_name(klass, name) : nullptr;
  return property ? mono_property_get_get_method(property) : nullptr;
}

bool invoke_ui3_setter(MonoObject *object, const char *class_name,
                       const char *name, void *value) {
  MonoMethod *setter = ui3_property_setter(class_name, name);
  if (!setter || !mono_runtime_invoke)
    return false;
  void *args[] = {value};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(setter, object, args, &exception);
  return exception == nullptr;
}

MonoMethod *widget_append_child_method() {
  static MonoMethod *method = nullptr;
  if (method)
    return method;
  if (!pui_img || !mono_class_from_name || !mono_method_desc_new ||
      !mono_method_desc_search_in_class || !mono_method_desc_free)
    return nullptr;
  MonoClass *widget_class =
      mono_class_from_name(pui_img, kPuiUi3Namespace, "Widget");
  MonoMethodDesc *description = mono_method_desc_new(
      ":AppendChild(Sce.PlayStation.PUI.UI3.Widget)", 1);
  if (widget_class && description)
    method = mono_method_desc_search_in_class(description, widget_class);
  if (description)
    mono_method_desc_free(description);
  return method;
}

}

void release_widget(BoundWidget &widget) {
  if (widget.handle && mono_gchandle_free)
    mono_gchandle_free(widget.handle);
  widget = {};
}

bool pin_widget(BoundWidget &bound, MonoObject *object, const char *) {
  if (!object || !mono_gchandle_new)
    return false;
  bound.handle = mono_gchandle_new(object, 1);
  if (!bound.handle)
    return false;
  bound.object = object;
  return true;
}

MonoObject *new_update_progress_panel() {
  if (!mono_class_from_name || !mono_object_new || !mono_runtime_object_init)
    return nullptr;
  MonoImage *legacy_image = getDLLimage(legacy_dec.c_str());
  MonoClass *klass =
      legacy_image ? mono_class_from_name(legacy_image, kUpdatePanelNamespace,
                                          kUpdatePanelClass)
                   : nullptr;
  MonoDomain *domain = current_domain();
  MonoObject *object =
      (klass && domain) ? mono_object_new(domain, klass) : nullptr;
  if (object)
    mono_runtime_object_init(object);
  return object;
}

MonoObject *widget_child_at(MonoObject *root, int index) {
  if (!pui_img || !root || index < 0 || !mono_class_from_name ||
      !mono_class_get_method_from_name || !mono_runtime_invoke)
    return nullptr;
  MonoClass *widget_class =
      mono_class_from_name(pui_img, kPuiUi3Namespace, "Widget");
  MonoMethod *get_child =
      widget_class
          ? mono_class_get_method_from_name(widget_class, "GetChildAt", 1)
          : nullptr;
  if (!get_child)
    return nullptr;
  void *args[] = {&index};
  MonoObject *exception = nullptr;
  MonoObject *result = mono_runtime_invoke(get_child, root, args, &exception);
  return exception ? nullptr : result;
}

int widget_children_count(MonoObject *widget) {
  if (!pui_img || !widget || !mono_class_from_name ||
      !mono_class_get_property_from_name || !mono_property_get_get_method ||
      !mono_runtime_invoke || !mono_object_unbox)
    return -1;
  MonoClass *widget_class =
      mono_class_from_name(pui_img, kPuiUi3Namespace, "Widget");
  MonoProperty *property =
      widget_class ? mono_class_get_property_from_name(widget_class,
                                                       "ChildrenCount")
                   : nullptr;
  MonoMethod *getter = property ? mono_property_get_get_method(property) : nullptr;
  if (!getter)
    return -1;
  MonoObject *exception = nullptr;
  MonoObject *boxed = mono_runtime_invoke(getter, widget, nullptr, &exception);
  int *value = (!exception && boxed)
                   ? static_cast<int *>(mono_object_unbox(boxed))
                   : nullptr;
  return value ? *value : -1;
}

bool append_panel(MonoObject *widget, MonoObject *panel) {
  MonoMethod *append = widget_append_child_method();
  if (!widget || !panel || !append || !mono_runtime_invoke)
    return false;
  void *args[] = {panel};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(append, widget, args, &exception);
  return exception == nullptr;
}

bool set_label_text(MonoObject *object, const std::string &text) {
  if (!mono_string_new)
    return false;
  MonoDomain *domain = current_domain();
  MonoString *value = domain ? mono_string_new(domain, text.c_str()) : nullptr;
  return value && invoke_ui3_setter(object, "Label", "Text", value);
}

bool set_progress_bar_progress(MonoObject *object, float value) {
  return invoke_ui3_setter(object, "ProgressBar", "Progress", &value);
}

bool set_progress_bar_status(MonoObject *object, int value) {
  return invoke_ui3_setter(object, "ProgressBar", "Status", &value);
}

bool read_progress_bar_progress(MonoObject *object, float &value) {
  MonoMethod *getter = ui3_property_getter("ProgressBar", "Progress");
  if (!getter || !mono_runtime_invoke || !mono_object_unbox)
    return false;
  MonoObject *exception = nullptr;
  MonoObject *boxed = mono_runtime_invoke(getter, object, nullptr, &exception);
  void *raw = (!exception && boxed) ? mono_object_unbox(boxed) : nullptr;
  if (!raw)
    return false;
  std::memcpy(&value, raw, sizeof(value));
  return true;
}

bool Panel::attach(MonoObject *widget) {
  if (!widget)
    return false;
  if (host.object == widget && panel.object)
    return true;
  release();
  MonoObject *created = new_update_progress_panel();
  if (!created)
    return false;
  if (!pin_widget(host, widget, "host") ||
      !pin_widget(panel, created, "panel")) {
    release();
    return false;
  }
  if (!append_panel(widget, created)) {
    release();
    return false;
  }
  if (widget_children_count(created) < 5) {
    release();
    return false;
  }
  MonoObject *title = widget_child_at(created, 0);
  MonoObject *phase = widget_child_at(created, 1);
  MonoObject *bar = widget_child_at(created, 2);
  MonoObject *percent = widget_child_at(created, 3);
  MonoObject *cancel = widget_child_at(created, 4);
  if (!pin_widget(title_label, title, "title") ||
      !pin_widget(phase_label, phase, "phase") ||
      !pin_widget(progress_bar, bar, "bar") ||
      !pin_widget(percent_label, percent, "percent") ||
      !pin_widget(cancel_label, cancel, "cancel")) {
    release();
    return false;
  }
  return true;
}

void Panel::release() {
  release_widget(host);
  release_widget(panel);
  release_widget(title_label);
  release_widget(phase_label);
  release_widget(percent_label);
  release_widget(cancel_label);
  release_widget(progress_bar);
}

}
