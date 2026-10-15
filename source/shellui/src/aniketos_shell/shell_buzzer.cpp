#include "aniketos_shell/shell_buzzer.hpp"

#include <onion/log.h>

namespace AniketosShell::Buzzer {
namespace {

using SetBuzzerFn = int (*)(int mode);
using SetDimSettingFn = int (*)(int level);

SetBuzzerFn s_set_buzzer = nullptr;
SetDimSettingFn s_set_dim_setting = nullptr;

} // namespace

void Configure(void *set_buzzer, void *set_dim_setting) {
  s_set_buzzer = reinterpret_cast<SetBuzzerFn>(set_buzzer);
  s_set_dim_setting = reinterpret_cast<SetDimSettingFn>(set_dim_setting);
  LOG_INFO("[ANIKETOSHEN:BUZZER] dynamic exports sound=%d volume=%d",
           s_set_buzzer ? 1 : 0, s_set_dim_setting ? 1 : 0);
}

bool IsSupported() { return s_set_buzzer != nullptr; }

bool PlayTest(Pattern pattern) {
  if (!IsSupported()) {
    LOG_WARN("[ANIKETOSHEN:BUZZER] unavailable on this firmware");
    return false;
  }

  const int mode = static_cast<int>(pattern);
  const int result = s_set_buzzer(mode);
  LOG_INFO("[ANIKETOSHEN:BUZZER] pattern=%d result=%d", mode, result);
  return result == 0;
}

bool SetVolumeAndConfirm(Volume volume) {
  if (!s_set_buzzer || !s_set_dim_setting) {
    LOG_WARN("[ANIKETOSHEN:BUZZER] volume control unavailable on this firmware");
    return false;
  }

  const int level = static_cast<int>(volume);
  const int set_result = s_set_dim_setting(level);
  if (set_result != 0) {
    LOG_WARN("[ANIKETOSHEN:BUZZER] set volume=%d result=%d", level,
             set_result);
    return false;
  }

  const int beep_result = s_set_buzzer(static_cast<int>(Pattern::Short));
  LOG_INFO("[ANIKETOSHEN:BUZZER] volume=%d confirm=%d", level, beep_result);
  return beep_result == 0;
}

} // namespace AniketosShell::Buzzer
