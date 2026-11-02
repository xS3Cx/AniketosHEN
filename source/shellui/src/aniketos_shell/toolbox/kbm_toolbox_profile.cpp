/* Copyright (C) 2026 AniketosHEN
 *
 * The Keyboard and Mouse profile being edited: reading and writing its values,
 * adding and deleting items, saving it and telling the daemon.
 */
#include "kbm_toolbox_internal.hpp"

#include "hooked_funcs.hpp"

#include <msg.hpp>
#include <onion/ipc_client.hpp>
#include <onion/platform.h>

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace kbm_toolbox {

Edit s_edit;
Path s_deleted;

namespace {

constexpr const char *kStepAxes[] = {"lx", "ly", "rx", "ry"};

std::string file_for(const std::string &title) {
  return title == "default" ? kb::DefaultProfilePath(kRoot)
                            : kb::TitleProfilePath(kRoot, title);
}

template <typename V> bool in_bounds(const V &v, int i) {
  return i < 0 || static_cast<size_t>(i) < v.size();
}

const PadName *pad_field(const std::string &f) {
  for (const PadName &n : kPadNames)
    if (f == n.field)
      return &n;
  return nullptr;
}

/* Every plain number or enum, for reading and writing alike. */
template <typename F> bool number(const Path &p, F &&fn) {
  const std::string &f = p.word;
  if (p.step >= 0) {
    if (f == "dur") return fn(step(p).duration_ms), true;
    return false;
  }
  if (p.macro >= 0) {
    if (f == "repeat") return fn(macro(p).repeat_while_held), true;
    return false;
  }
  if (p.output >= 0) {
    kb::OutputDef &o = output(p);
    if (f == "trg") return fn(o.trigger), true;
    if (f == "stick") return fn(o.axis), true;
    if (f == "rise") return fn(o.ramp.rise_ms), true;
    if (f == "fall") return fn(o.ramp.fall_ms), true;
    if (f == "rcurve") return fn(o.ramp.curve_x100), true;
    return false;
  }
  if (p.binding >= 0) {
    kb::BindingDef &b = binding(p);
    if (f == "gesture") return fn(b.gesture), true;
    if (f == "mode") return fn(b.mode), true;
    if (f == "hz") return fn(b.turbo_hz), true;
    if (f == "duty") return fn(b.turbo_duty), true;
    if (f == "tap") return fn(b.tap_ms), true;
    if (f == "long") return fn(b.long_ms), true;
    if (f == "double") return fn(b.double_ms), true;
    if (f == "pulse") return fn(b.pulse_ms), true;
    return false;
  }
  if (p.layer >= 0) {
    kb::LayerDef &l = layer(p);
    kb::MouseParams &m = l.mouse;
    if (f == "inherit") return fn(l.mouse_inherit), true;
    if (f == "socd") return fn(l.opposing), true;
    if (f == "move") return fn(l.mouse_digital_threshold), true;
    if (f == "mmode") return fn(m.mode), true;
    if (f == "mstick") return fn(m.stick), true;
    if (f == "sens") return fn(m.sensitivity_x100), true;
    if (f == "yx") return fn(m.yx_ratio_x100), true;
    if (f == "expo") return fn(m.exponent_x100), true;
    if (f == "dz") return fn(m.deadzone_x1000), true;
    if (f == "shape") return fn(m.deadzone_shape), true;
    if (f == "outer") return fn(m.outer_x1000), true;
    if (f == "smooth") return fn(m.smoothing_ms), true;
    if (f == "boost") return fn(m.boost_x100), true;
    if (f == "range") return fn(m.position_range), true;
    if (f == "spring") return fn(m.spring_per_s_x10), true;
    return false;
  }
  if (f == "on") return fn(s_edit.profile.enabled), true;
  if (f == "merge") return fn(s_edit.profile.merge_real_pad), true;
  return false;
}

bool parse_long(const std::string &text, long lo, long hi, long *out) {
  char *end = nullptr;
  const long v = std::strtol(text.c_str(), &end, 10);
  if (text.empty() || !end || *end || v < lo || v > hi)
    return false;
  *out = v;
  return true;
}

template <typename V> int find_named(const V &items, const std::string &name) {
  for (size_t i = 0; i < items.size(); ++i)
    if (items[i].name == name)
      return static_cast<int>(i);
  return -1;
}
int find_layer(const std::string &name) { return find_named(s_edit.profile.layers, name); }
int find_macro(const std::string &name) { return find_named(s_edit.profile.macros, name); }

bool is_layer_output(const kb::OutputDef &o) {
  return o.kind == kb::OutputKind::LayerHold ||
         o.kind == kb::OutputKind::LayerToggle;
}

/* Names are what outputs refer to: printable ASCII, unique, no edges. */
bool clean_name(std::string name, std::string *out) {
  while (!name.empty() && name.back() == ' ')
    name.pop_back();
  while (!name.empty() && name.front() == ' ')
    name.erase(0, 1);
  if (name.empty() || name.size() > kb::kMaxMacroNameBytes ||
      !std::all_of(name.begin(), name.end(),
                   [](char c) { return c >= 0x20 && c < 0x7f; }))
    return false;
  *out = name;
  return true;
}

void rename_refs(bool layers, const std::string &from, const std::string &to) {
  const auto refers = [layers](const kb::OutputDef &o) {
    return layers ? is_layer_output(o) : o.kind == kb::OutputKind::Macro;
  };
  for (kb::LayerDef &l : s_edit.profile.layers)
    for (kb::BindingDef &b : l.bindings) {
      for (kb::OutputDef &o : b.outputs) {
        std::string &ref = layers ? o.layer : o.macro;
        if (refers(o) && ref == from)
          ref = to;
      }
      if (to.empty()) /* the target is being deleted: drop what pointed at it */
        std::erase_if(b.outputs, [&](const kb::OutputDef &o) {
          return refers(o) && (layers ? o.layer : o.macro).empty();
        });
    }
}

kb::MacroStep new_step() {
  kb::MacroStep st{};
  for (int16_t &a : st.axes)
    a = INT16_MIN; /* untouched */
  st.duration_ms = 100;
  return st;
}

/* New names: "Layer 2", "Macro 3"... the first free one. ASCII on purpose:
   outputs refer to them and the engine keeps 23 bytes. */
std::string free_name(bool layers) {
  for (int i = 1;; ++i) {
    const std::string name = (layers ? "Layer " : "Macro ") + std::to_string(i);
    if ((layers ? find_layer(name) : find_macro(name)) < 0)
      return name;
  }
}

kb::Profile *editable(const Path &p) {
  if (p.title == "all" || p.title == "help")
    return nullptr;
  load(p.title);
  return s_edit.own && !under_deleted(p) && in_range(p) ? &s_edit.profile : nullptr;
}

/* A new output of the given kind on the binding at p. */
bool add_output(const Path &p, const std::string &what, Path *created) {
  if (p.output >= 0)
    return false;
  const kb::Profile *pr = &s_edit.profile;
  kb::BindingDef &b = binding(p);
  if (b.outputs.size() >= static_cast<size_t>(kb::kMaxOutputsPerBinding))
    return false;
  kb::OutputDef o;
  if (what == "button") {
    o.kind = kb::OutputKind::Button;
    o.buttons = kb::kPadCross;
  } else if (what == "axis") {
    o.kind = kb::OutputKind::AxisDirection;
    o.axis = kb::kAxisLY;
    o.value = -32767; /* left stick up */
  } else if (what == "trigger") {
    o.kind = kb::OutputKind::Trigger;
    o.trigger = kb::kTriggerR2;
    o.value = 32767;
  } else if (what == "layer") {
    o.kind = kb::OutputKind::LayerHold;
    if (pr->layers.size() > 1)
      o.layer = pr->layers[1].name;
  } else if (what == "macro") {
    o.kind = kb::OutputKind::Macro;
    if (!pr->macros.empty())
      o.macro = pr->macros[0].name;
  } else if (what == "scale") {
    o.kind = kb::OutputKind::StickScale;
    o.value = 16384; /* half: walking */
  } else if (what == "suspend") {
    o.kind = kb::OutputKind::MappingToggle;
  } else if (what == "block") {
    o.kind = kb::OutputKind::None;
  } else {
    return false;
  }
  b.outputs.push_back(std::move(o));
  *created = with(p, 'o', static_cast<int>(b.outputs.size()) - 1);
  return true;
}


/* The daemon is told off the UI thread (its reply may take long); changes
   made meanwhile ride along with the next send. */
std::atomic<bool> s_reload_wanted{false};
std::atomic<bool> s_reload_running{false};

void tell_daemon();

void *send_reloads(void *) {
  while (s_reload_wanted.exchange(false)) {
    std::string reply;
    if (!IPC_Client::getInstance(false).IPCSendCommand(BREW_KBM_RELOAD, reply))
      LOG_WARN("[kbm] daemon did not take the reload");
  }
  s_reload_running.store(false);
  /* a change that came in just as the loop ended */
  if (s_reload_wanted.load())
    tell_daemon();
  return nullptr;
}

void tell_daemon() {
  s_reload_wanted.store(true);
  if (s_reload_running.exchange(true))
    return;
  pthread_t thread;
  if (pthread_create(&thread, nullptr, send_reloads, nullptr) == 0)
    pthread_detach(thread);
  else
    s_reload_running.store(false);
}

} // namespace

void load(const std::string &title) {
  if (s_edit.title == title)
    return;
  s_edit = {};
  s_edit.title = title;
  std::string error;
  s_edit.own = kb::LoadProfile(file_for(title), &s_edit.profile, nullptr, &error);
  if (!error.empty())
    LOG_WARN("[kbm] %s", error.c_str());
  if (!s_edit.own) {
    /* the default profile always exists: the template until saved */
    s_edit.own = title == "default";
    s_edit.profile = s_edit.own ? kb::DefaultProfile()
                               : kb::ResolveProfile(kRoot, title);
  }
}

bool under_deleted(const Path &p) {
  if (s_deleted.title.empty() || s_deleted.title != p.title)
    return false;
  const std::pair<int, int> pairs[] = {{s_deleted.layer, p.layer},
                                       {s_deleted.binding, p.binding},
                                       {s_deleted.output, p.output},
                                       {s_deleted.macro, p.macro},
                                       {s_deleted.step, p.step}};
  for (const auto &[d, v] : pairs)
    if (d >= 0 && d != v)
      return false;
  return true;
}

bool in_range(const Path &p) {
  const kb::Profile &pr = s_edit.profile;
  return in_bounds(pr.layers, p.layer) && in_bounds(pr.macros, p.macro) &&
         (p.binding < 0 || (p.layer >= 0 && in_bounds(layer(p).bindings, p.binding))) &&
         (p.output < 0 || (p.binding >= 0 && in_bounds(binding(p).outputs, p.output))) &&
         (p.step < 0 || (p.macro >= 0 && in_bounds(macro(p).steps, p.step)));
}

bool save(const Path &p) {
  if (p.title == "all")
    return true; /* settings are saved where they change */
  if (!kb::SaveProfile(file_for(p.title), s_edit.profile)) {
    notify("notify.kbm.save_failed");
    return false;
  }
  s_edit.own = true;
  tell_daemon();
  return true;
}

bool create_profile(const Path &p, bool from_template) {
  load(p.title);
  kb::Profile fresh;
  if (from_template) {
    fresh = kb::DefaultProfile();
  } else {
    fresh.layers.emplace_back();
    fresh.layers[0].name = "base";
  }
  fresh.name = game_name(p.title);
  s_edit.profile = std::move(fresh);
  s_edit.own = true;
  s_deleted = {};
  return save(p);
}

bool delete_profile(const Path &p) {
  const bool deleted = kb::DeleteProfile(file_for(p.title));
  s_edit.title.clear();
  load(p.title);
  tell_daemon();
  return deleted;
}

bool add_item(const Path &p, const std::string &what, Path *created) {
  kb::Profile *pr = editable(p);
  if (!pr)
    return false;
  if (p.binding >= 0) {
    if (!add_output(p, what, created))
      return false;
  } else if (what == "layer" && p.layer < 0 && p.macro < 0) {
    if (pr->layers.size() >= static_cast<size_t>(kb::kMaxLayers))
      return false;
    kb::LayerDef l;
    l.name = free_name(true);
    l.mouse_inherit = true; /* a new layer starts with the mouse below it */
    pr->layers.push_back(std::move(l));
    *created = with(p, 'l', static_cast<int>(pr->layers.size()) - 1);
  } else if (what == "bind" && p.layer >= 0 && p.binding < 0) {
    size_t total = 0;
    for (const kb::LayerDef &l : pr->layers)
      total += l.bindings.size();
    if (total >= static_cast<size_t>(kb::kMaxBindings))
      return false;
    layer(p).bindings.emplace_back(); /* no input yet: the editor asks for it */
    *created = with(p, 'b', static_cast<int>(layer(p).bindings.size()) - 1);
  } else if (what == "macro" && p.layer < 0 && p.macro < 0) {
    if (pr->macros.size() >= static_cast<size_t>(kb::kMaxMacros))
      return false;
    kb::MacroDef m;
    m.name = free_name(false);
    m.steps.push_back(new_step()); /* a macro without steps is dropped */
    pr->macros.push_back(std::move(m));
    *created = with(p, 'm', static_cast<int>(pr->macros.size()) - 1);
  } else if (what == "step" && p.macro >= 0 && p.step < 0) {
    size_t total = 0;
    for (const kb::MacroDef &m : pr->macros)
      total += m.steps.size();
    if (total >= static_cast<size_t>(kb::kMaxMacroSteps))
      return false;
    macro(p).steps.push_back(new_step());
    *created = with(p, 's', static_cast<int>(macro(p).steps.size()) - 1);
  } else {
    return false;
  }
  return save(p);
}

int delete_item(const Path &p) {
  kb::Profile *pr = editable(p);
  if (!pr)
    return 0;
  int pages = 1;
  if (p.step >= 0) {
    macro(p).steps.erase(macro(p).steps.begin() + p.step);
  } else if (p.macro >= 0) {
    rename_refs(false, macro(p).name, std::string());
    pr->macros.erase(pr->macros.begin() + p.macro);
    pages = 2; /* macro options and the macro */
  } else if (p.output >= 0) {
    binding(p).outputs.erase(binding(p).outputs.begin() + p.output);
  } else if (p.binding >= 0) {
    layer(p).bindings.erase(layer(p).bindings.begin() + p.binding);
  } else if (p.layer > 0) {
    rename_refs(true, layer(p).name, std::string());
    pr->layers.erase(pr->layers.begin() + p.layer);
    pages = 2; /* layer options and the layer */
  } else {
    return 0;
  }
  s_deleted = p;
  s_deleted.word.clear();
  return save(p) ? pages : 0;
}

bool clear_modifiers(const Path &p) {
  if (!editable(p) || p.binding < 0 || p.output >= 0)
    return false;
  for (kb::Input &m : binding(p).modifiers)
    m = kb::kInputNone;
  return save(p);
}

SetInput set_input(const Path &p, kb::Input input, const kb::Input *mods) {
  kb::Settings settings = kb::LoadSettings(kRoot);
  if (p.title == "all") {
    /* a keyboard key only: a mouse button or notch would toggle the mapping
       with every click */
    if (input < kb::kKeyFirst || input > kb::kKeyLast ||
        !kb::IsValidSuspendKey(input))
      return SetInput::Reserved;
    settings.suspend_key = input;
    if (!kb::SaveSettings(kRoot, settings))
      return SetInput::Failed;
    tell_daemon();
    return SetInput::Ok;
  }
  if (!editable(p) || p.binding < 0 || p.output >= 0)
    return SetInput::Gone;
  if (input == settings.suspend_key)
    return SetInput::Reserved;
  kb::BindingDef &b = binding(p);
  b.input = input;
  for (int i = 0; i < kb::kMaxModifiers; ++i)
    b.modifiers[i] = mods && mods[i] != input ? mods[i] : kb::kInputNone;
  return save(p) ? SetInput::Ok : SetInput::Failed;
}

bool get_value(const Path &p, std::string &out) {
  const std::string &f = p.word;
  if (p.title == "all") {
    if (f != "on")
      return false;
    out = kb::LoadSettings(kRoot).enabled ? "1" : "0";
    return true;
  }
  if (!editable(p))
    return false;
  if (number(p, [&](auto &v) { out = std::to_string(static_cast<long>(v)); }))
    return true;
  if (const PadName *n = pad_field(f)) {
    const uint32_t mask = p.step >= 0 ? step(p).buttons : p.output >= 0 ? output(p).buttons : 0;
    out = (mask & n->mask) ? "1" : "0";
    return p.step >= 0 || p.output >= 0;
  }
  if (p.step >= 0) {
    const kb::MacroStep &s = step(p);
    for (int a = 0; a < 4; ++a)
      if (f == kStepAxes[a]) {
        if (s.axes[a] == INT16_MIN) out = "u";
        else out = std::to_string((s.axes[a] < 0 ? -1 : 1) * pct(s.axes[a], 32767));
        return true;
      }
    if (f == "tl2" || f == "tr2") {
      out = std::to_string(pct(f == "tl2" ? s.l2 : s.r2, 255));
      return true;
    }
    return false;
  }
  if (p.output >= 0) {
    const kb::OutputDef &o = output(p);
    if (f == "dir") out = std::to_string(direction_of(o));
    else if (f == "str" || f == "scale") out = std::to_string(pct(o.value, 32767));
    else if (f == "layer") out = std::to_string(std::max(find_layer(o.layer), 1));
    else if (f == "how") out = o.kind == kb::OutputKind::LayerToggle ? "1" : "0";
    else if (f == "macro") out = std::to_string(std::max(find_macro(o.macro), 0));
    else return false;
    return true;
  }
  if (p.macro >= 0 && f == "name") {
    out = macro(p).name;
    return true;
  }
  if (p.layer >= 0) {
    const uint8_t inv = layer(p).mouse.invert;
    if (f == "ix") out = (inv & 1) ? "1" : "0";
    else if (f == "iy") out = (inv & 2) ? "1" : "0";
    else if (f == "name") out = layer(p).name;
    else return false;
    return true;
  }
  return false;
}

bool set_value(const Path &p, const std::string &raw) {
  const std::string &f = p.word;
  /* toggles report "1"/"0", on some firmware "true"/"false" */
  const std::string value = raw == "true" ? "1" : raw == "false" ? "0" : raw;
  if (p.title == "all") {
    if (f != "on")
      return false;
    kb::Settings settings = kb::LoadSettings(kRoot);
    settings.enabled = value == "1";
    if (!kb::SaveSettings(kRoot, settings)) {
      notify("notify.kbm.save_failed");
      return false;
    }
    tell_daemon();
    return true;
  }
  if (!editable(p))
    return false;

  long n = 0;
  const bool numeric = parse_long(value, -100000, 100000, &n);
  /* list values come from our own items; still keep them inside the type */
  if (numeric && number(p, [&](auto &v) {
        using T = std::remove_reference_t<decltype(v)>;
        if constexpr (std::is_enum_v<T>) {
          v = static_cast<T>(std::clamp(n, 0L, 255L));
        } else if constexpr (std::is_same_v<T, bool>) {
          v = n != 0;
        } else {
          v = static_cast<T>(std::clamp<long>(n, std::numeric_limits<T>::min(),
                                              std::numeric_limits<T>::max()));
        }
      }))
    return true;

  if (const PadName *pn = pad_field(f)) {
    if (p.step < 0 && p.output < 0)
      return false;
    uint32_t &mask = p.step >= 0 ? step(p).buttons : output(p).buttons;
    mask = value == "1" ? (mask | pn->mask) : (mask & ~pn->mask);
    return true;
  }
  if (p.step >= 0) {
    kb::MacroStep &s = step(p);
    for (int a = 0; a < 4; ++a)
      if (f == kStepAxes[a]) {
        if (value == "u") s.axes[a] = INT16_MIN;
        else if (numeric && n >= -100 && n <= 100)
          s.axes[a] = static_cast<int16_t>(n * 32767 / 100);
        else return false;
        return true;
      }
    if ((f == "tl2" || f == "tr2") && numeric && n >= 0 && n <= 100) {
      (f == "tl2" ? s.l2 : s.r2) = static_cast<uint8_t>(n * 255 / 100);
      return true;
    }
    return false;
  }
  if (p.output >= 0) {
    kb::OutputDef &o = output(p);
    if (f == "dir" && numeric && n >= 0 && n < 8) {
      const int p100 = std::max(pct(o.value, 32767), 1);
      o.axis = kDirections[n].axis;
      o.value = static_cast<int16_t>((kDirections[n].positive ? 1 : -1) * p100 * 32767 / 100);
      return true;
    }
    if ((f == "str" || f == "scale") && numeric && n >= 1 && n <= 100) {
      o.value = static_cast<int16_t>((o.value < 0 ? -1 : 1) * n * 32767 / 100);
      return true;
    }
    if (f == "layer" && numeric && n >= 1 && n < static_cast<long>(s_edit.profile.layers.size())) {
      o.layer = s_edit.profile.layers[n].name;
      return true;
    }
    if (f == "how" && numeric && is_layer_output(o)) {
      o.kind = n ? kb::OutputKind::LayerToggle : kb::OutputKind::LayerHold;
      return true;
    }
    if (f == "macro" && numeric && n >= 0 && n < static_cast<long>(s_edit.profile.macros.size())) {
      o.macro = s_edit.profile.macros[n].name;
      return true;
    }
    return false;
  }
  if (f == "name" && (p.macro >= 0 || p.layer > 0)) {
    std::string name;
    const bool is_layer = p.macro < 0;
    std::string &current = is_layer ? layer(p).name : macro(p).name;
    if (!clean_name(value, &name) || (is_layer && name.size() > kb::kMaxLayerNameBytes)) {
      notify("notify.kbm.bad_name");
      return false;
    }
    if (name != current && (is_layer ? find_layer(name) : find_macro(name)) >= 0) {
      notify("notify.kbm.name_taken", name.c_str());
      return false;
    }
    rename_refs(is_layer, current, name);
    current = name;
    return true;
  }
  if (p.layer >= 0 && (f == "ix" || f == "iy")) {
    uint8_t &inv = layer(p).mouse.invert;
    const uint8_t bit = f == "ix" ? 1 : 2;
    inv = value == "1" ? (inv | bit) : (inv & ~bit);
    return true;
  }
  return false;
}

} // namespace kbm_toolbox
