/* Keyboard & mouse for games: default template, validation, compiler. */

#include <onion/kbm_names.hpp>
#include <onion/kbm_profile.hpp>

#include <string.h>

namespace onion::kbm {
namespace {

static_assert(sizeof(ProfileBlob) <= 0xffff, "ProfileBlob::size is 16-bit");

constexpr uint32_t kAllButtons =
    kPadL3 | kPadR3 | kPadOptions | kPadUp | kPadRight | kPadDown | kPadLeft |
    kPadL2 | kPadR2 | kPadL1 | kPadR1 | kPadTriangle | kPadCircle | kPadCross |
    kPadSquare | kPadTouchpad;

/* Clamp in place; one warning when the value had to move. */
template <typename T>
void fix(T &v, long lo, long hi, const std::string &where, const char *field,
         Warnings *w) {
  const long x = static_cast<long>(v);
  if (x >= lo && x <= hi) return;
  v = static_cast<T>(x < lo ? lo : hi);
  Warn(w, where + ": " + field + " out of range, set to " +
              std::to_string(static_cast<long>(v)));
}

bool valid_input(Input in) { return in != kInputNone && in < kInputCount; }

/* At most max bytes, never splitting a UTF-8 sequence. */
std::string cut_utf8(const std::string &s, size_t max) {
  if (s.size() <= max) return s;
  size_t n = max;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xc0) == 0x80) --n;
  return s.substr(0, n);
}

void cut_field(std::string &s, size_t max, const std::string &what, Warnings *w) {
  const std::string cut = cut_utf8(s, max);
  if (cut == s) return;
  Warn(w, what + " too long, cut to '" + cut + "'");
  s = cut;
}

template <typename V> int find_named(const V &items, const std::string &name) {
  for (size_t i = 0; i < items.size(); ++i)
    if (items[i].name == name) return static_cast<int>(i);
  return -1;
}

/* Empty or repeated names would make references ambiguous: the first holder
 * keeps the name, later ones get "<kind><n>" with n free across all items. */
template <typename V>
void name_items(V &items, const char *kind, size_t max, Warnings *w) {
  for (auto &it : items) cut_field(it.name, max, std::string(kind) + " name", w);
  for (size_t i = 0; i < items.size(); ++i) {
    std::string &n = items[i].name;
    bool taken = n.empty();
    for (size_t j = 0; j < i && !taken; ++j) taken = items[j].name == n;
    if (!taken) continue;
    const std::string old = n;
    for (size_t k = i + 1;; ++k) {
      const std::string candidate = kind + std::to_string(k);
      if (find_named(items, candidate) < 0) {
        n = candidate;
        break;
      }
    }
    if (!old.empty())
      Warn(w, std::string(kind) + " '" + old + "' repeated, renamed '" + n + "'");
  }
}

void fix_mouse(MouseParams &m, const std::string &where, Warnings *w) {
  fix(m.mode, 0, static_cast<long>(MouseMode::Position), where, "mouse mode", w);
  fix(m.stick, 0, 1, where, "mouse stick", w);
  fix(m.deadzone_shape, 0, static_cast<long>(DeadzoneShape::Square), where,
      "deadzone shape", w);
  fix(m.invert, 0, 3, where, "invert", w);
  fix(m.sensitivity_x100, 1, 5000, where, "sensitivity", w);
  fix(m.yx_ratio_x100, 0, 500, where, "Y/X ratio", w); /* 0: X only (mouse steering) */
  fix(m.exponent_x100, 25, 400, where, "exponent", w);
  fix(m.deadzone_x1000, 0, 500, where, "deadzone", w);
  fix(m.outer_x1000, 500, 1000, where, "outer deadzone", w);
  fix(m.smoothing_ms, 4, 100, where, "smoothing", w);
  fix(m.boost_x100, 100, 400, where, "boost", w);
  fix(m.position_range, 50, 20000, where, "position range", w);
  fix(m.spring_per_s_x10, 0, 1000, where, "spring", w);
  m.reserved = 0;
}

void fix_output(OutputDef &o, const std::string &where, Warnings *w) {
  if (!o.raw.empty()) return;
  if (o.kind == OutputKind::AxisDirection)
    fix(o.value, -32767, 32767, where, "value", w);
  else if (o.kind == OutputKind::Trigger || o.kind == OutputKind::StickScale)
    fix(o.value, 0, 32767, where, "value", w);
  fix(o.ramp.rise_ms, 0, 10000, where, "rise", w);
  fix(o.ramp.fall_ms, 0, 10000, where, "fall", w);
  fix(o.ramp.curve_x100, 10, 1000, where, "curve", w);
  o.ramp.reserved = 0;
  /* references follow the names they point at when those were cut */
  o.layer = cut_utf8(o.layer, kMaxLayerNameBytes);
  o.macro = cut_utf8(o.macro, kMaxMacroNameBytes);
}

