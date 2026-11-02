#include "aniketos_shell/hardware/pad_speaker.hpp"

#include "hooked_funcs.hpp"

#include <onion/log.h>
#include <onion/settings.hpp>

#include <ps5/kernel.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

extern "C" {
int sceKernelOpenEventFlag(void **flag, const char *name);
int sceKernelWaitEventFlag(void *flag, uint64_t pattern, uint32_t mode,
                           uint64_t *result, uint32_t *timeout_us);
int sceKernelCloseEventFlag(void *flag);
}

namespace PadSpeaker {
namespace {

constexpr int kPortPadSpeaker = 4;   /* sceAudioOutOpen port type */
constexpr unsigned kFloatMono = 3;   /* sceAudioOutOpen data format */
constexpr unsigned kFrames = 256;    /* the server's tick, 5.3 ms at 48 kHz */
constexpr unsigned kRate = 48000;
/* The capture rings: the final stereo mix, and the pad speakers' own
   stream, which only exists where the server mixes them into the first. */
constexpr int kCaptureFull = 0;
constexpr int kCapturePadSpeaker = 8;
/* one tick of the ring: stereo float32 */
constexpr uint32_t kPeriodBytes = kFrames * 2 * sizeof(float);
constexpr uint32_t kRingHeader = 0x10; /* a 16-byte header, then data */
constexpr size_t kMinimumRingBytes = 0x1000;
constexpr uint32_t kWaitOrClear = 0x22; /* as the capture service waits */

using OpenFn = int (*)(int user, int type, int index, unsigned length,
                       unsigned rate, unsigned format);
using OutputFn = int (*)(int port, const void *samples);
using CloseFn = int (*)(int port);
using InitFn = int (*)();
/* libSceAudioOut's request for a capture ring: maps it read-only */
using CaptureFn = int (*)(int type, void **address, size_t *size);

struct Api {
  OpenFn open = nullptr;
  OutputFn output = nullptr;
  CloseFn close = nullptr;
  CaptureFn capture = nullptr;
};
Api s_api;
bool s_resolved = false;

std::atomic<bool> s_running{false};
std::atomic<bool> s_stop{false};
time_t s_retry_at = 0;

/* libkernel's description of a loaded module (sceKernelGetModuleInfo). */
struct ModuleInfo {
  uint64_t size;
  char name[256];
  struct {
    uint64_t address;
    uint32_t size;
    int32_t prot;
  } segments[4];
  uint32_t segment_count;
  uint8_t fingerprint[20];
};
static_assert(sizeof(ModuleInfo) == 0x160);

const uint8_t *find_bytes(const uint8_t *from, size_t length, const void *needle,
                    size_t needle_length) {
  for (size_t i = 0; i + needle_length <= length; ++i)
    if (std::memcmp(from + i, needle, needle_length) == 0)
      return from + i;
  return nullptr;
}

/* The capture request has no known export name: it is the function that
   formats "socket_audio_%d" (the socket the server hands the ring's fd
   over) and invokes server method 0x6e. Its code is execute-only, so the
   text is made readable for the search and set back afterwards. */
CaptureFn find_capture(uint32_t module) {
  using GetModuleInfoFn = int (*)(uint32_t handle, ModuleInfo *info);
  uint32_t kernel = 0;
  if (kernel_dynlib_handle(getpid(), "libkernel_sys.sprx", &kernel) != 0 &&
      kernel_dynlib_handle(getpid(), "libkernel.sprx", &kernel) != 0)
    return nullptr;
  const auto get_info = reinterpret_cast<GetModuleInfoFn>(
      kernel_dynlib_dlsym(getpid(), kernel, "sceKernelGetModuleInfo"));
  ModuleInfo info{};
  info.size = sizeof(info);
  if (!get_info || get_info(module, &info) != 0 || info.segment_count == 0 ||
      info.segment_count > 4)
    return nullptr;
  const uint8_t *text = nullptr, *name = nullptr;
  size_t text_size = 0;
  int text_prot = 0;
  for (uint32_t i = 0; i < info.segment_count; ++i) {
    const auto *start = reinterpret_cast<const uint8_t *>(
        info.segments[i].address);
    if (info.segments[i].prot & PROT_EXEC) {
      text = start;
      text_size = info.segments[i].size;
      text_prot = info.segments[i].prot;
    } else if (!name && (info.segments[i].prot & PROT_READ)) {
      name = find_bytes(start, info.segments[i].size, "socket_audio_%d", 16);
    }
  }
  if (!text || !name ||
      kernel_mprotect(getpid(), reinterpret_cast<intptr_t>(text), text_size,
                      PROT_READ | PROT_EXEC) != 0)
    return nullptr;
  CaptureFn found = nullptr;
  for (size_t i = 0; !found && i + 7 <= text_size; ++i) {
    /* lea reg, [rip + disp32] pointing at the name */
    if ((text[i] & 0xfb) != 0x48 || text[i + 1] != 0x8d ||
        (text[i + 2] & 0xc7) != 0x05)
      continue;
    int32_t displacement;
    std::memcpy(&displacement, text + i + 3, 4);
    if (text + i + 7 + displacement != name)
      continue;
    for (size_t start = i; start > 0 && i - start < 0x400; --start) {
      if (std::memcmp(text + start, "\x55\x48\x89\xe5", 4) != 0)
        continue;
      if (find_bytes(text + start, i - start, "\xbe\x6e\x00\x00\x00", 5))
        found = reinterpret_cast<CaptureFn>(const_cast<uint8_t *>(text + start));
      break;
    }
  }
  (void)kernel_mprotect(getpid(), reinterpret_cast<intptr_t>(text), text_size,
                        text_prot);
  return found;
}

bool resolve() {
  if (s_resolved)
    return s_api.capture != nullptr;
  s_resolved = true;
  uint32_t module = 0;
  if (kernel_dynlib_handle(getpid(), "libSceAudioOut.sprx", &module) != 0)
    return false;
  auto symbol = [&](const char *name) {
    return reinterpret_cast<void *>(
        kernel_dynlib_dlsym(getpid(), module, name));
  };
  s_api.open = reinterpret_cast<OpenFn>(symbol("sceAudioOutOpen"));
  s_api.output = reinterpret_cast<OutputFn>(symbol("sceAudioOutOutput"));
  s_api.close = reinterpret_cast<CloseFn>(symbol("sceAudioOutClose"));
  if (auto init = reinterpret_cast<InitFn>(symbol("sceAudioOutInit")))
    (void)init(); /* already done by ShellUI: harmless */
  if (s_api.open && s_api.output && s_api.close)
    s_api.capture = find_capture(module);
  LOG_INFO("[PADSPK] open=%p output=%p capture=%p",
           reinterpret_cast<void *>(s_api.open),
           reinterpret_cast<void *>(s_api.output),
           reinterpret_cast<void *>(s_api.capture));
  return s_api.capture != nullptr;
}

void relay(const uint8_t *ring, size_t mapped_size, void *flag, int port) {
  const uint32_t ring_size = *reinterpret_cast<const volatile uint32_t *>(ring);
  /* +8: timestamp, +0xc: committed write position, stored together */
  const auto *commit = reinterpret_cast<const volatile uint64_t *>(ring + 8);
  uint32_t read = static_cast<uint32_t>(*commit >> 32);
  /* found by pattern, so another firmware's layout can land here */
  if (ring_size > mapped_size || ring_size < kRingHeader + kPeriodBytes ||
      read < kRingHeader || read >= ring_size)
    return;
  const uint32_t data = ring_size - kRingHeader;
  static float out[2][kFrames];
  float in[kFrames * 2];
  for (unsigned k = 0; !s_stop.load(std::memory_order_relaxed); k ^= 1) {
    uint32_t write = 0, available = 0;
    for (int tries = 0;; ++tries) {
      write = static_cast<uint32_t>(*commit >> 32);
      if (write < kRingHeader || write >= ring_size)
        return; /* not a ring this code understands */
      available = write >= read ? write - read : data - (read - write);
      if (available >= kPeriodBytes || tries > 20)
        break;
      uint32_t timeout = 2000;
      if (!flag || sceKernelWaitEventFlag(flag, 1ull << 40, kWaitOrClear,
                                          nullptr, &timeout) != 0)
        usleep(1000);
    }
    if (available < kPeriodBytes) {
      std::memset(out[k], 0, sizeof(out[k])); /* nothing plays: silence */
    } else {
      if (available > 4 * kPeriodBytes) /* fell behind: one tick late at most */
        read = write >= kRingHeader + kPeriodBytes ? write - kPeriodBytes
                                                   : write + data - kPeriodBytes;
      const uint32_t first = std::min(ring_size - read, kPeriodBytes);
      std::memcpy(in, ring + read, first);
      std::memcpy(reinterpret_cast<uint8_t *>(in) + first, ring + kRingHeader,
                  kPeriodBytes - first);
      read += kPeriodBytes;
      if (read >= ring_size)
        read -= data;
      /* stereo to mono, at the chosen loudness; read once per tick */
      const float gain =
          0.5f * std::clamp(g_settings.pad_speaker_volume, 0, 100) / 100.0f;
      for (unsigned i = 0; i < kFrames; ++i)
        out[k][i] = gain * (in[2 * i] + in[2 * i + 1]);
    }
    /* blocks until the speaker wants the next tick: this paces the loop */
    if (s_api.output(port, out[k]) < 0)
      return;
  }
}

void *thread_main(void *) {
  void *pad_ring = nullptr, *ring = nullptr;
  size_t pad_ring_size = 0, ring_size = 0;
  void *flag = nullptr;
  int user = -1, port = -1;
  /* Where the pad speakers are mixed into the final mix, relaying it would
     feed the speaker back into itself. */
  if (s_api.capture(kCapturePadSpeaker, &pad_ring, &pad_ring_size) == 0 &&
      pad_ring) {
    munmap(pad_ring, pad_ring_size);
    LOG_WARN("[PADSPK] the pad speaker is part of the mix here: not relaying");
  } else if (s_api.capture(kCaptureFull, &ring, &ring_size) != 0 || !ring) {
    LOG_WARN("[PADSPK] no capture ring");
    ring = nullptr;
  } else if (ring_size < kMinimumRingBytes) {
    LOG_WARN("[PADSPK] capture ring too small: %zu", ring_size);
  } else if (sceUserServiceGetForegroundUser(&user) != 0 || user <= 0 ||
             (port = s_api.open(user, kPortPadSpeaker, 0, kFrames, kRate,
                                kFloatMono)) < 0) {
    LOG_WARN("[PADSPK] no speaker port user=0x%x port=0x%x", user, port);
  } else {
    if (sceKernelOpenEventFlag(&flag, "/evf_acap.full") != 0)
      flag = nullptr; /* polls every millisecond instead */
    LOG_INFO("[PADSPK] relaying ring=%zu user=0x%x", ring_size, user);
    relay(static_cast<const uint8_t *>(ring), ring_size, flag, port);
    s_api.output(port, nullptr); /* lets the last tick play */
    s_api.close(port);
    if (flag)
      sceKernelCloseEventFlag(flag);
    LOG_INFO("[PADSPK] stopped");
  }
  if (ring)
    munmap(ring, ring_size);
  s_running.store(false, std::memory_order_release);
  return nullptr;
}

} // namespace

void Tick() {
  const bool wanted = g_settings.pad_speaker_mirror;
  if (!wanted) {
    s_stop.store(true, std::memory_order_relaxed);
    return;
  }
  if (s_running.load(std::memory_order_acquire))
    return;
  /* a relay that ended on its own (another user, no ring) retries slowly */
  const time_t now = std::time(nullptr);
  if (now < s_retry_at || !resolve())
    return;
  s_retry_at = now + 5;
  s_stop.store(false, std::memory_order_relaxed);
  s_running.store(true, std::memory_order_release);
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, thread_main, nullptr) != 0) {
    s_running.store(false, std::memory_order_release);
    return;
  }
  pthread_detach(thread);
}

} // namespace PadSpeaker
