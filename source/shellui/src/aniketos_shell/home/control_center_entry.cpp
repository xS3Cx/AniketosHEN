#include "aniketos_shell/home/control_center_entry.hpp"

#include "aniketos_shell/core/embedded_assets.hpp"
#include "aniketos_shell/home/hen_menu.hpp"
#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/mono_property_cache.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "hooked_funcs.hpp"

#include <onion/detour.h>
#include <onion/log.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace HomeScene = AniketosUI::HomeScene;
namespace Reflect = AniketosUI::Reflect;

namespace ControlCenterEntry {
namespace {

constexpr const char *kButtonTestId = "fc-button-gaming-lounge-icon-button";
constexpr const char *kLabelHolderTestId =
    "fc-button-gaming-lounge-label-layout";
constexpr const char *kLabel = "Aniketos";

using DispatchEvent = void (*)(MonoObject *, MonoObject *);
DispatchEvent s_dispatch_press_original = nullptr;

/* Written by the UI thread, read where React dispatches its events. */
std::atomic<bool> s_button_focused{false};
std::atomic<bool> s_pressed{false};

uint32_t s_icon_handle = 0; /* our ImageAsset, kept alive */
std::string s_icon_for;     /* the picture it was made of */
/* what the button showed right after our last set of each image */
uint32_t s_icon_shown_handle = 0;
std::atomic<bool> s_visible{false};
uint32_t s_inverted_icon_shown_handle = 0;

uint32_t s_button_handle = 0;
uint32_t s_label_holder_handle = 0;
uint32_t s_scene_handle = 0;
int s_scene_searches = 0; /* searches made in the scene held */
int s_search_countdown = 0;
int s_hidden_frames = 0;

MonoObject *get(MonoObject *object, const char *getter) {
  MonoObject *value = nullptr;
  return Reflect::Call(object, getter, 0, nullptr, &value) ? value : nullptr;
}

bool flag(MonoObject *object, const char *getter) {
  MonoObject *boxed = get(object, getter);
  return boxed && *static_cast<bool *>(mono_object_unbox(boxed));
}

std::string text(MonoObject *object, const char *getter) {
  return Mono_to_String(reinterpret_cast<MonoString *>(get(object, getter)));
}

std::string describe(MonoObject *object) {
  MonoObject *exception = nullptr;
  MonoString *managed = object ? mono_object_to_string(object, &exception)
                               : nullptr;
  return exception ? std::string() : Mono_to_String(managed);
}

int count_of(MonoObject *list) {
  MonoObject *boxed = get(list, "get_Count");
  return boxed ? *static_cast<int32_t *>(mono_object_unbox(boxed)) : 0;
}

MonoObject *item_of(MonoObject *list, int32_t index) {
  void *args[1] = {&index};
  MonoObject *item = nullptr;
  return Reflect::Call(list, "get_Item", 1, args, &item) ? item : nullptr;
}

/* The first scene below `scene` that satisfies `wanted`. */
template <typename Wanted>
MonoObject *find_scene(MonoObject *scene, int depth, const Wanted &wanted) {
  if (!scene || depth > 14)
    return nullptr;
  if (wanted(scene))
    return scene;
  MonoObject *list = Reflect::FieldValue(scene, "subSceneList");
  const int count = list ? count_of(list) : 0;
  for (int i = 0; i < count && i < 40; ++i)
    if (MonoObject *found = find_scene(item_of(list, i), depth + 1, wanted))
      return found;
  return nullptr;
}

/* The Control Center's React scene: the app scene under the layer called
   "PControlCenter" at the top of ShellUI's scene tree. */
MonoObject *control_center_scene() {
  MonoObject *scene = get(HomeScene::Root(), "get_ParentScene");
  for (int guard = 0; scene && guard < 16; ++guard) {
    MonoObject *up = Reflect::FieldValue(scene, "superScene");
    if (!up)
      break;
    scene = up;
  }
  MonoObject *layer = find_scene(scene, 0, [](MonoObject *candidate) {
    return describe(candidate).rfind("PControlCenter", 0) == 0;
  });
  return find_scene(layer, 0, [](MonoObject *candidate) {
    return !std::strcmp(mono_class_get_name(mono_object_get_class(candidate)),
                        "AppScene");
  });
}

void find_widgets(MonoObject *widget, int depth, int &budget) {
  if (!widget || depth > 40 || budget-- <= 0)
    return;
  const std::string test_id = text(widget, "get_TestId");
  if (test_id == kButtonTestId)
    Reflect::Hold(s_button_handle, widget);
  else if (test_id == kLabelHolderTestId)
    Reflect::Hold(s_label_holder_handle, widget);
  MonoObject *child = get(widget, "get_FirstChild");
  for (int guard = 0; child && guard < 64; ++guard) {
    find_widgets(child, depth + 1, budget);
    child = get(child, "get_NextSibling");
  }
}

/* PressEvent.Dispatch(RCTEventEmitter): how a press reaches the Control
   Center's JavaScript. A press goes to the view that has focus, so one that
   is dispatched while our button is focused is the press on our button; it
   is dropped here and answered by our menu instead. */
void dispatch_press_hook(MonoObject *event, MonoObject *emitter) {
  if (s_button_focused.load(std::memory_order_relaxed) &&
      HenMenu::Available()) {
    s_pressed.store(true, std::memory_order_relaxed);
    return;
  }
  if (s_dispatch_press_original)
    s_dispatch_press_original(event, emitter);
}

MonoMethod *string_constructor(MonoClass *klass) {
  void *iter = nullptr;
  while (MonoMethod *method = mono_class_get_methods(klass, &iter)) {
    if (std::strcmp(mono_method_get_name(method), ".ctor") != 0)
      continue;
    MonoMethodSignature *signature = mono_method_signature(method);
    if (!signature || mono_signature_get_param_count(signature) != 1)
      continue;
    void *param_iter = nullptr;
    char *type =
        mono_type_get_name(mono_signature_get_params(signature, &param_iter));
    const bool takes_string = type && !std::strcmp(type, "System.String");
    if (type && mono_free)
      mono_free(type);
    if (takes_string)
      return method;
  }
  return nullptr;
}

/* An ImageAsset of our icon, made once. */
MonoObject *icon_asset() {
  const char *path = AniketosIconPath();
  if (s_icon_for != path) {
    /* another colour: what the button shows is no longer ours to keep */
    Reflect::Release(s_icon_handle);
    Reflect::Release(s_icon_shown_handle);
    Reflect::Release(s_inverted_icon_shown_handle);
  }
  s_icon_for = path;
  if (MonoObject *asset = Reflect::Target(s_icon_handle))
    return asset;
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  MonoClass *klass =
      pui && *path ? mono_class_from_name(pui, "Sce.PlayStation.PUI", "ImageAsset")
                   : nullptr;
  MonoMethod *constructor = klass ? string_constructor(klass) : nullptr;
  if (!constructor)
    return nullptr;
  MonoObject *asset = mono_object_new(mono_domain_get(), klass);
  void *args[1] = {mono_string_new(mono_domain_get(), path)};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(constructor, asset, args, &exception);
  if (exception)
    return nullptr;
  Reflect::Hold(s_icon_handle, asset, /*strong=*/true);
  return asset;
}

void set_object(MonoObject *object, const char *setter, MonoObject *value) {
  void *args[1] = {value};
  Reflect::Call(object, setter, 1, args);
}

/* Sets `asset` through `setter` unless the button still shows what our last
   set left there (`shown`). */
void apply_icon(MonoObject *button, const char *getter, const char *setter,
                MonoObject *asset, uint32_t &shown) {
  MonoObject *current = get(button, getter);
  if (current == asset || (current && current == Reflect::Target(shown)))
    return;
  set_object(button, setter, asset);
  Reflect::Hold(shown, get(button, getter));
}

/* The button we hold, dropped once React has disposed of it. */
MonoObject *current_button() {
  MonoObject *button = Reflect::Target(s_button_handle);
  if (button && HomeScene::IsDisposed(button)) {
    Reflect::Hold(s_button_handle, nullptr);
    Reflect::Hold(s_label_holder_handle, nullptr);
    button = nullptr;
  }
  return button;
}

/* React builds the Control Center lazily and rebuilds it now and then; look
   for the button once a second until it is there. */
MonoObject *search_button() {
  if (--s_search_countdown > 0)
    return nullptr;
  s_search_countdown = 60;
  /* The scene found last is looked in again while its tree is alive (the
     button is rebuilt inside it); the scene tree is walked anew, with a
     ToString per scene, once its root is gone, or every tenth time in case
     React made another scene and left this one. */
  MonoObject *scene = Reflect::Target(s_scene_handle);
  MonoObject *root = Reflect::FieldValue(scene, "rootWidget");
  if (!root || HomeScene::IsDisposed(root) || ++s_scene_searches >= 10) {
    scene = control_center_scene();
    Reflect::Hold(s_scene_handle, scene);
    root = Reflect::FieldValue(scene, "rootWidget");
    s_scene_searches = 0;
  }
  int budget = 600;
  find_widgets(root, 0, budget);
  MonoObject *button = Reflect::Target(s_button_handle);
  if (button)
    LOG_INFO("[CCENTRY] button found %p", static_cast<void *>(button));
  return button;
}

/* The button itself, or the focus layer React wraps it in, has focus. */
bool button_has_focus(MonoObject *button) {
  bool focused = false;
  MonoObject *node_up = button;
  for (int depth = 0; node_up && depth < 4 && !focused; ++depth) {
    focused = flag(node_up, "get_Focused");
    node_up = get(node_up, "get_Parent");
  }
  return focused;
}

/* The caption is a label React creates while the button has focus. */
void show_label() {
  MonoObject *holder = Reflect::Target(s_label_holder_handle);
  if (!holder || HomeScene::IsDisposed(holder))
    return;
  MonoObject *node = get(holder, "get_FirstChild");
  for (int depth = 0; node && depth < 4; ++depth) {
    if (!std::strcmp(mono_class_get_name(mono_object_get_class(node)),
                     "Label")) {
      if (text(node, "get_Text") != kLabel) {
        void *args[1] = {mono_string_new(mono_domain_get(), kLabel)};
        Reflect::Call(node, "set_Text", 1, args);
      }
      break;
    }
    node = get(node, "get_FirstChild");
  }
}

} // namespace

void Install(MonoImage *react_pui) {
  if (!react_pui || s_dispatch_press_original)
    return;
  MonoClass *press_event =
      mono_class_from_name(react_pui, "ReactNative.Views.View", "PressEvent");
  const uint64_t address =
      press_event ? Get_Address_of_Method(react_pui, press_event, "Dispatch", 1)
                  : 0;
  const bool installed =
      address &&
      InstallDetour(address, reinterpret_cast<void *>(&dispatch_press_hook),
                    reinterpret_cast<void **>(&s_dispatch_press_original));
  LOG_INFO("[CCENTRY] press hook target=0x%lx installed=%d icon=%s",
           static_cast<unsigned long>(address), installed ? 1 : 0,
           AniketosIconPath());
  if (!installed)
    s_dispatch_press_original = nullptr;
}

void Tick() {
  if (!s_dispatch_press_original)
    return;
  if (s_pressed.exchange(false, std::memory_order_relaxed)) {
    LOG_INFO("[CCENTRY] button pressed");
    HenMenu::Request(Reflect::Target(s_scene_handle));
  }
  MonoObject *button = current_button();
  bool found_now = false;
  if (!button) {
    s_button_focused.store(false, std::memory_order_relaxed);
    s_visible.store(false, std::memory_order_relaxed);
    button = search_button();
    if (!button)
      return;
    found_now = true;
  }
  /* Focus stays where it was when the Control Center closes, so it only
     counts while the button is really on screen. Otherwise presses made in
     Home or anywhere else would be taken for presses on our button. */
  /* compiled getters: plain reads, no boxing, every frame for good */
  MonoClass *button_class = mono_object_get_class(button);
  const bool shown = AniketosUI::MonoPropertyCache::Get<bool>(
      button_class, button, "IsFinallyShown");
  const float opacity = shown ? AniketosUI::MonoPropertyCache::Get<float>(
                                    button_class, button, "FinalOpacity")
                              : 0.0f;
  s_button_focused.store(opacity > 0.5f && button_has_focus(button),
                         std::memory_order_relaxed);
  s_visible.store(opacity > 0.05f, std::memory_order_relaxed);
  /* Out of sight (the Control Center closed, most of the time) the icon and
     caption are seen to four times a second rather than on every frame. */
  if (opacity <= 0.0f && !found_now && ++s_hidden_frames % 15 != 0)
    return;

  /* React puts its own image back whenever it re-renders the button. The
     button keeps its own copy of what it is given, so the copy read back
     after our set is what tells ours from React's: setting it every frame
     cost a texture swap and an icon-colour pass, twice a frame. */
  if (MonoObject *asset = icon_asset()) {
    apply_icon(button, "get_IconImage", "set_IconImage", asset,
               s_icon_shown_handle);
    apply_icon(button, "get_InvertedIconImage", "set_InvertedIconImage",
               asset, s_inverted_icon_shown_handle);
  }

  show_label();
}

bool Visible() { return s_visible.load(std::memory_order_relaxed); }

} // namespace ControlCenterEntry
