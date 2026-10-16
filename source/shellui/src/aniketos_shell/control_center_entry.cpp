#include "aniketos_shell/home_menu.hpp"
#include "aniketos_shell/home_tabs.hpp"

#include "hooked_funcs.hpp"
#include "shell_clock.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
extern const unsigned char aniketos_icon_png_start[];
extern const unsigned char aniketos_icon_png_end[];
extern const unsigned char color_track_png_start[];
extern const unsigned char color_track_png_end[];
extern const unsigned char color_track_gray_png_start[];
extern const unsigned char color_track_gray_png_end[];
extern const unsigned char color_track_solid_png_start[];
extern const unsigned char color_track_solid_png_end[];
extern const unsigned char color_track_fade_png_start[];
extern const unsigned char color_track_fade_png_end[];
extern const unsigned char home_menu_internet_on_png_start[];
extern const unsigned char home_menu_internet_on_png_end[];
extern const unsigned char home_menu_internet_off_png_start[];
extern const unsigned char home_menu_internet_off_png_end[];
}

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

uint32_t s_button_handle = 0;
uint32_t s_label_holder_handle = 0;
uint32_t s_scene_handle = 0;
int s_search_countdown = 0;

MonoObject *target(uint32_t handle) {
  return handle ? mono_gchandle_get_target(handle) : nullptr;
}

void keep(uint32_t &handle, MonoObject *object) {
  if (handle)
    mono_gchandle_free(handle);
  handle = object ? mono_gchandle_new_weakref(object, 0) : 0;
}

MonoObject *get(MonoObject *object, const char *getter) {
  if (!object)
    return nullptr;
  MonoMethod *method = nullptr;
  for (MonoClass *k = mono_object_get_class(object); k && !method;
       k = mono_class_get_parent(k))
    method = mono_class_get_method_from_name(k, getter, 0);
  if (!method)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(method, object, nullptr, &exception);
  return exception ? nullptr : value;
}

std::string text_of(MonoObject *managed_string) {
  if (!managed_string)
    return {};
  const char *utf =
      mono_string_to_utf8(reinterpret_cast<MonoString *>(managed_string));
  std::string text = utf ? utf : "";
  if (utf && mono_free)
    mono_free(const_cast<char *>(utf));
  return text;
}

MonoObject *field(MonoObject *object, const char *name) {
  if (!object)
    return nullptr;
  for (MonoClass *k = mono_object_get_class(object); k;
       k = mono_class_get_parent(k))
    if (MonoClassField *f = mono_class_get_field_from_name(k, name)) {
      MonoObject *value = nullptr;
      mono_field_get_value(object, f, &value);
      return value;
    }
  return nullptr;
}

std::string describe(MonoObject *object) {
  MonoObject *exception = nullptr;
  MonoString *managed = object ? mono_object_to_string(object, &exception)
                               : nullptr;
  return exception ? std::string() : text_of(reinterpret_cast<MonoObject *>(managed));
}

int count_of(MonoObject *list) {
  MonoObject *boxed = get(list, "get_Count");
  return boxed ? *static_cast<int32_t *>(mono_object_unbox(boxed)) : 0;
}

MonoObject *item_of(MonoObject *list, int index) {
  MonoMethod *method = nullptr;
  for (MonoClass *k = mono_object_get_class(list); k && !method;
       k = mono_class_get_parent(k))
    method = mono_class_get_method_from_name(k, "get_Item", 1);
  if (!method)
    return nullptr;
  int32_t value = index;
  void *args[1] = {&value};
  MonoObject *exception = nullptr;
  MonoObject *item = mono_runtime_invoke(method, list, args, &exception);
  return exception ? nullptr : item;
}

/* The first scene below `scene` that satisfies `wanted`. */
template <typename Wanted>
MonoObject *find_scene(MonoObject *scene, int depth, const Wanted &wanted) {
  if (!scene || depth > 14)
    return nullptr;
  if (wanted(scene))
    return scene;
  MonoObject *list = field(scene, "subSceneList");
  const int count = list ? count_of(list) : 0;
  for (int i = 0; i < count && i < 40; ++i)
    if (MonoObject *found = find_scene(item_of(list, i), depth + 1, wanted))
      return found;
  return nullptr;
}

