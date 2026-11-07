/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What this start may launch; narrowed by the boot level
 *  (<onion/boot_safety.h>) after starts that did not finish. */
typedef struct BootstrapLaunchPlan {
  /* config.ini [shadowmount] autoload; off from the minimal level on. */
  bool shadowmount_autoload;
  /* config.ini [kstuff] autoload; off from the minimal level on. */
  bool kstuff_autoload;
  /* /data/AniketosHEN/kstuff.elf may replace the built-in kstuff. Off in any
   * cautious level: a user-supplied (or imported) kstuff built for another
   * firmware panics the kernel. */
  bool kstuff_user_override;
  /* Off in recovery: the daemon is what goes into ShellUI. */
  bool launch_daemon;
} BootstrapLaunchPlan;

/** True while the daemon of an earlier start runs. */
bool bootstrap_stack_running(void);

/** Launch the owned util, ShadowMountPlus, kstuff and daemon dependency chain. */
int bootstrap_launch_services(uint32_t firmware_version,
                              const BootstrapLaunchPlan *plan);

/** ShadowMountPlus skipped for want of kstuff is launched now if a kstuff
 *  from the user's autostart is running; otherwise the user is told. */
void bootstrap_launch_shadowmount_late(uint32_t firmware_version);

#ifdef __cplusplus
}
#endif
