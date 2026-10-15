/* Shell Menu fan options.
 *
 * In AniketosHEN the ShellUI core drove the ICC fan device itself. In this
 * distribution the daemon owns the fan: it applies the threshold, re-applies
 * it every two seconds and persists the setting. A second writer inside
 * SceShellUI would fight that loop, so the menu forwards to the same crit IPC
 * command the Toolbox uses and keeps no local worker.
 */
#include "fan_control.hpp"

#include "aniketos_runtime.hpp"
#include "ipc.hpp"

#include <onion/settings.hpp>

namespace AniketosFan {

bool Start() { return true; }

bool SetEnabled(bool enabled, int target_celsius) {
  if (!AniketosRuntime::service_available(false, "fan"))
    return false;
  return IPC_Client::getInstance(false).Set_Fan_Threshold(target_celsius,
                                                          enabled);
}

bool SetTarget(int target_celsius) { return SetEnabled(true, target_celsius); }

bool RestoreAutomatic() {
  return SetEnabled(false, onion::kFanAutomaticThresholdCelsius);
}

} // namespace AniketosFan