/* The Control Center's React scene: the app scene under the layer called
   "PControlCenter" at the top of ShellUI's scene tree. */
MonoObject *control_center_scene() {
  MonoObject *scene = get(ShellClock::GetHomeRoot(), "get_ParentScene");
  for (int guard = 0; scene && guard < 16; ++guard) {
    MonoObject *up = field(scene, "superScene");
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
  const std::string test_id = text_of(get(widget, "get_TestId"));
  if (test_id == kButtonTestId)
    keep(s_button_handle, widget);
  else if (test_id == kLabelHolderTestId)
    keep(s_label_holder_handle, widget);
  MonoObject *child = get(widget, "get_FirstChild");
  for (int guard = 0; child && guard < 64; ++guard) {
    find_widgets(child, depth + 1, budget);
    child = get(child, "get_NextSibling");
  }
}

bool call_string(MonoObject *object, const char *setter, const char *value) {
  MonoMethod *method = nullptr;
  for (MonoClass *k = mono_object_get_class(object); k && !method;
       k = mono_class_get_parent(k))
    method = mono_class_get_method_from_name(k, setter, 1);
  if (!method)
    return false;
  void *args[1] = {mono_string_new(mono_domain_get(), value)};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, object, args, &exception);
  return exception == nullptr;
}

/* PressEvent.Dispatch(RCTEventEmitter): how a press reaches the Control
   Center's JavaScript. A press goes to the view that has focus, so one that
   is dispatched while our button is focused is the press on our button; it
   is dropped here and answered by our menu instead. */
void dispatch_press_hook(MonoObject *event, MonoObject *emitter) {
  if (s_button_focused.load(std::memory_order_relaxed) &&
      HomeMenu::Available()) {
    s_pressed.store(true, std::memory_order_relaxed);
    return;
  }
  if (s_dispatch_press_original)
    s_dispatch_press_original(event, emitter);
}

bool is_focused(MonoObject *widget) {
  MonoObject *boxed = get(widget, "get_Focused");
  return boxed && *static_cast<bool *>(mono_object_unbox(boxed));
}

/* An ImageAsset of our icon, made once. */
MonoObject *icon_asset() {
  if (MonoObject *asset = target(s_icon_handle))
    return asset;
  const char *path = AniketosIconPath();
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  MonoClass *klass =
      pui && *path ? mono_class_from_name(pui, "Sce.PlayStation.PUI", "ImageAsset")
                   : nullptr;
  if (!klass)
    return nullptr;
  MonoMethod *constructor = nullptr;
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
    if (takes_string) {
      constructor = method;
      break;
    }
  }
  if (!constructor)
    return nullptr;
  MonoObject *asset = mono_object_new(mono_domain_get(), klass);
  void *args[1] = {mono_string_new(mono_domain_get(), path)};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(constructor, asset, args, &exception);
  if (exception)
    return nullptr;
  s_icon_handle = mono_gchandle_new(asset, 0);
  return asset;
}

