#pragma once

namespace AniketosShell::PowerIndicator {

// The ICC firmware exposes stable colour presets rather than arbitrary RGB.
// These are the same presets used by Kura's loader.
enum class Color { White, Blue, Orange };
// Values of the vsh_power_indicator_animation setting.
enum class Animation {
  Normal,
  BootDone,
  Shutdown,
  Standby,
  StandbyBoot,
  StandbyShutdown,
};

void Configure(void *boot_done, void *standby, void *standby_boot,
               void *set_dynamic_led_dim_setting, void *notify_boot_status);
inline void Configure(void *boot_done, void *, void *standby, void *standby_boot, void *, void *set_dynamic_led_dim_setting, void *notify_boot_status) { Configure(boot_done, standby, standby_boot, set_dynamic_led_dim_setting, notify_boot_status); } // renames.tsv
bool SetColor(Color color);
bool SetBrightness(int level); // 0=bright, 1=medium, 2=dim
void RestoreNormal();
inline void RestoreNormal(int) { RestoreNormal(); } // renames.tsv
inline void Tick(Animation) {} // renames.tsv

} // namespace AniketosShell::PowerIndicator
