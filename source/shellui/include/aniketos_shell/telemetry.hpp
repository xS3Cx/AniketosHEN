#pragma once

#include <cstdint>
#include <string>

namespace AniketosShell::Telemetry {

struct Snapshot {
  bool cpu_temperature_valid = false;
  bool soc_temperature_valid = false;
  bool fan_duty_valid = false;
  bool memory_valid = false;
  bool local_ip_valid = false;
  bool shell_fps_valid = false;
  int cpu_temperature_c = 0;
  int soc_temperature_c = 0;
  uint16_t fan_duty_raw = 0;
  float fan_duty_percent = 0.0f;
  int memory_total_mb = 0;
  int memory_used_mb = 0;
  int memory_free_mb = 0;
  float memory_used_percent = 0.0f;
  bool storage_valid = false;
  uint64_t storage_total_bytes = 0;
  uint64_t storage_used_bytes = 0;
  uint64_t storage_free_bytes = 0;
  float storage_used_percent = 0.0f;
  bool uptime_valid = false;
  bool system_name_valid = false;
  bool model_valid = false;
  bool serial_valid = false;
  bool cpu_frequency_valid = false;
  uint64_t uptime_seconds = 0;
  uint64_t cpu_frequency_hz = 0;
  float shell_fps = 0.0f;
  uint64_t sampled_at_ms = 0;
  std::string local_ip;
  std::string firmware;
  std::string system_name;
  std::string model;
  std::string serial;
  /* Read once from the kernel's parameters; empty or zero where a console
     does not give them. The two identifiers are 16 bytes each, in hex. */
  std::string soc;          /* the processor's part number */
  std::string kernel_build; /* revision, branch and build date */
  std::string idps;
  std::string open_psid;
  int cpu_threads = 0;
  uint64_t base_clock_hz = 0;
  uint64_t swap_bytes = 0;
  bool disc_drive = false;
};

bool Start();
void ReportShellFrame();
void SetRefreshSeconds(int seconds);
/** Sample every 500 ms while a page with live readings is on screen. */
void SetLiveView(bool live);
Snapshot GetSnapshot();

} // namespace AniketosShell::Telemetry
