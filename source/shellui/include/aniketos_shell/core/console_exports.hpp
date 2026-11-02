#pragma once

#include <cstdint>
#include <sys/types.h>

/* Resolved by name; null where a firmware does not export them. */
extern int (*sceKernelGetHwModelName)(char *name);
extern int (*sceKernelGetHwSerialNumber)(char *serial);
extern long (*sceKernelGetCpuFrequency)(void);
/* Free and total bytes of the user partition, the figures the system's own
   storage screens are built on. */
extern int (*sceShellCoreUtilGetFreeSizeOfUserPartition)(uint64_t *free_bytes,
                                                         uint64_t *total_bytes);
extern int (*sceShellCoreUtilGetEffectiveTotalSizeOfUserPartition)(
    uint64_t *total_bytes);

namespace ConsoleExports {

/* Finds the buzzer and power LED exports and hands them to their drivers. */
void Resolve(pid_t pid);
void LogResolved();

} // namespace ConsoleExports
