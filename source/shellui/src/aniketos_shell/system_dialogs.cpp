#include "aniketos_shell/system_dialogs.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ps5/kernel.h"
#include "shell_clock.hpp"

#include <onion/detour.h>
#include <onion/log.h>

#include <cstring>
#include <utility>

extern int g_libmono_handle;

namespace SystemDialogs {
namespace {

constexpr const char *kPuiImage = "Sce.PlayStation.PUI.dll";
constexpr const char *kUiLibImage = "Sce.Vsh.UILib.dll";
constexpr const char *kUi3 = "Sce.PlayStation.PUI.UI3";
constexpr const char *kErrorDialog = "Sce.Vsh.ErrorDialog";

uint32_t s_last_handle = 0; /* ErrorDialogOpenResult or a Dialog */
bool s_last_is_error = false;
uint32_t s_alert_handle = 0; /* the alert PollAlert watches */
uint32_t s_prompt_handle = 0; /* the prompt PollPrompt watches */

uint32_t s_menu_handle = 0;
std::vector<uint32_t> s_menu_entries; /* the entry configs, kept alive */
int s_menu_selected = -1;
using NotifyItemSelected = void (*)(MonoObject *, MonoObject *, int, int);
NotifyItemSelected s_notify_item_selected_original = nullptr;
/* The rows of the submenus of the menu shown last (configs, strong), with
   their ids, and the id of the one chosen last. */
std::vector<std::pair<uint32_t, std::string>> s_submenu_rows;
std::string s_submenu_selected;
void note_submenu_choice(MonoObject *menu, int index, int section);

/* PopupMenuBase.NotifyItemSelected(MenuSelectedEventArgs, int index, int
   section): PUI calls it when an entry is chosen. Managed events need a
   managed delegate, which native code cannot make here, so the choice is
   picked up on its way through instead. */
uint32_t s_native_menu = 0;                /* strong */
/* A widget of ours at the right edge of the scene, which the menu hangs off:
   PUI places a pop-up menu relative to the widget it belongs to. */
uint32_t s_native_anchor = 0;
std::vector<uint32_t> s_native_items;      /* its row configs, strong */
std::vector<int> s_native_kinds;           /* NativeMenuItem::Kind per row */
/* What each row holds, kept here: the row widgets are made from the configs
   by PUI and made again when the list scrolls, and they do not start from
   the value they were given. */
struct NativeRow {
  float number = 0;
  int chosen = 0;
  bool seen = false; /* its widget was there at the last look */
  /* The widget shows what the row holds. Until it does, what it reports is
     its own starting value, not anything the user did, and is not taken. */
  bool synced = false;
  std::string title; /* slider rows: the number is written after it */
  float step = 1;
  float labelled = -1e30f; /* the number its title shows now */
};
std::vector<NativeRow> s_native_rows;
int s_native_selected = -1;
bool s_native_was_open = false;

void notify_item_selected_hook(MonoObject *menu, MonoObject *args, int index,
                               int section) {
  if (menu && s_menu_handle &&
      mono_gchandle_get_target(s_menu_handle) == menu)
    s_menu_selected = index;
  if (menu && s_native_menu && section == 0 &&
      mono_gchandle_get_target(s_native_menu) == menu)
    s_native_selected = index;
  /* a menu PUI opened from one of our submenu rows */
  if (menu && s_menu_handle && !s_submenu_rows.empty() &&
      mono_gchandle_get_target(s_menu_handle) != menu)
    note_submenu_choice(menu, index, section);
  if (s_notify_item_selected_original)
    s_notify_item_selected_original(menu, args, index, section);
}

struct Failure {
  std::string *out;
  bool fail(const std::string &message) {
    if (out)
      *out = message;
    LOG_WARN("[SYSDIALOG] %s", message.c_str());
    return false;
  }
};

std::string exception_text(MonoObject *exception) {
  if (!exception)
    return {};
  MonoObject *nested = nullptr;
  MonoString *managed = mono_object_to_string(exception, &nested);
  if (nested || !managed)
    return "managed exception";
  const char *utf = mono_string_to_utf8(managed);
  std::string text = utf ? utf : "";
  if (utf && mono_free)
    mono_free(const_cast<char *>(utf));
  if (text.size() > 240)
    text.resize(240);
  return text;
}

MonoClass *find_class(const char *image_file, const char *space,
                      const char *name) {
  MonoImage *image = getDLLimage(image_file);
  return image ? mono_class_from_name(image, space, name) : nullptr;
}

MonoMethod *find_method(MonoClass *klass, const char *name, int parameters) {
  for (MonoClass *k = klass; k; k = mono_class_get_parent(k))
    if (MonoMethod *method =
            mono_class_get_method_from_name(k, name, parameters))
      return method;
  return nullptr;
}

/* An overload picked by the type of its first parameter. */
MonoMethod *find_method_taking(MonoClass *klass, const char *name,
                               int parameters, const char *first_type) {
  void *iter = nullptr;
  while (MonoMethod *method = mono_class_get_methods(klass, &iter)) {
    if (std::strcmp(mono_method_get_name(method), name) != 0)
      continue;
    MonoMethodSignature *signature = mono_method_signature(method);
    if (!signature ||
        static_cast<int>(mono_signature_get_param_count(signature)) !=
            parameters)
      continue;
    void *param_iter = nullptr;
    MonoType *type = mono_signature_get_params(signature, &param_iter);
    char *type_name = type ? mono_type_get_name(type) : nullptr;
    const bool match = type_name && std::strcmp(type_name, first_type) == 0;
    if (type_name && mono_free)
      mono_free(type_name);
    if (match)
      return method;
  }
  return nullptr;
}

MonoClassField *find_field(MonoObject *object, const char *name) {
  for (MonoClass *k = mono_object_get_class(object); k;
       k = mono_class_get_parent(k))
    if (MonoClassField *field = mono_class_get_field_from_name(k, name))
      return field;
  return nullptr;
}

bool set_field(MonoObject *object, const char *name, void *value) {
  MonoClassField *field = find_field(object, name);
  if (!field)
    return false;
  mono_field_set_value(object, field, value);
  return true;
}

MonoObject *create(MonoClass *klass) {
  /* Initialising a class without a parameterless constructor aborts the
     process, so ask first. */
  if (!klass || !mono_class_get_method_from_name(klass, ".ctor", 0))
    return nullptr;
  MonoObject *object = mono_object_new(mono_domain_get(), klass);
  if (object)
    mono_runtime_object_init(object);
  return object;
}

MonoObject *spannable(const std::string &text) {
  static MonoMethod *create_method = nullptr;
  if (!create_method)
    if (MonoClass *klass = find_class(kPuiImage, kUi3, "SpannableText"))
      create_method =
          find_method_taking(klass, "Create", 1, "System.String");
  if (!create_method)
    return nullptr;
  void *args[1] = {mono_string_new(mono_domain_get(), text.c_str())};
  MonoObject *exception = nullptr;
  MonoObject *value =
      mono_runtime_invoke(create_method, nullptr, args, &exception);
  return exception ? nullptr : value;
}

bool call(MonoObject *object, const char *name, void **args, int count,
          std::string *thrown = nullptr) {
  MonoMethod *method = find_method(mono_object_get_class(object), name, count);
  if (!method) {
    if (thrown)
      *thrown = std::string("no method ") + name;
    return false;
  }
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, object, args, &exception);
  if (exception && thrown)
    *thrown = std::string(name) + ": " + exception_text(exception);
  return exception == nullptr;
}

bool set_text_property(MonoObject *object, const char *setter,
                       const std::string &text) {
  if (text.empty())
    return true;
  void *args[1] = {spannable(text)};
  return args[0] && call(object, setter, args, 1);
}

/* The scene dialogs are opened on: the one Home lives in. */
MonoObject *host_scene() {
  MonoObject *root = ShellClock::GetHomeRoot();
  if (!root)
    return nullptr;
  MonoMethod *getter =
      find_method(mono_object_get_class(root), "get_ParentScene", 0);
  if (!getter)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *scene = mono_runtime_invoke(getter, root, nullptr, &exception);
  return exception ? nullptr : scene;
}

/* Nullable<bool> is two bytes whose order differs between class libraries;
   ask the runtime where "has value" and "value" are. */
void set_nullable_bool(MonoObject *object, const char *name, bool value) {
  MonoClassField *field = find_field(object, name);
  if (!field)
    return;
  static MonoClass *(*class_from_type)(MonoType *) = nullptr;
  if (!class_from_type)
    class_from_type = reinterpret_cast<decltype(class_from_type)>(
        kernel_dynlib_dlsym(-1, g_libmono_handle, "mono_class_from_mono_type"));
  int has_offset = 0;
  int value_offset = 1;
  if (class_from_type) {
    if (MonoClass *nullable = class_from_type(mono_field_get_type(field))) {
      MonoClassField *has = nullptr;
      for (const char *candidate : {"hasValue", "has_value"})
        if (!has)
          has = mono_class_get_field_from_name(nullable, candidate);
      MonoClassField *val = mono_class_get_field_from_name(nullable, "value");
      const int header = static_cast<int>(sizeof(void *) * 2);
      if (has && val) {
        has_offset = static_cast<int>(mono_field_get_offset(has)) - header;
        value_offset = static_cast<int>(mono_field_get_offset(val)) - header;
      }
    }
  }
  if (has_offset < 0 || has_offset > 7 || value_offset < 0 ||
      value_offset > 7 || has_offset == value_offset)
    return;
  unsigned char raw[8]{};
  raw[has_offset] = 1;
  raw[value_offset] = value ? 1 : 0;
  mono_field_set_value(object, field, raw);
}

void remember(MonoObject *object, bool is_error) {
  if (s_last_handle)
    mono_gchandle_free(s_last_handle);
  s_last_handle = object ? mono_gchandle_new(object, 0) : 0;
  s_last_is_error = is_error;
}

MonoObject *action(const std::string &title, const std::string &uri) {
  MonoObject *property =
      create(find_class(kUiLibImage, kErrorDialog, "ActionProperty"));
  if (!property)
    return nullptr;
  if (!title.empty())
    set_field(property, "Title", spannable(title));
  if (!uri.empty())
    set_field(property, "ActionUrl",
              mono_string_new(mono_domain_get(), uri.c_str()));
  return property;
}

} // namespace

