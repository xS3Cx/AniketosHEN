#pragma once

namespace AniketosShell::Memory {

struct Stats {
  bool valid = false;
  /* True only when the result describes all system RAM: either a system-wide
   * physical-memory source or a validated sum of the CPU and GPU UMA domains.
   * A single VM/page-table pool is valid telemetry, but is not total RAM. */
  bool whole_system_memory = false;
  int total_mb = 0;
  int used_mb = 0;
  int free_mb = 0;
};

/**
 * Read total system RAM from a validated physical-memory source or a confirmed
 * CPU+GPU UMA-domain pair. A single page-table pool remains valid for internal
 * diagnostics but is never labeled as the installed system total.
 */
bool ReadSystem(Stats &out);

} // namespace AniketosShell::Memory
