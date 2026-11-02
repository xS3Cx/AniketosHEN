/* Copyright (C) 2026 AniketosHEN
 *
 * What the Keyboard and Mouse pages call things: pad buttons, units, inputs
 * and one-line summaries of bindings, outputs, layers and steps.
 */
#include "kbm_toolbox_internal.hpp"

#include <onion/kbm_names.hpp>

#include <cstdio>
#include <cstdlib>

namespace kbm_toolbox {
namespace {

/* between a binding's input and its outputs in rows */
constexpr const char *kArrow = " → ";

std::string buttons_name(uint32_t mask) {
  std::string out;
  for (const PadName &n : kPadNames)
    if (mask & n.mask)
      out += (out.empty() ? "" : " + ") + pad_name(n);
  return out;
}

/* A name, with how far it goes when that is short of full. */
std::string with_strength(std::string name, const kb::OutputDef &o) {
  const int p = pct(o.value, 32767);
  return p < 100 ? name + " " + fmt_pct(p) : name;
}

} // namespace

std::string pad_name(const PadName &n) { return n.key ? tr(n.key) : n.literal; }

int direction_of(const kb::OutputDef &o) {
  for (int d = 0; d < 8; ++d)
    if (kDirections[d].axis == o.axis && kDirections[d].positive == (o.value > 0))
      return d;
  return 0;
}

int pct(long v, long full) { return static_cast<int>((std::labs(v) * 100 + full / 2) / full); }

std::string fmt_ms(long v) { return format("kbm.unit.ms", static_cast<int>(v)); }
std::string fmt_pct(long v) { return format("kbm.unit.pct", static_cast<int>(v)); }
std::string fmt_permille(long v) { return format("kbm.unit.pct", static_cast<int>(v / 10)); }
std::string fmt_x100(long v) {
  char text[16];
  std::snprintf(text, sizeof(text), "%ld.%02ld", v / 100, v % 100);
  return text;
}
std::string fmt_counts(long v) { return format("kbm.unit.counts", static_cast<int>(v)); }
std::string fmt_hz(long v) { return format("kbm.unit.hz", static_cast<int>(v)); }
std::string fmt_spring(long v) {
  if (v == 0)
    return tr("kbm.mouse.mode.off");
  char text[16];
  std::snprintf(text, sizeof(text), "%ld.%ld", v / 10, v % 10);
  return format("kbm.unit.per_s", text);
}

std::string layer_name(int i) {
  return i == 0 ? tr("kbm.layer.base") : s_edit.profile.layers[i].name;
}

std::string output_summary(const kb::OutputDef &o) {
  switch (o.kind) {
  case kb::OutputKind::None:
    return tr("kbm.sum.block");
  case kb::OutputKind::Button:
    return o.buttons ? buttons_name(o.buttons) : tr("kbm.sum.no_buttons");
  case kb::OutputKind::AxisDirection:
    return with_strength(tr(kDirections[direction_of(o)].key), o);
  case kb::OutputKind::Trigger:
    return with_strength(o.trigger == kb::kTriggerL2 ? "L2" : "R2", o);
  case kb::OutputKind::LayerHold:
    return format("kbm.sum.layer_hold", o.layer.c_str());
  case kb::OutputKind::LayerToggle:
    return format("kbm.sum.layer_toggle", o.layer.c_str());
  case kb::OutputKind::Macro:
    return format("kbm.sum.macro", o.macro.c_str());
  case kb::OutputKind::StickScale:
    return format("kbm.sum.scale",
                  tr(o.axis ? "kbm.stick.right" : "kbm.stick.left"),
                  pct(o.value, 32767));
  case kb::OutputKind::MappingToggle:
    return tr("kbm.kind.suspend");
  }
  return {};
}

const char *kind_key(kb::OutputKind k) {
  switch (k) {
  case kb::OutputKind::None: return "kbm.kind.block";
  case kb::OutputKind::Button: return "kbm.kind.button";
  case kb::OutputKind::AxisDirection: return "kbm.kind.axis";
  case kb::OutputKind::Trigger: return "kbm.kind.trigger";
  case kb::OutputKind::LayerHold:
  case kb::OutputKind::LayerToggle: return "kbm.kind.layer";
  case kb::OutputKind::Macro: return "kbm.kind.macro";
  case kb::OutputKind::StickScale: return "kbm.kind.scale";
  case kb::OutputKind::MappingToggle: return "kbm.kind.suspend";
  }
  return "kbm.kind.block";
}

std::string chord_name(const kb::BindingDef &b) {
  std::string s;
  for (kb::Input m : b.modifiers)
    if (m != kb::kInputNone)
      s += input_name(m) + " + ";
  return s + (b.input == kb::kInputNone ? tr("kbm.input.none") : input_name(b.input));
}

std::string binding_title(const kb::BindingDef &b) {
  std::string outs;
  for (const kb::OutputDef &o : b.outputs)
    outs += (outs.empty() ? "" : ", ") + output_summary(o);
  return chord_name(b) + kArrow + (outs.empty() ? tr("kbm.sum.nothing") : outs);
}

std::string gesture_summary(const kb::BindingDef &b) {
  std::string s = pick(kGestureKeys, static_cast<long>(b.gesture));
  switch (b.gesture) {
  case kb::Gesture::Tap: return s + " · " + fmt_ms(b.tap_ms);
  case kb::Gesture::LongPress: return s + " · " + fmt_ms(b.long_ms);
  case kb::Gesture::DoubleTap: return s + " · " + fmt_ms(b.double_ms);
  default: return s;
  }
}

std::string mode_summary(const kb::BindingDef &b) {
  std::string s = pick(kModeKeys, static_cast<long>(b.mode));
  if (b.mode == kb::Mode::Turbo)
    return s + " · " + fmt_hz(b.turbo_hz);
  if (b.mode == kb::Mode::Pulse)
    return s + " · " + fmt_ms(b.pulse_ms);
  return s;
}

std::string mouse_summary(const kb::LayerDef &l) {
  if (&l != &s_edit.profile.layers[0] && l.mouse_inherit)
    return tr("kbm.mouse.inherit");
  if (l.mouse.mode == kb::MouseMode::Off)
    return tr("kbm.mouse.mode.off");
  return std::string(tr(l.mouse.stick ? "kbm.stick.right" : "kbm.stick.left")) +
         " · " + pick(kMouseModeKeys, static_cast<long>(l.mouse.mode)) + " · " +
         fmt_x100(l.mouse.sensitivity_x100);
}

std::string step_summary(const kb::MacroStep &s) {
  std::string out = buttons_name(s.buttons);
  auto add = [&](const std::string &part) {
    out += (out.empty() ? "" : " + ") + part;
  };
  for (int a = 0; a < 4; ++a)
    if (s.axes[a] != INT16_MIN) {
      add(tr(a < 2 ? "kbm.stick.left" : "kbm.stick.right"));
      a |= 1; /* one name per stick */
    }
  if (s.l2)
    add("L2");
  if (s.r2)
    add("R2");
  return out.empty() ? tr("kbm.step.wait") : out;
}

std::string input_name(kb::Input in) {
  static constexpr const char *kMouse[] = {"kbm.in.mouse_left", "kbm.in.mouse_right",
                                           "kbm.in.mouse_middle", "kbm.in.mouse_back",
                                           "kbm.in.mouse_forward"};
  static constexpr const char *kWheel[] = {"kbm.in.wheel_up", "kbm.in.wheel_down",
                                           "kbm.in.wheel_left", "kbm.in.wheel_right"};
  static constexpr const char *kMove[] = {"kbm.in.move_right", "kbm.in.move_left",
                                          "kbm.in.move_down", "kbm.in.move_up"};
  if (in == kb::kInputNone)
    return tr("kbm.input.none");
  if (in >= kb::kMouseLeft && in <= kb::kMouseForward)
    return tr(kMouse[in - kb::kMouseLeft]);
  if (in > kb::kMouseForward && in <= kb::kMouseButtonLast)
    return format("kbm.in.mouse_n", in - kb::kMouseButtonFirst + 1);
  if (in >= kb::kWheelUp && in <= kb::kWheelRight)
    return tr(kWheel[in - kb::kWheelUp]);
  if (in >= kb::kMouseMoveRight && in <= kb::kMouseMoveUp)
    return tr(kMove[in - kb::kMouseMoveRight]);
  const std::string name = kb::InputDisplayName(in);
  return name.empty() ? tr("kbm.input.none") : name;
}

} // namespace kbm_toolbox