bool ShowError(const ErrorOptions &options, std::string *failure) {
  Failure result{failure};
  MonoClass *dialog = find_class(kUiLibImage, kErrorDialog, "ErrorDialog");
  MonoObject *content =
      create(find_class(kUiLibImage, kErrorDialog, "PropertyGeneratorResult"));
  MonoObject *param =
      create(find_class(kUiLibImage, kErrorDialog, "ErrorDialogParam"));
  if (!dialog || !content || !param)
    return result.fail("error dialog classes not found");
  MonoMethod *open = find_method(dialog, "OpenUI3", 3);
  if (!open)
    return result.fail("ErrorDialog.OpenUI3 not found");
  MonoObject *scene = host_scene();
  if (!scene)
    return result.fail("no Home scene to open the dialog on");

  if (!options.title.empty())
    set_field(content, "Title", spannable(options.title));
  if (!options.message.empty())
    set_field(content, "Description", spannable(options.message));
  if (!options.positive.empty() || !options.positive_uri.empty())
    set_field(content, "PositiveAction",
              action(options.positive, options.positive_uri));
  if (!options.negative.empty())
    set_field(content, "NegativeAction", action(options.negative, {}));
  if (options.show_code >= 0)
    set_nullable_bool(content, "ShowErrorCode", options.show_code != 0);
  /* Our test screens have no business in the console's error history. */
  set_nullable_bool(content, "ErrorHistory", false);

  int32_t code = options.error_code;
  int32_t style = options.popup ? 1 : 0;
  bool yes = true;
  set_field(param, "RootScene", scene);
  set_field(param, "ErrorCode", &code);
  set_field(param, "DialogStyle", &style);
  set_field(param, "HideOnPositiveButton", &yes);
  set_field(param, "HideOnNegativeButton", &yes);

  void *args[3] = {scene, param, content};
  MonoObject *exception = nullptr;
  MonoObject *opened = mono_runtime_invoke(open, nullptr, args, &exception);
  if (exception)
    return result.fail("OpenUI3: " + exception_text(exception));
  remember(opened, true);
  LOG_INFO("[SYSDIALOG] error dialog opened popup=%d code=0x%08x",
           options.popup ? 1 : 0, static_cast<unsigned>(options.error_code));
  return true;
}

