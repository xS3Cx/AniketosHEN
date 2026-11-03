#include "aniketos_shell/input/pad_reader.hpp"

#include "aniketos_ui/monotonic_clock.hpp"
#include "hooked_funcs.hpp"

#include <onion/platform.h>

#include <algorithm>
#include <atomic>
#include <pthread.h>

extern "C" int scePadOpen(int user_id, int type, int index, const void *param);
extern "C" int scePadClose(int handle);
extern "C" int scePadReadState(int handle, void *data);

namespace AniketosShell::Input {
namespace {

using AniketosUI::MonotonicMs;
using AniketosUI::SleepMs;

struct ScePadTouch {
  uint16_t x;
  uint16_t y;
  uint8_t finger;
  uint8_t reserved[3];
};
struct ScePadTouchData {
  uint8_t fingers;
  uint8_t reserved0[3];
  uint32_t reserved1;
  ScePadTouch touch[2];
};
struct ScePadData {
  uint32_t buttons;
  struct {
    uint8_t x, y;
  } left_stick;
  struct {
    uint8_t x, y;
  } right_stick;
  struct {
    uint8_t l2, r2;
  } analog_buttons;
  uint16_t reserved0;
  struct {
    float x, y, z, w;
  } quat;
  struct {
    float x, y, z;
  } velocity;
  struct {
    float x, y, z;
  } acceleration;
  ScePadTouchData touch_data;
  uint8_t connected;
  uint8_t reserved1[3];
  uint64_t timestamp;
  uint8_t extension[16];
  uint8_t count;
  uint8_t reserved2[15];
};
static_assert(sizeof(ScePadData) == 120, "unexpected ScePadData ABI");

constexpr uint32_t kNativeL3 = 0x00000002u;
constexpr uint32_t kNativeR3 = 0x00000004u;
constexpr uint32_t kNativeOption = 0x00000008u;
constexpr uint32_t kNativeUp = 0x00000010u;
constexpr uint32_t kNativeRight = 0x00000020u;
constexpr uint32_t kNativeDown = 0x00000040u;
constexpr uint32_t kNativeLeft = 0x00000080u;
constexpr uint32_t kNativeL1 = 0x00000400u;
constexpr uint32_t kNativeR1 = 0x00000800u;
constexpr uint32_t kNativeTriangle = 0x00001000u;
constexpr uint32_t kNativeCircle = 0x00002000u;
constexpr uint32_t kNativeCross = 0x00004000u;
constexpr uint32_t kNativeSquare = 0x00008000u;
/* Set by libScePad when the system has taken the controller from this
   process; the other bits are then empty. */
constexpr uint32_t kNativeIntercepted = 0x80000000u;
constexpr struct {
  uint32_t native;
  uint32_t managed;
} kManagedButtons[] = {
    {kNativeLeft, Left},     {kNativeUp, Up},
    {kNativeRight, Right},   {kNativeDown, Down},
    {kNativeSquare, Square}, {kNativeTriangle, Triangle},
    {kNativeCircle, Circle}, {kNativeCross, Cross},
    {kNativeOption, Option}, {kNativeL1, L1},
    {kNativeR1, R1},         {kNativeR3, R3},
};
constexpr uint32_t kShellCombo = kNativeL1 | kNativeR3;
constexpr uint32_t kCheatsCombo = kNativeL1 | kNativeL3 | kNativeR3;
/* L1+R3 is part of L1+L3+R3, and the three buttons never land in the same
   read: L1+R3 opens the Shell Menu once this long has passed without L3, or
   when it is let go sooner (a quick click of R3 is shorter than this) and
   L3 never came. */
constexpr uint64_t kComboSettleMs = 120;
/* Only against a bouncing contact: one press of the chord toggles once. */
constexpr uint64_t kToggleDebounceMs = 250;

std::atomic<bool> s_started{false};
std::atomic<bool> s_toggle_requested{false};
std::atomic<bool> s_cheats_toggle_requested{false};
std::atomic<bool> s_intercepted{false};
std::atomic<bool> s_capturing{false};
/* The buttons held as last read, and whether ShellUI is still kept from
   seeing them after the menu let go: the press that closed it (Cross on an
   entry that hides the menu, L1+R3) would otherwise reach Home as a new
   one, on whatever had the focus there; a game's tile started its game. */
std::atomic<uint32_t> s_held{0};
std::atomic<bool> s_release_guard{false};
std::atomic<uint32_t> s_buttons_down{0};
/* Subset of s_buttons_down produced by holding a direction (auto-repeat). */
std::atomic<uint32_t> s_buttons_repeated{0};
std::atomic<int> s_active_pad{-1};
std::atomic<int> s_left_stick_x{0};
std::atomic<int> s_left_stick_y{0};

struct RepeatState {
  uint64_t pressed_at = 0;
  uint64_t repeated_at = 0;
};

uint32_t to_managed_buttons(uint32_t native) {
  uint32_t out = 0;
  for (const auto &button : kManagedButtons)
    if (native & button.native)
      out |= button.managed;
  return out;
}

void clear_left_stick() {
  s_left_stick_x.store(0, std::memory_order_release);
  s_left_stick_y.store(0, std::memory_order_release);
}

/* -1000..1000 past a radial dead zone of 20. */
int normalize_axis(uint8_t raw) {
  const int centered = static_cast<int>(raw) - 128;
  const int magnitude = centered < 0 ? -centered : centered;
  if (magnitude <= 20)
    return 0;
  const int scaled = std::min(1000, (magnitude - 20) * 1000 / 107);
  return centered < 0 ? -scaled : scaled;
}

/* The directions whose hold repeats at `now`. */
uint32_t repeated_directions(uint32_t buttons, uint32_t native_down,
                             uint64_t now, RepeatState (&repeat)[4]) {
  constexpr uint32_t directions[4] = {kNativeLeft, kNativeUp, kNativeRight,
                                      kNativeDown};
  uint32_t repeated = 0;
  for (int i = 0; i < 4; ++i) {
    RepeatState &item = repeat[i];
    if (!(buttons & directions[i])) {
      item = {};
      continue;
    }
    if (native_down & directions[i]) {
      item.pressed_at = now;
      item.repeated_at = now;
      continue;
    }
    /* Left/right step sliders and stay quick. Up/down move the list
       cursor: about eight rows a second after a deliberate pause, slow
       enough to stop on the row you want. kListRepeatMs is also how long
       the cursor takes to glide one row while a direction is held. */
    const bool horizontal = i == 0 || i == 2;
    const uint64_t interval = horizontal ? 38u : kListRepeatMs;
    const uint64_t initial_delay = horizontal ? 260u : 340u;
    if (item.pressed_at && now - item.pressed_at >= initial_delay &&
        now - item.repeated_at >= interval) {
      repeated |= directions[i];
      item.repeated_at = now;
    }
  }
  return repeated;
}

void *reader_thread(void *) {
  int pad = -1;
  uint32_t previous = 0;
  bool shortcut_latched = false;
  uint64_t shortcut_pending_ms = 0;
  bool cheats_latched = false;
  uint64_t last_toggle_ms = 0;
  RepeatState repeat[4]{};
  LOG_INFO("[ANIVSH:INPUT] started backend=libScePad shortcut=L1+R3 "
           "cheats=L1+L3+R3");

  for (;;) {
    if (pad < 0) {
      int user_id = -1;
      if (sceUserServiceGetForegroundUser(&user_id) == 0 && user_id >= 0)
        pad = scePadOpen(user_id, 0, 0, nullptr);
      if (pad < 0) {
        SleepMs(500);
        continue;
      }
      previous = 0;
      s_active_pad.store(pad, std::memory_order_release);
      LOG_INFO("[ANIVSH:INPUT] controller opened handle=%d", pad);
    }

    ScePadData state{};
    if (scePadReadState(pad, &state) != 0) {
      s_active_pad.store(-1, std::memory_order_release);
      (void)scePadClose(pad);
      pad = -1;
      previous = 0;
      s_held.store(0, std::memory_order_release);
      s_release_guard.store(false, std::memory_order_release);
      shortcut_latched = false;
      shortcut_pending_ms = 0;
      cheats_latched = false;
      clear_left_stick();
      for (RepeatState &item : repeat)
        item = {};
      SleepMs(100);
      continue;
    }

    const bool intercepted = (state.buttons & kNativeIntercepted) != 0;
    if (intercepted != s_intercepted.exchange(intercepted,
                                              std::memory_order_acq_rel))
      LOG_INFO("[ANIVSH:INPUT] controller intercepted=%d", intercepted ? 1 : 0);
    const uint32_t native_down = state.buttons & ~previous;
    const bool shortcut = (state.buttons & kShellCombo) == kShellCombo;
    const bool cheats = (state.buttons & kCheatsCombo) == kCheatsCombo;
    bool shortcut_fire = false;
    if (shortcut && !shortcut_latched) {
      shortcut_latched = true;
      shortcut_pending_ms = MonotonicMs();
    } else if (!shortcut) {
      shortcut_latched = false;
      /* let go before the settle time: still a press of L1+R3 */
      shortcut_fire = shortcut_pending_ms != 0;
      shortcut_pending_ms = 0;
    }
    if (state.buttons & kNativeL3) {
      shortcut_pending_ms = 0;
      shortcut_fire = false;
    }
    if (shortcut_pending_ms &&
        MonotonicMs() - shortcut_pending_ms >= kComboSettleMs) {
      shortcut_pending_ms = 0;
      shortcut_fire = true;
    }
    if (shortcut_fire) {
      const uint64_t now = MonotonicMs();
      if (now - last_toggle_ms >= kToggleDebounceMs) {
        last_toggle_ms = now;
        s_toggle_requested.store(true, std::memory_order_release);
        LOG_INFO("[ANIVSH:INPUT] shortcut edge L1+R3");
      }
    }
    if (cheats && !cheats_latched) {
      cheats_latched = true;
      s_cheats_toggle_requested.store(true, std::memory_order_release);
      LOG_INFO("[ANIVSH:INPUT] shortcut edge L1+L3+R3");
    } else if (!cheats) {
      cheats_latched = false;
    }

    if (s_capturing.load(std::memory_order_acquire) && !shortcut) {
      const uint32_t repeated_native = repeated_directions(
          state.buttons, native_down, MonotonicMs(), repeat);
      const uint32_t managed_down =
          to_managed_buttons(native_down | repeated_native);
      if (managed_down)
        s_buttons_down.fetch_or(managed_down, std::memory_order_acq_rel);
      if (repeated_native)
        s_buttons_repeated.fetch_or(to_managed_buttons(repeated_native),
                                    std::memory_order_acq_rel);

      s_left_stick_x.store(normalize_axis(state.left_stick.x),
                           std::memory_order_release);
      s_left_stick_y.store(normalize_axis(state.left_stick.y),
                           std::memory_order_release);
    } else {
      clear_left_stick();
      for (RepeatState &item : repeat)
        item = {};
    }
    s_held.store(state.buttons, std::memory_order_release);
    if (!state.buttons)
      s_release_guard.store(false, std::memory_order_release);
    previous = state.buttons;
    /* The menu's auto-repeat and the release guard want 8 ms; the shortcuts
       alone (the rest of the time, a game included) one read a frame, which
       still sees the quickest press of a button. */
    SleepMs(IsMenuCapturing() ? 8 : 16);
  }
}

} // namespace

bool Start() {
  bool expected = false;
  if (!s_started.compare_exchange_strong(expected, true))
    return true;
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, reader_thread, nullptr) != 0) {
    s_started.store(false);
    LOG_ERROR("[ANIVSH:INPUT] failed to create reader thread");
    return false;
  }
  pthread_detach(thread);
  return true;
}

