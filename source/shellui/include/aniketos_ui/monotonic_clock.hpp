#pragma once

#include <cstdint>
#include <time.h>

namespace AniketosUI {

inline uint64_t MonotonicMs() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000u +
         static_cast<uint64_t>(now.tv_nsec) / 1000000u;
}

inline void SleepMs(long ms) {
  const timespec delay{ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&delay, nullptr);
}

} // namespace AniketosUI
