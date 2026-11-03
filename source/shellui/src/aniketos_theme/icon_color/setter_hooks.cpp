#include "icon_color_internal.hpp"

#include "aniketos_theme/icon_color.hpp"

#include <onion/detour.h>
#include <onion/log.h>

#include <algorithm>
#include <iterator>

namespace AniketosTheme::IconColor {
namespace {

constexpr size_t kMaxColorSetters = 8;
constexpr size_t kMaxImageSourceSetters = 6;
constexpr size_t kMaxMenuIconSetters = 6;
constexpr size_t kMaxButtonIconSetters = 4;

using ColorSetter = void (*)(MonoObject *, UIColor);
using ObjectSetter = void (*)(MonoObject *, MonoObject *);

template <typename Setter, size_t Capacity> struct SetterHooks {
  Setter originals[Capacity] = {};
  uint64_t addresses[Capacity] = {};
  MonoMethod *methods[Capacity] = {};
  size_t count = 0;
};

SetterHooks<ColorSetter, kMaxColorSetters> s_buttons;
SetterHooks<ColorSetter, kMaxColorSetters> s_images;
SetterHooks<ObjectSetter, kMaxImageSourceSetters> s_image_sources;
SetterHooks<ObjectSetter, kMaxMenuIconSetters> s_menu_icons;
SetterHooks<ObjectSetter, kMaxButtonIconSetters> s_button_icons;
ObjectSetter s_attach_original = nullptr;
uint64_t s_attach_address = 0;
bool s_image_source_active = false;
bool s_menu_icon_active = false;
bool s_button_icon_active = false;
bool s_attach_active = false;

UIColor with_chosen_rgb(UIColor color) {
  const UIColor chosen = current_color();
  color.r = chosen.r;
  color.g = chosen.g;
  color.b = chosen.b;
  return color;
}

void apply_button_setter(size_t index, MonoObject *object, UIColor color) {
  if (index >= s_buttons.count)
    return;
  ColorSetter original = s_buttons.originals[index];
  if (!original || !object)
    return;
  /* Off: Sony's setter and nothing else. What is on screen when the option
     comes on is found by RequestApply's scene scan. */
  if (!feature_on()) {
    original(object, color);
    return;
  }
  if (object_disposed(object))
    return;
  if (keeps_own_colours(object)) {
    leave_untinted(object);
    original(object, color);
    return;
  }
  note_system_color(object, s_buttons.methods[index], color);
  original(object, feature_on() ? with_chosen_rgb(color) : color);
}

template <int N> void button_setter_hook(MonoObject *object, UIColor color) {
  apply_button_setter(static_cast<size_t>(N), object, color);
}

constexpr ColorSetter kButtonHooks[kMaxColorSetters] = {
    &button_setter_hook<0>, &button_setter_hook<1>, &button_setter_hook<2>,
    &button_setter_hook<3>, &button_setter_hook<4>, &button_setter_hook<5>,
    &button_setter_hook<6>, &button_setter_hook<7>};

void apply_image_setter(size_t index, MonoObject *object, UIColor color) {
  if (index >= s_images.count)
    return;
  ColorSetter original = s_images.originals[index];
  if (!original || !object)
    return;
  if (!feature_on()) {
    original(object, color);
    return;
  }
  if (object_disposed(object))
    return;
  const bool target = widget_should_tint(object);
  if (target)
    note_system_color(object, s_images.methods[index], color);
  else
    leave_untinted(object);
  original(object, target && feature_on() ? with_chosen_rgb(color) : color);
}

template <int N> void image_setter_hook(MonoObject *object, UIColor color) {
  apply_image_setter(static_cast<size_t>(N), object, color);
}

constexpr ColorSetter kImageHooks[kMaxColorSetters] = {
    &image_setter_hook<0>, &image_setter_hook<1>, &image_setter_hook<2>,
    &image_setter_hook<3>, &image_setter_hook<4>, &image_setter_hook<5>,
    &image_setter_hook<6>, &image_setter_hook<7>};

/* Tinting calls managed code that can come back through the same setter:
   `active` lets that call through untouched. */
void set_then_tint(ObjectSetter original, bool &active, MonoObject *object,
                   MonoObject *value, void (*tint)(MonoObject *)) {
  if (!original || !object)
    return;
  if (active || !feature_on()) {
    original(object, value);
    return;
  }
  if (object_disposed(object))
    return;
  active = true;
  original(object, value);
  if (!object_disposed(object))
    tint(object);
  active = false;
}

void tint_new_image(MonoObject *object) {
  if (!s_image_filter.setter)
    return;
  if (widget_should_tint(object))
    tint_image(object);
  else
    leave_untinted(object); /* the picture before it may have been an icon */
}

template <int N> void image_source_hook(MonoObject *object, MonoObject *value) {
  if (static_cast<size_t>(N) < s_image_sources.count)
    set_then_tint(s_image_sources.originals[N], s_image_source_active, object,
                  value, &tint_new_image);
}

constexpr ObjectSetter kImageSourceHooks[kMaxImageSourceSetters] = {
    &image_source_hook<0>, &image_source_hook<1>, &image_source_hook<2>,
    &image_source_hook<3>, &image_source_hook<4>, &image_source_hook<5>};

template <int N> void menu_icon_hook(MonoObject *object, MonoObject *value) {
  if (static_cast<size_t>(N) < s_menu_icons.count)
    set_then_tint(s_menu_icons.originals[N], s_menu_icon_active, object, value,
                  &tint_menu_row);
}

constexpr ObjectSetter kMenuIconHooks[kMaxMenuIconSetters] = {
    &menu_icon_hook<0>, &menu_icon_hook<1>, &menu_icon_hook<2>,
    &menu_icon_hook<3>, &menu_icon_hook<4>, &menu_icon_hook<5>};

template <int N>
void button_icon_hook(MonoObject *object, MonoObject *value) {
  if (static_cast<size_t>(N) < s_button_icons.count)
    set_then_tint(s_button_icons.originals[N], s_button_icon_active, object,
                  value, &tint_button_icon);
}

constexpr ObjectSetter kButtonIconHooks[kMaxButtonIconSetters] = {
    &button_icon_hook<0>, &button_icon_hook<1>, &button_icon_hook<2>,
    &button_icon_hook<3>};

void tint_attached_image(MonoObject *object) {
  if (is_image_box_descendant(mono_object_get_class(object)) &&
      widget_should_tint(object))
    tint_image(object);
}

void attach_hook(MonoObject *object, MonoObject *value) {
  set_then_tint(s_attach_original, s_attach_active, object, value,
                &tint_attached_image);
}

template <typename Hooks> bool in_group(const Hooks &hooks, uint64_t address) {
  return std::find(std::begin(hooks.addresses), std::end(hooks.addresses),
                   address) != std::end(hooks.addresses);
}

bool address_hooked(uint64_t address) {
  return in_group(s_buttons, address) || in_group(s_images, address) ||
         in_group(s_image_sources, address) ||
         in_group(s_menu_icons, address) || in_group(s_button_icons, address) ||
         s_attach_address == address;
}

template <typename Setter, size_t Capacity>
void hook_group(const char *kind,
                const std::vector<SetterCandidate> &candidates,
                const Setter (&detours)[Capacity],
                SetterHooks<Setter, Capacity> &hooks) {
  for (const SetterCandidate &candidate : candidates) {
    /* subclasses share an inherited setter: one try per address */
    const bool tried = std::any_of(
        candidates.data(), &candidate, [&](const SetterCandidate &earlier) {
          return earlier.address == candidate.address;
        });
    if (tried || address_hooked(candidate.address))
      continue;
    if (hooks.count >= Capacity) {
      LOG_INFO("[ICONCOLOR] %s hook capacity reached: %zu", kind, Capacity);
      break;
    }
    const size_t index = hooks.count;
    const unsigned long long setter = candidate.address;
    if (!InstallDetour(candidate.address,
                       reinterpret_cast<void *>(detours[index]),
                       reinterpret_cast<void **>(&hooks.originals[index]))) {
      /* already detoured by another hook of ours, as MenuListItem.IconImage
         is by settings_row_icon, which calls MenuIconSet */
      LOG_INFO("[ICONCOLOR] %s setter not hooked type=%s property=%s "
               "setter=0x%llx",
               kind, candidate.type, candidate.property, setter);
      continue;
    }
    hooks.addresses[index] = candidate.address;
    hooks.methods[index] = candidate.setter;
    hooks.count = index + 1;
    LOG_INFO("[ICONCOLOR] hooked %s type=%s property=%s setter=0x%llx", kind,
             candidate.type, candidate.property, setter);
  }
  if (!hooks.count)
    LOG_INFO("[ICONCOLOR] %s hook not installed", kind);
}

} // namespace

void hook_button_setters(const std::vector<SetterCandidate> &candidates) {
  hook_group("button", candidates, kButtonHooks, s_buttons);
}

void hook_image_setters(const std::vector<SetterCandidate> &candidates) {
  hook_group("image", candidates, kImageHooks, s_images);
}

void hook_image_source_setters(const std::vector<SetterCandidate> &candidates) {
  hook_group("image-source", candidates, kImageSourceHooks, s_image_sources);
}

void hook_menu_icon_setters(const std::vector<SetterCandidate> &candidates) {
  hook_group("menu-icon", candidates, kMenuIconHooks, s_menu_icons);
}

void hook_button_icon_setters(const std::vector<SetterCandidate> &candidates) {
  hook_group("button-icon", candidates, kButtonIconHooks, s_button_icons);
}

bool hook_attach_setter(const SetterCandidate &candidate) {
  if (address_hooked(candidate.address) ||
      !InstallDetour(candidate.address, reinterpret_cast<void *>(&attach_hook),
                     reinterpret_cast<void **>(&s_attach_original)))
    return false;
  s_attach_address = candidate.address;
  LOG_INFO("[ICONCOLOR] hooked attach type=%s property=%s setter=0x%llx",
           candidate.type, candidate.property,
           static_cast<unsigned long long>(candidate.address));
  return true;
}

void MenuIconSet(MonoObject *row) {
  if (!row || s_menu_icon_active || !feature_on() || object_disposed(row))
    return;
  s_menu_icon_active = true;
  tint_menu_row(row);
  s_menu_icon_active = false;
}

} // namespace AniketosTheme::IconColor
