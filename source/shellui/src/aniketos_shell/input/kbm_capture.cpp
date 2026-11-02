#include "aniketos_shell/input/kbm_capture.hpp"

#include "aniketos_shell/input/mouse_control.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/mono_symbols.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>

#include <cstring>

namespace KbmCapture {
namespace {

namespace kbm = onion::kbm;

using AniketosUI::MonotonicMs;

/* ScePsmUtil's ImeKbController internal calls:
 * KbdOpenNative(ImeKeyboardParamInternal&, ulong& accessor): one libSceIme
 *   keyboard client; -1 when this user has one, 0x80bc0001 when any exists.
 * KbdGetEventNative(accessor, ImeEventNative&): 1 an event, 0 none (an empty
 *   queue is pumped first), -1 unknown accessor.
 * KbdCloseNative(accessor): -1 for an unknown accessor (looked up, never
 *   dereferenced). */
constexpr const char *kImage = "Sce.PlayStation.Ime";
constexpr const char *kSpace = "Sce.PlayStation.Core.Ime";
/* ImeEventId, as ScePsmUtil translates libSceIme's 0x101.. */
constexpr uint32_t kKeycodeDown = 0x1f, kKeycodeUp = 0x20,
                   kDisconnection = 0x23, kAbort = 0x24;
constexpr uint32_t kModifierShift = 8; /* ImeKeycodeState: LCtrl 0x100 .. RGui
                                          0x8000, the order of HID 0xe0.. */
constexpr Input kEscape = 0x29, kModifierFirst = 0xe0, kModifierLast = 0xe7;

struct Api {
  int (*open)(void *param, uint64_t *accessor) = nullptr;
  int (*get_event)(uint64_t accessor, void *event) = nullptr;
  int (*close)(uint64_t accessor) = nullptr;
  /* field offsets in the value types, from their metadata */
  int user = -1, option = -1, identifier = -1;
  int id = -1, keycode = -1, status = -1;
};
Api s_api;
bool s_api_tried = false;
bool s_api_ready = false;

MonoClass *class_named(MonoImage *image, const char *name) {
  MonoClass *klass = mono_class_from_name(image, kSpace, name);
  if (!klass)
    LOG_WARN("[KBM-CAPTURE] %s not found", name);
  return klass;
}

/* Offset of `name` from the value type's first field `first`. */
int offset_of(MonoClass *klass, const char *first, const char *name) {
  MonoClassField *base = mono_class_get_field_from_name(klass, first);
  MonoClassField *field = mono_class_get_field_from_name(klass, name);
  if (!base || !field) {
    LOG_WARN("[KBM-CAPTURE] field %s not found", name);
    return -1;
  }
  return static_cast<int>(mono_field_get_offset(field)) -
         static_cast<int>(mono_field_get_offset(base));
}

void *internal_call(MonoClass *klass, const char *name, int parameters) {
  static void *(*lookup)(MonoMethod *) = nullptr;
  if (!lookup)
    lookup = reinterpret_cast<decltype(lookup)>(
        libmono_symbol("mono_lookup_internal_call"));
  MonoMethod *method =
      klass ? mono_class_get_method_from_name(klass, name, parameters)
            : nullptr;
  void *native = method && lookup ? lookup(method) : nullptr;
  if (!native)
    LOG_WARN("[KBM-CAPTURE] %s not found", name);
  return native;
}

void load_api() {
  MonoImage *image = AniketosUI::Reflect::LoadedImage(kImage);
  if (!image)
    return; /* PUI loads it with the first text field: tried again later */
  s_api_tried = true;
  MonoClass *controller = class_named(image, "ImeKbController");
  MonoClass *param = class_named(image, "ImeKeyboardParamInternal");
  MonoClass *event = class_named(image, "ImeEventNative");
  if (!controller || !param || !event)
    return;
  s_api.open = reinterpret_cast<decltype(s_api.open)>(
      internal_call(controller, "KbdOpenNative", 2));
  s_api.get_event = reinterpret_cast<decltype(s_api.get_event)>(
      internal_call(controller, "KbdGetEventNative", 2));
  s_api.close = reinterpret_cast<decltype(s_api.close)>(
      internal_call(controller, "KbdCloseNative", 1));
  s_api.user = offset_of(param, "UserId", "UserId");
  s_api.option = offset_of(param, "UserId", "Option");
  s_api.identifier = offset_of(param, "UserId", "identifier");
  s_api.id = offset_of(event, "Id", "Id");
  s_api.keycode = offset_of(event, "Id", "KeycodeKeycode");
  s_api.status = offset_of(event, "Id", "KeycodeStatus");
  const auto in = [](int offset, int size) { return offset >= 0 && offset <= size; };
  s_api_ready = s_api.open && s_api.get_event && s_api.close &&
                in(s_api.user, 56) && in(s_api.option, 56) &&
                in(s_api.identifier, 56) && in(s_api.id, 248) &&
                in(s_api.keycode, 248) && in(s_api.status, 248);
  LOG_INFO("[KBM-CAPTURE] keyboard calls ready=%d (param %d/%d/%d, event "
           "%d/%d/%d)",
           s_api_ready ? 1 : 0, s_api.user, s_api.option, s_api.identifier,
           s_api.id, s_api.keycode, s_api.status);
}

template <typename T> void put(uint8_t *buffer, int offset, T value) {
  memcpy(buffer + offset, &value, sizeof(T));
}
template <typename T> T get(const uint8_t *buffer, int offset) {
  T value{};
  memcpy(&value, buffer + offset, sizeof(T));
  return value;
}

bool s_session = false;
uint64_t s_accessor = 0; /* 0: the keyboard is not ours */
uint64_t s_deadline_ms = 0;
uint64_t s_polled_ms = 0;
int s_logged_events = 0;
uint32_t s_modifiers = 0; /* ImeKeycodeState modifier bits last seen */
uint8_t s_down[(kbm::kInputCount + 7) / 8] = {};
Result s_result{};
Input s_candidate = kbm::kInputNone; /* a modifier or Esc, answers on release */
uint64_t s_candidate_ms = 0;

constexpr uint64_t kUnpolledMs = 3000;
constexpr uint64_t kEscapeCancelMs = 1000;
constexpr int kMaxLoggedEvents = 40;

bool held(Input input) { return s_down[input >> 3] & (1u << (input & 7)); }
void set_held(Input input, bool on) {
  if (on)
    s_down[input >> 3] |= static_cast<uint8_t>(1u << (input & 7));
  else
    s_down[input >> 3] &= static_cast<uint8_t>(~(1u << (input & 7)));
}
bool is_modifier(Input input) {
  return input >= kModifierFirst && input <= kModifierLast;
}

void close_keyboard() {
  if (!s_accessor)
    return;
  const int result = s_api.close(s_accessor);
  LOG_INFO("[KBM-CAPTURE] keyboard closed (0x%x)", static_cast<unsigned>(result));
  s_accessor = 0;
}

bool open_keyboard() {
  if (s_accessor)
    return true;
  if (!s_api_tried)
    load_api();
  if (!s_api_ready)
    return false;
  int user = -1;
  if (sceUserServiceGetForegroundUser(&user) != 0 || user < 0)
    return false;
  alignas(8) uint8_t param[64] = {};
  put<int32_t>(param, s_api.user, user);
  put<uint32_t>(param, s_api.option, 0); /* no repeat, no OSK keys */
  /* the key libSceIme files this client's events under: any unique value */
  put<uint64_t>(param, s_api.identifier, reinterpret_cast<uint64_t>(&s_accessor));
  uint64_t accessor = 0;
  const int result = s_api.open(param, &accessor);
  if (result != 0 || !accessor) {
    /* a web view has it (or another user): mouse only, theirs stays */
    LOG_INFO("[KBM-CAPTURE] keyboard busy for user 0x%x (0x%x)",
             static_cast<unsigned>(user), static_cast<unsigned>(result));
    return false;
  }
  /* shortcut: a HEN reload while a session runs leaves this client open
     until ShellUI restarts (capture is mouse only meanwhile); keep the
     accessor somewhere that survives the reload if that bites. */
  s_accessor = accessor;
  LOG_INFO("[KBM-CAPTURE] keyboard opened for user 0x%x",
           static_cast<unsigned>(user));
  return true;
}

void held_modifiers(Input except, Input out[kbm::kMaxModifiers]) {
  int count = 0;
  for (Input key = kModifierFirst; key <= kModifierLast; ++key)
    if (key != except && held(key) && count < kbm::kMaxModifiers)
      out[count++] = key;
  for (; count < kbm::kMaxModifiers; ++count)
    out[count] = kbm::kInputNone;
}

void stop_listening();

void finish(State state, Input input) {
  s_result = Result{state, input, {}};
  if (state == State::Captured)
    held_modifiers(input, s_result.modifiers);
  LOG_INFO("[KBM-CAPTURE] capture %s: 0x%x + 0x%x 0x%x",
           state == State::Captured    ? "done"
           : state == State::Cancelled ? "cancelled"
                                       : "timed out",
           input, s_result.modifiers[0], s_result.modifiers[1]);
  stop_listening(); /* the answer is in: keyboard and mouse go back now */
}

bool waiting() { return s_session && s_result.state == State::None; }

void on_down(Input input, uint64_t now) {
  if (input == kbm::kInputNone || input >= kbm::kInputCount || held(input))
    return;
  set_held(input, true);
  if (!waiting())
    return;
  if (is_modifier(input) || input == kEscape) {
    if (s_candidate == kbm::kInputNone) {
      s_candidate = input;
      s_candidate_ms = now;
    }
    return;
  }
  finish(State::Captured, input);
}

void on_up(Input input) {
  if (input == kbm::kInputNone || input >= kbm::kInputCount || !held(input))
    return; /* never seen down: held before the session began */
  set_held(input, false);
  if (input != s_candidate)
    return;
  s_candidate = kbm::kInputNone;
  if (waiting())
    finish(State::Captured, input);
}

/* A wheel notch is a press and its release at once. */
void on_notch(Input input, uint64_t now) {
  on_down(input, now);
  set_held(input, false);
}

/* The modifier keys' state rides on every key event: a modifier pressed
   alone may bring no event of its own. */
void follow_modifiers(uint32_t status, uint64_t now) {
  const uint32_t mods = (status >> kModifierShift) & 0xff;
  const uint32_t changed = mods ^ s_modifiers;
  s_modifiers = mods;
  for (int bit = 0; bit < 8; ++bit)
    if (changed & (1u << bit)) {
      const Input key = static_cast<Input>(kModifierFirst + bit);
      if (mods & (1u << bit))
        on_down(key, now);
      else
        on_up(key);
    }
}

/* The keyboard is gone: what it held was not let go, so it answers nothing. */
void release_all_held() {
  s_candidate = kbm::kInputNone;
  for (int input = 0; input < kbm::kInputCount; ++input)
    if (held(static_cast<Input>(input)))
      on_up(static_cast<Input>(input));
  s_modifiers = 0;
}

void read_keyboard(uint64_t now) {
  for (int i = 0; s_accessor && i < 64; ++i) {
    alignas(8) uint8_t event[256] = {};
    const int got = s_api.get_event(s_accessor, event);
    if (got <= 0) {
      if (got < 0) { /* our client is gone */
        LOG_WARN("[KBM-CAPTURE] keyboard read failed (%d)", got);
        s_accessor = 0;
        release_all_held();
      }
      return;
    }
    const uint32_t id = get<uint32_t>(event, s_api.id);
    const uint32_t keycode = get<uint32_t>(event, s_api.keycode) & 0xffff;
    const uint32_t status = get<uint32_t>(event, s_api.status);
    if (s_logged_events < kMaxLoggedEvents) {
      ++s_logged_events;
      LOG_INFO("[KBM-CAPTURE] event 0x%x key 0x%x status 0x%x", id, keycode,
               status);
    }
    if (id == kKeycodeDown || id == kKeycodeUp) {
      follow_modifiers(status, now);
      const Input key = keycode < 0x100 ? static_cast<Input>(keycode)
                                        : kbm::kInputNone;
      if (id == kKeycodeDown)
        on_down(key, now);
      else
        on_up(key);
    } else if (id == kDisconnection) {
      release_all_held();
    } else if (id == kAbort) { /* the system took the keyboard back */
      LOG_INFO("[KBM-CAPTURE] keyboard aborted");
      close_keyboard();
      release_all_held();
      if (waiting())
        finish(State::Cancelled, kbm::kInputNone);
    }
  }
}

/* MouseControl's frame, while a session runs: nothing on screen is clicked. */
void mouse_sink(uint32_t buttons, uint32_t went_down, int wheel, int tilt) {
  const uint64_t now = MonotonicMs();
  for (int bit = 0; bit < 8; ++bit) {
    const Input button = static_cast<Input>(kbm::kMouseButtonFirst + bit);
    if (went_down & (1u << bit))
      on_down(button, now);
    if (!(buttons & (1u << bit)))
      on_up(button);
  }
  if (wheel)
    on_notch(wheel > 0 ? kbm::kWheelUp : kbm::kWheelDown, now);
  if (tilt)
    on_notch(tilt > 0 ? kbm::kWheelRight : kbm::kWheelLeft, now);
}

void stop_listening() {
  close_keyboard();
  MouseControl::SetCaptureSink(nullptr);
  memset(s_down, 0, sizeof(s_down));
  s_modifiers = 0;
  s_candidate = kbm::kInputNone;
}

void end_session() {
  stop_listening();
  s_session = false;
}

} // namespace

bool BeginCapture(uint32_t timeout_ms) {
  stop_listening();
  s_session = true;
  s_result = Result{};
  s_polled_ms = MonotonicMs();
  s_deadline_ms = s_polled_ms + timeout_ms;
  s_logged_events = 0;
  MouseControl::SetCaptureSink(&mouse_sink);
  const bool keyboard = open_keyboard();
  LOG_INFO("[KBM-CAPTURE] capture started (%u ms, keyboard %d)",
           static_cast<unsigned>(timeout_ms), keyboard ? 1 : 0);
  return keyboard;
}

Result PollCapture() {
  s_polled_ms = MonotonicMs();
  return s_session ? s_result : Result{State::Cancelled, 0, {}};
}

void EndCapture() {
  if (s_session)
    end_session();
}

void Tick() {
  if (!s_session)
    return;
  const uint64_t now = MonotonicMs();
  if (now - s_polled_ms > kUnpolledMs) {
    LOG_WARN("[KBM-CAPTURE] nobody polls: session ended");
    end_session();
    return;
  }
  if (!waiting())
    return;
  read_keyboard(now);
  if (!waiting())
    return;
  if (s_candidate == kEscape && now - s_candidate_ms >= kEscapeCancelMs)
    finish(State::Cancelled, kbm::kInputNone);
  else if (now >= s_deadline_ms)
    finish(State::TimedOut, kbm::kInputNone);
}

} // namespace KbmCapture