bool ShowAlert(const AlertOptions &options, std::string *failure,
               MonoObject *scene) {
  Failure result{failure};
  MonoClass *klass = find_class(kPuiImage, kUi3, "AlertDialog");
  if (!scene)
    scene = host_scene();
  if (!klass)
    return result.fail("UI3.AlertDialog not found");
  if (!scene)
    return result.fail("no Home scene to open the dialog on");
  MonoObject *dialog = mono_object_new(mono_domain_get(), klass);
  MonoMethod *constructor = find_method(klass, ".ctor", 1);
  if (!dialog || !constructor)
    return result.fail("AlertDialog constructor not found");
  int32_t style = static_cast<int32_t>(options.style);
  void *ctor_args[1] = {&style};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(constructor, dialog, ctor_args, &exception);
  if (exception)
    return result.fail("AlertDialog..ctor: " + exception_text(exception));

  set_text_property(dialog, "set_DialogTitle", options.title);
  set_text_property(dialog, "set_Message", options.message);
  set_text_property(dialog, "set_PositiveButtonTitle", options.positive);
  set_text_property(dialog, "set_NegativeButtonTitle", options.negative);
  set_text_property(dialog, "set_NeutralButtonTitle", options.neutral);
  set_text_property(dialog, "set_SecondNeutralButtonTitle",
                    options.second_neutral);
  set_text_property(dialog, "set_CheckBoxTitle", options.check_box);

  void *open_args[1] = {scene};
  std::string thrown;
  if (!call(dialog, "Open", open_args, 1, &thrown))
    return result.fail(thrown);
  remember(dialog, false);
  if (s_alert_handle)
    mono_gchandle_free(s_alert_handle);
  s_alert_handle = mono_gchandle_new(dialog, 0);
  LOG_INFO("[SYSDIALOG] alert opened style=%d", style);
  return true;
}

bool PollAlert(int *result) {
  MonoObject *dialog =
      s_alert_handle ? mono_gchandle_get_target(s_alert_handle) : nullptr;
  if (!dialog || !result)
    return false;
  MonoMethod *getter =
      find_method(mono_object_get_class(dialog), "get_Result", 0);
  if (!getter)
    return false;
  /* A Nullable<DialogResult> comes back boxed: null until a button is
     pressed, the enum value afterwards. */
  MonoObject *exception = nullptr;
  MonoObject *boxed = mono_runtime_invoke(getter, dialog, nullptr, &exception);
  if (exception || !boxed)
    return false;
  *result = *static_cast<int32_t *>(mono_object_unbox(boxed));
  mono_gchandle_free(s_alert_handle);
  s_alert_handle = 0;
  return true;
}

bool ShowPrompt(const PromptOptions &options, std::string *failure,
                MonoObject *scene) {
  Failure result{failure};
  MonoClass *klass = find_class(kPuiImage, kUi3, "JavaScriptPromptDialog");
  if (!scene)
    scene = host_scene();
  if (!klass)
    return result.fail("UI3.JavaScriptPromptDialog not found");
  if (!scene)
    return result.fail("no Home scene to open the dialog on");
  MonoMethod *constructor = find_method(klass, ".ctor", 2);
  MonoObject *dialog = mono_object_new(mono_domain_get(), klass);
  if (!dialog || !constructor)
    return result.fail("JavaScriptPromptDialog constructor not found");
  void *ctor_args[2] = {
      mono_string_new(mono_domain_get(), options.message.c_str()),
      mono_string_new(mono_domain_get(), options.text.c_str())};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(constructor, dialog, ctor_args, &exception);
  if (exception)
    return result.fail("JavaScriptPromptDialog..ctor: " +
                       exception_text(exception));

  set_text_property(dialog, "set_DialogTitle", options.title);
  set_text_property(dialog, "set_PositiveButtonTitle", options.positive);
  set_text_property(dialog, "set_NegativeButtonTitle", options.negative);

  void *open_args[1] = {scene};
  std::string thrown;
  if (!call(dialog, "Open", open_args, 1, &thrown))
    return result.fail(thrown);
  remember(dialog, false);
  if (s_prompt_handle)
    mono_gchandle_free(s_prompt_handle);
  s_prompt_handle = mono_gchandle_new(dialog, 0);
  LOG_INFO("[SYSDIALOG] prompt opened");
  return true;
}

bool PollPrompt(bool *confirmed, std::string *text) {
  MonoObject *dialog =
      s_prompt_handle ? mono_gchandle_get_target(s_prompt_handle) : nullptr;
  if (!dialog)
    return false;
  MonoMethod *getter =
      find_method(mono_object_get_class(dialog), "get_Result", 0);
  MonoObject *exception = nullptr;
  MonoObject *boxed =
      getter ? mono_runtime_invoke(getter, dialog, nullptr, &exception)
             : nullptr;
  if (exception || !boxed)
    return false; /* still open */
  const int32_t button = *static_cast<int32_t *>(mono_object_unbox(boxed));
  /* The field lives in the dialog's body; its text is what was typed. */
  std::string typed;
  MonoClassField *field = find_field(dialog, "body");
  MonoObject *body = nullptr;
  if (field)
    mono_field_get_value(dialog, field, &body);
  for (const char *name : {"get_InputText", "get_Text"}) {
    MonoMethod *read =
        body ? find_method(mono_object_get_class(body), name, 0) : nullptr;
    MonoObject *value =
        read ? mono_runtime_invoke(read, body, nullptr, &exception) : nullptr;
    if (!read || exception || !value)
      continue;
    const char *utf = mono_string_to_utf8(reinterpret_cast<MonoString *>(value));
    typed = utf ? utf : "";
    if (utf && mono_free)
      mono_free(const_cast<char *>(utf));
    break;
  }
  mono_gchandle_free(s_prompt_handle);
  s_prompt_handle = 0;
  if (confirmed)
    *confirmed = button == 0;
  if (text)
    *text = typed;
  LOG_INFO("[SYSDIALOG] prompt closed button=%d length=%zu", button,
           typed.size());
  return true;
}

void Install(MonoImage *pui) {
  if (!pui || s_notify_item_selected_original)
    return;
  MonoClass *menu_base = mono_class_from_name(pui, kUi3, "PopupMenuBase");
  const uint64_t address =
      menu_base ? Get_Address_of_Method(pui, menu_base, "NotifyItemSelected", 3)
                : 0;
  const bool installed =
      address &&
      InstallDetour(address,
                    reinterpret_cast<void *>(&notify_item_selected_hook),
                    reinterpret_cast<void **>(&s_notify_item_selected_original));
  LOG_INFO("[SYSDIALOG] popup menu selection hook target=0x%lx installed=%d",
           static_cast<unsigned long>(address), installed ? 1 : 0);
  if (!installed)
    s_notify_item_selected_original = nullptr;
}

bool PopupMenuAvailable() { return s_notify_item_selected_original != nullptr; }

