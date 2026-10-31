/* Keyboard and mouse input inside the game: see kbm_input.hpp. The calls,
   structures and event ids are the ones proven in GTA V on 11.40
   (.docker/kbm-agent/agent.c). */
#include "kbm_input.hpp"

#include <atomic>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

namespace onion::kbm::hooks {
/* W5's helper: the import slots in the game's own modules (not .sprx) that
   hold `fn`, made writable. Weak: without it the game's IME is not
   forwarded and only our pump runs. */
__attribute__((weak)) int FindImportSlots(const void *fn, uint64_t **out,
                                          int max);
} // namespace onion::kbm::hooks

namespace onion::kbm::agent {
namespace {

uint64_t NowUs() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<uint64_t>(t.tv_sec) * 1000000u +
         static_cast<uint64_t>(t.tv_nsec) / 1000u;
}

/* ---- log (klog, at most a few lines a second) ----------------------------- */

int (*s_out)(int, const char *);
uint64_t s_log_window_us;
int s_log_count, s_log_dropped;
constexpr int kLogsPerSecond = 8;

__attribute__((format(printf, 1, 2))) void Log(const char *fmt, ...) {
  if (!s_out)
    return;
  char line[256];
  const uint64_t now = NowUs();
  if (now - s_log_window_us >= 1000000) {
    if (s_log_dropped) {
      snprintf(line, sizeof(line), "[KBM-INPUT] (%d lines dropped)\n",
               s_log_dropped);
      s_out(0, line);
    }
    s_log_window_us = now;
    s_log_count = s_log_dropped = 0;
  }
  if (++s_log_count > kLogsPerSecond) {
    ++s_log_dropped;
    return;
  }
  int n = snprintf(line, sizeof(line), "[KBM-INPUT] ");
  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
  va_end(ap);
  if (n > static_cast<int>(sizeof(line)) - 2)
    n = sizeof(line) - 2;
  line[n] = '\n';
  line[n + 1] = 0;
  s_out(0, line);
}

/* ---- libraries -------------------------------------------------------------- */

const char *(*s_sandbox_word)();
int (*s_load_start)(const char *, size_t, const void *, uint32_t, void *,
                    int *);
int (*s_set_priority)(pthread_t, int);

void *Sym(uint32_t module, const char *name) {
  const intptr_t address =
      module ? kernel_dynlib_dlsym(getpid(), module, name) : 0;
  if (address <= 0)
    Log("%s not found", name);
  return address > 0 ? reinterpret_cast<void *>(address) : nullptr;
}

/* A system library by name; when the game has not loaded it, from where this
   sandbox sees /system (/<random word>/common/lib): the plain path is
   refused (0x80020002) inside a game. No userland call gives a loaded
   module's directory (sceKernelGetModuleInfo has its file name only), so the
   _ex candidates stay a guess; our three libraries live in common/lib. */
uint32_t Module(const char *name) {
  uint32_t handle = 0;
  if (kernel_dynlib_handle(getpid(), name, &handle) == 0)
    return handle;
  const char *word = s_sandbox_word ? s_sandbox_word() : nullptr;
  const char *const dirs[] = {"common/lib", "common_ex/lib"};
  char path[256];
  int id = 0;
  for (int i = 0; i < 4 && s_load_start; ++i) {
    if (i < 2 && !word)
      continue;
    if (i < 2)
      snprintf(path, sizeof(path), "/%s%s/%s/%s", word, i ? "_ex" : "",
               dirs[i], name);
    else
      snprintf(path, sizeof(path), "/%s/%s/%s", i == 2 ? "system" : "system_ex",
               dirs[i - 2], name);
    int result = 0;
    id = s_load_start(path, 0, nullptr, 0, nullptr, &result);
    if (kernel_dynlib_handle(getpid(), name, &handle) == 0) {
      Log("%s loaded from %s", name, path);
      return handle;
    }
  }
  Log("%s not loaded (0x%x, word %s)", name, static_cast<unsigned>(id),
      word ? word : "?");
  return 0;
}

/* ---- system structures ---------------------------------------------------- */

struct MouseData { /* SceMouseData */
  uint64_t timestamp;
  uint8_t connected;
  uint8_t padding[3];
  uint32_t buttons;
  int32_t x, y, wheel, tilt;
  uint8_t reserved[8];
};
static_assert(sizeof(MouseData) == 40, "SceMouseData");
struct MouseOpenParam {
  uint8_t behavior;
  uint8_t reserved[7];
};
constexpr uint8_t kMouseMerged = 0x01; /* every mouse plugged in, as one */
constexpr uint32_t kMouseIntercepted = 0x80000000u; /* the system has it */

struct ImeKeycode {
  uint16_t keycode; /* USB HID usage */
  uint16_t character;
  uint32_t status; /* bits 8-15: LCtrl LShift LAlt LGui RCtrl RShift RAlt RGui */
  uint32_t type;
  int32_t user;
  uint32_t resource;
  uint64_t timestamp;
};
struct ImeEvent {
  uint32_t id;
  uint32_t pad;
  union {
    ImeKeycode keycode;
    uint8_t raw[64];
  } param;
};
using ImeHandler = void (*)(void *arg, const ImeEvent *event);
struct ImeKeyboardParam {
  uint32_t option;
  int8_t reserved1[4];
  void *arg;
  ImeHandler handler;
  int8_t reserved2[8];
};
constexpr uint32_t kImeKeyDown = 0x101, kImeKeyUp = 0x102,
                   kImeKeyRepeat = 0x103, kImeConnection = 0x104,
                   kImeDisconnection = 0x105, kImeAbort = 0x106;
constexpr int kImeBusy = static_cast<int>(0x80bc0001u); /* someone has it */
constexpr int kImeNotOpen = static_cast<int>(0x80bc0002u);
constexpr Input kModifierFirst = 0xe0;

struct Api {
  int (*foreground_user)(int *) = nullptr;
  int (*mouse_init)() = nullptr;
  int (*mouse_open)(int, int, int, const MouseOpenParam *) = nullptr;
  int (*mouse_read)(int, MouseData *, int) = nullptr;
  int (*mouse_close)(int) = nullptr;
  int (*ime_open)(int, const ImeKeyboardParam *) = nullptr;
  int (*ime_update)(ImeHandler) = nullptr;
  int (*ime_close)(int) = nullptr;
};
Api s_api;
bool MouseUsable() {
  return s_api.mouse_open && s_api.mouse_read && s_api.mouse_close;
}
bool KeyboardUsable() {
  return s_api.ime_open && s_api.ime_update && s_api.ime_close;
}

/* ---- the hand-off to the hook thread ---------------------------------------
   Plain atomics: the reader publishes the held set word by word and adds
   motion; a taker swaps the accumulators and the press latches to zero. */

constexpr int kWords = (kInputCount + 63) / 64;
static_assert(sizeof(InputFrame::down) <= kWords * 8, "held set");
static_assert(kMouseButtonFirst == 4 * 64 && kMouseButtonLast < 4 * 64 + 8,
              "mouse buttons are the low byte of word 4");
std::atomic<uint64_t> s_held[kWords];
std::atomic<uint64_t> s_pressed[kWords]; /* went down since the last take */
std::atomic<int32_t> s_dx, s_dy, s_wheel, s_tilt;
std::atomic<uint64_t> s_alive_us, s_last_take_us;
std::atomic<bool> s_running, s_stop;
constexpr uint64_t kStaleUs = 500000; /* opens and closes may block a while */
constexpr uint64_t kTakeGapUs = 100000; /* older motion is not this aim's */

void Discard() {
  for (auto &word : s_pressed)
    word.store(0, std::memory_order_relaxed);
  s_dx.store(0, std::memory_order_relaxed);
  s_dy.store(0, std::memory_order_relaxed);
  s_wheel.store(0, std::memory_order_relaxed);
  s_tilt.store(0, std::memory_order_relaxed);
}

/* ---- the game's own IME, forwarded -----------------------------------------
   Once we own the keyboard, the game's sceImeUpdate import calls GameUpdate:
   it hands the game, on its own thread, every event our pump received (its
   chat and text fields keep working). While we do not own it, the game pumps
   the real IME itself. */

constexpr int kRing = 64; /* power of two */
ImeEvent s_ring[kRing];
std::atomic<uint32_t> s_ring_head, s_ring_tail; /* reader writes head */
std::atomic<bool> s_owned;      /* our keyboard is open */
std::atomic<bool> s_game_pumps; /* the game called its sceImeUpdate */
std::atomic<void *> s_game_arg; /* from the game's own sceImeKeyboardOpen */
constexpr int kMaxImeSlots = 8;
uint64_t *s_ime_slots[kMaxImeSlots];
uint64_t s_ime_originals[kMaxImeSlots];
int s_ime_slot_count;

void Forward(const ImeEvent &event) {
  if (!s_game_pumps.load(std::memory_order_relaxed))
    return;
  const uint32_t head = s_ring_head.load(std::memory_order_relaxed);
  if (head - s_ring_tail.load(std::memory_order_acquire) >= kRing)
    return; /* the game stopped pumping: drop */
  s_ring[head & (kRing - 1)] = event;
  s_ring_head.store(head + 1, std::memory_order_release);
}

int GameUpdate(ImeHandler handler) {
  s_game_pumps.store(true, std::memory_order_relaxed);
  if (!s_owned.load(std::memory_order_acquire))
    return s_api.ime_update(handler);
  void *const arg = s_game_arg.load(std::memory_order_relaxed);
  uint32_t tail = s_ring_tail.load(std::memory_order_relaxed);
  const uint32_t head = s_ring_head.load(std::memory_order_acquire);
  for (; tail != head; ++tail) {
    const ImeEvent event = s_ring[tail & (kRing - 1)];
    s_ring_tail.store(tail + 1, std::memory_order_release);
    if (handler)
      handler(arg, &event);
  }
  return 0;
}

/* The game opening its keyboard while we own it gets "done": its events
   already come through GameUpdate. */
int GameKeyboardOpen(int user, const ImeKeyboardParam *param) {
  if (param)
    s_game_arg.store(param->arg, std::memory_order_relaxed);
  if (s_owned.load(std::memory_order_acquire))
    return 0;
  return s_api.ime_open(user, param);
}

void PatchSlots(const void *real, const void *ours) {
  uint64_t *found[kMaxImeSlots];
  const int n =
      hooks::FindImportSlots(real, found, kMaxImeSlots - s_ime_slot_count);
  for (int i = 0; i < n && s_ime_slot_count < kMaxImeSlots; ++i) {
    uint64_t expected = reinterpret_cast<uint64_t>(real);
    if (!__atomic_compare_exchange_n(found[i], &expected,
                                     reinterpret_cast<uint64_t>(ours), false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
      continue;
    s_ime_slots[s_ime_slot_count] = found[i];
    s_ime_originals[s_ime_slot_count++] = reinterpret_cast<uint64_t>(real);
  }
}

/* Done before our keyboard opens: GameUpdate passes through until then. */
void HookGameIme() {
  if (!hooks::FindImportSlots) {
    Log("no import-slot helper: the game's own IME is not forwarded");
    return;
  }
  PatchSlots(reinterpret_cast<const void *>(s_api.ime_update),
             reinterpret_cast<const void *>(&GameUpdate));
  PatchSlots(reinterpret_cast<const void *>(s_api.ime_open),
             reinterpret_cast<const void *>(&GameKeyboardOpen));
  Log("game IME import slots forwarded: %d", s_ime_slot_count);
}

/* Back to the real functions, so a newer agent finds them by value. */
void UnhookGameIme() {
  for (int i = 0; i < s_ime_slot_count; ++i) {
    uint64_t ours = __atomic_load_n(s_ime_slots[i], __ATOMIC_RELAXED);
    if (ours == reinterpret_cast<uint64_t>(&GameUpdate) ||
        ours == reinterpret_cast<uint64_t>(&GameKeyboardOpen))
      __atomic_compare_exchange_n(s_ime_slots[i], &ours, s_ime_originals[i],
                                  false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
  }
  s_ime_slot_count = 0;
}

/* ---- reader state (reader thread only) ------------------------------------ */

pthread_t s_thread;
ControlPage *s_control;
int s_user = -1;
int s_mouse = -1, s_mouse_error;
bool s_mouse_connected;
uint8_t s_mouse_buttons;
uint64_t s_mouse_retry_us, s_mouse_seen_us;
int s_ime_user = -1, s_ime_error;
bool s_ime_lost;
bool s_closed_game_keyboard; /* only ever once: never fight the game */
int s_ime_busy_count;
uint64_t s_ime_retry_us;
uint64_t s_keys[4]; /* HID usage -> held */
int s_samples, s_key_events;
constexpr uint64_t kRetryUs = 1000000, kBusyRetryUs = 5000000;
constexpr uint64_t kMouseGoneUs = 200000;
constexpr int kReaderPriority = 767; /* the lowest: never in the game's way */

constexpr uint32_t kStatusMouse = 1u << 2, kStatusKeyboard = 1u << 3;
void SetStatus(uint32_t bit, bool on) {
  if (!s_control)
    return;
  if (on)
    __atomic_fetch_or(&s_control->agent_status, bit, __ATOMIC_RELAXED);
  else
    __atomic_fetch_and(&s_control->agent_status, ~bit, __ATOMIC_RELAXED);
}

void Publish() {
  for (int w = 0; w < 4; ++w)
    s_held[w].store(s_keys[w], std::memory_order_relaxed);
  s_held[4].store(s_mouse_buttons, std::memory_order_relaxed);
}

void SetKey(Input key, bool down) {
  const uint64_t bit = 1ull << (key & 63);
  if (!down) {
    s_keys[key >> 6] &= ~bit;
  } else if (!(s_keys[key >> 6] & bit)) {
    s_keys[key >> 6] |= bit;
    s_pressed[key >> 6].fetch_or(bit, std::memory_order_relaxed);
  }
}

void OnImeEvent(void *, const ImeEvent *e) {
  if (!e)
    return;
  const uint16_t key = e->param.keycode.keycode;
  switch (e->id) {
  case kImeKeyDown:
  case kImeKeyUp:
  case kImeKeyRepeat: {
    /* modifiers ride on every key event: pressed alone they may bring none */
    const uint32_t mods = (e->param.keycode.status >> 8) & 0xff;
    for (int bit = 0; bit < 8; ++bit)
      SetKey(static_cast<Input>(kModifierFirst + bit), mods & (1u << bit));
    if (e->id != kImeKeyRepeat && key >= kKeyFirst && key <= kKeyLast) {
      SetKey(key, e->id == kImeKeyDown);
      s_key_events += e->id == kImeKeyDown;
    }
    break;
  }
  case kImeConnection:
  case kImeDisconnection:
  case kImeAbort:
    memset(s_keys, 0, sizeof(s_keys));
    Log("keyboard %s", e->id == kImeConnection      ? "connected"
                       : e->id == kImeDisconnection ? "disconnected"
                                                    : "taken away (abort)");
    if (e->id == kImeAbort)
      s_ime_lost = true;
    break;
  default:
    break;
  }
  Forward(*e);
}

void CloseMouse() {
  if (s_mouse >= 0) {
    s_api.mouse_close(s_mouse);
    Log("mouse closed");
  }
  s_mouse = -1;
  s_mouse_buttons = 0;
  SetStatus(kStatusMouse, false);
}

void OpenMouse(uint64_t now) {
  s_mouse_retry_us = now + kRetryUs;
  const MouseOpenParam param{kMouseMerged, {}};
  const int handle = s_api.mouse_open(s_user, 0, 0, &param);
  if (handle < 0) {
    if (handle != s_mouse_error)
      Log("mouse open for user 0x%x failed: 0x%x",
          static_cast<unsigned>(s_user), static_cast<unsigned>(handle));
    s_mouse_error = handle;
    return;
  }
  s_mouse = handle;
  s_mouse_error = 0;
  s_mouse_connected = false;
  s_mouse_seen_us = now;
  SetStatus(kStatusMouse, true);
  Log("mouse open for user 0x%x (handle %d)", static_cast<unsigned>(s_user),
      handle);
}

void ReadMouse(uint64_t now) {
  MouseData samples[64];
  const int count = s_api.mouse_read(s_mouse, samples, 64);
  if (count < 0) {
    Log("mouse read failed: 0x%x", static_cast<unsigned>(count));
    CloseMouse();
    s_mouse_retry_us = now + kRetryUs;
    return;
  }
  int32_t dx = 0, dy = 0, wheel = 0, tilt = 0;
  uint8_t buttons = s_mouse_buttons, went_down = 0;
  bool connected = false;
  for (int i = 0; i < count; ++i) {
    const MouseData &sample = samples[i];
    if (!sample.connected)
      continue;
    connected = true;
    if (sample.buttons & kMouseIntercepted) { /* the system's screen is up */
      buttons = 0;
      continue;
    }
    dx += sample.x;
    dy += sample.y;
    wheel += sample.wheel;
    tilt += sample.tilt;
    const uint8_t now_down = static_cast<uint8_t>(sample.buttons);
    went_down |= now_down & ~buttons;
    buttons = now_down;
    ++s_samples;
  }
  if (connected)
    s_mouse_seen_us = now;
  if (count > 0 && connected != s_mouse_connected) {
    Log("mouse %s", connected ? "connected" : "unplugged");
    s_mouse_connected = connected;
  }
  /* shortcut: assumes a held button keeps sending samples; if an idle mouse
     sends none, a long hold is released after 200 ms (check on hardware) */
  if ((count > 0 && !connected) || now - s_mouse_seen_us > kMouseGoneUs)
    buttons = 0;
  s_mouse_buttons = buttons;
  if (went_down)
    s_pressed[4].fetch_or(went_down, std::memory_order_relaxed);
  if (dx)
    s_dx.fetch_add(dx, std::memory_order_relaxed);
  if (dy)
    s_dy.fetch_add(dy, std::memory_order_relaxed);
  if (wheel)
    s_wheel.fetch_add(wheel, std::memory_order_relaxed);
  if (tilt)
    s_tilt.fetch_add(tilt, std::memory_order_relaxed);
}

void Disown() {
  s_owned.store(false, std::memory_order_release);
  s_ime_user = -1;
  SetStatus(kStatusKeyboard, false);
}

/* Closes our IME keyboard, only while it is still ours. */
void CloseKeyboard() {
  if (s_ime_user >= 0 && !s_ime_lost)
    Log("keyboard close -> 0x%x",
        static_cast<unsigned>(s_api.ime_close(s_ime_user)));
  Disown();
  memset(s_keys, 0, sizeof(s_keys));
}

/* Our IME keyboard. The game may hold it already (busy): we close the
   user's keyboard and open it again once; after that a busy keyboard is
   someone's on purpose, so we wait 5 s, try once more and then give up. */
void OpenKeyboard(uint64_t now) {
  s_ime_retry_us = now + kRetryUs;
  ImeKeyboardParam param;
  memset(&param, 0, sizeof(param));
  param.handler = OnImeEvent;
  int result = s_api.ime_open(s_user, &param);
  if (result == kImeBusy && !s_closed_game_keyboard) {
    s_closed_game_keyboard = true;
    const int closed = s_api.ime_close(s_user);
    result = s_api.ime_open(s_user, &param);
    Log("keyboard held by the game: close 0x%x, open again 0x%x",
        static_cast<unsigned>(closed), static_cast<unsigned>(result));
  }
  if (result == kImeBusy) {
    if (++s_ime_busy_count == 1) {
      s_ime_retry_us = now + kBusyRetryUs;
    } else {
      s_ime_retry_us = UINT64_MAX;
      Log("keyboard stays busy: giving up on it");
    }
    return;
  }
  if (result != 0) {
    if (result != s_ime_error)
      Log("keyboard open for user 0x%x failed: 0x%x",
          static_cast<unsigned>(s_user), static_cast<unsigned>(result));
    s_ime_error = result;
    return;
  }
  s_ime_user = s_user;
  s_ime_error = 0;
  s_ime_busy_count = 0;
  s_ime_lost = false;
  s_owned.store(true, std::memory_order_release);
  SetStatus(kStatusKeyboard, true);
  Log("keyboard open for user 0x%x", static_cast<unsigned>(s_user));
}

/* The IME delivers events only from sceImeUpdate, called by the opener. */
void PumpKeyboard(uint64_t now) {
  const int result = s_api.ime_update(OnImeEvent);
  if (result == kImeNotOpen)
    s_ime_lost = true;
  else if (result != 0 && result != s_ime_error) {
    Log("keyboard update: 0x%x", static_cast<unsigned>(result));
    s_ime_error = result;
  }
  if (s_ime_lost) { /* not ours any more: open it again later, never close */
    Log("keyboard lost (0x%x)", static_cast<unsigned>(result));
    Disown();
    memset(s_keys, 0, sizeof(s_keys));
    s_ime_retry_us = now + kRetryUs;
  }
}

/* The mouse and keyboard belong to the user in front of the screen. */
void FollowUser() {
  int user = -1;
  if (!s_api.foreground_user || s_api.foreground_user(&user) != 0 || user < 0 ||
      user == s_user)
    return;
  if (s_user >= 0)
    Log("foreground user 0x%x -> 0x%x", static_cast<unsigned>(s_user),
        static_cast<unsigned>(user));
  if (s_mouse >= 0)
    CloseMouse();
  if (s_ime_user >= 0)
    CloseKeyboard();
  s_user = user;
  s_mouse_retry_us = s_ime_retry_us = 0;
  s_mouse_error = s_ime_error = s_ime_busy_count = 0;
}

/* An agent before us lets go of the mouse and keyboard when it sees us. */
void WaitForOlderAgent() {
  if (!s_control)
    return;
  const uint64_t until = NowUs() + 500000;
  while (__atomic_load_n(&s_control->agent_status, __ATOMIC_ACQUIRE) &
         (kStatusMouse | kStatusKeyboard)) {
    if (NowUs() >= until) {
      Log("the agent before still holds input; going ahead");
      SetStatus(kStatusMouse | kStatusKeyboard, false);
      break;
    }
    s_alive_us.store(NowUs(), std::memory_order_release);
    usleep(10000);
  }
}

void *Reader(void *) {
  if (s_set_priority)
    s_set_priority(pthread_self(), kReaderPriority);
  WaitForOlderAgent();
  if (KeyboardUsable())
    HookGameIme();
  uint64_t user_at = 0, stats_at = NowUs();
  while (!s_stop.load(std::memory_order_acquire)) {
    const uint64_t now = NowUs();
    if (now >= user_at) {
      user_at = now + kRetryUs;
      FollowUser();
    }
    if (s_user >= 0 && s_mouse < 0 && MouseUsable() && now >= s_mouse_retry_us)
      OpenMouse(now);
    if (s_user >= 0 && s_ime_user < 0 && KeyboardUsable() &&
        now >= s_ime_retry_us)
      OpenKeyboard(now);
    if (s_mouse >= 0)
      ReadMouse(now);
    if (s_ime_user >= 0)
      PumpKeyboard(now);
    Publish();
    s_alive_us.store(NowUs(), std::memory_order_release);
    if (s_control)
      __atomic_fetch_add(&s_control->heartbeat, 1u, __ATOMIC_RELAXED);
    if (now - stats_at >= 10000000) {
      Log("10 s: mouse samples %d, key presses %d (user 0x%x, mouse %d, "
          "keyboard %s)",
          s_samples, s_key_events, static_cast<unsigned>(s_user), s_mouse,
          s_ime_user >= 0 ? "open" : "closed");
      s_samples = s_key_events = 0;
      stats_at = now;
    }
    usleep(s_mouse >= 0 || s_ime_user >= 0 ? 4000 : 50000);
  }
  UnhookGameIme();
  if (s_mouse >= 0)
    CloseMouse();
  if (s_ime_user >= 0)
    CloseKeyboard();
  Publish();
  return nullptr;
}

} // namespace

bool InputInit() {
  uint32_t kernel = 0;
  if (kernel_dynlib_handle(getpid(), "libkernel.sprx", &kernel) != 0)
    kernel_dynlib_handle(getpid(), "libkernel_sys.sprx", &kernel);
  s_out = reinterpret_cast<decltype(s_out)>(Sym(kernel, "sceKernelDebugOutText"));
  s_sandbox_word = reinterpret_cast<decltype(s_sandbox_word)>(
      Sym(kernel, "sceKernelGetFsSandboxRandomWord"));
  s_load_start = reinterpret_cast<decltype(s_load_start)>(
      Sym(kernel, "sceKernelLoadStartModule"));
  s_set_priority = reinterpret_cast<decltype(s_set_priority)>(
      Sym(kernel, "scePthreadSetprio"));

  const uint32_t users = Module("libSceUserService.sprx");
  s_api.foreground_user = reinterpret_cast<decltype(s_api.foreground_user)>(
      Sym(users, "sceUserServiceGetForegroundUser"));
  const uint32_t mouse = Module("libSceMouse.sprx");
  s_api.mouse_init =
      reinterpret_cast<decltype(s_api.mouse_init)>(Sym(mouse, "sceMouseInit"));
  s_api.mouse_open =
      reinterpret_cast<decltype(s_api.mouse_open)>(Sym(mouse, "sceMouseOpen"));
  s_api.mouse_read =
      reinterpret_cast<decltype(s_api.mouse_read)>(Sym(mouse, "sceMouseRead"));
  s_api.mouse_close =
      reinterpret_cast<decltype(s_api.mouse_close)>(Sym(mouse, "sceMouseClose"));
  const uint32_t ime = Module("libSceIme.sprx");
  s_api.ime_open = reinterpret_cast<decltype(s_api.ime_open)>(
      Sym(ime, "sceImeKeyboardOpen"));
  s_api.ime_update =
      reinterpret_cast<decltype(s_api.ime_update)>(Sym(ime, "sceImeUpdate"));
  s_api.ime_close = reinterpret_cast<decltype(s_api.ime_close)>(
      Sym(ime, "sceImeKeyboardClose"));

  if (MouseUsable() && s_api.mouse_init)
    Log("mouse init 0x%x", static_cast<unsigned>(s_api.mouse_init()));
  if (!s_api.foreground_user || (!MouseUsable() && !KeyboardUsable())) {
    Log("no input: user %d, mouse %d, keyboard %d",
        s_api.foreground_user != nullptr, MouseUsable(), KeyboardUsable());
    return false;
  }
  return true;
}

bool InputStart(ControlPage *control) {
  if (s_running.load())
    return true;
  s_control = control;
  s_stop.store(false);
  s_alive_us.store(NowUs());
  if (pthread_create(&s_thread, nullptr, Reader, nullptr) != 0) {
    Log("reader thread not started");
    return false;
  }
  s_running.store(true, std::memory_order_release);
  return true;
}

bool TakeFrame(InputFrame *out) {
  memset(out, 0, sizeof(*out));
  const uint64_t now = NowUs();
  out->now_us = now;
  if (!s_running.load(std::memory_order_acquire) ||
      now - s_alive_us.load(std::memory_order_acquire) > kStaleUs)
    return false;
  /* the game did not read its pad for a while (menu, loading): what piled
     up meanwhile is not for this frame */
  if (now - s_last_take_us.exchange(now, std::memory_order_relaxed) >
      kTakeGapUs)
    Discard();
  uint64_t words[kWords];
  for (int w = 0; w < kWords; ++w)
    words[w] = s_held[w].load(std::memory_order_relaxed) |
               s_pressed[w].exchange(0, std::memory_order_relaxed);
  memcpy(out->down, words, sizeof(out->down)); /* bit i of byte i/8: little endian */
  out->mouse_dx = s_dx.exchange(0, std::memory_order_relaxed);
  out->mouse_dy = s_dy.exchange(0, std::memory_order_relaxed);
  const int32_t wheel = s_wheel.exchange(0, std::memory_order_relaxed);
  const int32_t tilt = s_tilt.exchange(0, std::memory_order_relaxed);
  out->wheel_v = static_cast<int16_t>(wheel > 32767 ? 32767 : wheel < -32767 ? -32767 : wheel);
  out->wheel_h = static_cast<int16_t>(tilt > 32767 ? 32767 : tilt < -32767 ? -32767 : tilt);
  return true;
}

void InputDiscard() { Discard(); }

void InputStop() {
  if (!s_running.load())
    return;
  s_stop.store(true, std::memory_order_release);
  pthread_join(s_thread, nullptr);
  s_running.store(false, std::memory_order_release);
  for (auto &word : s_held)
    word.store(0);
  Discard();
  Log("input stopped");
}

} // namespace onion::kbm::agent
