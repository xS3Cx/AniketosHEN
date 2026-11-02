#include "aniketos_shell/hardware/memory_stats.hpp"

#include <onion/log.h>

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

struct Domain {
  bool valid = false;
  int vm = -1;
  int type = -1;
  Stats stats{};
};

bool valid(int total_mb, int free_mb) {
  return total_mb > 0 && free_mb >= 0 && free_mb <= total_mb;
}

Domain query(int vm, int type) {
  Domain result{};
  result.vm = vm;
  result.type = type;
  int total_mb = 0;
  int free_mb = 0;
  const int rc = get_page_table_stats(vm, type, &total_mb, &free_mb);
  if (rc != 0 || !valid(total_mb, free_mb))
    return result;
  result.valid = true;
  result.stats.valid = true;
  result.stats.total_mb = total_mb;
  result.stats.free_mb = free_mb;
  result.stats.used_mb = total_mb - free_mb;
  return result;
}

Domain largest(const std::array<Domain, kVmLast - kVmFirst + 1> &domains) {
  Domain best{};
  for (const Domain &candidate : domains) {
    if (!candidate.valid)
      continue;
    if (!best.valid || candidate.stats.total_mb > best.stats.total_mb)
      best = candidate;
  }
  return best;
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
   * cache either the valid system-wide result or the unsupported outcome. */
  static std::mutex cache_mutex;
  static bool probed = false;
  static Stats cached{};
  std::lock_guard<std::mutex> lock(cache_mutex);
  if (probed) {
    out = cached;
    return cached.valid;
  }
  probed = true;

  uint64_t physical_bytes = 0;
  uint64_t free_pages = 0;
  uint64_t page_size = 0;
  if (!read_unsigned_sysctl("hw.physmem", physical_bytes) ||
      !read_unsigned_sysctl("vm.stats.vm.v_free_count", free_pages) ||
      !read_unsigned_sysctl("hw.pagesize", page_size) || page_size == 0 ||
      free_pages > UINT64_MAX / page_size)
    return false;

  constexpr uint64_t kMegabyte = 1024u * 1024u;
  const uint64_t total_mb = physical_bytes / kMegabyte;
  const uint64_t free_mb = (free_pages * page_size) / kMegabyte;
  /* Reject missing, partial, or plainly implausible sysctl results instead of
   * displaying a bogus total. This is a sanity range, not a hardware offset. */
  if (total_mb < 1024u || total_mb > 131072u || free_mb > total_mb)
    return false;

  cached = {};
  cached.valid = true;
  cached.whole_system_memory = true;
  cached.total_mb = static_cast<int>(total_mb);
  cached.free_mb = static_cast<int>(free_mb);
  cached.used_mb = cached.total_mb - cached.free_mb;
  out = cached;
  return true;
}

bool read_page_table_pool(Stats &out) {
  std::array<Domain, kVmLast - kVmFirst + 1> cpu{};
  std::array<Domain, kVmLast - kVmFirst + 1> gpu{};
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
    candidate.total_mb = cpu[index].stats.total_mb + gpu[index].stats.total_mb;
    candidate.free_mb = cpu[index].stats.free_mb + gpu[index].stats.free_mb;
    candidate.used_mb = candidate.total_mb - candidate.free_mb;
    if (better_system_candidate(candidate, true, out, best_pair)) {
      out = candidate;
      best_pair = true;
    }
  }

  for (const Domain &candidate : cpu) {
    if (candidate.valid &&
        better_system_candidate(candidate.stats, false, out, best_pair)) {
      out = candidate.stats;
      best_pair = false;
    }
  }

  if (!out.valid) {
    for (const Domain &candidate : gpu) {
      if (candidate.valid &&
          better_system_candidate(candidate.stats, false, out, best_pair)) {
        out = candidate.stats;
        best_pair = false;
      }
    }
  }
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

bool ReadGraphics(Stats &out) {
  out = {};
  std::array<Domain, kVmLast - kVmFirst + 1> gpu{};
  for (int vm = kVmFirst; vm <= kVmLast; ++vm)
    gpu[static_cast<size_t>(vm - kVmFirst)] = query(vm, kTypeGpu);
  out = largest(gpu).stats;
  return out.valid;
}

} // namespace AniketosShell::Memory