namespace {

/* The popup stays open when an entry is chosen (ShowPopupMenu's stay_open),
   and has been seen open since it was shown. */
bool s_menu_stays = false;
bool s_menu_was_open = false;

bool popup_is_open(MonoObject *menu) {
  MonoMethod *getter =
      menu ? find_method(mono_object_get_class(menu), "get_IsOpened", 0)
           : nullptr;
  MonoObject *exception = nullptr;
  MonoObject *boxed =
      getter ? mono_runtime_invoke(getter, menu, nullptr, &exception) : nullptr;
  return !exception && boxed && *static_cast<bool *>(mono_object_unbox(boxed));
}

/* A new list of the type `owner`'s `getter` is declared to return (PUI's
   List<IMenuListItemConfig>). */
MonoObject *new_list_for(MonoObject *owner, const char *getter) {
  static MonoClass *(*class_from_type)(MonoType *) = nullptr;
  if (!class_from_type)
    class_from_type = reinterpret_cast<decltype(class_from_type)>(
        kernel_dynlib_dlsym(-1, g_libmono_handle, "mono_class_from_mono_type"));
  MonoMethod *method =
      owner ? find_method(mono_object_get_class(owner), getter, 0) : nullptr;
  MonoMethodSignature *signature =
      method ? mono_method_signature(method) : nullptr;
  return signature && class_from_type
             ? create(class_from_type(mono_signature_get_return_type(signature)))
             : nullptr;
}

/* The config of one row (and of its submenu's rows, to any depth). */
MonoObject *make_row(const MenuEntry &entry, MonoMethod *from_text, bool top,
                     Failure &result) {
  MonoObject *exception = nullptr;
  MonoObject *config = nullptr;
  if (!entry.children.empty()) {
    config = create(find_class(kPuiImage, kUi3, "MenuListSubmenuConfig"));
    MonoObject *rows = config ? new_list_for(config, "get_Items") : nullptr;
    if (!rows) {
      result.fail("cannot create a submenu");
      return nullptr;
    }
    set_text_property(config, "set_Title", entry.title);
    for (const MenuEntry &child : entry.children) {
      MonoObject *row = make_row(child, from_text, false, result);
      void *add_args[1] = {row};
      std::string thrown;
      if (!row || !call(rows, "Add", add_args, 1, &thrown)) {
        if (row)
          result.fail(thrown);
        return nullptr;
      }
    }
    void *rows_args[1] = {rows};
    call(config, "set_Items", rows_args, 1);
  } else if (entry.is_switch) {
    config = create(find_class(kPuiImage, kUi3, "MenuListSwitchConfig"));
    if (config) {
      set_text_property(config, "set_Title", entry.title);
      bool value = entry.value;
      void *value_args[1] = {&value};
      call(config, "set_Value", value_args, 1);
    }
  } else {
    void *text_args[1] = {
        mono_string_new(mono_domain_get(), entry.title.c_str())};
    config = mono_runtime_invoke(from_text, nullptr, text_args, &exception);
  }
  if (exception || !config) {
    result.fail("popup menu entry: " + exception_text(exception));
    return nullptr;
  }
  const uint32_t handle = mono_gchandle_new(config, 0);
  if (top)
    s_menu_entries.push_back(handle);
  else
    s_submenu_rows.push_back({handle, entry.id});
  if (!entry.header.empty())
    set_text_property(config, "set_Header", entry.header);
  if (!entry.icon.empty()) {
    void *icon_args[1] = {
        mono_string_new(mono_domain_get(), entry.icon.c_str())};
    call(config, "set_IconUri", icon_args, 1);
  }
  if (!entry.enabled) {
    bool enabled = false;
    void *enabled_args[1] = {&enabled};
    call(config, "set_Enabled", enabled_args, 1);
  }
  return config;
}

/* Gives `menu` these entries: on a menu that is on screen its rows change
   where they are. */
bool fill_popup(MonoObject *menu, const std::vector<MenuEntry> &entries,
                const std::string &header, Failure &result) {
  MonoClass *entry_class = find_class(kPuiImage, kUi3, "MenuListButtonConfig");
  MonoMethod *from_text =
      entry_class ? find_method_taking(entry_class, "op_Implicit", 1,
                                       "System.String")
                  : nullptr;
  if (!menu || !from_text)
    return result.fail("popup menu classes not found");

  /* The entry list is a List<IMenuListItemConfig>: make one of whatever type
     the ContextItems property is declared with. */
  MonoObject *list = new_list_for(menu, "get_ContextItems");
  if (!list)
    return result.fail("cannot create the popup menu entry list");

  for (uint32_t handle : s_menu_entries)
    if (handle)
      mono_gchandle_free(handle);
  s_menu_entries.clear();
  for (const auto &row : s_submenu_rows)
    if (row.first)
      mono_gchandle_free(row.first);
  s_submenu_rows.clear();
  s_submenu_selected.clear();
  bool first = true;
  for (const MenuEntry &entry : entries) {
    MonoObject *config = make_row(entry, from_text, true, result);
    if (!config)
      return false;
    if (first && !header.empty())
      set_text_property(config, "set_Header", header);
    first = false;
    void *add_args[1] = {config};
    std::string thrown;
    if (!call(list, "Add", add_args, 1, &thrown))
      return result.fail(thrown);
  }
  void *list_args[1] = {list};
  std::string thrown;
  if (!call(menu, "set_ContextItems", list_args, 1, &thrown))
    return result.fail(thrown);
  return true;
}

/* A row chosen in some menu other than ours: one of our submenus' rows? */
void note_submenu_choice(MonoObject *menu, int index, int section) {
  MonoMethod *getter =
      find_method(mono_object_get_class(menu), "GetMenuListItemConfig", 2);
  if (!getter)
    return;
  int32_t section_index = section, row_index = index;
  void *args[2] = {&section_index, &row_index};
  MonoObject *exception = nullptr;
  MonoObject *config = mono_runtime_invoke(getter, menu, args, &exception);
  if (exception || !config)
    return;
  for (const auto &row : s_submenu_rows)
    if (row.first && mono_gchandle_get_target(row.first) == config) {
      s_submenu_selected = row.second;
      LOG_INFO("[SYSDIALOG] submenu row chosen id='%s'", row.second.c_str());
      return;
    }
}

void focus_popup_row(MonoObject *menu, int focus) {
  if (focus <= 0 ||
      !find_method(mono_object_get_class(menu), "set_SelectedIndex", 1))
    return;
  int32_t index = focus;
  void *arguments[1] = {&index};
  call(menu, "set_SelectedIndex", arguments, 1);
}

} // namespace

