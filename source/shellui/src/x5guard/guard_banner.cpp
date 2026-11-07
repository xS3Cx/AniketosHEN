#include "x5guard/guard_banner.hpp"

#include "aniketos_ui/mono_reflect.hpp"
#include "hooked_funcs.hpp"

#include <atomic>

#include <onion/ipc_server.hpp>
#include <onion/log.h>

#include <cstdint>
#include <ps5/kernel.h>
#include <unistd.h>

namespace X5Guard {
namespace {

using SendById = int (*)(int user_id, bool is_logged, const char *use_case,
                         const char *params);

/* The overlay's debug use case for its persistent banners: it takes the
   banner payload as it is (notif.js, PERSISTENT_BANNER_DEBUG). */
constexpr const char *kUseCase = "PERSISTENT_BANNER_DEBUG";
constexpr int kEveryone = 0xFE;

/* The overlay drops a payload whose itemType has no banner slot of its own
   (createTaskExecuters maps the 16 built-in types: RemotePlay, Broadcast,
   VrThermalWarning, ...). RemotePlay is the one seen working on a console; a
   begin on the same type updates the banner on screen in place. */
constexpr const char *kItemType = "RemotePlay";
constexpr int kDurationMs = 4000;

SendById resolve_send() {
  uint32_t module = 0;
  if (kernel_dynlib_handle(getpid(), "libSceNotification.sprx", &module) != 0) {
    /* Not loaded in ShellUI yet: load it the way package_files.cpp does. */
    uint32_t kernel = 0;
    if (kernel_dynlib_handle(getpid(), "libkernel_sys.sprx", &kernel) != 0 &&
        kernel_dynlib_handle(getpid(), "libkernel.sprx", &kernel) != 0)
      return nullptr;
    const intptr_t load_address =
        kernel_dynlib_dlsym(-1, kernel, "sceKernelLoadStartModule");
    if (load_address <= 0)
      return nullptr;
    const auto load = reinterpret_cast<int (*)(
        const char *, size_t, const void *, uint32_t, void *, int *)>(
        load_address);
    const int loaded = load("/system/common/lib/libSceNotification.sprx", 0,
                            nullptr, 0, nullptr, nullptr);
    if (loaded <= 0)
      return nullptr;
    module = static_cast<uint32_t>(loaded);
  }
  const intptr_t address =
      kernel_dynlib_dlsym(-1, module, "sceNotificationSendById");
  return address > 0 ? reinterpret_cast<SendById>(address) : nullptr;
}

} // namespace

std::string BannerPrefix() {
  /* U+26A0 WARNING SIGN (U+FE0E asks for text presentation; the console
     still draws its yellow emoji), then the name, which the PSN shield's
     script paints the same yellow in the notification overlay (the banner's
     text colour is fixed white there). Keep in step with kPrefix in
     psn_shield_js.inc. */
  return "\xE2\x9A\xA0\xEF\xB8\x8E x5Guard\xE2\x84\xA2";
}

static SendById sender() {
  static const SendById send = resolve_send(); /* thread-safe once */
  return send;
}

void PrepareBanner() { (void)sender(); }

bool ShowBanner(const std::string &text) {
  const SendById send = sender();
  if (!send) {
    LOG_WARN("[x5Guard] sceNotificationSendById not available");
    return false;
  }
  const std::string params =
      std::string("{\"itemType\":\"") + kItemType +
      "\",\"template\":\"PersistentToast\",\"eventType\":\"begin\","
      "\"priority\":\"High\","
      "\"duration\":" +
      std::to_string(kDurationMs) + ",\"viewData\":{\"text\":\"" +
      onion::ipc_json_escape(text) + "\"}}";
  const int rc = send(kEveryone, false, kUseCase, params.c_str());
  if (rc != 0)
    LOG_WARN("[x5Guard] banner refused: 0x%08x", static_cast<unsigned>(rc));
  return rc == 0;
}

namespace {
std::atomic<bool> s_sound_due{false};
} // namespace

void QueueAlertSound() { s_sound_due.store(true, std::memory_order_release); }

void PlayPendingAlertSound() {
  if (!s_sound_due.exchange(false, std::memory_order_acq_rel))
    return;
  /* Application.Instance.PlaySystemSound(SystemSoundType, VibrationConfig),
     all by name in Sce.PlayStation.PUI.dll; resolved once. */
  static bool resolved = false;
  static MonoMethod *instance = nullptr;
  static MonoMethod *play = nullptr;
  static int32_t sound = -1;
  if (!resolved) {
    resolved = true;
    MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
    MonoClass *app = pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI",
                                                "Application")
                         : nullptr;
    MonoClass *type = pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI",
                                                 "SystemSoundType")
                          : nullptr;
    instance = AniketosUI::Reflect::MethodOf(app, "get_Instance", 0);
    play = AniketosUI::Reflect::MethodOf(app, "PlaySystemSound", 2);
    if (!type ||
        !AniketosUI::Reflect::EnumValue(type, "DisabledWidgetPressed", &sound))
      sound = -1;
    if (!instance || !play || sound < 0)
      LOG_WARN("[X5GUARD] no system sound (PlaySystemSound %d, type %d)",
               play ? 1 : 0, sound);
  }
  if (!instance || !play || sound < 0)
    return;
  MonoObject *exception = nullptr;
  MonoObject *app = mono_runtime_invoke(instance, nullptr, nullptr, &exception);
  if (exception || !app)
    return;
  void *arguments[2] = {&sound, nullptr}; /* no VibrationConfig */
  mono_runtime_invoke(play, app, arguments, &exception);
  if (exception)
    LOG_WARN("[X5GUARD] PlaySystemSound threw");
}

} // namespace X5Guard
