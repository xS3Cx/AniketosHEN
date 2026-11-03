#include "icon_color_internal.hpp"

#include "aniketos_theme/customization.hpp"
#include "aniketos_theme/icon_color.hpp"
#include "aniketos_ui/mono_reflect.hpp"

#include <onion/log.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace AniketosTheme::IconColor {
namespace {

namespace Reflect = AniketosUI::Reflect;

constexpr size_t kMaxTracked = 4096;
constexpr size_t kMinPruneAt = 256;
constexpr float kRainbowStep = 0.001f;
constexpr UIColor kWhite{1.0f, 1.0f, 1.0f, 1.0f, 0};

struct TrackedIcon {
  uint32_t handle = 0; /* weak */
  MonoMethod *setter = nullptr;
  UIColor restore{}; /* as the system last set it */
};

struct ApplyCount {
  size_t applied = 0;
  size_t skipped = 0;
};

std::vector<TrackedIcon> s_tracked;
UIColor s_last_rainbow{};
bool s_has_last_rainbow = false;
bool s_logged_capacity = false;
/* The setter hooks track in every state: the list is pruned when it doubles. */
size_t s_prune_at = kMinPruneAt;
/* The icon set_color is writing: its setter hook sees our colour there. */
MonoObject *s_being_set = nullptr;
/* The chosen colour can be on icons, so the option going off has to take it
   away. */
bool s_tint_shown = false;

TrackedIcon *find_tracked(MonoObject *object) {
  for (TrackedIcon &entry : s_tracked) {
    if (Reflect::Target(entry.handle) == object)
      return &entry;
  }
  return nullptr;
}

/* `drop` holds indices in ascending order. */
void erase_tracked(const std::vector<size_t> &drop) {
  for (size_t i = drop.size(); i > 0; --i) {
    Reflect::Release(s_tracked[drop[i - 1]].handle);
    s_tracked.erase(s_tracked.begin() +
                    static_cast<std::ptrdiff_t>(drop[i - 1]));
  }
}

void prune_tracked() {
  std::vector<size_t> drop;
  for (size_t i = 0; i < s_tracked.size(); ++i) {
    MonoObject *object = Reflect::Target(s_tracked[i].handle);
    if (!object || object_disposed(object))
      drop.push_back(i);
  }
  erase_tracked(drop);
  s_prune_at = std::max(kMinPruneAt, s_tracked.size() * 2);
}

bool on_screen(MonoObject *object) {
  return object && !object_disposed(object) && object_visible(object);
}

bool apply_entry(const TrackedIcon &entry, MonoObject *object,
                 const UIColor &color) {
  UIColor applied = color;
  applied.a = entry.restore.a;
  applied.color_space = entry.restore.color_space;
  return set_color(entry.setter, object, applied);
}

ApplyCount apply_to_visible(const UIColor &color) {
  ApplyCount count;
  std::vector<size_t> drop;
  /* By index: a setter can track more icons, which moves the list. */
  for (size_t i = 0; i < s_tracked.size(); ++i) {
    MonoObject *object = Reflect::Target(s_tracked[i].handle);
    if (!on_screen(object)) {
      ++count.skipped;
      continue;
    }
    if (apply_entry(s_tracked[i], object, color)) {
      ++count.applied;
    } else {
      ++count.skipped;
      drop.push_back(i);
    }
  }
  erase_tracked(drop);
  return count;
}

void apply_all(const UIColor &color) {
  prune_tracked();
  const ApplyCount count = apply_to_visible(color);
  LOG_INFO("[ICONCOLOR] live apply count=%zu skipped=%zu", count.applied,
           count.skipped);
}

/* Hidden icons too: this runs once, so nothing comes back to them later. */
void restore_tracked() {
  prune_tracked();
  size_t restored = 0;
  std::vector<size_t> drop;
  /* By index: a setter can track more icons, which moves the list. */
  for (size_t i = 0; i < s_tracked.size(); ++i) {
    MonoObject *object = Reflect::Target(s_tracked[i].handle);
    if (!object || object_disposed(object))
      continue;
    if (set_color(s_tracked[i].setter, object, s_tracked[i].restore))
      ++restored;
    else
      drop.push_back(i);
  }
  erase_tracked(drop);
  LOG_INFO("[ICONCOLOR] system colours back count=%zu", restored);
}

bool any_on_screen() {
  for (const TrackedIcon &entry : s_tracked) {
    if (on_screen(Reflect::Target(entry.handle)))
      return true;
  }
  return false;
}

bool same_as_last_rainbow(const UIColor &color) {
  return s_has_last_rainbow &&
         std::fabs(color.r - s_last_rainbow.r) < kRainbowStep &&
         std::fabs(color.g - s_last_rainbow.g) < kRainbowStep &&
         std::fabs(color.b - s_last_rainbow.b) < kRainbowStep;
}

} // namespace