/* False (with the reason in *why): the binding is broken and dropped. */
bool fix_modifiers(BindingDef &b, std::string *why) {
  Input mods[kMaxModifiers] = {};
  int n = 0;
  for (Input m : b.modifiers) {
    if (m == kInputNone) continue;
    if (!valid_input(m)) return *why = "modifier out of range", false;
    if (m == b.input) return *why = "modifier is the input itself", false;
    bool dup = false;
    for (int i = 0; i < n; ++i) dup = dup || mods[i] == m;
    if (!dup) mods[n++] = m;
  }
  memcpy(b.modifiers, mods, sizeof(mods));
  return true;
}

bool same_trigger(const BindingDef &a, const BindingDef &b) {
  static_assert(kMaxModifiers == 2, "compare as an unordered pair");
  return a.input == b.input && a.gesture == b.gesture &&
         ((a.modifiers[0] == b.modifiers[0] && a.modifiers[1] == b.modifiers[1]) ||
          (a.modifiers[0] == b.modifiers[1] && a.modifiers[1] == b.modifiers[0]));
}

std::string binding_where(const LayerDef &l, const BindingDef &b) {
  const std::string in = InputName(b.input);
  return "layer '" + l.name + "' " + (in.empty() ? std::string("binding") : in);
}

/* The blob output for o, or false (with the reason) when it cannot run. */
bool pack_output(const OutputDef &o, int layer_count,
                 const Profile &p, const std::vector<int> &macro_index, Output *d,
                 const char **why) {
  memset(d, 0, sizeof(*d));
  d->kind = o.kind;
  d->value = o.value;
  d->ramp = o.ramp;
  if (!o.raw.empty()) return *why = "unknown to this HEN", false;
  switch (o.kind) {
  case OutputKind::None:
  case OutputKind::MappingToggle:
    return true;
  case OutputKind::Button:
    d->buttons = o.buttons & kAllButtons;
    return d->buttons ? true : (*why = "no button", false);
  case OutputKind::AxisDirection:
    d->axis = o.axis;
    return o.axis <= kAxisRY ? true : (*why = "bad axis", false);
  case OutputKind::Trigger:
    d->trigger = o.trigger;
    return o.trigger <= kTriggerR2 ? true : (*why = "bad trigger", false);
  case OutputKind::StickScale:
    d->axis = o.axis;
    return o.axis <= 1 ? true : (*why = "bad stick", false);
  case OutputKind::LayerHold:
  case OutputKind::LayerToggle: {
    const int i = find_named(p.layers, o.layer);
    if (i < 0 || i >= layer_count)
      return *why = "no such layer", false;
    d->layer = static_cast<uint8_t>(i);
    return true;
  }
  case OutputKind::Macro: {
    const int i = find_named(p.macros, o.macro);
    if (i < 0 || macro_index[static_cast<size_t>(i)] < 0)
      return *why = "no such macro", false;
    d->macro = static_cast<uint8_t>(macro_index[static_cast<size_t>(i)]);
    return true;
  }
  }
  return *why = "unknown kind", false;
}

} // namespace

bool IsValidSuspendKey(Input key) {
  return key == kInputNone || (key >= kKeyFirst && key <= kKeyLast) ||
         (key >= kMouseMiddle && key <= kMouseButtonLast);
}

LayerDef::LayerDef() : mouse{} {
  mouse.mode = MouseMode::Velocity;
  mouse.stick = 1;
  mouse.deadzone_shape = DeadzoneShape::Circular;
  mouse.invert = 0;
  mouse.sensitivity_x100 = 100;
  mouse.yx_ratio_x100 = 100;
  mouse.exponent_x100 = 100;
  mouse.deadzone_x1000 = 100;
  mouse.outer_x1000 = 1000;
  mouse.smoothing_ms = 12;
  mouse.boost_x100 = 100;
  mouse.position_range = 400;
  mouse.spring_per_s_x10 = 30;
}