bool ShowPopupMenu(const std::vector<MenuEntry> &entries,
                   const std::string &header, std::string *failure,
                   MonoObject *scene, bool stay_open, int focus) {
  Failure result{failure};
  if (!PopupMenuAvailable())
    return result.fail("popup menu selection hook not installed");
  if (!scene)
    scene = host_scene();
  if (!scene)
    return result.fail("no Home scene to open the menu on");
  MonoObject *menu = create(find_class(kPuiImage, kUi3, "PopupMenu"));
  if (!fill_popup(menu, entries, header, result))
    return false;
  if (stay_open &&
      find_method(mono_object_get_class(menu), "set_DismissOnSelect", 1)) {
    bool dismiss = false;
    void *arguments[1] = {&dismiss};
    call(menu, "set_DismissOnSelect", arguments, 1);
  }
  focus_popup_row(menu, focus);
  void *scene_args[1] = {scene};
  std::string thrown;
  if (!call(menu, "Open", scene_args, 1, &thrown))
    return result.fail(thrown);
  if (s_menu_handle)
    mono_gchandle_free(s_menu_handle);
  s_menu_handle = mono_gchandle_new(menu, 0);
  s_menu_selected = -1;
  s_menu_stays = stay_open;
  s_menu_was_open = false;
  LOG_INFO("[SYSDIALOG] popup menu opened entries=%zu stay=%d", entries.size(),
           stay_open ? 1 : 0);
  return true;
}

bool UpdatePopupMenu(const std::vector<MenuEntry> &entries,
                     const std::string &header, int focus,
                     std::string *failure) {
  Failure result{failure};
  MonoObject *menu =
      s_menu_handle ? mono_gchandle_get_target(s_menu_handle) : nullptr;
  if (!menu || !popup_is_open(menu))
    return result.fail("no popup menu on screen");
  if (!fill_popup(menu, entries, header, result))
    return false;
  focus_popup_row(menu, focus);
  s_menu_selected = -1;
  LOG_INFO("[SYSDIALOG] popup menu now has entries=%zu", entries.size());
  return true;
}

bool PopupMenuDismissed() {
  if (!s_menu_handle || !s_menu_stays)
    return false;
  MonoObject *menu = mono_gchandle_get_target(s_menu_handle);
  const bool open = popup_is_open(menu);
  if (open) {
    s_menu_was_open = true;
    return false;
  }
  if (menu && !s_menu_was_open)
    return false; /* still on its way in */
  mono_gchandle_free(s_menu_handle);
  s_menu_handle = 0;
  s_menu_stays = false;
  return true;
}

void DebugPressPopupMenu(int index) {
  if (s_menu_handle)
    s_menu_selected = index;
}

void ClosePopupMenu() {
  MonoObject *menu =
      s_menu_handle ? mono_gchandle_get_target(s_menu_handle) : nullptr;
  if (menu && popup_is_open(menu)) {
    std::string thrown;
    call(menu, "Close", nullptr, 0, &thrown);
  }
  if (s_menu_handle)
    mono_gchandle_free(s_menu_handle);
  s_menu_handle = 0;
  s_menu_stays = false;
}

bool PopupMenuSwitchValue(int index, bool *value) {
  if (index < 0 || static_cast<size_t>(index) >= s_menu_entries.size() ||
      !value)
    return false;
  MonoObject *config = mono_gchandle_get_target(s_menu_entries[index]);
  MonoObject *menu =
      s_menu_handle ? mono_gchandle_get_target(s_menu_handle) : nullptr;
  if (!config || !menu)
    return false;
  MonoMethod *getter = find_method(mono_object_get_class(config), "get_Value", 0);
  if (!getter)
    return false;
  MonoObject *exception = nullptr;
  MonoObject *boxed = mono_runtime_invoke(getter, config, nullptr, &exception);
  if (exception || !boxed)
    return false;
  *value = *static_cast<bool *>(mono_object_unbox(boxed));
  return true;
}

void SetPopupMenuEntryIcon(int index, const std::string &icon) {
  if (index < 0 || static_cast<size_t>(index) >= s_menu_entries.size())
    return;
  MonoObject *config = mono_gchandle_get_target(s_menu_entries[index]);
  if (!config)
    return;
  void *args[1] = {mono_string_new(mono_domain_get(), icon.c_str())};
  call(config, "set_IconUri", args, 1);
}

bool PollPopupMenu(int *index) {
  if (s_menu_selected < 0 || !index)
    return false;
  *index = s_menu_selected;
  s_menu_selected = -1;
  if (s_menu_stays)
    return true; /* still on screen: ours to close */
  if (s_menu_handle)
    mono_gchandle_free(s_menu_handle);
  s_menu_handle = 0;
  return true;
}

bool PollPopupSubmenu(std::string *id) {
  if (s_submenu_selected.empty() || !id)
    return false;
  *id = s_submenu_selected;
  s_submenu_selected.clear();
  return true;
}

MonoObject *FocusedScene() {
  /* Application.Instance.FocusActiveScene */
  MonoClass *application =
      find_class(kPuiImage, "Sce.PlayStation.PUI", "Application");
  MonoMethod *instance =
      application ? mono_class_get_method_from_name(application, "get_Instance", 0)
                  : nullptr;
  MonoObject *exception = nullptr;
  MonoObject *app =
      instance ? mono_runtime_invoke(instance, nullptr, nullptr, &exception)
               : nullptr;
  if (exception || !app)
    return nullptr;
  MonoMethod *focused =
      find_method(mono_object_get_class(app), "get_FocusActiveScene", 0);
  MonoObject *scene =
      focused ? mono_runtime_invoke(focused, app, nullptr, &exception) : nullptr;
  return exception ? nullptr : scene;
}

namespace {

void free_native_menu() {
  for (uint32_t handle : s_native_items)
    if (handle)
      mono_gchandle_free(handle);
  s_native_items.clear();
  s_native_kinds.clear();
  s_native_rows.clear();
  if (s_native_menu)
    mono_gchandle_free(s_native_menu);
  s_native_menu = 0;
  if (MonoObject *anchor = s_native_anchor
                               ? mono_gchandle_get_target(s_native_anchor)
                               : nullptr) {
    std::string thrown;
    call(anchor, "RemoveFromParent", nullptr, 0, &thrown);
    call(anchor, "Dispose", nullptr, 0, &thrown);
  }
  if (s_native_anchor)
    mono_gchandle_free(s_native_anchor);
  s_native_anchor = 0;
  s_native_selected = -1;
  s_native_was_open = false;
}

MonoObject *native_item(int index) {
  if (index < 0 || static_cast<size_t>(index) >= s_native_items.size())
    return nullptr;
  return mono_gchandle_get_target(s_native_items[static_cast<size_t>(index)]);
}

bool native_menu_is_open() {
  MonoObject *menu =
      s_native_menu ? mono_gchandle_get_target(s_native_menu) : nullptr;
  MonoMethod *getter =
      menu ? find_method(mono_object_get_class(menu), "get_IsOpened", 0)
           : nullptr;
  MonoObject *exception = nullptr;
  MonoObject *boxed =
      getter ? mono_runtime_invoke(getter, menu, nullptr, &exception) : nullptr;
  return !exception && boxed && *static_cast<bool *>(mono_object_unbox(boxed));
}

} // namespace

