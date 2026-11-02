#include "aniketos_shell/home/internet_switch.hpp"

#include "hooked_funcs.hpp"

#include <onion/log.h>

#include <cstdint>
#include <initializer_list>

/* The same registry flag Settings > Network writes (found by the name of its
   key constant), followed by the same connect or disconnect request.
   Everything is looked up by name in ShellUI's managed assemblies. */
namespace InternetSwitch {
namespace {

MonoMethod *static_method(const char *dll, const char *space, const char *klass,
                          const char *method, int parameters) {
  MonoImage *image = getDLLimage(dll);
  MonoClass *type = image ? mono_class_from_name(image, space, klass) : nullptr;
  return type ? mono_class_get_method_from_name(type, method, parameters)
              : nullptr;
}

MonoMethod *registry_method(const char *name) {
  return static_method("ReactNative.PUI.dll", "ReactNative.SystemMonitor",
                       "SystemMonitorServer", name, 2);
}

bool internet_flag_key(int32_t *key) {
  MonoImage *image = getDLLimage("Sce.Vsh.Registry.dll");
  MonoClass *registry =
      image ? mono_class_from_name(image, "Sce.Vsh", "Registry") : nullptr;
  MonoClassField *field =
      registry ? mono_class_get_field_from_name(
                     registry, "SCE_REGMGR_ENT_KEY_NET_COMMON_net_flag")
               : nullptr;
  if (!field)
    return false;
  mono_field_static_get_value(mono_class_vtable(mono_domain_get(), registry),
                              field, key);
  return *key != 0;
}

} // namespace

bool Read(bool *enabled) {
  int32_t key = 0;
  MonoMethod *get = registry_method("SceRegMgrGetInt");
  if (!get || !internet_flag_key(&key))
    return false;
  int32_t value = 0;
  void *args[2] = {&key, &value};
  MonoObject *exception = nullptr;
  MonoObject *result = mono_runtime_invoke(get, nullptr, args, &exception);
  if (exception || !result ||
      *static_cast<int32_t *>(mono_object_unbox(result)) != 0)
    return false;
  *enabled = value != 0;
  return true;
}

bool Set(bool enabled) {
  int32_t key = 0;
  MonoMethod *set = registry_method("SceRegMgrSetInt");
  if (!set || !internet_flag_key(&key))
    return false;
  int32_t value = enabled ? 1 : 0;
  void *args[2] = {&key, &value};
  MonoObject *exception = nullptr;
  MonoObject *result = mono_runtime_invoke(set, nullptr, args, &exception);
  const int32_t set_result =
      !exception && result ? *static_cast<int32_t *>(mono_object_unbox(result))
                           : -1;
  /* The flag alone is only read at the next connection attempt; ask for the
     change now, as Settings does. */
  int32_t request_result = -1;
  for (const char *name :
       enabled ? std::initializer_list<const char *>{"NetCtlConnectWithRetry",
                                                     "NetCtlConnect",
                                                     "NetCtlConnectNative"}
               : std::initializer_list<const char *>{"NetCtlDisconnect",
                                                     "NetCtlDisconnectNative"}) {
    MonoMethod *request =
        static_method("Sce.Vsh.ShellUI.Legacy.dll",
                      "Sce.Vsh.ShellUI.Settings.NetCtl", "NetCtrlWrapper",
                      name, 0);
    if (!request)
      continue;
    MonoObject *thrown = nullptr;
    MonoObject *answer = mono_runtime_invoke(request, nullptr, nullptr, &thrown);
    if (!thrown && answer)
      request_result = *static_cast<int32_t *>(mono_object_unbox(answer));
    LOG_INFO("[HOMEMENU] %s -> 0x%x%s", name,
             static_cast<unsigned>(request_result), thrown ? " (threw)" : "");
    if (!thrown)
      break;
  }
  LOG_INFO("[HOMEMENU] internet flag set to %d: registry=0x%x", value,
           static_cast<unsigned>(set_result));
  return set_result == 0;
}

} // namespace InternetSwitch
