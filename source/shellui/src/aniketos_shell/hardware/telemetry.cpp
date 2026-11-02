#include "aniketos_shell/hardware/telemetry.hpp"
#include "aniketos_shell/hardware/memory_stats.hpp"
#include "aniketos_shell/hardware/firmware_version.hpp"

#include "external_symbols.hpp"

#include <cstdio>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <onion/net.h>
#include <onion/platform.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <onion/log.h>
#include <time.h>

extern "C" {
int sceKernelGetCpuTemperature(int *out_celsius);
int sceKernelGetSocSensorTemperature(int sensor_id, int *out_celsius);
// Same public RegMgr import used by Kura Loader for the editable console
// nickname.  0x02050000 is the registry entity, not a firmware address.
int sceRegMgrGetStr(int entity, char *out, size_t size);
}

namespace AniketosShell::Telemetry {
namespace {

constexpr float kFanRawMaximum = 1024.0f;
constexpr int kMinimumTemperature = 0;
constexpr int kMaximumTemperature = 130;
// Main user-data mount on the PS5; games, apps and saves live here, so its
// usage is what "disk storage" means to the player.
constexpr const char *kStoragePath = "/user";
/* Kura's read-only nickname path uses the public RegMgr entity for the
 * user-editable console name. This is an API key, not a firmware offset. */
constexpr int kConsoleNicknameRegistry = 0x02050000;

#ifndef CLOCK_UPTIME_PRECISE
#define CLOCK_UPTIME_PRECISE CLOCK_MONOTONIC
#endif

std::atomic<bool> s_started{false};
std::atomic<int> s_refresh_seconds{1};
/* A menu page showing live readings is open: sample twice a second. */
std::atomic<bool> s_live_view{false};
std::atomic<uint64_t> s_frame_count{0};
std::mutex s_snapshot_mutex;
Snapshot s_snapshot{};

uint64_t monotonic_millis() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000u +
         static_cast<uint64_t>(now.tv_nsec) / 1000000u;
}

bool valid_temperature(int value) {
  return value >= kMinimumTemperature && value <= kMaximumTemperature;
}

bool valid_kernel_text(const char *text) {
  if (!text || !text[0])
    return false;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
       *p; ++p) {
    if (*p < 0x20 || *p == 0x7f)
      return false;
  }
  return true;
}

/* A kernel parameter by name; false if this console has none by that name
   or will not give it to this process. */
bool kernel_parameter(const char *name, void *out, size_t capacity,
                      size_t *size = nullptr) {
  size_t length = capacity;
  if (sysctlbyname(name, out, &length, nullptr, 0) != 0 || length == 0)
    return false;
  if (size)
    *size = length;
  return true;
}

std::string kernel_text(const char *name) {
  char text[128]{};
  size_t size = 0;
  if (!kernel_parameter(name, text, sizeof(text) - 1, &size))
    return {};
  std::string out(text, strnlen(text, size));
  while (!out.empty() && (out.back() == ' ' || out.back() == '\n'))
    out.pop_back();
  return valid_kernel_text(out.c_str()) ? out : std::string();
}

std::string kernel_hex(const char *name, size_t expected) {
  unsigned char bytes[64]{};
  size_t size = 0;
  if (expected > sizeof(bytes) ||
      !kernel_parameter(name, bytes, sizeof(bytes), &size) || size != expected)
    return {};
  std::string out;
  char pair[3];
  for (size_t i = 0; i < size; ++i) {
    std::snprintf(pair, sizeof(pair), "%02X", bytes[i]);
    out += pair;
  }
  return out;
}

