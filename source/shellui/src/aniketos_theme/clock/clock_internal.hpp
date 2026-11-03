/* Private to the clock: what shell_clock.cpp, clock_label.cpp and
 * clock_format.cpp share. All of it belongs to the UI thread. */
#pragma once

#include "aniketos_theme/clock/shell_clock.hpp"
#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/mono_property_cache.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <string>

/* Per-frame code: resolve each managed property once and reuse the compiled
 * accessor (see aniketos_ui/mono_property_cache.hpp) instead of looking it up by
 * name on every call. Same call syntax and semantics as the hooked_funcs.hpp
 * helpers, so that header has to come first. */
#define Get_Property AniketosUI::MonoPropertyCache::Get
#define Set_Property AniketosUI::MonoPropertyCache::Set
#define Set_Property_Invoke AniketosUI::MonoPropertyCache::SetInvoke

namespace ShellClock {

namespace HomeScene = AniketosUI::HomeScene;
namespace Reflect = AniketosUI::Reflect;
using AniketosUI::MonotonicMs;

constexpr uint64_t kAnimationFrameMs = 50;
/* A clock label that sits behind another scene (Home behind Settings or the
 * Control Center) is not hidden outright: PUI parks it at a FinalOpacity of
 * about 0.02. Anything at or below this counts as not shown, so the date does
 * not stay painted over other screens and a clock that really is on screen
 * can take over from one that is only parked. */
constexpr float kShownOpacity = 0.05f;
constexpr size_t kMaxNativeClockAliases = 12;

/* The label being worked on: the clock we follow, or for the length of
   update_clock_alias a second clock. */
struct NativeClockState {
  std::string stock_time; /* survives release_clock: it gives the hour format */
  float base_x = 0.0f;
  float base_width = 0.0f;
  float base_height = 0.0f;
  float visible_width = 0.0f;
  float center_x = 0.0f;
  float bottom_y = 0.0f;
  float opacity = 0.0f;
  bool customized = false;
  bool visible = false;
  bool has_global_geometry = false;
};

// ShellUI can keep more than one native time label alive while switching
// between Home and system overlays. Keep each label's stock layout so a
// newly active clock never forces the previous visible one back to white.
struct NativeClockAlias {
  uint32_t handle = 0;
  float base_x = 0.0f;
  float base_width = 0.0f;
  float base_height = 0.0f;
  std::string stock_time;
  bool customized = false;
  uint32_t stock_color = 0; /* pinned box of the label's own TextColor */
};

extern NativeClockState s_native;
extern std::array<NativeClockAlias, kMaxNativeClockAliases> s_clock_aliases;
extern DateOverlayState s_date_overlay;
extern std::string s_last_rendered;
extern time_t s_last_second;
extern bool s_force_refresh;
/* A game or app has the screen; sampled once a second, read by the script's
   settings (ScriptConfig) from Home's module thread. */
extern std::atomic<bool> s_game_focused;
extern float s_measured_time_width;
extern uint64_t s_animation_started_ms;
/* The original setter: what we write must not come back through the hook. */
extern void (*s_native_label_set_text_orig)(MonoObject *, MonoString *);

inline bool local_time(time_t now, std::tm &local) {
  return localtime_r(&now, &local) != nullptr;
}

/* clock_format.cpp */
void sync_clock_language(time_t now);
uint64_t settings_signature();
std::string format_time(const std::tm &local);
std::string format_date(const std::tm &local);

/* clock_label.cpp */
bool clock_animation_active();
MonoClass *property_class(MonoObject *object, const char *name);
float label_dimension(MonoObject *label, const char *property);
void prepare_space_tab_label(MonoObject *label, const std::string &stock);
bool looks_like_stock_clock(MonoObject *label, const char *text);
bool custom_clock_on();
bool custom_clock_drawn_for(MonoObject *label);
bool custom_clock_drawn_here();
bool plausible_native_clock_geometry(MonoObject *label);
void note_unbound_clock(MonoObject *label, const std::string &stock);
int find_clock_alias(MonoObject *label);
int remember_clock_alias(MonoObject *label, const std::string &stock);
void forget_clock_alias(MonoObject *label);
void load_clock_alias(const NativeClockAlias &alias);
MonoString *ui_string(const std::string &text);
void write_label_text(MonoObject *label, MonoString *text);
bool write_label_text(MonoObject *label, const std::string &text);
MonoString *label_text(MonoObject *raw);
std::string raw_text(MonoObject *raw);
float native_clock_opacity(MonoObject *label);
void publish_date_overlay(time_t now, bool target_ready);
void apply_custom_time_color(MonoObject *label);
void restore_all_native_labels();
void layout_native_clock(MonoObject *label, const std::string &time_text);
/* Layout plus how visible the label now is. */
void dress_native_clock(MonoObject *label, const std::string &time_text);
void adopt_native_geometry(MonoObject *label, NativeClockAlias &alias,
                           bool active);
bool update_clock_alias(MonoObject *label, size_t alias_index, time_t now);
bool restyled_by_react(MonoObject *target);
void let_go_of_label(MonoObject *label, const std::string &text);
bool has_clock_test_id(MonoObject *label);
bool drawn_by_script(MonoObject *label);

/* shell_clock.cpp */
MonoObject *clock_target();
void release_clock();

} // namespace ShellClock
