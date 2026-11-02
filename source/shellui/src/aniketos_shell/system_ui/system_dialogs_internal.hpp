#pragma once

#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>

#include <string>

/* What the dialogs, the popup menu and the native menu share. Defined in
 * system_dialogs.cpp, except where a comment says otherwise. */
namespace SystemDialogs {

namespace Reflect = AniketosUI::Reflect;

constexpr const char *kUi3 = "Sce.PlayStation.PUI.UI3";

struct Failure {
  std::string *out;
  bool fail(const std::string &message) {
    if (out)
      *out = message;
    LOG_WARN("[SYSDIALOG] %s", message.c_str());
    return false;
  }
};

std::string exception_text(MonoObject *exception);

MonoClass *ui3_class(const char *name);
/* An overload picked by the type of its first parameter. */
MonoMethod *find_method_taking(MonoClass *klass, const char *name,
                               int parameters, const char *first_type);
/* The implicit conversion from a string of a UI3 menu row config. */
MonoMethod *from_string_operator(const char *config_class);
MonoObject *config_from_text(MonoMethod *from_text, const std::string &text,
                             MonoObject **exception);

MonoString *managed_string(const std::string &text);
MonoObject *create(MonoClass *klass);
/* A new list of the type `owner`'s `getter` is declared to return (PUI's
   List<IMenuListItemConfig>). */
MonoObject *new_list_for(MonoObject *owner, const char *getter);

/* `name(argument)`. What went wrong goes to `result`. */
bool checked_call(MonoObject *object, const char *name, void *argument,
                  Failure &result);

/* Null when there is no object or getter, or it threw. */
MonoObject *property_object(MonoObject *object, const char *getter);
/* Also false for a Nullable with no value, which comes back as null. */
template <typename T>
bool property_value(MonoObject *object, const char *getter, T *out) {
  MonoObject *boxed = property_object(object, getter);
  if (!boxed)
    return false;
  *out = *static_cast<T *>(mono_object_unbox(boxed));
  return true;
}
inline bool menu_is_open(MonoObject *menu) {
  bool open = false;
  return property_value(menu, "get_IsOpened", &open) && open;
}
/* Does nothing when there is no object or setter. `value` is a pointer to a
   value type, or a managed object. */
void set_property(MonoObject *object, const char *setter, void *value);
void set_text_property(MonoObject *object, const char *setter,
                       const std::string &text);
void decorate_row(MonoObject *config, const std::string &icon, bool enabled);

/* The scene dialogs are opened on: the one Home lives in. */
MonoObject *host_scene();

/* In native_menu.cpp: called by the one NotifyItemSelected detour, which
   lives with the popup menu. */
void note_native_selection(MonoObject *menu, int index, int section);

} // namespace SystemDialogs
