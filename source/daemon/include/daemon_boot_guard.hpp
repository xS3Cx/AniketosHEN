#pragma once

#include "daemon_power_state.hpp"

/** Call on every power state poll: leaving WORKING marks the session ended
 *  for the boot guard. One thread only. */
void boot_guard_note_power_state(DaemonPowerState state);
