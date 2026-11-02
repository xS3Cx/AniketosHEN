/* Copyright (C) 2026 AniketosHEN
 *
 * What the Keyboard and Mouse files share. kbm_toolbox.hpp holds the grammar of
 * paths and what the rest of ShellUI calls.
 */
#pragma once

#include "aniketos_shell/toolbox/kbm_toolbox.hpp"
#include "toolbox_i18n.hpp"

#include <onion/kbm_profile.hpp>
#include <onion/kbm_store.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kbm_toolbox {
namespace kb = onion::kbm;
using toolbox_i18n::format;
using toolbox_i18n::tr;

inline constexpr const char *kRoot = kb::kStoreRootUser;

struct Path {
  std::string title; /* title id, "default", "help", or "all" for the main page */
  int layer = -1, binding = -1, output = -1, macro = -1, step = -1;
  std::string word; /* resource: page name; element id: field */
};

/* kbm_toolbox_path.cpp */

/* "<T>[-l0][-b1]...[-word]": the text of a path, without any prefix. */
bool parse_tokens(std::string_view text, Path *out);
std::string path_text(const Path &p);
/* "id_kbm-<path>-<field>" → path + field. False for anything else,
   including list items ("<list id>.<value>"). */
bool parse_id(std::string_view id, Path *out);
std::string element_id(const Path &p, const char *field);
std::string resource(const Path &p, const char *page = nullptr);
/* p with one more index (letter as in the grammar) and no word. */
Path with(Path p, char letter, int v);
Path field(Path p, const char *f);

/* kbm_toolbox_profile.cpp */

struct Edit {
  std::string title;
  bool own = false; /* false: the title runs on the default profile */
  kb::Profile profile;
};
extern Edit s_edit;
extern Path s_deleted; /* title empty: nothing deleted */

void load(const std::string &title);
bool under_deleted(const Path &p);
/* Every index in the path points at something that exists. */
bool in_range(const Path &p);

inline kb::LayerDef &layer(const Path &p) { return s_edit.profile.layers[p.layer]; }
inline kb::BindingDef &binding(const Path &p) { return layer(p).bindings[p.binding]; }
inline kb::OutputDef &output(const Path &p) { return binding(p).outputs[p.output]; }
inline kb::MacroDef &macro(const Path &p) { return s_edit.profile.macros[p.macro]; }
inline kb::MacroStep &step(const Path &p) { return macro(p).steps[p.step]; }

bool get_value(const Path &p, std::string &out);
/* Changes the profile being edited (or the global settings for "all");
   false when the path is gone or the value is refused. */
bool set_value(const Path &p, const std::string &value);
/* Saves the edited profile and asks the daemon to apply it. */
bool save(const Path &p);

/* Structure edits on the profile being edited; each one saves. */
bool create_profile(const Path &p, bool from_template);
bool delete_profile(const Path &p);
/* what: layer, bind, macro, step, or an output kind (button, axis, trigger,
   layer, macro, scale, suspend, block). *created: the new item's path. */
bool add_item(const Path &p, const std::string &what, Path *created);
/* Deletes what the path points at; returns how many pages to close (0:
   nothing deleted). Pages under it show "deleted" until another opens. */
int delete_item(const Path &p);
bool clear_modifiers(const Path &p);
enum class SetInput { Ok, Gone, Reserved, Failed };
/* A binding's input and modifiers, or the pause key for title "all". */
SetInput set_input(const Path &p, kb::Input input, const kb::Input *modifiers);

/* kbm_toolbox_labels.cpp */