void read_hardware_details(Snapshot &snapshot) {
  snapshot.soc = kernel_text("hw.model");
  snapshot.kernel_build = kernel_text("kern.version");
  snapshot.idps = kernel_hex("machdep.idps", 16);
  snapshot.open_psid = kernel_hex("machdep.openpsid", 16);
  int threads = 0;
  if (kernel_parameter("hw.ncpu", &threads, sizeof(threads)) && threads > 0 &&
      threads <= 256)
    snapshot.cpu_threads = threads;
  uint64_t value = 0;
  if (kernel_parameter("machdep.tsc_freq", &value, sizeof(value)))
    snapshot.base_clock_hz = value;
  value = 0;
  if (kernel_parameter("vm.swap_total", &value, sizeof(value)))
    snapshot.swap_bytes = value;
  /* the optical drive is a device of its own when one is fitted */
  struct stat drive {};
  snapshot.disc_drive = stat("/dev/cd0", &drive) == 0;
  LOG_INFO("[TELEMETRY] soc='%s' threads=%d base=%llu Hz drive=%d swap=%llu "
           "idps=%s psid=%s kernel='%s'",
           snapshot.soc.c_str(), snapshot.cpu_threads,
           static_cast<unsigned long long>(snapshot.base_clock_hz),
           snapshot.disc_drive ? 1 : 0,
           static_cast<unsigned long long>(snapshot.swap_bytes),
           snapshot.idps.empty() ? "none" : "read",
           snapshot.open_psid.empty() ? "none" : "read",
           snapshot.kernel_build.c_str());
}

void read_hardware_identity(Snapshot &snapshot) {
  char buffer[128]{};
  if (sceKernelGetHwModelName && sceKernelGetHwModelName(buffer) == 0 &&
      valid_kernel_text(buffer)) {
    /* Hardware model is separate from the user-editable system name. */
    snapshot.model = buffer;
    snapshot.model_valid = true;
  }
  char nickname[65]{};
  if (sceRegMgrGetStr(kConsoleNicknameRegistry, nickname, sizeof(nickname)) ==
          0 &&
      valid_kernel_text(nickname)) {
    snapshot.system_name = nickname;
    snapshot.system_name_valid = true;
  }

  std::memset(buffer, 0, sizeof(buffer));
  if (sceKernelGetHwSerialNumber &&
      sceKernelGetHwSerialNumber(buffer) == 0 && valid_kernel_text(buffer)) {
    snapshot.serial = buffer;
    snapshot.serial_valid = true;
  }

  if (sceKernelGetCpuFrequency) {
    const long frequency = sceKernelGetCpuFrequency();
    if (frequency > 0) {
      snapshot.cpu_frequency_hz = static_cast<uint64_t>(frequency);
      snapshot.cpu_frequency_valid = true;
    }
  }

  read_hardware_details(snapshot);

  OrbisKernelSwVersion firmware{};
  if (Firmware::Read(firmware))
{
    snapshot.firmware = Firmware::Short(firmware);
    LOG_INFO("[TELEMETRY] system software %s (number 0x%08x, system text '%s')",
             snapshot.firmware.c_str(), firmware.version,
             firmware.version_str);
  }
}

/* Free and total bytes of the user partition as the system reports them;
   the total is the effective one (what the partition can really hold),
   else the plain total given with the free figure. */
bool read_user_partition(uint64_t &free_bytes, uint64_t &total_bytes) {
  uint64_t free_size = 0;
  uint64_t total_size = 0;
  if (!sceShellCoreUtilGetFreeSizeOfUserPartition ||
      sceShellCoreUtilGetFreeSizeOfUserPartition(&free_size, &total_size) != 0)
    return false;
  uint64_t effective = 0;
  if (sceShellCoreUtilGetEffectiveTotalSizeOfUserPartition &&
      sceShellCoreUtilGetEffectiveTotalSizeOfUserPartition(&effective) == 0 &&
      effective >= free_size)
    total_size = effective;
  if (total_size == 0 || free_size > total_size)
    return false;
  free_bytes = free_size;
  total_bytes = total_size;
  return true;
}