Profile DefaultProfile() {
  auto bind = [](const char *input, std::initializer_list<const char *> outs) {
    BindingDef b;
    ParseInput(input, &b.input);
    for (const char *name : outs) {
      OutputDef o;
      ParseOutput(name, &o);
      b.outputs.push_back(o);
    }
    return b;
  };

  Profile p;
  p.name = "Default";
  LayerDef base;
  base.name = "base";
  base.bindings = {
      bind("key:W", {"axis:ly-"}),        bind("key:S", {"axis:ly+"}),
      bind("key:A", {"axis:lx-"}),        bind("key:D", {"axis:lx+"}),
      bind("mouse:left", {"trigger:r2"}), bind("mouse:right", {"trigger:l2", "layer:hold"}),
      bind("key:Space", {"pad:cross"}),   bind("key:C", {"pad:circle"}),
      bind("key:R", {"pad:square"}),      bind("key:E", {"pad:triangle"}),
      bind("key:Q", {"pad:l1"}),          bind("key:G", {"pad:r1"}),
      bind("key:LShift", {"pad:l3"}),     bind("key:V", {"pad:r3"}),
      bind("mouse:middle", {"pad:r3"}),
      bind("key:1", {"pad:up"}),          bind("key:2", {"pad:right"}),
      bind("key:3", {"pad:down"}),        bind("key:4", {"pad:left"}),
      bind("key:Escape", {"pad:options"}), bind("key:Tab", {"pad:touchpad"}),
  };
  for (BindingDef &b : base.bindings)
    for (OutputDef &o : b.outputs)
      if (o.kind == OutputKind::LayerHold) o.layer = "aim";

  LayerDef aim; /* while RMB is held: same keys, slower mouse (absolute) */
  aim.name = "aim";
  aim.mouse.sensitivity_x100 = 50;

  p.layers = {base, aim};
  return p; /* Pause suspends: Settings::suspend_key, not a binding */
}

void Validate(Profile &p, Warnings *w) {
  cut_field(p.name, kMaxProfileNameBytes, "profile name", w);
  fix(p.trigger_button_threshold, 0, 255, "profile", "trigger threshold", w);

  if (p.layers.empty()) {
    p.layers.emplace_back();
    p.layers.back().name = "base";
    Warn(w, "profile had no layers, added 'base'");
  }
  name_items(p.layers, "layer", kMaxLayerNameBytes, w);
  name_items(p.macros, "macro", kMaxMacroNameBytes, w);

  for (MacroDef &m : p.macros) {
    for (MacroStep &s : m.steps) {
      s.buttons &= kAllButtons; /* axes: every int16 is valid, INT16_MIN = untouched */
      fix(s.duration_ms, 16, 10000, "macro '" + m.name + "'", "step duration", w);
    }
  }

  for (size_t li = 0; li < p.layers.size(); ++li) {
    LayerDef &l = p.layers[li];
    const std::string lwhere = "layer '" + l.name + "'";
    if (li == 0 && l.mouse_inherit) {
      l.mouse_inherit = false; /* nothing below the base layer */
      Warn(w, lwhere + ": base layer cannot inherit the mouse");
    }
    fix_mouse(l.mouse, lwhere, w);
    fix(l.opposing, 0, static_cast<long>(Opposing::FirstWins), lwhere, "opposing keys", w);

    for (size_t bi = 0; bi < l.bindings.size();) {
      BindingDef &b = l.bindings[bi];
      if (!b.raw.empty()) { /* a newer HEN's binding: keep untouched */
        ++bi;
        continue;
      }
      const std::string where = binding_where(l, b);
      std::string why;
      if (!fix_modifiers(b, &why)) {
        Warn(w, where + ": " + why + ", binding dropped");
        l.bindings.erase(l.bindings.begin() + static_cast<long>(bi));
        continue;
      }
      cut_field(b.label, kMaxLabelBytes, where + " label", w);
      fix(b.gesture, 0, static_cast<long>(Gesture::Release), where, "gesture", w);
      fix(b.mode, 0, static_cast<long>(Mode::Pulse), where, "mode", w);
      fix(b.turbo_hz, 1, 30, where, "turbo rate", w);
      fix(b.turbo_duty, 10, 90, where, "turbo duty", w);
      fix(b.tap_ms, 50, 2000, where, "tap time", w);
      fix(b.long_ms, 100, 5000, where, "long-press time", w);
      fix(b.double_ms, 100, 2000, where, "double-tap time", w);
      fix(b.pulse_ms, 16, 2000, where, "pulse time", w);
      for (OutputDef &o : b.outputs) fix_output(o, where, w);
      ++bi;
    }
  }
}

