/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.
*/

#include "bootstrap_assets.h"
#include "bootstrap_config.h"
#include "bootstrap_filesystem.h"
#include "bootstrap_migration.h"
#include "bootstrap_notify.h"
#include "bootstrap_runtime.h"
#include "bootstrap_safety.h"
#include "launch_pipeline.h"
#include "payload_autostart.h"

#include <onion/conflict.h>
#include <onion/log.h>

int main(void) {
  BootstrapConfig config{};
  if (!bootstrap_config_load(&config))
    return -1;

  if (!bootstrap_runtime_prepare())
    return -1;
  bootstrap_runtime_enable_remote_logging();

  if (!bootstrap_safety_lock())
    return 0;

  const char *conflict = onion_conflict_detect();
  if (conflict) {
    LOG_ERROR("refusing start: %s already running", conflict);
    bootstrap_notify("notify.boot.conflict", conflict);
    return 0;
  }

  /* The stack can be stopped from here, but the module of the running start
     stays in ShellUI, and a second one on top of it can panic the console:
     it has to be unloaded first. */
  if (bootstrap_stack_running()) {
    LOG_ERROR("refusing start: " ONION_DIST_NAME " is already running");
    bootstrap_notify("notify.boot.conflict", ONION_DIST_NAME);
    return 0;
  }

  LOG_DEBUG("============== Spawner (Bootstrapper) Started =================");
  /* Before anything else is written: a start cut short by a restart left
     its logs, and they decide how cautious this start is. */
  const onion_boot_level level = bootstrap_safety_begin(config.firmware_text);

  /* Before anything creates files under the data root: an imported file must
     never lose to a freshly generated default. */
  if (level == ONION_BOOT_NORMAL)
    bootstrap_migrate_legacy_data();
  else
    LOG_WARN("%s start: data import skipped", onion_boot_level_name(level));
  bootstrap_filesystem_create_directories();
  if (!bootstrap_filesystem_mount_system()) {
    /* it stopped by itself: the next report must not call this a crash */
    onion_boot_guard_mark_ended(ONION_BOOT_STATE_PATH);
    return -1;
  }

  LOG_DEBUG("Writing embedded assets ...");
  const bool icon_ready = bootstrap_assets_write();
  bootstrap_notify_starting(icon_ready);
  bootstrap_safety_flush(icon_ready);
  LOG_DEBUG("   Written!");

  bootstrap_filesystem_disable_updates();

  const BootstrapLaunchPlan plan = {
      .shadowmount_autoload =
          config.shadowmount_autoload && level < ONION_BOOT_MINIMAL,
      .kstuff_autoload = config.kstuff_autoload && level < ONION_BOOT_MINIMAL,
      .kstuff_user_override = level == ONION_BOOT_NORMAL,
      .launch_daemon = level < ONION_BOOT_RECOVERY,
  };
  const int launch_result =
      bootstrap_launch_services(config.firmware_version, &plan);
  if (launch_result != 0) {
    onion_boot_guard_mark_ended(ONION_BOOT_STATE_PATH);
    return launch_result;
  }

  if (level == ONION_BOOT_NORMAL)
    bootstrap_payload_autostart();
  else
    LOG_WARN("%s start: payload autostart skipped",
             onion_boot_level_name(level));
  LOG_DEBUG("============== Spawner (Bootstrapper) Finished =================");
  return 0;
}
