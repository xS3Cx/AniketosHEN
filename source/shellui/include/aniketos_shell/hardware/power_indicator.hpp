#pragma once

namespace AniketosShell::PowerIndicator {

// The ICC firmware exposes stable colour presets rather than arbitrary RGB.
// These are the same presets used by Kura's loader.
enum class Color { White, Blue, Orange };

void Configure(void *boot_done, void *standby, void *standby_boot,
               void *set_dynamic_led_dim_setting, void *notify_boot_status);
bool SetColor(Color color);
void RestoreNormal();

} // namespace AniketosShell::PowerIndicator
