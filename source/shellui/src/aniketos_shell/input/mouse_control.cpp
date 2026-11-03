#include "aniketos_shell/input/mouse_control.hpp"

#include "aniketos_shell/core/host_scene.hpp"
#include "aniketos_shell/core/menu_host.hpp"
#include "aniketos_shell/games/cheats_menu.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_ui/mono_property_cache.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "aniketos_ui/pui_primitives.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <ps5/kernel.h>
#include <unistd.h>

namespace MouseControl {
namespace {

namespace Pui = AniketosUI::Pui;
namespace Reflect = AniketosUI::Reflect;

struct MouseData { /* SceMouseData */
  uint64_t timestamp;
  uint8_t connected;
  uint8_t padding[3];
  uint32_t buttons;
  int32_t x, y, wheel, tilt;
  uint8_t reserved[8];
};
static_assert(sizeof(MouseData) == 40, "SceMouseData");

struct OpenParam { /* SceMouseOpenParam */
  uint8_t behavior;
  uint8_t reserved[7];
};
constexpr uint8_t kMerged = 0x01; /* every mouse plugged in, as one */

constexpr uint32_t kLeft = 0x01, kRight = 0x02, kMiddle = 0x04;
constexpr uint32_t kSideBack = 0x08, kSideForward = 0x10;
constexpr uint32_t kIntercepted = 0x80000000u; /* another process has it */

struct Api {
  int (*init)() = nullptr;
  int (*open)(int, int, int, const OpenParam *) = nullptr;
  int (*read)(int, MouseData *, int) = nullptr;
  int (*close)(int) = nullptr;
};
Api s_api;
bool s_api_tried = false;
bool s_api_ready = false;
int s_handle = -1;
int s_handle_user = -1;
int s_reopen_frames = 0;
int s_last_error = 0;

void load_api() {
  s_api_tried = true;
  uint32_t module = 0;
  if (kernel_dynlib_handle(getpid(), "libSceMouse.sprx", &module) != 0) {
    LOG_WARN("[MOUSE] libSceMouse is not loaded in ShellUI");
    return;
  }
  const auto find = [module](const char *name) {
    const intptr_t address = kernel_dynlib_dlsym(-1, module, name);
    if (address <= 0)
      LOG_WARN("[MOUSE] %s not found", name);
    return address > 0 ? reinterpret_cast<void *>(address) : nullptr;
  };
  s_api.init = reinterpret_cast<decltype(s_api.init)>(find("sceMouseInit"));
  s_api.open = reinterpret_cast<decltype(s_api.open)>(find("sceMouseOpen"));
  s_api.read = reinterpret_cast<decltype(s_api.read)>(find("sceMouseRead"));
  s_api.close = reinterpret_cast<decltype(s_api.close)>(find("sceMouseClose"));
  if (!s_api.init || !s_api.open || !s_api.read || !s_api.close)
    return;
  LOG_INFO("[MOUSE] library ready (init 0x%x)",
           static_cast<unsigned>(s_api.init()));
  s_api_ready = true;
}

void close_mouse() {
  if (s_handle >= 0 && s_api.close)
    s_api.close(s_handle);
  s_handle = -1;
}

/* The mouse of the user in front of the screen; tried again now and then
   while there is none (no user signed in yet, or the user changed). */
void open_mouse() {
  int user = -1;
  if (sceUserServiceGetForegroundUser(&user) != 0 || user < 0)
    return;
  if (s_handle >= 0 && user == s_handle_user)
    return;
  close_mouse();
  const OpenParam param{kMerged, {}};
  const int handle = s_api.open(user, 0, 0, &param);
  if (handle < 0) {
    if (handle != s_last_error)
      LOG_WARN("[MOUSE] open for user 0x%x failed: 0x%x",
               static_cast<unsigned>(user), static_cast<unsigned>(handle));
    s_last_error = handle;
    return;
  }
  s_handle = handle;
  s_handle_user = user;
  s_last_error = 0;
  LOG_INFO("[MOUSE] opened for user 0x%x (handle %d)",
           static_cast<unsigned>(user), handle);
}

MonoObject *call(MonoObject *object, const char *name, int parameters,
                 void **arguments) {
  MonoObject *result = nullptr;
  return Reflect::Call(object, name, parameters, arguments, &result) ? result
                                                                     : nullptr;
}

bool bool_property(MonoObject *object, const char *getter) {
  MonoObject *value = call(object, getter, 0, nullptr);
  return value && *static_cast<bool *>(mono_object_unbox(value));
}

const char *type_name(MonoObject *object) {
  MonoClass *klass = object ? mono_object_get_class(object) : nullptr;
  return klass ? mono_class_get_name(klass) : "?";
}

/* The system web browser's own pointers: an arrow, and a hand over something
   that can be chosen. 48x56 with their outline and shadow; the hot spot is
   the arrow's tip and the finger's tip. */
constexpr const char *kArrowIcon = "cxml://ui3/image_webview_pointer_cursor";
constexpr const char *kHandIcon = "cxml://ui3/image_webview_finger_cursor";
constexpr float kPointerWidth = 48.0f, kPointerHeight = 56.0f;
constexpr float kArrowHotX = 16.0f, kHandHotX = 17.0f, kHotY = 3.0f;
constexpr uint64_t kHideAfterMs = 5000;

Pui::Runtime s_pui;
bool s_pui_tried = false;
uint32_t s_root = 0; /* weak: the layer the pointer is in */
uint32_t s_arrow = 0, s_hand = 0; /* strong */
float s_width = 1920.0f, s_height = 1080.0f;
int s_raise_frames = 0;
bool s_drawn_hand = false;
bool s_drawn_visible = false;
float s_drawn_x = -1.0f, s_drawn_y = -1.0f;

bool already_held(MonoObject *, void *) { return true; } /* by s_arrow/s_hand */

void drop_pointer() {
  Reflect::Release(s_arrow);
  Reflect::Release(s_hand);
  Reflect::Release(s_root);
  s_drawn_visible = false;
}

/* The overlay layer the Shell Menu draws in; resolved on our own when the
   menu has let go of it (a game running behind Home), at most twice a
   second while it cannot be found. */
MonoObject *overlay_root() {
  if (MonoObject *root = AniketosShell::HostRoot())
    return root;
  static AniketosShell::HostScene context;
  static bool context_ready = false;
  static int retry_frames = 0;
  if (!context_ready) {
    if (retry_frames-- > 0)
      return nullptr;
    retry_frames = 30;
    context_ready =
        context.Initialize(getDLLimage("Sce.Vsh.ShellUI.AppSystem.dll"),
                           getDLLimage("Sce.PlayStation.PUI.dll"));
    if (!context_ready)
      return nullptr;
  }
  if (MonoObject *known = Reflect::Target(s_root))
    return known; /* still alive: no need to look it up again */
  if (retry_frames-- > 0)
    return nullptr;
  retry_frames = 30;
  AniketosShell::HostSnapshot host;
  return context.Resolve(host) ? host.root : nullptr;
}

/* The pointer's images in the overlay layer, made when first needed and
   again if that layer is replaced (ShellUI rebuilt it). */
bool ensure_pointer() {
  if (!s_pui_tried) {
    s_pui_tried = true;
    if (!Pui::Resolve(s_pui, getDLLimage("Sce.PlayStation.PUI.dll"),
                      already_held, nullptr) ||
        !s_pui.image_box)
      LOG_WARN("[MOUSE] PUI image class not found");
  }
  if (!s_pui.image_box)
    return false;
  MonoObject *root = overlay_root();
  if (!root)
    return false;
  if (Reflect::Target(s_root) == root && Reflect::Target(s_arrow) &&
      Reflect::Target(s_hand))
    return true;
  drop_pointer();
  MonoObject *arrow = Pui::CreateSystemImage(s_pui, root, "id_aniketos_mouse_arrow",
                                             kArrowIcon);
  MonoObject *hand =
      Pui::CreateSystemImage(s_pui, root, "id_aniketos_mouse_hand", kHandIcon);
  if (!arrow || !hand) {
    /* tried again next frame: leave no image behind each time */
    Pui::Remove(s_pui, arrow);
    Pui::Remove(s_pui, hand);
    return false;
  }
  Reflect::Hold(s_arrow, arrow, true);
  Reflect::Hold(s_hand, hand, true);
  Reflect::Hold(s_root, root);
  bool no = false;
  void *arguments[1] = {&no};
  call(arrow, "set_Focusable", 1, arguments);
  call(hand, "set_Focusable", 1, arguments);
  Reflect::FloatProperty(root, "get_Width", &s_width);
  Reflect::FloatProperty(root, "get_Height", &s_height);
  s_width = std::max(s_width, 640.0f);
  s_height = std::max(s_height, 360.0f);
  s_drawn_x = s_drawn_y = -1.0f;
  LOG_INFO("[MOUSE] pointer placed (%.0fx%.0f)", s_width, s_height);
  return true;
}

/* Kept above whatever else the layer shows (the Shell Menu, notices). */
void raise_pointer() {
  MonoObject *root = Reflect::Target(s_root);
  if (!root)
    return;
  for (const uint32_t handle : {s_arrow, s_hand}) {
    MonoObject *widget = Reflect::Target(handle);
    if (!widget)
      continue;
    call(widget, "RemoveFromParent", 0, nullptr);
    void *child[1] = {widget};
    call(root, "AppendChild", 1, child);
  }
}

/* Writes only what changed: two setters while it moves. */
void draw_pointer(float x, float y, bool visible, bool hand) {
  MonoObject *arrow = Reflect::Target(s_arrow), *finger = Reflect::Target(s_hand);
  if (!arrow || !finger)
    return;
  if (visible == s_drawn_visible && hand == s_drawn_hand && x == s_drawn_x &&
      y == s_drawn_y)
    return;
  if (visible != s_drawn_visible || hand != s_drawn_hand) {
    Pui::ImageGeometry(s_pui, arrow, x - kArrowHotX, y - kHotY, kPointerWidth,
                       kPointerHeight, visible && !hand);
    Pui::ImageGeometry(s_pui, finger, x - kHandHotX, y - kHotY, kPointerWidth,
                       kPointerHeight, visible && hand);
  } else {
    MonoObject *shown = hand ? finger : arrow;
    const float hot_x = hand ? kHandHotX : kArrowHotX;
    AniketosUI::MonoPropertyCache::Set(s_pui.image_box, shown, "X", x - hot_x);
    AniketosUI::MonoPropertyCache::Set(s_pui.image_box, shown, "Y", y - kHotY);
  }
  s_drawn_visible = visible;
  s_drawn_hand = hand;
  s_drawn_x = x;
  s_drawn_y = y;
}

struct Vector2 {
  float x, y;
};

/* The selectable widget under (x, y) in the screen that has the focus: the
   widget PUI's touch input would hit there, or the nearest selectable one it
   is part of. */
MonoObject *selectable_at(MonoObject *scene, float x, float y) {
  Vector2 point{x, y};
  void *arguments[1] = {&point};
  MonoObject *widget = call(scene, "FindHitWidget", 1, arguments);
  for (int depth = 0; widget && depth < 40; ++depth) {
    if (bool_property(widget, "get_Focusable") &&
        bool_property(widget, "get_Enabled"))
      return widget;
    widget = call(widget, "get_Parent", 0, nullptr);
  }
  return nullptr;
}

float s_x = 960.0f, s_y = 540.0f;
uint64_t s_last_activity_ms = 0;
uint64_t s_last_move_ms = 0;
uint64_t s_last_test_ms = 0;
float s_tested_x = -1.0f, s_tested_y = -1.0f;
uint32_t s_buttons = 0;
bool s_over_selectable = false;
uint32_t s_hovered = 0; /* weak: the widget last found under the pointer */

/* A click waits for the focus to land on what was clicked, then presses the
   confirm button and holds it while the mouse button is held. */
uint32_t s_click_target = 0; /* weak */
int s_click_frames = 0;
bool s_click_released = false; /* the mouse button came up meanwhile */
bool s_confirm_held = false;
int s_confirm_release_frames = 0;

/* Presses for GetData, as controller buttons. */
std::atomic<uint32_t> s_pad_held{0};
std::atomic<uint32_t> s_pad_down{0};
std::atomic<uint32_t> s_pad_up{0};
std::atomic<int> s_wheel{0}; /* notches still to send: + up, - down */
std::atomic<int> s_tilt{0};  /* + right, - left */
std::atomic<bool> s_active{false}; /* the pointer is in use: GetData listens */
/* Which face button means confirm on this console (Cross, or Circle where
   the system swaps them): learnt from the controller's own Enter bit. */
std::atomic<bool> s_circle_is_enter{false};
CaptureSink s_sink = nullptr;

uint32_t confirm_buttons() {
  return Enter | (s_circle_is_enter.load() ? Circle : Cross);
}
uint32_t back_buttons() {
  return Back | (s_circle_is_enter.load() ? Cross : Circle);
}

void press(uint32_t pad) {
  s_pad_held.fetch_or(pad);
  s_pad_down.fetch_or(pad);
}

void release(uint32_t pad) {
  const uint32_t held = s_pad_held.fetch_and(~pad) & pad;
  if (held)
    s_pad_up.fetch_or(held);
}

void release_all() {
  const uint32_t held = s_pad_held.exchange(0);
  if (held)
    s_pad_up.fetch_or(held);
  s_confirm_held = false;
  s_confirm_release_frames = 0;
  Reflect::Release(s_click_target);
}

/* What is under the pointer now; selects it when `select` is set. */
MonoObject *test_under_pointer(bool select) {
  s_tested_x = s_x;
  s_tested_y = s_y;
  s_last_test_ms = AniketosUI::MonotonicMs();
  MonoObject *scene = SystemDialogs::FocusedScene();
  MonoObject *widget = scene ? selectable_at(scene, s_x, s_y) : nullptr;
  s_over_selectable = widget != nullptr;
  if (widget != Reflect::Target(s_hovered)) {
    Reflect::Hold(s_hovered, widget);
    if (widget)
      LOG_INFO("[MOUSE] over %s in %s at %.0f,%.0f", type_name(widget),
               type_name(scene), s_x, s_y);
  }
  if (select && widget && !bool_property(widget, "get_Focused")) {
    bool yes = true;
    void *arguments[1] = {&yes};
    call(widget, "SetFocus", 1, arguments);
  }
  return widget;
}

/* Hover: once the pointer rests (or every so often while it glides), the
   widget under it takes the focus, as on a computer. Only after the pointer
   itself moved: content scrolling under a still pointer selects nothing. */
void follow_pointer(uint64_t now) {
  if (s_buttons & (kLeft | kSideForward) || s_click_target)
    return;
  if (std::fabs(s_x - s_tested_x) + std::fabs(s_y - s_tested_y) < 2.0f)
    return;
  const bool resting = now - s_last_move_ms >= 60;
  if (!resting && now - s_last_test_ms < 150)
    return;
  test_under_pointer(true);
}

void start_click() {
  MonoObject *widget = test_under_pointer(true);
  if (!widget)
    return; /* nothing to choose there: a click on empty space does nothing */
  Reflect::Hold(s_click_target, widget);
  s_click_frames = 0;
  s_click_released = false;
}

/* The click's confirm press, once the clicked widget has the focus; given up
   after a few frames if the screen put the focus elsewhere. */
void advance_click() {
  if (s_confirm_release_frames > 0 && --s_confirm_release_frames == 0) {
    release(confirm_buttons());
    s_confirm_held = false;
  }
  if (!s_click_target)
    return;
  MonoObject *widget = Reflect::Target(s_click_target);
  const bool focused = widget && bool_property(widget, "get_Focused");
  if (focused) {
    press(confirm_buttons());
    s_confirm_held = true;
    /* a quick click came up already: a short press all the same */
    if (s_click_released)
      s_confirm_release_frames = 3;
    LOG_INFO("[MOUSE] click on %s", type_name(widget));
  } else if (widget && ++s_click_frames < 8) {
    return;
  } else {
    LOG_INFO("[MOUSE] click dropped: %s did not take the focus",
             type_name(widget));
  }
  Reflect::Release(s_click_target);
}

/* The mouse's buttons, as the controller's. */
void follow_buttons(uint32_t held, uint32_t went_down) {
  /* a press and its release in one read are both counted, or the press sticks */
  const uint32_t came_up = (s_buttons | went_down) & ~held;
  s_buttons = held;
  if (went_down & (kLeft | kSideForward))
    start_click();
  if (came_up & (kLeft | kSideForward)) {
    if (s_click_target)
      s_click_released = true;
    else if (s_confirm_held && s_confirm_release_frames == 0) {
      release(confirm_buttons());
      s_confirm_held = false;
    }
  }
  if (went_down & (kRight | kSideBack))
    press(back_buttons());
  if ((came_up & (kRight | kSideBack)) && !(held & (kRight | kSideBack)))
    release(back_buttons());
  if (went_down & kMiddle)
    press(Option);
  if (came_up & kMiddle)
    release(Option);
}

/* s_buttons keeps what is held: a button still down when the pointer comes
   back is no new click. */
void stand_down() {
  release_all();
  s_active = false;
  draw_pointer(s_x, s_y, false, s_drawn_hand);
}

} // namespace

void Tick() {
  if (!g_settings.mouse_control_enabled) {
    if (s_handle >= 0) {
      close_mouse();
      stand_down();
    }
    return;
  }
  if (!s_api_tried)
    load_api();
  if (!s_api_ready)
    return;
  if (--s_reopen_frames <= 0) {
    s_reopen_frames = 120; /* the user may change; look every two seconds */
    open_mouse();
  }
  if (s_handle < 0)
    return;

  MouseData samples[64];
  const int count = s_api.read(s_handle, samples, 64);
  if (count < 0) {
    LOG_WARN("[MOUSE] read failed: 0x%x", static_cast<unsigned>(count));
    close_mouse();
    stand_down();
    return;
  }
  int dx = 0, dy = 0, wheel = 0, tilt = 0;
  uint32_t buttons = s_buttons, went_down = 0;
  bool connected = false, intercepted = false;
  for (int i = 0; i < count; ++i) {
    const MouseData &sample = samples[i];
    if (!sample.connected)
      continue;
    connected = true;
    if (sample.buttons & kIntercepted) {
      intercepted = true;
      continue;
    }
    dx += sample.x;
    dy += sample.y;
    wheel += sample.wheel;
    tilt += sample.tilt;
    /* every press in between counts, not just the newest state */
    went_down |= sample.buttons & ~buttons;
    buttons = sample.buttons;
  }
  const uint64_t now = AniketosUI::MonotonicMs();
  /* a game, or the screen a game is on, has the mouse: nothing of ours */
  if (intercepted || (count > 0 && !connected) ||
      AniketosShell::CheatsMenu::GameHasFocus()) {
    if (s_active)
      stand_down();
    return;
  }
  if (dx || dy || wheel || tilt || went_down || buttons != s_buttons)
    s_last_activity_ms = now;
  if (s_sink) {
    s_sink(buttons & 0xff, went_down & 0xff, wheel, tilt);
    s_buttons = buttons;
    return;
  }
  if (!s_last_activity_ms)
    return; /* not touched yet: no pointer */
  if (!ensure_pointer()) {
    /* never select or click with a pointer nobody can see */
    if (s_active)
      stand_down();
    s_buttons = buttons;
    return;
  }
  if (!s_active)
    LOG_INFO("[MOUSE] in use (buttons 0x%x)", buttons);
  s_active = true;

  if (dx || dy) {
    /* a little faster the quicker it moves, as on a computer */
    const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
    const float speed = 1.0f + std::min(distance / 25.0f, 1.5f);
    s_x = std::clamp(s_x + static_cast<float>(dx) * speed, 0.0f, s_width - 1.0f);
    s_y =
        std::clamp(s_y + static_cast<float>(dy) * speed, 0.0f, s_height - 1.0f);
    s_last_move_ms = now;
  }
  const bool visible = now - s_last_activity_ms < kHideAfterMs;
  if (visible && --s_raise_frames <= 0) {
    s_raise_frames = 60;
    raise_pointer();
  }
  draw_pointer(s_x, s_y, visible, s_over_selectable);
  /* the open Shell Menu has the controller; leave the screen under it be */
  if (AniketosShell::ShouldSuppressShellInput()) {
    release_all(); /* follow_buttons will not see the button come up */
    s_buttons = buttons;
    return;
  }
  if (wheel)
    s_wheel.fetch_add(wheel);
  if (tilt)
    s_tilt.fetch_add(tilt);
  follow_buttons(buttons, went_down);
  follow_pointer(now);
  advance_click();
}

void FilterPad(GamePadData &data) {
  if (data.Buttons & Enter)
    s_circle_is_enter = (data.Buttons & Circle) != 0;
  if (!s_active.load()) {
    /* a press stand_down let go of ends now, not when the pointer is back */
    const uint32_t released = s_pad_up.exchange(0);
    data.ButtonsUp = static_cast<GamePadButtons>(data.ButtonsUp | released);
    return;
  }
  uint32_t held = s_pad_held.load();
  uint32_t down = s_pad_down.exchange(0);
  uint32_t up = s_pad_up.exchange(0);
  /* one wheel notch per call: a press now, its release the next time */
  static uint32_t notch_release = 0;
  if (notch_release) {
    up |= notch_release;
    notch_release = 0;
  } else if (const int notches = s_wheel.load(); notches != 0) {
    const uint32_t key = notches > 0 ? Up : Down;
    s_wheel.fetch_add(notches > 0 ? -1 : 1);
    held |= key;
    down |= key;
    notch_release = key;
  } else if (const int tilts = s_tilt.load(); tilts != 0) {
    const uint32_t key = tilts > 0 ? Right : Left;
    s_tilt.fetch_add(tilts > 0 ? -1 : 1);
    held |= key;
    down |= key;
    notch_release = key;
  }
  if (!held && !down && !up)
    return;
  data.Buttons = static_cast<GamePadButtons>(data.Buttons | held | down);
  data.ButtonsDown = static_cast<GamePadButtons>(data.ButtonsDown | down);
  data.ButtonsUp = static_cast<GamePadButtons>(data.ButtonsUp | up);
}

void SetCaptureSink(CaptureSink sink) {
  if (sink) {
    release_all();
    s_wheel = 0;
    s_tilt = 0;
  }
  s_sink = sink;
}

} // namespace MouseControl
