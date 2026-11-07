#include "aniketos_shell/system_ui/system_dialogs.hpp"

#include "aniketos_ui/home_scene.hpp"
#include "system_dialogs_internal.hpp"

#include <cstring>
#include <initializer_list>

namespace SystemDialogs {
namespace {

constexpr const char *kPuiImage = "Sce.PlayStation.PUI.dll";

uint32_t s_alert_handle = 0;  /* the alert PollAlert watches */
uint32_t s_prompt_handle = 0; /* the prompt PollPrompt watches */
uint64_t s_prompt_token = 0;
uint64_t s_prompt_next_token = 0;

MonoClass *find_class(const char *space, const char *name) {
  MonoImage *image = getDLLimage(kPuiImage);
  return image ? mono_class_from_name(image, space, name) : nullptr;
}

MonoObject *spannable(const std::string &text) {
  static MonoMethod *create_method = nullptr;
  if (!create_method)
    if (MonoClass *klass = ui3_class("SpannableText"))
      create_method =
          find_method_taking(klass, "Create", 1, "System.String");
  if (!create_method)
    return nullptr;
  void *args[1] = {managed_string(text)};
  MonoObject *exception = nullptr;
  MonoObject *value =
      mono_runtime_invoke(create_method, nullptr, args, &exception);
  return exception ? nullptr : value;
}

struct DialogText {
  const char *setter;
  const std::string &text;
};

/* Makes UI3 class `name` with `arguments`, gives it its texts and opens it on
   `scene` (null: the Home scene). `handle` then holds it. */
bool open_dialog(const char *name, void **arguments, int count,
                 std::initializer_list<DialogText> texts, MonoObject *scene,
                 uint32_t &handle, Failure &result) {
  MonoClass *klass = ui3_class(name);
  if (!scene)
    scene = host_scene();
  if (!klass)
    return result.fail(std::string("UI3.") + name + " not found");
  if (!scene)
    return result.fail("no Home scene to open the dialog on");
  MonoObject *dialog = mono_object_new(mono_domain_get(), klass);
  MonoMethod *constructor = Reflect::MethodOf(klass, ".ctor", count);
  if (!dialog || !constructor)
    return result.fail(std::string(name) + " constructor not found");
  MonoObject *exception = nullptr;
  mono_runtime_invoke(constructor, dialog, arguments, &exception);
  if (exception)
    return result.fail(std::string(name) + "..ctor: " +
                       exception_text(exception));
  for (const DialogText &text : texts)
    set_text_property(dialog, text.setter, text.text);
  if (!checked_call(dialog, "Open", scene, result))
    return false;
  Reflect::Hold(handle, dialog, /*strong=*/true);
  return true;
}

} // namespace

std::string exception_text(MonoObject *exception) {
  if (!exception)
    return {};
  MonoObject *nested = nullptr;
  MonoString *managed = mono_object_to_string(exception, &nested);
  if (nested || !managed)
    return "managed exception";
  std::string text = Mono_to_String(managed);
  if (text.size() > 240)
    text.resize(240);
  return text;
}

MonoClass *ui3_class(const char *name) {
  return find_class(kUi3, name);
}

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

MonoMethod *from_string_operator(const char *config_class) {
  MonoClass *klass = ui3_class(config_class);
  return klass ? find_method_taking(klass, "op_Implicit", 1, "System.String")
               : nullptr;
}

MonoObject *config_from_text(MonoMethod *from_text, const std::string &text,
                             MonoObject **exception) {
  void *arguments[1] = {managed_string(text)};
  return mono_runtime_invoke(from_text, nullptr, arguments, exception);
}

MonoString *managed_string(const std::string &text) {
  return mono_string_new(mono_domain_get(), text.c_str());
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

MonoObject *new_list_for(MonoObject *owner, const char *getter) {
  static MonoClass *(*class_from_type)(MonoType *) = nullptr;
  if (!class_from_type)
    class_from_type = reinterpret_cast<decltype(class_from_type)>(
        libmono_symbol("mono_class_from_mono_type"));
  MonoMethod *method = Reflect::MethodOf(owner, getter, 0);
  MonoMethodSignature *signature =
      method ? mono_method_signature(method) : nullptr;
  return signature && class_from_type
             ? create(class_from_type(mono_signature_get_return_type(signature)))
             : nullptr;
}

bool checked_call(MonoObject *object, const char *name, void *argument,
                  Failure &result) {
  MonoMethod *method = Reflect::MethodOf(object, name, 1);
  if (!method)
    return result.fail(std::string("no method ") + name);
  void *arguments[1] = {argument};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, object, arguments, &exception);
  return !exception ||
         result.fail(std::string(name) + ": " + exception_text(exception));
}

MonoObject *property_object(MonoObject *object, const char *getter) {
  MonoObject *value = nullptr;
  return Reflect::Call(object, getter, 0, nullptr, &value) ? value : nullptr;
}

void set_property(MonoObject *object, const char *setter, void *value) {
  void *arguments[1] = {value};
  Reflect::Call(object, setter, 1, arguments);
}

void set_text_property(MonoObject *object, const char *setter,
                       const std::string &text) {
  if (text.empty())
    return;
  if (MonoObject *value = spannable(text))
    set_property(object, setter, value);
}

void decorate_row(MonoObject *config, const std::string &icon, bool enabled) {
  if (!icon.empty())
    set_property(config, "set_IconUri", managed_string(icon));
  if (!enabled) {
    bool value = false;
    set_property(config, "set_Enabled", &value);
  }
}

MonoObject *host_scene() {
  return property_object(AniketosUI::HomeScene::Root(), "get_ParentScene");
}

MonoObject *FocusedScene() {
  /* Application.Instance.FocusActiveScene */
  MonoClass *application = find_class("Sce.PlayStation.PUI", "Application");
  MonoMethod *instance =
      application ? mono_class_get_method_from_name(application, "get_Instance", 0)
                  : nullptr;
  MonoObject *exception = nullptr;
  MonoObject *app =
      instance ? mono_runtime_invoke(instance, nullptr, nullptr, &exception)
               : nullptr;
  return exception ? nullptr : property_object(app, "get_FocusActiveScene");
}

bool ShowAlert(const AlertOptions &options, std::string *failure,
               MonoObject *scene) {
  Failure result{failure};
  int32_t style = static_cast<int32_t>(options.style);
  void *arguments[1] = {&style};
  if (!open_dialog("AlertDialog", arguments, 1,
                   {{"set_DialogTitle", options.title},
                    {"set_Message", options.message},
                    {"set_PositiveButtonTitle", options.positive},
                    {"set_NegativeButtonTitle", options.negative},
                    {"set_NeutralButtonTitle", options.neutral},
                    {"set_SecondNeutralButtonTitle", options.second_neutral},
                    {"set_CheckBoxTitle", options.check_box}},
                   scene, s_alert_handle, result))
    return false;
  LOG_INFO("[SYSDIALOG] alert opened style=%d", style);
  return true;
}

bool PollAlert(int *result) {
  /* A Nullable<DialogResult> comes back boxed: null until a button is
     pressed, the enum value afterwards. */
  int32_t button = 0;
  if (!result ||
      !property_value(Reflect::Target(s_alert_handle), "get_Result", &button))
    return false;
  *result = button;
  Reflect::Release(s_alert_handle);
  return true;
}

bool ShowPrompt(const PromptOptions &options, std::string *failure,
                MonoObject *scene, uint64_t *token) {
  Failure result{failure};
  void *arguments[2] = {managed_string(options.message),
                        managed_string(options.text)};
  if (!open_dialog("JavaScriptPromptDialog", arguments, 2,
                   {{"set_DialogTitle", options.title},
                    {"set_PositiveButtonTitle", options.positive},
                    {"set_NegativeButtonTitle", options.negative}},
                   scene, s_prompt_handle, result))
    return false;
  s_prompt_token = ++s_prompt_next_token;
  if (token)
    *token = s_prompt_token;
  LOG_INFO("[SYSDIALOG] prompt opened");
  return true;
}

bool PromptPending(uint64_t token) {
  return token != 0 && token == s_prompt_token &&
         Reflect::Target(s_prompt_handle) != nullptr;
}

bool PollPrompt(uint64_t token, bool *confirmed, std::string *text) {
  if (token == 0 || token != s_prompt_token)
    return false;
  MonoObject *dialog = Reflect::Target(s_prompt_handle);
  int32_t button = 0;
  if (!property_value(dialog, "get_Result", &button))
    return false; /* still open */
  /* The field lives in the dialog's body; its text is what was typed. */
  std::string typed;
  MonoClassField *field = Reflect::FindField(dialog, "body");
  MonoObject *body = nullptr;
  if (field)
    mono_field_get_value(dialog, field, &body);
  for (const char *name : {"get_InputText", "get_Text"})
    if (MonoObject *value = property_object(body, name)) {
      typed = Mono_to_String(reinterpret_cast<MonoString *>(value));
      break;
    }
  Reflect::Release(s_prompt_handle);
  s_prompt_token = 0;
  if (confirmed)
    *confirmed = button == 0;
  if (text)
    *text = typed;
  LOG_INFO("[SYSDIALOG] prompt closed button=%d length=%zu", button,
           typed.size());
  return true;
}

} // namespace SystemDialogs
