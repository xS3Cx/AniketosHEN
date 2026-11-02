#include "icon_color_internal.hpp"

#include "aniketos_theme/customization.hpp"
#include "aniketos_theme/icon_color.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/monotonic_clock.hpp"

#include <onion/log.h>

#include <cmath>
#include <cstddef>

namespace AniketosTheme::IconColor {
namespace {

namespace Reflect = AniketosUI::Reflect;

constexpr size_t kMaxTracked = 4096;
constexpr uint64_t kWhiteHoldMs = 250;
constexpr uint64_t kWhiteHoldLogMs = 1000;
constexpr float kRainbowStep = 0.001f;
constexpr UIColor kWhite{1.0f, 1.0f, 1.0f, 1.0f, 0};

struct TrackedIcon {
  uint32_t handle = 0; /* weak */
  MonoMethod *setter = nullptr;
  UIColor restore{};
};

struct ApplyCount {
  size_t applied = 0;
  size_t skipped = 0;
};

std::vector<TrackedIcon> s_tracked;
UIColor s_last_rainbow{};
bool s_has_last_rainbow = false;
bool s_logged_capacity = false;
uint64_t s_last_white_ms = 0;
uint64_t s_last_white_log_ms = 0;

bool is_tracked(MonoObject *object) {
  for (const TrackedIcon &entry : s_tracked) {
    if (Reflect::Target(entry.handle) == object)
      return true;
  }
  return false;
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

size_t apply_all(const UIColor &color) {
  prune_tracked();
  const ApplyCount count = apply_to_visible(color);
  LOG_INFO("[ICONCOLOR] live apply count=%zu skipped=%zu", count.applied,
           count.skipped);
  return count.applied;
}

void hold_white() {
  if (s_tracked.empty())
    return;
  const uint64_t now = AniketosUI::MonotonicMs();
  if (s_last_white_ms && now - s_last_white_ms < kWhiteHoldMs)
    return;
  s_last_white_ms = now;
  prune_tracked();
  const size_t applied = apply_to_visible(kWhite).applied;
  if (now - s_last_white_log_ms >= kWhiteHoldLogMs) {
    s_last_white_log_ms = now;
    LOG_INFO("[ICONCOLOR] white hold count=%zu", applied);
  }
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
  if (!object || !setter || is_tracked(object))
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

bool set_color(MonoMethod *setter, MonoObject *object, UIColor color) {
  void *args[1] = {&color};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(setter, object, args, &exception);
  return !exception;
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
  if (!feature_on()) {
    hold_white();
    return;
  }
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
  if (!feature_on()) {
    const size_t count = apply_all(kWhite);
    LOG_INFO("[ICONCOLOR] live white count=%zu", count);
    return;
  }
  apply_all(current_color());
  run_scene_scan();
}

} // namespace AniketosTheme::IconColor