bool ConsumeToggleRequest() {
  return s_toggle_requested.exchange(false, std::memory_order_acq_rel);
}

bool ConsumeCheatsToggleRequest() {
  return s_cheats_toggle_requested.exchange(false, std::memory_order_acq_rel);
}

bool IsIntercepted() { return s_intercepted.load(std::memory_order_acquire); }

uint32_t HeldButtons() {
  return s_held.load(std::memory_order_acquire) & ~kNativeIntercepted;
}

uint32_t ConsumeRepeatedButtons() {
  return s_buttons_repeated.exchange(0, std::memory_order_acq_rel);
}

uint32_t ConsumeButtonsDown() {
  return s_buttons_down.exchange(0, std::memory_order_acq_rel);
}

void ReadLeftStick(float &x, float &y) {
  x = s_left_stick_x.load(std::memory_order_acquire) / 1000.0f;
  y = s_left_stick_y.load(std::memory_order_acquire) / 1000.0f;
}

void SetMenuCapturing(bool capturing) {
  const bool was = s_capturing.exchange(capturing, std::memory_order_acq_rel);
  if (capturing)
    return;
  if (was && s_held.load(std::memory_order_acquire) != 0)
    s_release_guard.store(true, std::memory_order_release);
  s_buttons_down.store(0, std::memory_order_release);
  clear_left_stick();
}

bool IsMenuCapturing() {
  return s_capturing.load(std::memory_order_acquire) ||
         s_release_guard.load(std::memory_order_acquire);
}

int ActivePadHandle() { return s_active_pad.load(std::memory_order_acquire); }

} // namespace AniketosShell::Input
