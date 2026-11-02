#pragma once

#include <cstdint>

namespace AniketosShell::PowerIndicator {

// Firmware-owned lifecycle sequences. They preserve Sony's native fades and
// are intentionally not represented as arbitrary RGB values.
enum class Sequence { BootDone, Shutdown, Standby, StandbyBoot, StandbyShutdown };
// The ICC firmware exposes stable colour presets rather than arbitrary RGB.
// These are the same presets used by Kura's loader.
enum class Color { White, Blue, Orange };
enum class Animation {
  Normal,
  BootDone,
  Shutdown,
  Standby,
  StandbyBoot,
  StandbyShutdown,
};

void Configure(void *boot_done, void *shutdown, void *standby,
               void *standby_boot, void *standby_shutdown,
               void *set_dynamic_led_dim_setting,
               void *notify_boot_status);
bool Play(Sequence sequence);
bool SetColor(Color color);
bool SetBrightness(int level); // 0=bright, 1=medium, 2=dim
void Tick(Animation animation);
void RestoreNormal(int brightness);

} // namespace AniketosShell::PowerIndicator