bool NativeMenuOpen() { return s_native_menu != 0; }

void DebugPressNativeMenu(int index) {
  if (s_native_menu)
    s_native_selected = index;
}

void CloseNativeMenu() {
  MonoObject *menu =
      s_native_menu ? mono_gchandle_get_target(s_native_menu) : nullptr;
  if (menu && native_menu_is_open()) {
    std::string thrown;
    call(menu, "Close", nullptr, 0, &thrown);
  }
  free_native_menu();
}

bool OpenNativeMenu(const std::vector<NativeMenuItem> &items,
                    const std::string &header, int focus, std::string *failure,
                    MonoObject *scene) {
  Failure result{failure};
  if (!PopupMenuAvailable())
    return result.fail("popup menu selection hook not installed");
  if (!scene)
    scene = FocusedScene();
  if (!scene)
    scene = host_scene();
  if (!scene)
    return result.fail("no scene to open the menu on");
  CloseNativeMenu();
  /* The anchor: a point at the right edge, half way down the scene. */
  MonoObject *menu = nullptr;
  {
    MonoMethod *root_getter =
        find_method(mono_object_get_class(scene), "get_RootWidget", 0);
    MonoObject *exception = nullptr;
    MonoObject *root = root_getter ? mono_runtime_invoke(root_getter, scene,
                                                         nullptr, &exception)
                                   : nullptr;
    MonoObject *anchor =
        root && !exception ? create(find_class(kPuiImage, kUi3, "Widget"))
                           : nullptr;
    MonoClass *menu_class = find_class(kPuiImage, kUi3, "PopupMenu");
    MonoMethod *with_target =
        menu_class ? find_method_taking(menu_class, ".ctor", 1, "Widget")
                   : nullptr;
    if (anchor && with_target) {
      float width = 1920.0f, height = 1080.0f, one = 1.0f;
      for (int axis = 0; axis < 2; ++axis) {
        MonoMethod *getter = find_method(mono_object_get_class(root),
                                         axis ? "get_Height" : "get_Width", 0);
        MonoObject *boxed = getter ? mono_runtime_invoke(getter, root, nullptr,
                                                         &exception)
                                   : nullptr;
        if (boxed && !exception)
          *(axis ? &height : &width) =
              *static_cast<float *>(mono_object_unbox(boxed));
      }
      float x = width - 96.0f, y = height * 0.5f;
      void *arguments[1] = {&x};
      call(anchor, "set_X", arguments, 1);
      arguments[0] = &y;
      call(anchor, "set_Y", arguments, 1);
      arguments[0] = &one;
      call(anchor, "set_Width", arguments, 1);
      call(anchor, "set_Height", arguments, 1);
      void *child[1] = {anchor};
      std::string thrown;
      if (call(root, "AppendChild", child, 1, &thrown)) {
        s_native_anchor = mono_gchandle_new(anchor, 0);
        menu = mono_object_new(mono_domain_get(), menu_class);
        void *target[1] = {anchor};
        exception = nullptr;
        if (menu)
          mono_runtime_invoke(with_target, menu, target, &exception);
        if (exception)
          menu = nullptr;
      }
    }
  }
  const bool anchored = menu != nullptr;
  if (!menu)
    menu = create(find_class(kPuiImage, kUi3, "PopupMenu"));
  MonoClass *button_class = find_class(kPuiImage, kUi3, "MenuListButtonConfig");
  MonoMethod *from_text =
      button_class ? find_method_taking(button_class, "op_Implicit", 1,
                                        "System.String")
                   : nullptr;
  if (!menu || !from_text)
    return result.fail("popup menu classes not found");
  static MonoClass *(*class_from_type)(MonoType *) = nullptr;
  if (!class_from_type)
    class_from_type = reinterpret_cast<decltype(class_from_type)>(
        kernel_dynlib_dlsym(-1, g_libmono_handle, "mono_class_from_mono_type"));
  MonoMethod *items_getter =
      find_method(mono_object_get_class(menu), "get_ContextItems", 0);
  MonoMethodSignature *signature =
      items_getter ? mono_method_signature(items_getter) : nullptr;
  MonoObject *list =
      signature && class_from_type
          ? create(class_from_type(mono_signature_get_return_type(signature)))
          : nullptr;
  if (!list)
    return result.fail("cannot create the menu's row list");
  s_native_menu = mono_gchandle_new(menu, 0);

  bool first = true;
  for (const NativeMenuItem &item : items) {
    MonoObject *exception = nullptr;
    MonoObject *config = nullptr;
    if (item.kind == NativeMenuItem::Kind::Switch) {
      config = create(find_class(kPuiImage, kUi3, "MenuListSwitchConfig"));
      if (config) {
        bool value = item.on;
        void *arguments[1] = {&value};
        call(config, "set_Value", arguments, 1);
      }
    } else if (item.kind == NativeMenuItem::Kind::Slider) {
      /* marks for a few steps, a plain bar for many */
      const float span = (item.maximum - item.minimum) /
                         (item.step > 0 ? item.step : 1.0f);
      config = create(find_class(kPuiImage, kUi3,
                                 span > 16.0f ? "MenuListContinuousSliderConfig"
                                              : "MenuListDiscreteSliderConfig"));
      if (config) {
        float minimum = item.minimum, maximum = item.maximum;
        float step = item.step > 0 ? item.step : 1.0f, value = item.number;
        void *arguments[1] = {&minimum};
        call(config, "set_MinValue", arguments, 1);
        arguments[0] = &maximum;
        call(config, "set_MaxValue", arguments, 1);
        arguments[0] = &step;
        call(config, "set_Step", arguments, 1);
        arguments[0] = &value;
        call(config, "set_Value", arguments, 1);
      }
    } else if (item.kind == NativeMenuItem::Kind::Choice) {
      config = create(find_class(kPuiImage, kUi3, "MenuListDropdownConfig"));
      MonoClass *choice_class =
          find_class(kPuiImage, kUi3, "MenuListSelectableConfig");
      MonoMethod *choice_from_text =
          choice_class ? find_method_taking(choice_class, "op_Implicit", 1,
                                            "System.String")
                       : nullptr;
      MonoMethod *choices_getter =
          config ? find_method(mono_object_get_class(config), "get_Items", 0)
                 : nullptr;
      MonoMethodSignature *choices_signature =
          choices_getter ? mono_method_signature(choices_getter) : nullptr;
      MonoObject *choices =
          choices_signature && choice_from_text
              ? create(class_from_type(
                    mono_signature_get_return_type(choices_signature)))
              : nullptr;
      if (!choices) {
        config = nullptr;
      } else {
        for (const std::string &name : item.choices) {
          void *arguments[1] = {
              mono_string_new(mono_domain_get(), name.c_str())};
          MonoObject *thrown = nullptr;
          MonoObject *choice =
              mono_runtime_invoke(choice_from_text, nullptr, arguments, &thrown);
          void *add_arguments[1] = {choice};
          if (choice && !thrown)
            call(choices, "Add", add_arguments, 1);
        }
        void *arguments[1] = {choices};
        call(config, "set_Items", arguments, 1);
        int32_t chosen = item.chosen;
        arguments[0] = &chosen;
        call(config, "set_SelectedIndex", arguments, 1);
      }
    } else {
      void *arguments[1] = {
          mono_string_new(mono_domain_get(), item.title.c_str())};
      config = mono_runtime_invoke(from_text, nullptr, arguments, &exception);
      if (config && !exception)
        set_text_property(config, "set_ButtonTitle", item.value);
    }
    if (exception || !config) {
      free_native_menu();
      return result.fail("menu row: " + exception_text(exception));
    }
    s_native_items.push_back(mono_gchandle_new(config, 0));
    s_native_kinds.push_back(static_cast<int>(item.kind));
    NativeRow row;
    row.number = item.number;
    row.chosen = item.chosen;
    row.title = item.title;
    row.step = item.step > 0 ? item.step : 1.0f;
    s_native_rows.push_back(row);
    if (item.kind != NativeMenuItem::Kind::Button)
      set_text_property(config, "set_Title", item.title);
    set_text_property(config, "set_Description", item.description);
    if (first)
      set_text_property(config, "set_Header", header);
    first = false;
    if (!item.icon.empty()) {
      void *arguments[1] = {
          mono_string_new(mono_domain_get(), item.icon.c_str())};
      call(config, "set_IconUri", arguments, 1);
    }
    if (!item.enabled) {
      bool enabled = false;
      void *arguments[1] = {&enabled};
      call(config, "set_Enabled", arguments, 1);
    }
    void *add_arguments[1] = {config};
    std::string thrown;
    if (!call(list, "Add", add_arguments, 1, &thrown)) {
      free_native_menu();
      return result.fail(thrown);
    }
  }
  /* It is ours to close: a press must not dismiss it. */
  if (find_method(mono_object_get_class(menu), "set_DismissOnSelect", 1)) {
    bool dismiss = false;
    void *arguments[1] = {&dismiss};
    call(menu, "set_DismissOnSelect", arguments, 1);
  }
  /* To the left of the anchor, centred on it. */
  if (anchored) {
    int32_t left_of = 1; /* RelativePosition.Left */
    int32_t middle = 2;  /* VerticalAlignment.Center */
    void *arguments[1] = {&left_of};
    if (find_method(mono_object_get_class(menu), "set_RelativePosition", 1))
      call(menu, "set_RelativePosition", arguments, 1);
    arguments[0] = &middle;
    if (find_method(mono_object_get_class(menu), "set_VerticalAlignment", 1))
      call(menu, "set_VerticalAlignment", arguments, 1);
  }
  if (find_method(mono_object_get_class(menu), "set_MinWidth", 1)) {
    float width = 720.0f; /* one width for every page */
    void *arguments[1] = {&width};
    call(menu, "set_MinWidth", arguments, 1);
  }
  void *list_arguments[1] = {list};
  void *scene_arguments[1] = {scene};
  std::string thrown;
  if (!call(menu, "set_ContextItems", list_arguments, 1, &thrown)) {
    free_native_menu();
    return result.fail(thrown);
  }
  if (focus > 0 && focus < static_cast<int>(items.size())) {
    int32_t index = focus;
    void *arguments[1] = {&index};
    call(menu, "set_SelectedIndex", arguments, 1);
  }
  if (!call(menu, "Open", scene_arguments, 1, &thrown)) {
    free_native_menu();
    return result.fail(thrown);
  }
  s_native_selected = -1;
  s_native_was_open = false;
  return true;
}

