#include "x5guard/native_shields.hpp"

#include "aniketos_ui/mono_reflect.hpp"
#include "hooked_funcs.hpp"
#include "x5guard/guard.hpp"
#include "x5guard/guard_banner.hpp"

#include <onion/detour.h>
#include <onion/log.h>

#include <cstdint>
#include <string>

/* Sign-in and system update actions ShellUI runs in managed code, where the
   JS shield never sees them. Every detour asks the shield's switch at call
   time and behaves stock when it is off. None of the methods runs at boot:
   each is a button or a page action. */
namespace X5Guard {
namespace {

namespace Reflect = AniketosUI::Reflect;

/* The links the JS shield's blockedUrl() stops, named for the report; null
   for any other link (game patches, PSN status, store...). */
const char *blocked_link(const std::string &url) {
  if (url.rfind("psaccounts:signin", 0) == 0)
    return "psaccounts:signin";
  if (url.rfind("pssmdlg:", 0) == 0 && url.find("SystemUpdater") != std::string::npos)
    return "SystemUpdaterDialog";
  if (url.rfind("pssettings:", 0) == 0 &&
      (url.find("system_software_update_settings") != std::string::npos ||
       url.find("entry=update") != std::string::npos))
    return "pssettings update";
  return nullptr;
}

/* Sce.Vsh.UILib: static PropertyGenerator.OpenLink(int userId, string url),
   where the common error dialog opens a button's link (Update Now, Sign In).
   Skipping it leaves the dialog's own close path untouched; nothing waits
   for a link to open. */
void (*s_open_link)(int32_t, MonoString *) = nullptr;

void open_link_hook(int32_t user_id, MonoString *url) {
  if (Enabled(Shield::PsnBlocker)) {
    if (const char *what = blocked_link(Mono_to_String(url))) {
      Report(Shield::PsnBlocker, "native.errordialog", what);
      return;
    }
  }
  s_open_link(user_id, url);
}

/* Sce.Vsh.ShellUI.Legacy: Settings > Network > Test Internet Connection asks
   for a system update in its own dialog; not shown at all. */
void (*s_show_update_dialog)(MonoObject *) = nullptr;

void show_update_dialog_hook(MonoObject *page) {
  if (Enabled(Shield::PsnBlocker)) {
    Report(Shield::PsnBlocker, "native.nettest",
           "PageInternetConnectionTest.ShowSystemUpdateDialog");
    return;
  }
  s_show_update_dialog(page);
}

/* Sce.Vsh.ShellUI.Legacy: static LoginMgrUtil.SignupUtility.LaunchSignup(
   int userId, SignupFinishHandler onSignupFinished, SceneBase parentScene).
   The caller waits for its handler, so it is answered as the user's cancel
   (CheckSigninStateJob.SignupResult.UserCanceled, read by name). */
void (*s_launch_signup)(int32_t, MonoObject *, MonoObject *) = nullptr;
int32_t s_user_canceled = 0;

void launch_signup_hook(int32_t user_id, MonoObject *on_finished,
                        MonoObject *parent_scene) {
  if (!Enabled(Shield::PsnBlocker)) {
    s_launch_signup(user_id, on_finished, parent_scene);
    return;
  }
  Report(Shield::PsnBlocker, "native.signup", "SignupUtility.LaunchSignup");
  void *args[1] = {&s_user_canceled};
  if (on_finished && !Reflect::Call(on_finished, "Invoke", 1, args))
    LOG_WARN("[X5GUARD] sign-up handler threw on UserCanceled");
}

template <typename Fn>
bool hook(const char *name, MonoImage *image, MonoClass *klass,
          const char *method, int argc, Fn detour, Fn *original) {
  const uint64_t address =
      klass ? Get_Address_of_Method(image, klass, method, argc) : 0;
  const bool ok = address && InstallDetour(address, reinterpret_cast<void *>(detour),
                                           reinterpret_cast<void **>(original));
  if (!ok) {
    *original = nullptr;
    LOG_WARN("[X5GUARD] %s not %s; that native shield stays off", name,
             address ? "hooked" : "found");
  }
  return ok;
}

} // namespace

void InstallNativeShields() {
  if (s_open_link || s_show_update_dialog || s_launch_signup)
    return;
  PrepareBanner(); /* the first banner then shows at once */
  MonoImage *uilib = getDLLimage("Sce.Vsh.UILib.dll");
  MonoImage *legacy = getDLLimage("Sce.Vsh.ShellUI.Legacy.dll");

  const bool link = uilib &&
      hook("PropertyGenerator.OpenLink", uilib,
           mono_class_from_name(uilib, "Sce.Vsh.ErrorDialog", "PropertyGenerator"),
           "OpenLink", 2, &open_link_hook, &s_open_link);

  const bool nettest = legacy &&
      hook("PageInternetConnectionTest.ShowSystemUpdateDialog", legacy,
           mono_class_from_name(legacy, "Sce.Vsh.ShellUI.Settings.NetworkUI3",
                                "PageInternetConnectionTest"),
           "ShowSystemUpdateDialog", 0, &show_update_dialog_hook,
           &s_show_update_dialog);

  /* Only with the cancel answer at hand: without it the caller would wait. */
  MonoClass *login = legacy ? mono_class_from_name(legacy, "Sce.Vsh.ShellUI",
                                                   "LoginMgrUtil")
                            : nullptr;
  MonoClass *result = Reflect::NestedClass(
      Reflect::NestedClass(login, "CheckSigninStateJob"), "SignupResult");
  const bool canceled =
      Reflect::EnumValue(result, "UserCanceled", &s_user_canceled);
  if (!canceled)
    LOG_WARN("[X5GUARD] SignupResult.UserCanceled not found; sign-up shield "
             "stays off");
  const bool signup =
      canceled && hook("SignupUtility.LaunchSignup", legacy,
                       Reflect::NestedClass(login, "SignupUtility"),
                       "LaunchSignup", 3, &launch_signup_hook, &s_launch_signup);

  LOG_INFO("[X5GUARD] native shields OpenLink=%d NetTest=%d Signup=%d",
           link ? 1 : 0, nettest ? 1 : 0, signup ? 1 : 0);
}

} // namespace X5Guard
