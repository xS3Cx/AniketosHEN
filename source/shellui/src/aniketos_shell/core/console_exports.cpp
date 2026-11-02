#include "aniketos_shell/core/console_exports.hpp"

#include "aniketos_shell/hardware/power_indicator.hpp"
#include "aniketos_shell/hardware/shell_buzzer.hpp"
#include "defs.h"
#include "proc.h"
#include "ps5/kernel.h"

#include <onion/log.h>

int (*sceKernelGetHwModelName)(char *name) = nullptr;
int (*sceKernelGetHwSerialNumber)(char *serial) = nullptr;
long (*sceKernelGetCpuFrequency)(void) = nullptr;
int (*sceShellCoreUtilGetFreeSizeOfUserPartition)(uint64_t *free_bytes,
                                                  uint64_t *total_bytes) = nullptr;
int (*sceShellCoreUtilGetEffectiveTotalSizeOfUserPartition)(
    uint64_t *total_bytes) = nullptr;

namespace {

/* Console buzzer and power LED, driven from the Shell Menu. Every one is
   optional and null-checked by its driver, so a firmware without an export
   simply loses that menu action. KERNEL_DLSYM looks them up by these names. */
void *sceKernelIccSetBuzzer = nullptr;
void *sceKernelIccSetBuzzerDimSetting = nullptr;
void *sceKernelIccIndicatorBootDone = nullptr;
void *sceKernelIccIndicatorShutdown = nullptr;
void *sceKernelIccIndicatorStandby = nullptr;
void *sceKernelIccIndicatorStandbyBoot = nullptr;
void *sceKernelIccIndicatorStandbyShutdown = nullptr;
void *sceKernelIccNotifyBootStatus = nullptr;
void *sceKernelIccSetDynamicLedDimSetting = nullptr;

} // namespace

namespace ConsoleExports {

void Resolve(pid_t pid) {
  /* The runtime handle is not available in every ShellUI state; 0x2001 is the
     fixed dynlib handle of libkernel that the rest of start-up already relies
     on. Without the fallback none of these resolved and the buzzer and
     power-LED pages silently did nothing. */
  int libkernel = get_module_handle(pid, "libkernel.sprx");
  if (libkernel <= 0)
    libkernel = libSceKernelHandle;

  KERNEL_DLSYM(libkernel, sceKernelIccSetBuzzer);
  KERNEL_DLSYM(libkernel, sceKernelIccSetBuzzerDimSetting);
  KERNEL_DLSYM(libkernel, sceKernelIccIndicatorBootDone);
  KERNEL_DLSYM(libkernel, sceKernelIccIndicatorShutdown);
  KERNEL_DLSYM(libkernel, sceKernelIccIndicatorStandby);
  KERNEL_DLSYM(libkernel, sceKernelIccIndicatorStandbyBoot);
  KERNEL_DLSYM(libkernel, sceKernelIccIndicatorStandbyShutdown);
  KERNEL_DLSYM(libkernel, sceKernelIccNotifyBootStatus);
  KERNEL_DLSYM(libkernel, sceKernelIccSetDynamicLedDimSetting);

  AniketosShell::Buzzer::Configure(sceKernelIccSetBuzzer,
                                   sceKernelIccSetBuzzerDimSetting);
  AniketosShell::PowerIndicator::Configure(
      sceKernelIccIndicatorBootDone, sceKernelIccIndicatorShutdown,
      sceKernelIccIndicatorStandby, sceKernelIccIndicatorStandbyBoot,
      sceKernelIccIndicatorStandbyShutdown, sceKernelIccSetDynamicLedDimSetting,
      sceKernelIccNotifyBootStatus);
}

void LogResolved() {
  LOG_INFO("console exports: buzzer=%d buzzer_volume=%d led_boot=%d "
           "led_standby=%d led_brightness=%d led_status=%d",
           sceKernelIccSetBuzzer ? 1 : 0,
           sceKernelIccSetBuzzerDimSetting ? 1 : 0,
           sceKernelIccIndicatorBootDone ? 1 : 0,
           sceKernelIccIndicatorStandby ? 1 : 0,
           sceKernelIccSetDynamicLedDimSetting ? 1 : 0,
           sceKernelIccNotifyBootStatus ? 1 : 0);
}

} // namespace ConsoleExports