namespace {

MonoObject *property_object(MonoObject *object, const char *getter) {
  MonoMethod *method =
      object ? find_method(mono_object_get_class(object), getter, 0) : nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value =
      method ? mono_runtime_invoke(method, object, nullptr, &exception)
             : nullptr;
  return exception ? nullptr : value;
}

bool property_float(MonoObject *object, const char *getter, float *out) {
  MonoObject *boxed = property_object(object, getter);
  if (!boxed)
    return false;
  *out = *static_cast<float *>(mono_object_unbox(boxed));
  return true;
}

void set_property(MonoObject *object, const char *setter, void *value) {
  void *arguments[1] = {value};
  if (object && find_method(mono_object_get_class(object), setter, 1))
    call(object, setter, arguments, 1);
}

MonoObject *named_descendant(MonoObject *widget, const char *name, int depth) {
  if (!widget || depth > 4)
    return nullptr;
  MonoObject *child = property_object(widget, "get_FirstChild");
  for (int guard = 0; child && guard < 16; ++guard) {
    MonoObject *text = property_object(child, "get_Name");
    if (text && Mono_to_String(reinterpret_cast<MonoString *>(text)) == name)
      return child;
    if (MonoObject *found = named_descendant(child, name, depth + 1))
      return found;
    child = property_object(child, "get_NextSibling");
  }
  return nullptr;
}

/* Once a frame while the menu is up.
 *  - The card goes to the right of the screen, vertically centred: PUI puts
 *    a menu that hangs off no widget wherever it likes.
 *  - Slider rows are adjusted by Left and Right as soon as they have the
 *    focus (PUI's default wants a press first), and slider and drop-down
 *    rows show what they hold: a row's widget is given its value when it
 *    appears, and read when it was already there, which is the user having
 *    changed it. */
void maintain_native_menu() {
  MonoObject *menu =
      s_native_menu ? mono_gchandle_get_target(s_native_menu) : nullptr;
  MonoObject *list = property_object(menu, "get_ListPanel");
  MonoObject *frame = property_object(list, "get_Parent");
  MonoObject *root = property_object(frame, "get_Parent");
  float frame_width = 0, frame_height = 0, root_width = 0, root_height = 0;
  if (frame && root && property_float(frame, "get_Width", &frame_width) &&
      property_float(frame, "get_Height", &frame_height) &&
      property_float(root, "get_Width", &root_width) &&
      property_float(root, "get_Height", &root_height) && frame_width > 8 &&
      root_width > frame_width) {
    float x = std::round(root_width - frame_width - 96.0f);
    float y = std::round(std::max(48.0f, (root_height - frame_height) * 0.5f));
    float now_x = 0, now_y = 0;
    property_float(frame, "get_X", &now_x);
    property_float(frame, "get_Y", &now_y);
    static int logged = 0;
    if (logged < 6 && (std::fabs(now_x - x) > 0.5f ||
                       std::fabs(now_y - y) > 0.5f)) {
      ++logged;
      LOG_INFO("[SYSDIALOG] menu card at %.0f,%.0f (%.0fx%.0f), wanted %.0f,%.0f",
               now_x, now_y, frame_width, frame_height, x, y);
    }
    /* Only a menu PUI placed by itself is moved; one hung off our anchor is
       where PUI put it from the first frame. */
    if (!s_native_anchor) {
      if (std::fabs(now_x - x) > 0.5f)
        set_property(frame, "set_X", &x);
      if (std::fabs(now_y - y) > 0.5f)
        set_property(frame, "set_Y", &y);
    }
  }

  std::vector<bool> seen(s_native_rows.size(), false);
  MonoObject *container = named_descendant(list, "ItemContainer", 0);
  MonoObject *shim = property_object(container, "get_FirstChild");
  for (int guard = 0; shim && guard < 64; ++guard) {
    float y = 0, height = 0;
    MonoObject *row = property_object(shim, "get_FirstChild");
    if (row && property_float(shim, "get_Y", &y) &&
        property_float(shim, "get_Height", &height) && height > 8) {
      const size_t index = static_cast<size_t>(std::lround(y / height));
      if (index < s_native_rows.size()) {
        NativeRow &state = s_native_rows[index];
        const int kind = s_native_kinds[index];
        if (kind == static_cast<int>(NativeMenuItem::Kind::Slider)) {
          bool press_first = false;
          set_property(row, "set_ClickToEdit", &press_first);
          float value = state.number;
          if (property_float(row, "get_Value", &value)) {
            if (!state.seen) {
              state.synced = false;
              state.labelled = -1e30f; /* a new widget: label it again */
            }
            if (!state.synced) {
              if (std::fabs(value - state.number) <= 0.0001f)
                state.synced = true;
              else
                set_property(row, "set_Value", &state.number);
            } else {
              state.number = value; /* the user moved it */
            }
          }
          /* the number, after the title: a slider row has no place for it */
          if (state.labelled != state.number) {
            state.labelled = state.number;
            char number[32];
            if (state.step >= 1.0f)
              std::snprintf(number, sizeof(number), "%d",
                            static_cast<int>(std::lround(state.number)));
            else
              std::snprintf(number, sizeof(number), "%.2f",
                            static_cast<double>(state.number));
            MonoObject *label = named_descendant(row, "title", 3);
            void *text = mono_string_new(
                mono_domain_get(), (state.title + ":  " + number).c_str());
            set_property(label, "set_Text", text);
          }
          seen[index] = true;
        } else if (kind == static_cast<int>(NativeMenuItem::Kind::Choice)) {
          /* the row's Value is its selection; SelectedIndex cannot be set
             on the widget */
          MonoObject *boxed = property_object(row, "get_Value");
          if (boxed) {
            const int32_t chosen = *static_cast<int32_t *>(mono_object_unbox(boxed));
            if (!state.seen)
              state.synced = false;
            if (!state.synced) {
              if (chosen == state.chosen) {
                state.synced = true;
              } else {
                int32_t wanted = state.chosen;
                set_property(row, "set_Value", &wanted);
              }
            } else {
              state.chosen = chosen; /* the user picked another */
            }
          }
          seen[index] = true;
        }
      }
    }
    shim = property_object(shim, "get_NextSibling");
  }
  for (size_t index = 0; index < s_native_rows.size(); ++index)
    s_native_rows[index].seen = seen[index];
}

} // namespace

