#pragma once

#include <string>

/* x5Guard PlayStation Network Blocker, React Native side.

   nanoDNS already keeps the console off PSN; this keeps the interface from
   starting a sign-in or a system update (an update would end the
   jailbreak). A script evaluated before the bundle of each React Native app
   that can start one (psn_shield_js.inc, put in by script_injector) makes
   the sign-in and update calls do nothing and drops the system toasts that
   invite them; the system's own texts stay. Each blocked action goes to
   X5Guard::Report. Toolbox > x5Guard switches it
   (X5Guard::Enabled(Shield::PsnBlocker)); an app opened after the change
   follows it. */
namespace X5Guard::PsnShield {

/* Whether the script goes into the app whose executor is named `app`. */
bool WantsApp(const std::string &app);
const char *InjectedScript();
/* The script runs only once this has been evaluated before it. */
/* The script's settings for the app whose executor is named `app`. */
std::string ScriptConfig(const std::string &app);

/* Once per frame on the UI thread (shell_features BeforeNativeFrame). */
void Tick();

} // namespace X5Guard::PsnShield