void track_object(MonoObject *object, MonoMethod *setter,
                  const UIColor &restore) {
  if (!object || !setter || find_tracked(object))
    return;
  if (s_tracked.size() >= kMaxTracked) {
    if (!s_logged_capacity) {
      s_logged_capacity = true;
      LOG_INFO("[ICONCOLOR] tracked capacity reached: %zu", kMaxTracked);
    }
    return;
  }
  TrackedIcon entry{};
  entry.setter = setter;
  entry.restore = restore;
  Reflect::Hold(entry.handle, object);
  if (entry.handle)
    s_tracked.push_back(entry);
}

void note_system_color(MonoObject *object, MonoMethod *setter,
                       const UIColor &color) {
  if (object == s_being_set)
    return;
  if (TrackedIcon *entry = find_tracked(object))
    entry->restore = color;
  else
    track_object(object, setter, color);
}

bool set_color(MonoMethod *setter, MonoObject *object, UIColor color) {
  void *args[1] = {&color};
  MonoObject *exception = nullptr;
  MonoObject *const outer = s_being_set;
  s_being_set = object;
  mono_runtime_invoke(setter, object, args, &exception);
  s_being_set = outer;
  return !exception;
}

void leave_untinted(MonoObject *object) {
  TrackedIcon *found = find_tracked(object);
  if (!found)
    return;
  /* out of the list first: the setter's hook comes back here */
  const TrackedIcon entry = *found;
  s_tracked.erase(s_tracked.begin() + (found - s_tracked.data()));
  if (!object_disposed(object))
    set_color(entry.setter, object, entry.restore);
  uint32_t handle = entry.handle;
  Reflect::Release(handle);
}

/* An image whose colour cannot be read still takes the tint: it is only left
   out of the list. */
bool tint_image(MonoObject *object) {
  UIColor original = kWhite;
  const bool known = read_color(s_image_filter.getter, object, &original);
  if (!set_color(s_image_filter.setter, object, current_color()))
    return false;
  if (known)
    track_object(object, s_image_filter.setter, original);
  return true;
}

void Tick() {
  if (s_tracked.size() >= s_prune_at)
    prune_tracked();
  if (!feature_on()) {
    if (s_tint_shown) {
      s_tint_shown = false;
      restore_tracked();
    }
    return;
  }
  s_tint_shown = true;
  if (g_settings.system_icon_color_animation != 1 || s_tracked.empty())
    return;
  prune_tracked();
  if (!any_on_screen())
    return;
  const UIColor color = current_color();
  if (same_as_last_rainbow(color))
    return;
  s_last_rainbow = color;
  s_has_last_rainbow = true;
  apply_to_visible(color);
}

void RequestApply() {
  restart_rainbow();
  s_has_last_rainbow = false;
  if (!feature_on())
    return; /* Tick brings the system colours back */
  apply_all(current_color());
  run_scene_scan();
}

} // namespace AniketosTheme::IconColor