/* Fallback: the file system's own count for the /user mount. */
bool read_storage_mount(uint64_t &free_bytes, uint64_t &total_bytes) {
  struct statvfs vfs {};
  if (statvfs(kStoragePath, &vfs) != 0 || vfs.f_blocks == 0)
    return false;
  const uint64_t unit = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
  free_bytes = static_cast<uint64_t>(vfs.f_bavail) * unit;
  total_bytes = static_cast<uint64_t>(vfs.f_blocks) * unit;
  return true;
}

void sample(Snapshot &next, float fps) {
  int value = 0;
  next.cpu_temperature_valid =
      sceKernelGetCpuTemperature(&value) == 0 && valid_temperature(value);
  if (next.cpu_temperature_valid)
    next.cpu_temperature_c = value;

  value = 0;
  next.soc_temperature_valid =
      sceKernelGetSocSensorTemperature(0, &value) == 0 &&
      valid_temperature(value);
  if (next.soc_temperature_valid)
    next.soc_temperature_c = value;

  uint16_t raw_duty = 0;
  uint64_t chassis = 0;
  next.fan_duty_valid =
      sceKernelGetCurrentFanDuty &&
      sceKernelGetCurrentFanDuty(&raw_duty, &chassis) == 0 &&
      raw_duty <= static_cast<uint16_t>(kFanRawMaximum);
  if (next.fan_duty_valid) {
    next.fan_duty_raw = raw_duty;
    next.fan_duty_percent =
        static_cast<float>(raw_duty) * 100.0f / kFanRawMaximum;
  }

  Memory::Stats memory{};
  next.memory_valid = Memory::ReadSystem(memory) &&
                      memory.whole_system_memory;
  if (next.memory_valid) {
    next.memory_total_mb = memory.total_mb;
    next.memory_free_mb = memory.free_mb;
    next.memory_used_mb = memory.used_mb;
    next.memory_used_percent =
        static_cast<float>(next.memory_used_mb) * 100.0f /
        static_cast<float>(memory.total_mb);
  }

  uint64_t free_bytes = 0;
  uint64_t total_bytes = 0;
  if (!read_user_partition(free_bytes, total_bytes))
    read_storage_mount(free_bytes, total_bytes);
  if (total_bytes > 0 && free_bytes <= total_bytes) {
    next.storage_valid = true;
    next.storage_total_bytes = total_bytes;
    next.storage_free_bytes = free_bytes;
    next.storage_used_bytes = total_bytes - free_bytes;
    next.storage_used_percent =
        static_cast<float>(static_cast<double>(next.storage_used_bytes) *
                           100.0 / static_cast<double>(total_bytes));
  }

  timespec uptime{};
  if (clock_gettime(CLOCK_UPTIME_PRECISE, &uptime) == 0 ||
      clock_gettime(CLOCK_MONOTONIC, &uptime) == 0) {
    next.uptime_valid = true;
    next.uptime_seconds = static_cast<uint64_t>(uptime.tv_sec);
  }

  /* NetCtl goes on answering 0.0.0.0 for a while after the connection comes
     back (and that is not an address worth showing at any time). The
     address the console would send from says what it really is: a UDP
     socket "connected" to a public address, which sends nothing. */
  char ip[64]{};
  bool have_ip = onion_net_get_ip_address(ip, sizeof(ip)) == 0 && ip[0] &&
                 std::strcmp(ip, "0.0.0.0") != 0;
  if (!have_ip) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
      sockaddr_in remote{};
      remote.sin_family = AF_INET;
      remote.sin_port = htons(53);
      remote.sin_addr.s_addr = htonl(0x01010101u); /* 1.1.1.1 */
      sockaddr_in local{};
      socklen_t length = sizeof(local);
      if (connect(fd, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)) ==
              0 &&
          getsockname(fd, reinterpret_cast<sockaddr *>(&local), &length) == 0 &&
          local.sin_addr.s_addr != 0) {
        const uint32_t address = ntohl(local.sin_addr.s_addr);
        std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", address >> 24,
                      (address >> 16) & 0xff, (address >> 8) & 0xff,
                      address & 0xff);
        have_ip = true;
      }
      close(fd);
    }
  }
  next.local_ip_valid = have_ip;
  next.local_ip = have_ip ? ip : "";
  next.shell_fps = fps;
  next.shell_fps_valid = std::isfinite(fps) && fps > 0.0f && fps < 240.0f;
  next.sampled_at_ms = monotonic_millis();
}

