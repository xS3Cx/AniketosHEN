#include "daemon_boot_guard.hpp"

#include <onion/boot_safety.h>
#include <onion/log.h>

namespace {
bool s_was_working = false;
} // namespace

/* Leaving WORKING is the console going to rest mode, turning off or
   restarting: chosen by the user, not a crash, so a start cut short by it
   must not make the next one more cautious (<onion/boot_safety.h>). */
void boot_guard_note_power_state(DaemonPowerState state) {
  if (state == DaemonPowerState::Working) {
    s_was_working = true;
  } else if (s_was_working) {
    s_was_working = false;
    if (!onion_boot_guard_mark_ended(ONION_BOOT_STATE_PATH))
      LOG_WARN("rest: could not record the power action for the boot guard");
  }
}
