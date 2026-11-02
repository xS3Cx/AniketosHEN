#include "aniketos_shell/hardware/memory_stats.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <sys/types.h>
#include <sys/sysctl.h>

extern "C" int get_page_table_stats(int vm, int type, int *total_mb,
                                     int *free_mb);

namespace AniketosShell::Memory {
namespace {

/* These are page-table domain/type selectors, not firmware offsets.  The
 * runtime probes every bounded combination and chooses only validated data.
 * This is required because some firmware exposes the CPU/GPU pools as two
 * vm=0 domains while other firmware exposes an aggregate domain. */
constexpr int kVmFirst = 0;
constexpr int kVmLast = 3;
constexpr int kTypeCpu = 1;
constexpr int kTypeGpu = 2;

constexpr uint64_t kMegabyte = 1024u * 1024u;

using PoolList = std::array<Stats, kVmLast - kVmFirst + 1>;

bool valid(int total_mb, int free_mb) {
  return total_mb > 0 && free_mb >= 0 && free_mb <= total_mb;
}

Stats query(int vm, int type) {
  Stats result{};
  int total_mb = 0;
  int free_mb = 0;
  const int rc = get_page_table_stats(vm, type, &total_mb, &free_mb);
  if (rc != 0 || !valid(total_mb, free_mb))
    return result;
  result.valid = true;
  result.total_mb = total_mb;
  result.free_mb = free_mb;
  result.used_mb = total_mb - free_mb;
  return result;
}

bool better_system_candidate(const Stats &candidate, bool candidate_pair,
                             const Stats &best, bool best_pair) {
  if (!candidate.valid)
    return false;
  if (!best.valid)
    return true;
  if (candidate.total_mb != best.total_mb)
    return candidate.total_mb > best.total_mb;
  return candidate_pair && !best_pair;
}

bool read_unsigned_sysctl(const char *name, uint64_t &out) {
  out = 0;
  size_t size = sizeof(out);
  if (sysctlbyname(name, &out, &size, nullptr, 0) != 0 || size == 0 ||
      size > sizeof(out))
    return false;
  return true;
}

bool read_physical_system(Stats &out) {
  out = {};
  /* Some retail firmwares reject hw.physmem and print a kernel warning for
   * every attempt. The permission is fixed for this process, so probe once and
   * remember the total; zero marks this source as unsupported. */
  static std::mutex cache_mutex;
  static bool probed = false;
  static uint64_t total_mb = 0;
  static uint64_t page_size = 0;
  std::lock_guard<std::mutex> lock(cache_mutex);
  if (!probed) {
    probed = true;
    uint64_t physical_bytes = 0;
    /* Reject missing, partial, or plainly implausible sysctl results instead
     * of displaying a bogus total. This is a sanity range, not a hardware
     * offset. */
    if (read_unsigned_sysctl("hw.physmem", physical_bytes) &&
        read_unsigned_sysctl("hw.pagesize", page_size) && page_size != 0 &&
        physical_bytes / kMegabyte >= 1024u &&
        physical_bytes / kMegabyte <= 131072u)
      total_mb = physical_bytes / kMegabyte;
  }

  uint64_t free_pages = 0;
  if (total_mb == 0 ||
      !read_unsigned_sysctl("vm.stats.vm.v_free_count", free_pages) ||
      free_pages > UINT64_MAX / page_size ||
      (free_pages * page_size) / kMegabyte > total_mb) {
    total_mb = 0;
    return false;
  }

  out.valid = true;
  out.whole_system_memory = true;
  out.total_mb = static_cast<int>(total_mb);
  out.free_mb = static_cast<int>((free_pages * page_size) / kMegabyte);
  out.used_mb = out.total_mb - out.free_mb;
  return true;
}

bool read_page_table_pool(Stats &out) {
  PoolList cpu{};
  PoolList gpu{};
  for (int vm = kVmFirst; vm <= kVmLast; ++vm) {
    cpu[static_cast<size_t>(vm - kVmFirst)] = query(vm, kTypeCpu);
    gpu[static_cast<size_t>(vm - kVmFirst)] = query(vm, kTypeGpu);
  }

  bool best_pair = false;
  for (size_t index = 0; index < cpu.size(); ++index) {
    if (!cpu[index].valid || !gpu[index].valid)
      continue;
    Stats candidate{};
    candidate.valid = true;
    candidate.whole_system_memory = true;
    candidate.total_mb = cpu[index].total_mb + gpu[index].total_mb;
    candidate.free_mb = cpu[index].free_mb + gpu[index].free_mb;
    candidate.used_mb = candidate.total_mb - candidate.free_mb;
    if (better_system_candidate(candidate, true, out, best_pair)) {
      out = candidate;
      best_pair = true;
    }
  }

  const auto consider_single_pools = [&](const PoolList &pools) {
    for (const Stats &candidate : pools) {
      if (better_system_candidate(candidate, false, out, best_pair)) {
        out = candidate;
        best_pair = false;
      }
    }
  };
  consider_single_pools(cpu);
  if (!out.valid)
    consider_single_pools(gpu);
  return out.valid;
}

} // namespace

bool ReadSystem(Stats &out) {
  out = {};
  Stats pools{};
  const bool pools_valid = read_page_table_pool(pools);
  /* The target PS5 exposes two vm=0 UMA domains. Their independently queried
   * CPU and GPU totals sum to the console's 16 GiB physical total. Accept only
   * the complete pair as whole-system memory; never call one 8 GiB pool the
   * system total. */
  if (pools_valid && pools.whole_system_memory) {
    out = pools;
    return true;
  }
  if (read_physical_system(out))
    return true;
  if (pools_valid) {
    out = pools;
    return true;
  }
  return false;
}

} // namespace AniketosShell::Memory
