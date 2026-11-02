/* Keyboard & mouse for games: the editable profile.
 *
 * ShellUI edits a Profile, the store saves it as JSON, the daemon compiles it
 * into the flat ProfileBlob the in-game engine runs. Layers and macros are
 * referred to by name here, so deleting or reordering one never leaves an
 * output pointing at the wrong thing; Compile() turns names into indices.
 *
 * Not for the game process: this uses std::vector / std::string. */
#pragma once

#include <onion/kbm_types.hpp>

#include <stddef.h>

#include <string>
#include <vector>

namespace onion::kbm {

constexpr int kProfileSchema = 1;        /* JSON "schema"; newer files are refused */
constexpr Input kDefaultSuspendKey = 0x48; /* Pause */

using Warnings = std::vector<std::string>;

inline void Warn(Warnings *warnings, const std::string &message) {
  if (warnings) warnings->push_back(message);
}

/* Byte caps (cut at a UTF-8 character boundary). They keep a full-size
 * profile well under kMaxProfileFileSize. */
constexpr size_t kMaxProfileNameBytes = 48;
constexpr size_t kMaxLabelBytes = 48;
constexpr size_t kMaxLayerNameBytes = sizeof(Layer::name) - 1;
constexpr size_t kMaxMacroNameBytes = 32;

/* Keys and mouse buttons from middle on; "none" (kInputNone) turns the
 * suspend key off. Left/right click would suspend on every shot. */
bool IsValidSuspendKey(Input key);

struct OutputDef {
  OutputKind kind = OutputKind::None;
  uint32_t buttons = 0; /* Button: PadButton mask */
  uint8_t axis = 0;     /* AxisDirection: Axis; StickScale: 0 left, 1 right */
  uint8_t trigger = 0;  /* Trigger: Trigger */
  int16_t value = 0;    /* AxisDirection -32767..32767, Trigger/StickScale 0..32767 */
  Ramp ramp{0, 0, 100, 0};
  std::string layer; /* LayerHold / LayerToggle: LayerDef::name */
  std::string macro; /* Macro: MacroDef::name */
  /* Non-empty: an output this HEN does not know (newer file). Kept as JSON
   * text and written back unchanged; never compiled. */
  std::string raw;
};

struct BindingDef {
  Input input = kInputNone;
  Input modifiers[kMaxModifiers] = {};
  Gesture gesture = Gesture::Press;
  Mode mode = Mode::Hold;
  uint8_t turbo_hz = 10;
  uint8_t turbo_duty = 50;
  uint16_t tap_ms = 200;
  uint16_t long_ms = 400;
  uint16_t double_ms = 250;
  uint16_t pulse_ms = 60;
  std::vector<OutputDef> outputs;
  std::string label; /* free text shown in the editor */
  /* Non-empty: input, modifier, gesture or mode this HEN does not know.
   * The whole binding is kept as JSON text and never compiled. */
  std::string raw;
};

struct LayerDef {
  LayerDef();

  std::string name;
  bool mouse_inherit = false;
  /* Absolute, not relative to the layer below: the blob has no relative
   * sensitivity, so changing the base layer does not change "aim". */
  MouseParams mouse;
  Opposing opposing = Opposing::LastWins;
  uint16_t mouse_digital_threshold = 2000;
  std::vector<BindingDef> bindings;
};

struct MacroDef {
  std::string name;
  bool repeat_while_held = false;
  std::vector<MacroStep> steps; /* axes INT16_MIN: untouched */
};

struct Profile {
  std::string name;
  bool enabled = true;
  bool merge_real_pad = true;
  uint16_t trigger_button_threshold = 30;
  std::vector<LayerDef> layers; /* [0] is the base layer */
  std::vector<MacroDef> macros;
};

/* The one generic template ("create from default"). No per-game presets. */
Profile DefaultProfile();

/* Clamp numbers, cap names and labels, make layer/macro names unique.
 * Drops only broken bindings (a modifier that is out of range or equals the
 * input). Half-built items the editor saves (no input, no outputs, an
 * output without its layer/macro) are kept so editing can continue. */
void Validate(Profile &profile, Warnings *warnings = nullptr);

/* Validate a copy and pack it, skipping with a warning whatever cannot run:
 * incomplete or unknown (raw) items, bindings that use suspend_key as input
 * or modifier, the earlier of two identical bindings, and anything past the
 * blob's capacity. False only if `out` is null. */
bool Compile(const Profile &profile, ProfileBlob *out,
             Input suspend_key = kDefaultSuspendKey,
             Warnings *warnings = nullptr);

} // namespace onion::kbm