struct PadName {
  uint32_t mask;
  const char *field;
  const char *key;     /* translated */
  const char *literal; /* or as printed on the pad */
};
inline constexpr PadName kPadNames[] = {
    {kb::kPadCross, "pbcross", "kbm.pad.cross", nullptr},
    {kb::kPadCircle, "pbcircle", "kbm.pad.circle", nullptr},
    {kb::kPadSquare, "pbsquare", "kbm.pad.square", nullptr},
    {kb::kPadTriangle, "pbtriangle", "kbm.pad.triangle", nullptr},
    {kb::kPadL1, "pbl1", nullptr, "L1"},
    {kb::kPadR1, "pbr1", nullptr, "R1"},
    {kb::kPadL2, "pbl2", nullptr, "L2"},
    {kb::kPadR2, "pbr2", nullptr, "R2"},
    {kb::kPadL3, "pbl3", nullptr, "L3"},
    {kb::kPadR3, "pbr3", nullptr, "R3"},
    {kb::kPadUp, "pbup", "kbm.pad.up", nullptr},
    {kb::kPadDown, "pbdown", "kbm.pad.down", nullptr},
    {kb::kPadLeft, "pbleft", "kbm.pad.left", nullptr},
    {kb::kPadRight, "pbright", "kbm.pad.right", nullptr},
    {kb::kPadOptions, "pboptions", "kbm.pad.options", nullptr},
    {kb::kPadTouchpad, "pbtouchpad", "kbm.pad.touchpad", nullptr},
};

/* Axis directions as the editor offers them: d/2 picks the axis, odd d is
   the positive way (right, down). */
struct Direction {
  uint8_t axis;
  bool positive;
  const char *key;
};
inline constexpr Direction kDirections[] = {
    {kb::kAxisLY, false, "kbm.dir.ls_up"},   {kb::kAxisLY, true, "kbm.dir.ls_down"},
    {kb::kAxisLX, false, "kbm.dir.ls_left"}, {kb::kAxisLX, true, "kbm.dir.ls_right"},
    {kb::kAxisRY, false, "kbm.dir.rs_up"},   {kb::kAxisRY, true, "kbm.dir.rs_down"},
    {kb::kAxisRX, false, "kbm.dir.rs_left"}, {kb::kAxisRX, true, "kbm.dir.rs_right"},
};

inline constexpr const char *kGestureKeys[] = {"kbm.gesture.press", "kbm.gesture.tap",
                                        "kbm.gesture.long", "kbm.gesture.double",
                                        "kbm.gesture.release"};
inline constexpr const char *kModeKeys[] = {"kbm.mode.hold", "kbm.mode.toggle",
                                     "kbm.mode.turbo", "kbm.mode.pulse"};
inline constexpr const char *kMouseModeKeys[] = {"kbm.mouse.mode.off",
                                          "kbm.mouse.mode.velocity",
                                          "kbm.mouse.mode.position"};
inline constexpr const char *kOpposingKeys[] = {"kbm.socd.last", "kbm.socd.neutral",
                                         "kbm.socd.first"};

template <size_t N> const char *pick(const char *const (&keys)[N], long i) {
  return tr(keys[i >= 0 && static_cast<size_t>(i) < N ? i : 0]);
}

std::string pad_name(const PadName &n);
int direction_of(const kb::OutputDef &o);
int pct(long v, long full);

std::string fmt_ms(long v);
std::string fmt_pct(long v);
std::string fmt_permille(long v);
std::string fmt_x100(long v);
std::string fmt_counts(long v);
std::string fmt_hz(long v);
std::string fmt_spring(long v);

std::string input_name(kb::Input input);
std::string layer_name(int i);
std::string output_summary(const kb::OutputDef &o);
const char *kind_key(kb::OutputKind k);
std::string chord_name(const kb::BindingDef &b);
/* "Left Shift + W → Left stick up, Cross" */
std::string binding_title(const kb::BindingDef &b);
std::string gesture_summary(const kb::BindingDef &b);
std::string mode_summary(const kb::BindingDef &b);
std::string mouse_summary(const kb::LayerDef &l);
std::string step_summary(const kb::MacroStep &s);

/* kbm_toolbox_games.cpp */

struct Game {
  std::string id, name;
};
extern std::vector<Game> s_games;
void scan_games();
std::string game_name(const std::string &title);

} // namespace kbm_toolbox