bool Compile(const Profile &in, ProfileBlob *out, Input suspend_key, Warnings *w) {
  if (!out) return false;
  Profile p = in;
  Validate(p, w);

  memset(out, 0, sizeof(*out));
  out->magic = kBlobMagic;
  out->version = kBlobVersion;
  out->size = sizeof(ProfileBlob);
  out->enabled = p.enabled;
  out->suspend_key = suspend_key;
  out->trigger_button_threshold = p.trigger_button_threshold;
  out->merge_real_pad = p.merge_real_pad;

  if (p.layers.size() > static_cast<size_t>(kMaxLayers))
    Warn(w, std::to_string(p.layers.size() - kMaxLayers) + " layers over the limit of " +
                std::to_string(kMaxLayers) + " skipped");
  const int layer_count =
      static_cast<int>(p.layers.size() < kMaxLayers ? p.layers.size() : kMaxLayers);
  out->layer_count = static_cast<uint8_t>(layer_count);
  for (int li = 0; li < layer_count; ++li) {
    const LayerDef &l = p.layers[static_cast<size_t>(li)];
    Layer &dst = out->layers[li];
    memcpy(dst.name, l.name.data(), l.name.size()); /* Validate capped the length */
    dst.mouse = l.mouse;
    dst.mouse_inherit = l.mouse_inherit;
    dst.opposing = l.opposing;
    dst.mouse_digital_threshold = l.mouse_digital_threshold;
  }

  /* compiled macro index per p.macros entry, -1: skipped */
  std::vector<int> macro_index(p.macros.size(), -1);
  for (size_t mi = 0; mi < p.macros.size(); ++mi) {
    const MacroDef &m = p.macros[mi];
    const std::string where = "macro '" + m.name + "'";
    if (m.steps.empty()) {
      Warn(w, where + ": no steps, skipped");
      continue;
    }
    if (out->macro_count == kMaxMacros ||
        out->macro_step_count + m.steps.size() > static_cast<size_t>(kMaxMacroSteps)) {
      Warn(w, where + ": over the limit of " + std::to_string(kMaxMacros) + " macros / " +
                  std::to_string(kMaxMacroSteps) + " steps, skipped");
      continue;
    }
    Macro &dst = out->macros[out->macro_count];
    dst.first_step = out->macro_step_count;
    dst.step_count = static_cast<uint16_t>(m.steps.size());
    dst.repeat_while_held = m.repeat_while_held;
    memcpy(&out->macro_steps[out->macro_step_count], m.steps.data(),
           m.steps.size() * sizeof(MacroStep));
    out->macro_step_count = static_cast<uint16_t>(out->macro_step_count + m.steps.size());
    macro_index[mi] = out->macro_count++;
  }

  for (int li = 0; li < layer_count; ++li) {
    const LayerDef &l = p.layers[static_cast<size_t>(li)];
    for (size_t bi = 0; bi < l.bindings.size(); ++bi) {
      const BindingDef &b = l.bindings[bi];
      const std::string where = binding_where(l, b);
      if (!b.raw.empty()) {
        Warn(w, where + ": unknown to this HEN, skipped");
        continue;
      }
      if (!valid_input(b.input)) {
        Warn(w, where + ": no input yet, skipped");
        continue;
      }
      if (suspend_key != kInputNone &&
          (b.input == suspend_key || b.modifiers[0] == suspend_key ||
           b.modifiers[1] == suspend_key)) {
        Warn(w, where + ": uses the suspend key, skipped");
        continue;
      }
      bool shadowed = false; /* the later of two identical bindings wins */
      for (size_t j = bi + 1; j < l.bindings.size() && !shadowed; ++j)
        shadowed = l.bindings[j].raw.empty() && same_trigger(b, l.bindings[j]);
      if (shadowed) {
        Warn(w, where + ": bound twice, first skipped");
        continue;
      }

      Binding dst{};
      dst.input = b.input;
      memcpy(dst.modifiers, b.modifiers, sizeof(dst.modifiers));
      dst.layer = static_cast<uint8_t>(li);
      dst.gesture = b.gesture;
      dst.mode = b.mode;
      dst.turbo_hz = b.turbo_hz;
      dst.turbo_duty = b.turbo_duty;
      dst.tap_ms = b.tap_ms;
      dst.long_ms = b.long_ms;
      dst.double_ms = b.double_ms;
      dst.pulse_ms = b.pulse_ms;
      for (const OutputDef &o : b.outputs) {
        if (dst.output_count == kMaxOutputsPerBinding) {
          Warn(w, where + ": more than " + std::to_string(kMaxOutputsPerBinding) +
                      " outputs, rest skipped");
          break;
        }
        const char *why = "";
        if (pack_output(o, layer_count, p, macro_index,
                        &dst.outputs[dst.output_count], &why))
          ++dst.output_count;
        else
          Warn(w, where + ": output " + OutputName(o) + " " + why + ", skipped");
      }
      if (dst.output_count == 0) {
        Warn(w, where + ": no outputs, skipped");
        continue;
      }
      if (out->binding_count == kMaxBindings) {
        Warn(w, where + ": over the limit of " + std::to_string(kMaxBindings) +
                    " bindings, skipped");
        continue;
      }
      out->bindings[out->binding_count++] = dst;
    }
  }
  return true;
}

} // namespace onion::kbm
