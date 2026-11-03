#include "aniketos_shell/rn_scripts/rn_reload.hpp"

#include "aniketos_ui/monotonic_clock.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>

/* Signed: a start noted on another thread can be later than this reading. */
static int64_t now_ms() {
  return static_cast<int64_t>(AniketosUI::MonotonicMs());
}

/* When a delayed Home reload is due; 0 when none is. */
static std::atomic<int64_t> s_home_reload_due_ms{0};

void shellui_request_home_reload_after(int delay_ms) {
  s_home_reload_due_ms.store(now_ms() + delay_ms, std::memory_order_release);
}

/* When Home's bundle last started to run; 0 before the first time. A
   reload asked for while Home is still starting waits until it has been up
   kHomeSettleMs: three switches flipped in a row once reloaded Home a second
   into its start, and ShellUI hung (2026-10-17, slim 11.40). */
static std::atomic<int64_t> s_home_started_ms{0};
static constexpr int64_t kHomeSettleMs = 4000;

void shellui_note_home_start(void) {
  s_home_started_ms.store(now_ms(), std::memory_order_release);
}

bool shellui_home_reload_held(std::atomic<bool> &pending) {
  int64_t due = s_home_reload_due_ms.load(std::memory_order_acquire);
  if (due != 0 && now_ms() >= due &&
      s_home_reload_due_ms.compare_exchange_strong(due, 0,
                                                   std::memory_order_acq_rel))
    pending.store(true, std::memory_order_release);
  if (!pending.load(std::memory_order_acquire))
    return true;
  const int64_t started = s_home_started_ms.load(std::memory_order_acquire);
  return started != 0 && now_ms() - started < kHomeSettleMs;
}

/* When the presence-dot reload is due; 0 when none is. */
static std::atomic<int64_t> s_presence_reload_due_ms{0};

static std::atomic<bool> s_presence_reload_home_too{false};

void shellui_request_presence_reload(int delay_ms, bool home_too) {
  if (home_too)
    s_presence_reload_home_too.store(true, std::memory_order_release);
  s_presence_reload_due_ms.store(now_ms() + delay_ms,
                                 std::memory_order_release);
}

void shellui_poll_presence_reload(void) {
  const int64_t due = s_presence_reload_due_ms.load(std::memory_order_acquire);
  if (due == 0 || now_ms() < due)
    return;
  s_presence_reload_due_ms.store(0, std::memory_order_release);
  const bool home =
      s_presence_reload_home_too.exchange(false, std::memory_order_acq_rel);
  LOG_INFO("reloading %sControl Center for its script (presence dot, clock)",
           home ? "Home and " : "");
  /* Home through its own queue: one reload however many asked, never into a
     Home still starting, and not on the same tick as the Control Center. */
  if (home)
    shellui_request_home_reload();
  ReloadRNPSApp("NPXS40003");
}