NativeMenuEvent PollNativeMenu(int *index) {
  if (!s_native_menu)
    return NativeMenuEvent::None;
  if (native_menu_is_open())
    maintain_native_menu();
  if (s_native_selected >= 0) {
    if (index)
      *index = s_native_selected;
    s_native_selected = -1;
    return NativeMenuEvent::Selected;
  }
  /* Open() takes a frame or two to show it; closed counts once it has been
     seen open. */
  const bool open = native_menu_is_open();
  if (open) {
    s_native_was_open = true;
  } else if (s_native_was_open) {
    free_native_menu();
    return NativeMenuEvent::Closed;
  }
  return NativeMenuEvent::None;
}

bool NativeMenuSwitch(int index, bool *value) {
  MonoObject *config = native_item(index);
  MonoMethod *getter =
      config ? find_method(mono_object_get_class(config), "get_Value", 0)
             : nullptr;
  if (!getter || !value ||
      s_native_kinds[static_cast<size_t>(index)] !=
          static_cast<int>(NativeMenuItem::Kind::Switch))
    return false;
  MonoObject *exception = nullptr;
  MonoObject *boxed = mono_runtime_invoke(getter, config, nullptr, &exception);
  if (exception || !boxed)
    return false;
  *value = *static_cast<bool *>(mono_object_unbox(boxed));
  return true;
}

bool NativeMenuSlider(int index, float *value) {
  if (index < 0 || static_cast<size_t>(index) >= s_native_rows.size() ||
      !value ||
      s_native_kinds[static_cast<size_t>(index)] !=
          static_cast<int>(NativeMenuItem::Kind::Slider))
    return false;
  *value = s_native_rows[static_cast<size_t>(index)].number;
  return true;
}

bool NativeMenuChoice(int index, int *chosen) {
  if (index < 0 || static_cast<size_t>(index) >= s_native_rows.size() ||
      !chosen ||
      s_native_kinds[static_cast<size_t>(index)] !=
          static_cast<int>(NativeMenuItem::Kind::Choice))
    return false;
  *chosen = s_native_rows[static_cast<size_t>(index)].chosen;
  return true;
}

void CloseLast() {
  MonoObject *object =
      s_last_handle ? mono_gchandle_get_target(s_last_handle) : nullptr;
  if (!object)
    return;
  std::string thrown;
  call(object, s_last_is_error ? "CloseDialog" : "Close", nullptr, 0, &thrown);
  remember(nullptr, false);
}

} // namespace SystemDialogs
