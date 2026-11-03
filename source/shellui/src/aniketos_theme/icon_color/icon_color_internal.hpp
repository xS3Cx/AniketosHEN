/* Private to the system icon colour: what the files of icon_color/ share. All
 * of it belongs to the UI thread. */
#pragma once

#include "aniketos_ui/ui_color.hpp"
#include "external_symbols.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace AniketosTheme::IconColor {

using AniketosUI::UIColor;

/* From Mono's metadata; the static bit is the same for fields and methods. */
constexpr uint32_t kStaticAttribute = 0x0010u;
constexpr int kMonoTypeClass = 0x12;

/* ImageBox's FilterColor, what images are tinted through. The class and the
   setter are found together; the getter can be missing. */
struct ImageFilter {
  MonoClass *box_class = nullptr;
  MonoMethod *setter = nullptr;
  MonoMethod *getter = nullptr;
};
extern ImageFilter s_image_filter;

/* A managed setter Install found for setter_hooks.cpp to detour. */
struct SetterCandidate {
  const char *type;
  const char *property;
  MonoMethod *setter;
  uint64_t address;
  MonoMethod *getter;
};

std::string ascii_lower(std::string value);
bool contains(const std::string &value, const char *needle);
std::string type_name(MonoType *type);
const char *class_name(MonoClass *klass);
bool derives_from(MonoClass *klass, MonoClass *ancestor);
MonoProperty *find_property_in_hierarchy(MonoClass *klass, const char *wanted);
/* The parameter of a void method of one parameter, else null. */
MonoType *setter_parameter(MonoMethod *method);
bool uicolor_setter(MonoMethod *method);
bool read_color(MonoMethod *getter, MonoObject *object, UIColor *color);
bool object_disposed(MonoObject *object);
bool object_visible(MonoObject *object);
bool is_image_box_descendant(MonoClass *klass);
/* Pictures with colours of their own: our logo and the rest of our art (the
   logo has its own colour option), and emoji. */
bool keeps_own_colours(MonoObject *object);
/* Icons and everything in Settings, but no artwork, avatars or tiles, and
   nothing that keeps its own colours. */
bool widget_should_tint(MonoObject *object);

bool feature_on();
UIColor current_color();
void restart_rainbow();

void track_object(MonoObject *object, MonoMethod *setter,
                  const UIColor &restore);
/* A hooked setter's call: the colour the system wants, what is restored when
   the option goes off. Our own set_color passes through here and is ignored. */
void note_system_color(MonoObject *object, MonoMethod *setter,
                       const UIColor &color);
bool set_color(MonoMethod *setter, MonoObject *object, UIColor color);
bool tint_image(MonoObject *object);
/* Gives a tracked icon back the system's colour and stops tracking it: it now
   shows a picture that keeps its own colours. */
void leave_untinted(MonoObject *object);

void hook_button_setters(const std::vector<SetterCandidate> &candidates);
void hook_image_setters(const std::vector<SetterCandidate> &candidates);
void hook_image_source_setters(const std::vector<SetterCandidate> &candidates);
void hook_menu_icon_setters(const std::vector<SetterCandidate> &candidates);
void hook_button_icon_setters(const std::vector<SetterCandidate> &candidates);
bool hook_attach_setter(const SetterCandidate &candidate);

void tint_menu_row(MonoObject *row);
/* A button whose IconFilterColor nobody set shows its icon white. */
void tint_button_icon(MonoObject *button);
void scan_menu_icons(MonoObject *row);
void run_scene_scan();

} // namespace AniketosTheme::IconColor
