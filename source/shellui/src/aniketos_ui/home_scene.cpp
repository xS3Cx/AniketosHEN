#include "aniketos_ui/home_scene.hpp"

#include "aniketos_theme/clock/shell_clock.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <cstdio>
#include <cstring>

namespace AniketosUI::HomeScene {

namespace {

MonoMethod *s_is_disposed_getter = nullptr;
uint32_t s_own_root = 0; /* weak */

MonoObject *scene_field(MonoObject *object, const char *name) {
  MonoClassField *field = Reflect::FindField(object, name);
  MonoObject *value = nullptr;
  if (field)
    mono_field_get_value(object, field, &value);
  return value;
}

MonoObject *scene_call(MonoObject *object, const char *name, int parameters,
                       void **arguments) {
  MonoObject *value = nullptr;
  return Reflect::Call(object, name, parameters, arguments, &value) ? value
                                                                    : nullptr;
}

template <typename Wanted>
MonoObject *find_sub_scene(MonoObject *scene, int depth, Wanted wanted) {
  if (!scene || depth > 8)
    return nullptr;
  if (wanted(scene))
    return scene;
  MonoObject *list = scene_field(scene, "subSceneList");
  MonoObject *boxed = scene_call(list, "get_Count", 0, nullptr);
  const int32_t count =
      boxed ? *static_cast<int32_t *>(mono_object_unbox(boxed)) : 0;
  for (int32_t index = 0; index < count && index < 40; ++index) {
    void *arguments[1] = {&index};
    if (MonoObject *found = find_sub_scene(
            scene_call(list, "get_Item", 1, arguments), depth + 1, wanted))
      return found;
  }
  return nullptr;
}

void visit_scene(MonoObject *scene, int depth,
                 void (*visit)(MonoObject *root, void *context),
                 void *context) {
  if (!scene || depth > 10)
    return;
  if (MonoObject *root = scene_field(scene, "rootWidget"))
    if (!IsDisposed(root))
      visit(root, context);
  for (const char *list_name : {"subSceneList", "modalSceneList"}) {
    MonoObject *list = scene_field(scene, list_name);
    MonoObject *boxed = scene_call(list, "get_Count", 0, nullptr);
    const int32_t count =
        boxed ? *static_cast<int32_t *>(mono_object_unbox(boxed)) : 0;
    for (int32_t index = 0; index < count && index < 40; ++index) {
      void *arguments[1] = {&index};
      visit_scene(scene_call(list, "get_Item", 1, arguments), depth + 1, visit,
                  context);
    }
  }
}

MonoMethod *property_getter(MonoObject *object, const char *name) {
  char method[64];
  std::snprintf(method, sizeof(method), "get_%s", name);
  return Reflect::MethodOf(object, method, 0);
}

} // namespace

void ResolveDisposedGetter(MonoClass *widget_class) {
  for (MonoClass *klass = widget_class;
       klass && !s_is_disposed_getter && mono_class_get_parent;
       klass = mono_class_get_parent(klass)) {
    MonoProperty *disposed = mono_class_get_property_from_name
                                 ? mono_class_get_property_from_name(
                                       klass, "IsDisposed")
                                 : nullptr;
    s_is_disposed_getter =
        disposed && mono_property_get_get_method
            ? mono_property_get_get_method(disposed)
            : nullptr;
  }
}

bool IsDisposed(MonoObject *widget) {
  if (!widget || !s_is_disposed_getter)
    return false;
  /* Called for every node of the clock's parent chain on every frame. Use the
     compiled getter: mono_runtime_invoke boxes the bool, which meant a dozen
     managed allocations per frame for as long as ShellUI ran. IsDisposed is a
     plain field read and cannot throw. */
  static bool (*getter)(MonoObject *) = nullptr;
  if (!getter && mono_compile_method)
    getter = decltype(getter)(mono_compile_method(s_is_disposed_getter));
  return getter ? getter(widget) : false;
}

MonoObject *Parent(MonoObject *widget) {
  if (!widget || !mono_object_get_class || !mono_class_get_method_from_name ||
      !mono_runtime_invoke || !mono_class_get_parent)
    return nullptr;
  for (MonoClass *klass = mono_object_get_class(widget); klass;
       klass = mono_class_get_parent(klass)) {
    for (const char *getter_name : {"get_Parent", "get_NativeParent"}) {
      MonoMethod *getter =
          mono_class_get_method_from_name(klass, getter_name, 0);
      if (!getter)
        continue;
      MonoObject *exception = nullptr;
      MonoObject *parent =
          mono_runtime_invoke(getter, widget, nullptr, &exception);
      if (!exception && parent)
        return parent;
    }
  }
  return nullptr;
}

MonoObject *TopWidget(MonoObject *widget) {
  for (int depth = 0; depth < 24; ++depth) {
    MonoObject *parent = Parent(widget);
    if (!parent || parent == widget)
      break;
    widget = parent;
  }
  return widget;
}

/* The clock being followed is not always Home's: Settings and the Control
 * Center have clocks of their own, and the root of the scene the followed
 * clock is in used to pass for "Home's root". Whatever was hung there (the
 * date, the OSD, the video, the icon menu) then went to a scene that is not
 * on screen. Home's root is found in the scene tree instead: from any scene
 * up to the top, into the layer Home is shown in (TopMenu), and there to the
 * React scene that draws. Names only; no title id, no offsets. */
MonoObject *OwnRoot() {
  MonoObject *cached = Reflect::Target(s_own_root);
  if (cached && !IsDisposed(cached))
    return cached;
  Reflect::Release(s_own_root);
  /* The tree is walked at most four times a second while Home is not there.
     By time, not by calls: every label React sets asks (space_switcher_labels),
     so while a reloading Home builds its labels a count let the walk run
     several times a frame. */
  static uint64_t next_try_ms = 0;
  const uint64_t now_ms = MonotonicMs();
  if (now_ms < next_try_ms)
    return nullptr;
  next_try_ms = now_ms + 250;
  MonoObject *scene =
      scene_call(ShellClock::ClockSceneRoot(), "get_ParentScene", 0, nullptr);
  for (int guard = 0; scene && guard < 16; ++guard) {
    MonoObject *up = scene_field(scene, "superScene");
    if (!up)
      break;
    scene = up;
  }
  MonoObject *layer = find_sub_scene(scene, 0, [](MonoObject *candidate) {
    MonoObject *exception = nullptr;
    MonoString *text = mono_object_to_string(candidate, &exception);
    return text && !exception &&
           Mono_to_String(text).rfind("TopMenu ", 0) == 0;
  });
  MonoObject *render = find_sub_scene(layer, 0, [](MonoObject *candidate) {
    return !std::strcmp(mono_class_get_name(mono_object_get_class(candidate)),
                        "AppScene") &&
           scene_field(candidate, "rootWidget");
  });
  MonoObject *root = scene_field(render, "rootWidget");
  if (!root || IsDisposed(root) || !mono_gchandle_new_weakref)
    return nullptr;
  s_own_root = mono_gchandle_new_weakref(root, 0);
  LOG_INFO("[CLOCK] Home scene root found (%p)", static_cast<void *>(root));
  return root;
}

bool Contains(MonoObject *widget) {
  MonoObject *home = OwnRoot();
  return home && widget && TopWidget(widget) == home;
}

MonoObject *Root() {
  /* Keeps the clock's scene known to the clock's recovery, as well as being
     the answer when Home's own root is not found. */
  MonoObject *clock_root = ShellClock::ClockSceneRoot();
  MonoObject *home = OwnRoot();
  return home ? home : clock_root;
}

void VisitSceneRoots(void (*visit)(MonoObject *root, void *context),
                     void *context) {
  MonoObject *scene = scene_call(Root(), "get_ParentScene", 0, nullptr);
  for (int guard = 0; scene && guard < 16; ++guard) {
    MonoObject *up = scene_field(scene, "superScene");
    if (!up)
      break;
    scene = up;
  }
  visit_scene(scene, 0, visit, context);
}

MonoObject *PropertyObject(MonoObject *object, const char *name) {
  MonoMethod *getter = property_getter(object, name);
  if (!getter)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, object, nullptr, &exception);
  return exception ? nullptr : value;
}

std::string PropertyText(MonoObject *object, const char *name) {
  MonoMethod *getter = property_getter(object, name);
  if (!getter)
    return "-";
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, object, nullptr, &exception);
  if (exception || !value)
    return exception ? "!" : "null";
  MonoObject *string_exception = nullptr;
  MonoString *text = mono_object_to_string(value, &string_exception);
  std::string out = string_exception || !text ? "?" : Mono_to_String(text);
  if (out.size() > 60)
    out.resize(60);
  return out;
}

/* ShellUI outlives a reload of this module, and with it whatever an earlier
 * instance left under Home's root. Drop those before adding ours. */
void RemoveStaleChildren(MonoObject *root) {
  MonoObject *parents[2] = {root, PropertyObject(root, "FirstChild")};
  for (MonoObject *parent : parents) {
    MonoObject *child = PropertyObject(parent, "FirstChild");
    for (int guard = 0; child && guard < 64; ++guard) {
      MonoObject *next = PropertyObject(child, "NextSibling");
      if (PropertyText(child, "Name").rfind("id_anivsh_", 0) == 0)
        Reflect::Call(child, "RemoveFromParent", 0);
      child = next;
    }
  }
}

} // namespace AniketosUI::HomeScene