void *worker(void *) {
  Snapshot initial{};
  read_hardware_identity(initial);

  uint64_t previous_frames = s_frame_count.load(std::memory_order_acquire);
  uint64_t previous_ms = monotonic_millis();
  bool logged = false;
  for (;;) {
    /* Sleep in short slices so a change of interval (or a live page being
       opened) takes effect at once instead of after the current period. */
    for (uint64_t slept_ms = 0;;) {
      const uint64_t period_ms =
          s_live_view.load(std::memory_order_acquire)
              ? 500
              : static_cast<uint64_t>(
                    s_refresh_seconds.load(std::memory_order_acquire)) * 1000;
      if (slept_ms >= period_ms)
        break;
      const timespec delay{0, 100 * 1000 * 1000};
      nanosleep(&delay, nullptr);
      slept_ms += 100;
    }

    const uint64_t now_ms = monotonic_millis();
    const uint64_t frames = s_frame_count.load(std::memory_order_acquire);
    const uint64_t elapsed = now_ms - previous_ms;
    const float fps = elapsed ? static_cast<float>(frames - previous_frames) *
                                  1000.0f / static_cast<float>(elapsed)
                              : 0.0f;
    previous_frames = frames;
    previous_ms = now_ms;

    Snapshot next = initial;
    sample(next, fps);
    {
      std::lock_guard<std::mutex> lock(s_snapshot_mutex);
      s_snapshot = next;
    }
    if (!logged) {
      logged = true;
      LOG_INFO("[ANIVSH:TELEMETRY] cpu=%s/%dC soc=%s/%dC fan=%s/raw:%u/%.1f%% ram=%s/%d/%dMB ip=%s fps=%.1f",
               next.cpu_temperature_valid ? "ok" : "na",
               next.cpu_temperature_c,
               next.soc_temperature_valid ? "ok" : "na",
               next.soc_temperature_c, next.fan_duty_valid ? "ok" : "na",
               static_cast<unsigned>(next.fan_duty_raw),
               next.fan_duty_percent, next.memory_valid ? "ok" : "na",
               next.memory_used_mb, next.memory_total_mb,
               next.local_ip_valid ? next.local_ip.c_str() : "na", fps);
    }
  }
}

} // namespace

bool Start() {
  bool expected = false;
  if (!s_started.compare_exchange_strong(expected, true))
    return true;
  /* Publish identity before the first one-second telemetry interval so the
   * information page never opens empty immediately after injection. */
  Snapshot initial{};
  read_hardware_identity(initial);
  {
    std::lock_guard<std::mutex> lock(s_snapshot_mutex);
    s_snapshot = initial;
  }
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, worker, nullptr) != 0) {
    s_started.store(false, std::memory_order_release);
    LOG_ERROR("[ANIVSH:TELEMETRY] worker start failed");
    return false;
  }
  pthread_detach(thread);
  LOG_INFO("[ANIVSH:TELEMETRY] worker started");
  return true;
}

void ReportShellFrame() {
  s_frame_count.fetch_add(1, std::memory_order_relaxed);
}

void SetLiveView(bool live) {
  s_live_view.store(live, std::memory_order_release);
}

void SetRefreshSeconds(int seconds) {
  if (seconds < 1)
    seconds = 1;
  if (seconds > 10)
    seconds = 10;
  s_refresh_seconds.store(seconds, std::memory_order_release);
}

Snapshot GetSnapshot() {
  std::lock_guard<std::mutex> lock(s_snapshot_mutex);
  return s_snapshot;
}

} // namespace AniketosShell::Telemetry
