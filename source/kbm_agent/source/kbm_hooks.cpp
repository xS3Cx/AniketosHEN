/* KB+M agent: the game's pad reads, hooked (see kbm_hooks.hpp). */
#include "kbm_hooks.hpp"

#include <atomic>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>

#include <ps5/kernel.h>

#include "kbm_input.hpp"

extern "C" {
int sceKernelGetModuleList(int32_t *handles, size_t max, size_t *count);
int sceKernelQueryMemoryProtection(void *address, void **start, void **end,
                                   uint32_t *prot);
}


namespace onion::kbm::hooks {
namespace {

/* libkernel's description of a loaded module (sceKernelGetModuleInfo). */
struct ModuleSegment {
  uint64_t address;
  uint32_t size;
  int32_t prot;
};
struct ModuleInfo {
  uint64_t size;
  char name[256];
  ModuleSegment segments[4];
  uint32_t segment_count;
  uint8_t fingerprint[20];
};
static_assert(sizeof(ModuleInfo) == 0x160, "SceKernelModuleInfo");
extern "C" int sceKernelGetModuleInfo(int32_t handle, ModuleInfo *info);

constexpr struct {
  const char *name, *nid;
  uint32_t stride; /* bytes per sample */
} kReads[kKindCount] = {
    {"scePadReadState", "YndgXqQVV7c", 0x78},
    {"scePadRead", "q1cHNfGycLI", 0x78},
    {"scePadReadStateExt", "5Wf4q349s+Q", 0x80},
    {"scePadReadExt", "QjwkT2Ycmew", 0x80},
};
constexpr uint64_t kStaleUs = 200000;
constexpr uint64_t kRescanUs = 3000000;
constexpr uint64_t kHandleCheckUs = 1000000;
constexpr size_t kPageSize = 0x4000;
constexpr size_t kControlSize = (sizeof(ControlPage) + kPageSize - 1) & ~(kPageSize - 1);
constexpr size_t kMaxModules = 256;

ControlPage *s_ctl;
uint32_t s_generation;
TickFn s_tick;
LogFn s_log;
uint64_t s_real[kKindCount]; /* libScePad's functions */
uint64_t s_hook[kKindCount]; /* ours, same order */

/* Profile hand-off: Service copies into the buffer the engine does not use
   and publishes it; the hook acknowledges once the engine has let go of the
   other one, and only then may Service overwrite that one. */
ProfileBlob s_profiles[2];
std::atomic<const ProfileBlob *> s_published{nullptr};
std::atomic<const ProfileBlob *> s_acked{nullptr};
uint32_t s_profile_generation;
std::atomic<bool> s_profile_enabled{false}; /* the published one is on */

/* hook side, owned by whoever holds s_busy */
std::atomic<int> s_busy{0};
const ProfileBlob *s_engine_profile;
uint32_t s_beat_seen = ~0u;
uint64_t s_beat_at;

/* the last result, for a game thread that reads while another computes */
struct Result {
  PadState pad;
  uint8_t replace;
};
std::atomic<uint32_t> s_seq{0};
Result s_cached;

uint64_t s_scan_at;
bool s_full_logged;
size_t s_scanned_modules; /* module count at the last scan */

/* The pad handle of the user in front of the screen: only that pad gets
   our state (a second player keeps their own). -1: not known, all pads. */
std::atomic<int32_t> s_our_handle{-1};
int32_t (*s_get_handle)(int32_t user, int32_t type, int32_t index);
int (*s_foreground_user)(int32_t *user);
uint64_t s_handle_at;

/* The system had the pad: the engine starts over (everything released) at
   the next read we own. */
std::atomic<bool> s_intercepted{false};

#define LOG(...) (s_log ? s_log(__VA_ARGS__) : (void)0)

PadState neutral() {
  PadState s{};
  s.lx = s.ly = s.rx = s.ry = 128;
  return s;
}

/* Runs the engine for one read; false: pass the real pad through. */
bool compute(Result *out) {
  if (s_busy.exchange(1, std::memory_order_acquire)) {
    /* seqlock read of the other thread's last result */
    for (int i = 0; i < 4; ++i) {
      const uint32_t seq = s_seq.load(std::memory_order_acquire);
      if (seq & 1)
        continue;
      Result copy;
      memcpy(&copy, &s_cached, sizeof(copy));
      std::atomic_thread_fence(std::memory_order_acquire);
      if (s_seq.load(std::memory_order_relaxed) == seq) {
        *out = copy;
        return copy.pad.active != 0;
      }
    }
    return false;
  }
  const ProfileBlob *profile = s_published.load(std::memory_order_acquire);
  Result r{neutral(), 0};
  const bool reset = s_intercepted.exchange(false, std::memory_order_acq_rel);
  if (!profile || !profile->enabled) {
    s_engine_profile = nullptr; /* hand it over again when we resume */
  } else {
    /* the clock and the reader's beat matter only while a profile is on */
    const uint32_t beat = __atomic_load_n(&s_ctl->heartbeat, __ATOMIC_RELAXED);
    const uint64_t now = agent::NowUs();
    if (beat != s_beat_seen) {
      s_beat_seen = beat;
      s_beat_at = now;
    }
    if (now - s_beat_at <= kStaleUs) {
      /* a new profile, or back from the system: the engine starts over */
      const bool changed = profile != s_engine_profile || reset;
      s_engine_profile = profile;
      s_tick(profile, changed, &r.pad);
      /* "controller works too" off: only while a mouse or keyboard is open */
      const uint32_t status =
          __atomic_load_n(&s_ctl->agent_status, __ATOMIC_RELAXED);
      r.replace = !profile->merge_real_pad &&
                  (status & (kStatusMouseOpen | kStatusKeyboardOpen));
    }
  }
  s_acked.store(profile, std::memory_order_release);
  s_seq.fetch_add(1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  memcpy(&s_cached, &r, sizeof(r));
  s_seq.fetch_add(1, std::memory_order_release);
  s_busy.store(0, std::memory_order_release);
  *out = r;
  return r.pad.active != 0;
}

/* Lays `ours` over one ScePadData sample (buttons, sticks, triggers only):
   buttons ORed, each stick axis the value further from 128, triggers the
   larger; `replace` takes our sticks and triggers outright while the real
   pad's buttons still count (its Options and PS stay usable). */
void merge(uint8_t *p, const PadState &ours, bool replace) {
  uint32_t buttons;
  memcpy(&buttons, p, sizeof(buttons));
  buttons |= ours.buttons; /* the real buttons always count */
  memcpy(p, &buttons, sizeof(buttons));
  const uint8_t axes[4] = {ours.lx, ours.ly, ours.rx, ours.ry};
  for (int i = 0; i < 4; ++i) {
    const int real = p[4 + i] - 128, mine = axes[i] - 128;
    if (replace || mine * mine > real * real)
      p[4 + i] = axes[i];
  }
  if (replace || ours.l2 > p[8])
    p[8] = ours.l2;
  if (replace || ours.r2 > p[9])
    p[9] = ours.r2;
}

/* Our state over `count` samples; computed once, at the first sample the
   system does not hold, so every sample of one read gets the same state. */
void apply(void *data, int count, uint32_t stride) {
  auto *p = static_cast<uint8_t *>(data);
  Result r;
  int state = -1; /* -1 not computed, 0 pass through, 1 merge */
  for (int i = 0; i < count; ++i, p += stride) {
    uint32_t buttons;
    memcpy(&buttons, p, sizeof(buttons));
    if (buttons & kPadIntercepted) {
      /* the system has the pad (PS button, Control Center): nothing held now
         may land later */
      s_intercepted.store(true, std::memory_order_release);
      agent::InputDiscard();
      continue;
    }
    if (state < 0)
      state = compute(&r);
    if (!state)
      return;
    merge(p, r.pad, r.replace);
  }
}

bool our_pad(int32_t handle) {
  const int32_t ours = s_our_handle.load(std::memory_order_relaxed);
  return ours < 0 || handle == ours;
}

template <Kind K> int hook_state(int32_t handle, void *data) {
  using Fn = int (*)(int32_t, void *);
  const int rc = reinterpret_cast<Fn>(s_real[K])(handle, data);
  if (rc == 0 && data && our_pad(handle))
    apply(data, 1, kReads[K].stride);
  return rc;
}

/* The Ext layouts are only known to start like ScePadData: of several
   samples only the first is touched. */
template <Kind K> int hook_read(int32_t handle, void *data, int32_t num) {
  using Fn = int (*)(int32_t, void *, int32_t);
  const int rc = reinterpret_cast<Fn>(s_real[K])(handle, data, num);
  if (rc > 0 && data && our_pad(handle)) {
    const int count = rc < num ? rc : num;
    apply(data, K == kReadExt ? 1 : count, kReads[K].stride);
  }
  return rc;
}

template <typename Fn> void bind(Fn &target, uint32_t module, const char *name) {
  const intptr_t address = kernel_dynlib_dlsym(getpid(), module, name);
  target = address > 0 ? reinterpret_cast<Fn>(address) : nullptr;
}

bool resolve() {
  uint32_t pad = 0;
  if (kernel_dynlib_handle(getpid(), "libScePad.sprx", &pad) != 0) {
    LOG("libScePad not loaded in the game");
    return false;
  }
  bind(s_get_handle, pad, "scePadGetHandle");
  uint32_t users = 0;
  if (kernel_dynlib_handle(getpid(), "libSceUserService.sprx", &users) == 0)
    bind(s_foreground_user, users, "sceUserServiceGetForegroundUser");
  for (uint32_t k = 0; k < kKindCount; ++k) {
    intptr_t a = kernel_dynlib_dlsym(getpid(), pad, kReads[k].name);
    if (a <= 0)
      a = kernel_dynlib_resolve(getpid(), pad, kReads[k].nid);
    s_real[k] = a > 0 ? uint64_t(a) : 0;
    LOG("%s at 0x%lx", kReads[k].name, (unsigned long)s_real[k]);
  }
  s_hook[kReadState] = reinterpret_cast<uint64_t>(&hook_state<kReadState>);
  s_hook[kRead] = reinterpret_cast<uint64_t>(&hook_read<kRead>);
  s_hook[kReadStateExt] = reinterpret_cast<uint64_t>(&hook_state<kReadStateExt>);
  s_hook[kReadExt] = reinterpret_cast<uint64_t>(&hook_read<kReadExt>);
  return s_real[kReadState] || s_real[kRead] || s_real[kReadStateExt] ||
         s_real[kReadExt];
}

/* The protection of the mapping at `address` now, and where it ends. */
bool mapped(uint64_t address, uint32_t *prot, uint64_t *end = nullptr) {
  void *first = nullptr, *last = nullptr;
  if (sceKernelQueryMemoryProtection(reinterpret_cast<void *>(address), &first,
                                     &last, prot) != 0)
    return false;
  if (end)
    *end = reinterpret_cast<uint64_t>(last);
  return true;
}

/* Readable for `length` bytes from `address`, by the kernel's account. */
bool readable(uint64_t address, size_t length, uint32_t *prot) {
  uint64_t end = 0;
  return mapped(address, prot, &end) && (*prot & PROT_READ) &&
         address + length <= end;
}

/* Runs `write` on the slot, its page opened just for that when `prot` (the
   protection now) says read-only. false: the page stayed shut, nothing ran. */
template <typename Write>
bool write_slot(uint64_t slot, uint32_t prot, Write write) {
  const uint64_t page = slot & ~uint64_t(kPageSize - 1);
  const bool was_writable = prot & PROT_WRITE;
  if (!was_writable && kernel_mprotect(getpid(), page, kPageSize,
                                       PROT_READ | PROT_WRITE) != 0)
    return false;
  write();
  if (!was_writable)
    kernel_mprotect(getpid(), page, kPageSize, prot);
  return true;
}

/* An older agent's page in this process: a read-write anonymous mapping
   that starts with our magic and layout. */
ControlPage *find_existing() {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, getpid()};
  size_t size = 0;
  if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0 || !size)
    return nullptr;
  size += 16 * 1024;
  const size_t buf_size = size; /* sysctl lowers `size` to what it wrote */
  void *buf = mmap(nullptr, buf_size, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
  if (buf == MAP_FAILED)
    return nullptr;
  ControlPage *found = nullptr;
  if (sysctl(mib, 4, buf, &size, nullptr, 0) == 0) {
    for (char *p = static_cast<char *>(buf); p < static_cast<char *>(buf) + size;) {
      const auto *e = reinterpret_cast<const kinfo_vmentry *>(p);
      if (e->kve_structsize <= 0)
        break;
      p += e->kve_structsize;
      const uint64_t start = e->kve_start;
      uint32_t prot = 0;
      if ((e->kve_protection & 3) != 3 || (e->kve_protection & 4) ||
          e->kve_end - start < sizeof(ControlPage) ||
          !readable(start, sizeof(ControlPage), &prot))
        continue;
      const auto *page = reinterpret_cast<const ControlPage *>(start);
      if (page->magic == kControlMagic &&
          page->layout_version == kControlLayoutVersion &&
          page->page_size >= sizeof(ControlPage)) {
        found = reinterpret_cast<ControlPage *>(start);
        break;
      }
    }
  }
  munmap(buf, buf_size);
  return found;
}

/* An older agent's page is taken over; otherwise a new one wherever the
   kernel puts it (kControlHint is only a hint: nothing is mapped over). The
   header goes in last, magic last of all, so the daemon never finds half a
   page. */
bool map_control() {
  if (ControlPage *page = find_existing()) {
    s_ctl = page;
    LOG("control page of an older agent at %p", static_cast<void *>(page));
    return true;
  }
  void *p = mmap(reinterpret_cast<void *>(kControlHint), kControlSize,
                 PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (p == MAP_FAILED) {
    LOG("control page not mapped");
    return false;
  }
  s_ctl = static_cast<ControlPage *>(p);
  memset(s_ctl, 0, kControlSize);
  s_ctl->layout_version = kControlLayoutVersion;
  s_ctl->page_size = static_cast<uint32_t>(kControlSize);
  __atomic_store_n(&s_ctl->magic, kControlMagic, __ATOMIC_RELEASE);
  LOG("control page at %p", p);
  return true;
}

/* The slots an older agent recorded now call our hooks. Only entries whose
   original is the real function they claim to import are believed. */
void take_over() {
  uint32_t kept = 0;
  const uint32_t count = s_ctl->hook_count < uint32_t(kMaxHookSlots)
                             ? s_ctl->hook_count
                             : uint32_t(kMaxHookSlots);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t kind = s_ctl->hook_kinds[i];
    const uint64_t slot = s_ctl->hook_slots[i];
    uint32_t prot = 0;
    if (kind >= kKindCount || !s_real[kind] ||
        s_ctl->hook_originals[i] != s_real[kind] || (slot & 7) ||
        !mapped(slot, &prot) || (prot & PROT_EXEC) || !(prot & PROT_READ))
      continue;
    /* only a slot that still holds the real read or an older agent's hook
       (executable memory) is ours to repoint */
    const uint64_t current = *reinterpret_cast<const uint64_t *>(slot);
    uint32_t target_prot = 0;
    if (current != s_real[kind] &&
        !(mapped(current, &target_prot) && (target_prot & PROT_EXEC)))
      continue;
    if (!write_slot(slot, prot, [slot, kind] {
          __atomic_store_n(reinterpret_cast<uint64_t *>(slot), s_hook[kind],
                           __ATOMIC_RELEASE);
        }))
      continue;
    s_ctl->hook_slots[kept] = slot;
    s_ctl->hook_originals[kept] = s_real[kind];
    s_ctl->hook_kinds[kept] = kind;
    ++kept;
  }
  __atomic_store_n(&s_ctl->hook_count, kept, __ATOMIC_RELEASE);
  LOG("took over %u of %u slot(s)", kept, count);
}

/* Shipped with the system (.sprx, from /system/common/lib); a game's own modules
   are eboot.bin and .prx files from /app0/sce_module. */
bool system_module(const char *name) {
  const size_t n = strnlen(name, 256);
  return n >= 5 && memcmp(name + n - 5, ".sprx", 5) == 0;
}

bool module_info(int32_t handle, ModuleInfo *info) {
  memset(info, 0, sizeof(*info));
  info->size = sizeof(*info);
  return sceKernelGetModuleInfo(handle, info) == 0;
}

/* Calls visit(segment, module name) for the data segments of the game's own
   modules among `handles`, until it returns false. */
template <typename Visit>
void for_each_game_segment(const int32_t *handles, size_t count, Visit visit) {
  for (size_t h = 0; h < count; ++h) {
    ModuleInfo info;
    if (!module_info(handles[h], &info) || system_module(info.name))
      continue;
    const uint32_t segments = info.segment_count < 4 ? info.segment_count : 4;
    for (uint32_t s = 0; s < segments; ++s) {
      const ModuleSegment &seg = info.segments[s];
      /* code is execute-only: reading it faults */
      if ((seg.prot & PROT_EXEC) || !(seg.prot & PROT_READ))
        continue;
      if (!visit(seg, info.name))
        return;
    }
  }
}

/* Points the import slots of one segment that still hold a real read at our
   hooks. */
void patch_segment(const ModuleSegment &seg, const char *module) {
  auto *words = reinterpret_cast<uint64_t *>(seg.address);
  /* the protection now, not the one at load: a page may have changed */
  uint32_t prot = 0;
  if (!readable(seg.address, seg.size, &prot))
    return;
  bool writable = prot & PROT_WRITE;
  bool made_writable = false;
  bool full = false;
  for (uint32_t w = 0; w < seg.size / 8 && !full && !Superseded(); ++w) {
    const uint64_t value = words[w];
    for (uint32_t k = 0; k < kKindCount; ++k) {
      if (!s_real[k] || value != s_real[k])
        continue;
      const uint32_t i = s_ctl->hook_count;
      if (i >= uint32_t(kMaxHookSlots)) {
        if (!s_full_logged)
          LOG("more than %d pad import slots; the rest stay unhooked",
              kMaxHookSlots);
        s_full_logged = true;
        full = true;
        break;
      }
      if (!writable) {
        if (kernel_mprotect(getpid(), seg.address, seg.size,
                            PROT_READ | PROT_WRITE) != 0) {
          LOG("%s: segment 0x%lx not writable", module,
              (unsigned long)seg.address);
          return;
        }
        writable = made_writable = true;
      }
      uint64_t expected = s_real[k];
      if (!__atomic_compare_exchange_n(&words[w], &expected, s_hook[k], false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        break;
      s_ctl->hook_slots[i] = reinterpret_cast<uint64_t>(&words[w]);
      s_ctl->hook_originals[i] = s_real[k];
      s_ctl->hook_kinds[i] = k;
      __atomic_store_n(&s_ctl->hook_count, i + 1, __ATOMIC_RELEASE);
      LOG("%s: %s slot %p hooked", module, kReads[k].name,
          static_cast<void *>(&words[w]));
      break;
    }
  }
  if (made_writable) /* read-only again, as we found it */
    kernel_mprotect(getpid(), seg.address, seg.size, prot);
}

void scan() {
  int32_t handles[kMaxModules];
  size_t count = 0;
  if (sceKernelGetModuleList(handles, kMaxModules, &count) != 0 ||
      count > kMaxModules) {
    /* refused: the low handle numbers that answer stand in for the list */
    count = 0;
    for (int32_t handle = 0; handle < int32_t(kMaxModules); ++handle) {
      ModuleInfo info;
      if (module_info(handle, &info))
        handles[count++] = handle;
    }
  }
  if (count == s_scanned_modules)
    return; /* nothing loaded or unloaded since the last scan */
  s_scanned_modules = count;
  for_each_game_segment(handles, count,
                        [](const ModuleSegment &seg, const char *module) {
                          patch_segment(seg, module);
                          return true;
                        });
  if (__atomic_load_n(&s_ctl->hook_count, __ATOMIC_ACQUIRE))
    __atomic_or_fetch(&s_ctl->agent_status, kStatusHooked, __ATOMIC_RELEASE);
}

/* Copies a newly written profile once the hook has let go of the spare
   buffer. A copy that raced the daemon's write is discarded. */
void copy_profile() {
  const uint32_t generation =
      __atomic_load_n(&s_ctl->profile_generation, __ATOMIC_ACQUIRE);
  const ProfileBlob *current = s_published.load(std::memory_order_relaxed);
  /* odd: the daemon is writing it right now */
  if ((generation & 1) || generation == s_profile_generation ||
      s_acked.load(std::memory_order_acquire) != current)
    return;
  ProfileBlob *spare = current == &s_profiles[0] ? &s_profiles[1] : &s_profiles[0];
  memcpy(spare, &s_ctl->profile, sizeof(*spare));
  std::atomic_thread_fence(std::memory_order_acquire);
  if (__atomic_load_n(&s_ctl->profile_generation, __ATOMIC_RELAXED) !=
      generation)
    return;
  s_profile_generation = generation;
  const bool valid = spare->magic == kBlobMagic &&
                     spare->version == kBlobVersion &&
                     spare->size == sizeof(ProfileBlob);
  LOG("profile generation %u %s", generation, valid ? "loaded" : "invalid");
  s_published.store(valid ? spare : nullptr, std::memory_order_release);
  s_profile_enabled.store(valid && spare->enabled, std::memory_order_relaxed);
}

} // namespace

bool Init(TickFn tick, LogFn log) {
  s_log = log;
  s_tick = tick;
  if (!tick || !resolve() || !map_control())
    return false;
  s_generation =
      __atomic_add_fetch(&s_ctl->agent_generation, 1u, __ATOMIC_ACQ_REL);
  LOG("agent generation %u", s_generation);
  take_over();
  scan();
  s_scan_at = agent::NowUs();
  return true;
}

void Service() {
  if (!s_ctl || Superseded())
    return;
  copy_profile();
  const uint64_t now = agent::NowUs();
  if (now - s_handle_at >= kHandleCheckUs && s_get_handle && s_foreground_user) {
    s_handle_at = now;
    int32_t user = -1;
    int32_t handle = -1;
    if (s_foreground_user(&user) == 0 && user >= 0)
      handle = s_get_handle(user, 0, 0);
    s_our_handle.store(handle >= 0 ? handle : -1, std::memory_order_relaxed);
  }
  if (now - s_scan_at >= kRescanUs) {
    s_scan_at = now;
    scan();
  }
}

bool Superseded() {
  return s_ctl && __atomic_load_n(&s_ctl->agent_generation, __ATOMIC_ACQUIRE) !=
                      s_generation;
}

bool ProfileEnabled() {
  return s_profile_enabled.load(std::memory_order_relaxed);
}

void Unhook() {
  if (!s_ctl || Superseded())
    return;
  const uint32_t count = __atomic_load_n(&s_ctl->hook_count, __ATOMIC_ACQUIRE);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t kind = s_ctl->hook_kinds[i];
    if (kind < kKindCount)
      SwapSlot(reinterpret_cast<uint64_t *>(s_ctl->hook_slots[i]), s_hook[kind],
               s_ctl->hook_originals[i]);
  }
  __atomic_store_n(&s_ctl->hook_count, 0u, __ATOMIC_RELEASE);
  __atomic_and_fetch(&s_ctl->agent_status, ~kStatusHooked, __ATOMIC_RELEASE);
  LOG("%u slot(s) restored", count);
}

void SwapSlot(uint64_t *slot, uint64_t from, uint64_t to) {
  const uint64_t address = reinterpret_cast<uint64_t>(slot);
  uint32_t prot = 0;
  if (!mapped(address, &prot))
    return;
  /* a failed swap writes too, and scan left this page read-only */
  write_slot(address, prot, [slot, from, to] {
    uint64_t expected = from;
    __atomic_compare_exchange_n(slot, &expected, to, false, __ATOMIC_ACQ_REL,
                                __ATOMIC_RELAXED);
  });
}

ControlPage *Control() { return s_ctl; }

int FindImportSlots(const void *fn, uint64_t **out, int max) {
  if (!fn || !out || max <= 0)
    return 0;
  int32_t handles[kMaxModules];
  size_t count = 0;
  if (sceKernelGetModuleList(handles, kMaxModules, &count) != 0 ||
      count > kMaxModules)
    return 0;
  int found = 0;
  for_each_game_segment(handles, count, [&](const ModuleSegment &seg, const char *) {
    uint32_t prot = 0;
    if (!readable(seg.address, seg.size, &prot))
      return true;
    auto *words = reinterpret_cast<uint64_t *>(seg.address);
    bool writable = prot & PROT_WRITE;
    for (uint32_t w = 0; w < seg.size / 8 && found < max; ++w) {
      if (words[w] != reinterpret_cast<uint64_t>(fn))
        continue;
      if (!writable) {
        if (kernel_mprotect(getpid(), seg.address, seg.size,
                            PROT_READ | PROT_WRITE) != 0)
          break;
        writable = true;
      }
      out[found++] = &words[w];
    }
    return found < max;
  });
  return found;
}

} // namespace onion::kbm::hooks
