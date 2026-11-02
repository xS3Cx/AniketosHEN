#include "aniketos_shell/hardware/power_indicator.hpp"

#include <onion/log.h>

#include <time.h>

namespace AniketosShell::PowerIndicator {
namespace {
using IndicatorFn = unsigned long long (*)();
using SetBrightnessFn = int (*)(int);
using NotifyBootStatusFn = int (*)(int);

IndicatorFn s_boot_done = nullptr;
IndicatorFn s_standby = nullptr;
IndicatorFn s_standby_boot = nullptr;
SetBrightnessFn s_set_brightness = nullptr;
NotifyBootStatusFn s_notify_boot_status = nullptr;
Color s_last_color = Color::White;
bool s_orange_context_ready = false;

bool enter_orange_context() {
  // Kura's working orange path is not a direct Standby() call. The ICC
  // firmware first needs the rest-context sweep: 64 boot-status notifications
  // spaced by 60 ms, then the normal Standby preset renders solid orange.
  constexpr int kRestContextSteps = 64;
  constexpr long kRestContextStepNs = 60 * 1000 * 1000;
  if (s_orange_context_ready)
    return true;
  if (!s_notify_boot_status) {
    LOG_WARN("[ANIKETOSHEN:INDICATOR] orange sweep unavailable: "
             "sceKernelIccNotifyBootStatus unresolved");
    return false;
  }

  LOG_INFO("[ANIKETOSHEN:INDICATOR] orange sweep start steps=%d",
           kRestContextSteps);
  const timespec delay{0, kRestContextStepNs};
  for (int status = 0; status < kRestContextSteps; ++status) {
    (void)s_notify_boot_status(status);
    (void)nanosleep(&delay, nullptr);
  }
  s_orange_context_ready = true;
  LOG_INFO("[ANIKETOSHEN:INDICATOR] orange sweep complete");
  return true;
}

void leave_orange_context() {
  // Kura's state machine clears the rest context with three zero status
  // notifications before asking ICC for the normal white preset. A
  // StandbyBoot call by itself changes the visible preset but does not clear
  // this context on the affected firmware, so a following BootDone remains
  // orange.
  constexpr int kNormalContextResets = 3;
  constexpr long kNormalContextStepNs = 60 * 1000 * 1000;
  const timespec delay{0, kNormalContextStepNs};
  LOG_INFO("[ANIKETOSHEN:INDICATOR] orange exit start resets=%d",
           kNormalContextResets);
  for (int i = 0; i < kNormalContextResets; ++i) {
    const int result = s_notify_boot_status(0);
    LOG_INFO("[ANIKETOSHEN:INDICATOR] orange exit reset=%d result=%d", i,
             result);
    (void)nanosleep(&delay, nullptr);
  }
  s_orange_context_ready = false;
  LOG_INFO("[ANIKETOSHEN:INDICATOR] orange exit complete");
}

IndicatorFn preset_of(Color color) {
  switch (color) {
  case Color::White: return s_boot_done;
  case Color::Blue: return s_standby_boot;
  case Color::Orange: return s_standby;
  }
  return nullptr;
}
} // namespace

void Configure(void *boot_done, void *standby, void *standby_boot,
               void *set_dynamic_led_dim_setting, void *notify_boot_status) {
  s_boot_done = reinterpret_cast<IndicatorFn>(boot_done);
  s_standby = reinterpret_cast<IndicatorFn>(standby);
  s_standby_boot = reinterpret_cast<IndicatorFn>(standby_boot);
  s_set_brightness = reinterpret_cast<SetBrightnessFn>(set_dynamic_led_dim_setting);
  s_notify_boot_status = reinterpret_cast<NotifyBootStatusFn>(notify_boot_status);
  s_orange_context_ready = false;
  LOG_INFO("[ANIKETOSHEN:INDICATOR] boot=%d standby=%d standby_boot=%d brightness=%d notify=%d",
           s_boot_done ? 1 : 0, s_standby ? 1 : 0, s_standby_boot ? 1 : 0,
           s_set_brightness ? 1 : 0, s_notify_boot_status ? 1 : 0);
}

bool SetColor(Color color) {
  // The orange preset is not overridden by BootDone directly. Clear the
  // firmware's rest context first, using the same transition as Kura.
  if (color != Color::Orange && s_last_color == Color::Orange &&
      s_notify_boot_status) {
    leave_orange_context();
  }
  if (color == Color::Orange)
    (void)enter_orange_context();

  const IndicatorFn preset = preset_of(color);
  if (!preset) {
    LOG_WARN("[ANIKETOSHEN:INDICATOR] color=%d unavailable",
             static_cast<int>(color));
    return false;
  }
  const auto result = preset();
  s_last_color = color;
  LOG_INFO("[ANIKETOSHEN:INDICATOR] color=%d result=%llu",
           static_cast<int>(color), result);
  return true;
}

void RestoreNormal() { (void)SetColor(Color::White); }

} // namespace AniketosShell::PowerIndicator
