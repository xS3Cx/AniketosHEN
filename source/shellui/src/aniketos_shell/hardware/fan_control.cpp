/* The daemon owns the fan: it applies the threshold, re-applies it every two
 * seconds and persists the setting. A second writer inside SceShellUI would
 * fight that loop, so the menu forwards to the same IPC command the Toolbox
 * uses. */
#include "aniketos_shell/hardware/fan_control.hpp"

#include "aniketos_shell/core/service_available.hpp"
#include "ipc.hpp"

namespace AniketosFan {

bool SetEnabled(bool enabled, int target_celsius) {
  if (!AniketosRuntime::service_available(false, "fan"))
    return false;
  return IPC_Client::getInstance(false).Set_Fan_Threshold(target_celsius,
                                                          enabled);
}

bool SetTarget(int target_celsius) { return SetEnabled(true, target_celsius); }

} // namespace AniketosFan
