#include "x5guard/psn_shield.hpp"

#include "aniketos_ui/monotonic_clock.hpp"
#include "hooked_funcs.hpp"
#include "x5guard/guard.hpp"
#include "x5guard/guard_banner.hpp"

#include <onion/log.h>

#include <cstdint>

namespace X5Guard::PsnShield {

bool WantsApp(const std::string &app) {
  /* The apps that draw a sign-in or update prompt or launch one; the store
     is "elysion", the rest are "rnps-..." (ShellUI's app list). */
  static const char *const kApps[] = {
      "elysion",           "rnps-explore-hub",   "rnps-control-center",
      "rnps-profile",      "rnps-settings",      "rnps-trophy",
      "rnps-game-hub",     "rnps-library",       "rnps-media-gallery",
      "rnps-capture-menu", "rnps-agent-popupgui", "rnps-action-cards",
      "universal-checkout", "millennium-falcon", "millenniumfalcon",
      "rnps-notification-overlay", "rnps-system-modal-dialog",
      "SystemModalDialog", "monte-carlo", "igc-browse", "titlestore-preview",
      "service-hub-psplus", "gaming-lounge", "loginmgr"};
  for (const char *name : kApps)
    if (app.find(name) != std::string::npos)
      return true;
  return false;
}

const char *InjectedScript() {
  static const char script[] =
#include "psn_shield_js.inc"
      ;
  return script;
}

std::string ScriptConfig(const std::string &app) {
  /* the notification overlay also paints the guard banner's prefix red */
  const bool overlay = app.find("rnps-notification-overlay") != std::string::npos;
  return std::string("globalThis.__x5guardPsn = {overlay: ") +
         (overlay ? "true" : "false") + "};";
}

/* The apps ShellUI starts with itself (the notification overlay above
   all) ran their bundle before the script hook was in: the overlay is
   reloaded once, a few seconds after the HEN is up, so the system's toasts
   and the guard banner go through the shield. UI thread (ReloadRNPSApp). */
void Tick() {
  PlayPendingAlertSound(); /* the banner's warning sound, UI thread */
  static int64_t due_ms = 0;
  static bool done = false;
  if (done)
    return;
  const int64_t now = static_cast<int64_t>(AniketosUI::MonotonicMs());
  if (due_ms == 0) {
    due_ms = now + 4000;
    return;
  }
  if (now < due_ms)
    return;
  done = true;
  if (!Enabled(Shield::PsnBlocker))
    return;
  LOG_INFO("[X5GUARD] reloading the notification overlay for the PSN shield");
  ReloadRNPSApp("NPXS40011");
}

} // namespace X5Guard::PsnShield