void set_object(MonoObject *object, const char *setter, MonoObject *value) {
  MonoMethod *method = nullptr;
  for (MonoClass *k = mono_object_get_class(object); k && !method;
       k = mono_class_get_parent(k))
    method = mono_class_get_method_from_name(k, setter, 1);
  if (!method)
    return;
  void *args[1] = {value};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, object, args, &exception);
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
    HomeMenu::Request(target(s_scene_handle));
  }
  MonoObject *button = target(s_button_handle);
  if (button && ShellClock::IsWidgetDisposed(button)) {
    keep(s_button_handle, nullptr);
    keep(s_label_holder_handle, nullptr);
    button = nullptr;
  }
  if (!button)
    s_button_focused.store(false, std::memory_order_relaxed);
  if (!button) {
    /* React builds the Control Center lazily and rebuilds it now and then;
       look for the button once a second until it is there. */
    if (--s_search_countdown > 0)
      return;
    s_search_countdown = 60;
    MonoObject *scene = control_center_scene();
    keep(s_scene_handle, scene);
    int budget = 600;
    find_widgets(field(scene, "rootWidget"), 0, budget);
    button = target(s_button_handle);
    if (!button)
      return;
    LOG_INFO("[CCENTRY] button found %p", static_cast<void *>(button));
  }

  /* The button itself, or the focus layer React wraps it in, has focus. */
  bool focused = false;
  MonoObject *node_up = button;
  for (int depth = 0; node_up && depth < 4 && !focused; ++depth) {
    focused = is_focused(node_up);
    node_up = get(node_up, "get_Parent");
  }
  /* Focus stays where it was when the Control Center closes, so it only
     counts while the button is really on screen. Otherwise presses made in
     Home or anywhere else would be taken for presses on our button. */
  if (focused) {
    MonoObject *shown = get(button, "get_IsFinallyShown");
    MonoObject *opacity = get(button, "get_FinalOpacity");
    focused = shown && *static_cast<bool *>(mono_object_unbox(shown)) &&
              opacity && *static_cast<float *>(mono_object_unbox(opacity)) > 0.5f;
  }
  s_button_focused.store(focused, std::memory_order_relaxed);

  /* React puts its own image back whenever it re-renders the button. */
  if (MonoObject *asset = icon_asset()) {
    if (get(button, "get_IconImage") != asset)
      set_object(button, "set_IconImage", asset);
    if (get(button, "get_InvertedIconImage") != asset)
      set_object(button, "set_InvertedIconImage", asset);
  }

  /* The caption is a label React creates while the button has focus. */
  MonoObject *holder = target(s_label_holder_handle);
  if (!holder || ShellClock::IsWidgetDisposed(holder))
    return;
  MonoObject *node = get(holder, "get_FirstChild");
  for (int depth = 0; node && depth < 4; ++depth) {
    if (!std::strcmp(mono_class_get_name(mono_object_get_class(node)),
                     "Label")) {
      if (text_of(get(node, "get_Text")) != kLabel)
        call_string(node, "set_Text", kLabel);
      break;
    }
    node = get(node, "get_FirstChild");
  }
}

} // namespace ControlCenterEntry

namespace {
/* Writes an image embedded in this module into our own data folder, once,
   and gives its path (empty if it could not be written). */
const char *embedded_image(const char *file, const unsigned char *start,
                           const unsigned char *end, char (&path)[160],
                           bool &tried) {
  if (tried)
    return path;
  tried = true;
  const size_t size = static_cast<size_t>(end - start);
  char candidate[160];
  std::snprintf(candidate, sizeof(candidate), "%s/%s",
                ONION_DIST_USER_DATA_ROOT, file);
  struct stat st {};
  if (stat(candidate, &st) != 0 || static_cast<size_t>(st.st_size) != size) {
    const int fd = open(candidate, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
      return path;
    const ssize_t written = write(fd, start, size);
    close(fd);
    if (written != static_cast<ssize_t>(size)) {
      unlink(candidate);
      return path;
    }
  }
  std::snprintf(path, sizeof(path), "%s", candidate);
  return path;
}
} // namespace

const char *AniketosIconPath() {
  static char path[160];
  static bool tried = false;
  return embedded_image("aniketos_icon.png", aniketos_icon_png_start,
                        aniketos_icon_png_end, path, tried);
}

const char *ColorTrackPath(ColorTrack track) {
  static char paths[4][160];
  static bool tried[4] = {};
  switch (track) {
  case ColorTrack::Gray:
    return embedded_image("color_track_gray.png", color_track_gray_png_start,
                          color_track_gray_png_end, paths[1], tried[1]);
  case ColorTrack::Solid:
    return embedded_image("color_track_solid.png",
                          color_track_solid_png_start,
                          color_track_solid_png_end, paths[2], tried[2]);
  case ColorTrack::Fade:
    return embedded_image("color_track_fade.png", color_track_fade_png_start,
                          color_track_fade_png_end, paths[3], tried[3]);
  case ColorTrack::Hues:
  default:
    return embedded_image("color_track.png", color_track_png_start,
                          color_track_png_end, paths[0], tried[0]);
  }
}

const char *InternetIconPath(bool enabled) {
  static char on_path[160], off_path[160];
  static bool on_tried = false, off_tried = false;
  return enabled ? embedded_image("internet_on.png",
                                  home_menu_internet_on_png_start,
                                  home_menu_internet_on_png_end, on_path,
                                  on_tried)
                 : embedded_image("internet_off.png",
                                  home_menu_internet_off_png_start,
                                  home_menu_internet_off_png_end, off_path,
                                  off_tried);
}
