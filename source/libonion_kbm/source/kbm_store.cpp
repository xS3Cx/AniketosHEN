/* Keyboard & mouse for games: profile JSON and the kbm/ folder.
 *
 * Fractions (stick values, sensitivity, curves) are written as plain
 * numbers (1.0, 0.5) so the files stay readable for FTP users; the model
 * keeps the blob's fixed-point units. Unknown keys are ignored, so files
 * written by a newer HEN with the same schema still load. */

#include <onion/fs.h>
#include <onion/kbm_names.hpp>
#include <onion/kbm_store.hpp>

#include "onion_cjson.hpp"

#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <limits>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

namespace onion::kbm {
namespace {

using onion_cjson::item;
using onion_cjson::string_item;

constexpr double kUnit = 32767.0; /* JSON 1.0 == blob 32767 */

/* Number → integer field, clamped to the field's type (Validate does the
 * real ranges). Absent or non-numeric keeps the current value. */
template <typename T>
void get_num(const cJSON *o, const char *key, T &dst, double scale = 1.0) {
  const cJSON *v = item(o, key);
  if (!cJSON_IsNumber(v)) return;
  double x = round(v->valuedouble * scale);
  const double lo = static_cast<double>(std::numeric_limits<T>::min());
  const double hi = static_cast<double>(std::numeric_limits<T>::max());
  dst = static_cast<T>(x < lo ? lo : x > hi ? hi : x);
}

void get_bool(const cJSON *o, const char *key, bool &dst) {
  const cJSON *v = item(o, key);
  if (cJSON_IsBool(v) || cJSON_IsNumber(v)) dst = onion_cjson::bool_value(v);
}

void get_str(const cJSON *o, const char *key, std::string &dst) {
  if (const char *s = string_item(o, key)) dst = s;
}

/* False when the key holds a name this HEN does not know. */
template <typename E>
bool get_enum(const cJSON *o, const char *key, bool (*parse)(const char *, E *),
              E &dst, const std::string &where, Warnings *w) {
  const char *s = string_item(o, key);
  if (!s || parse(s, &dst)) return true;
  Warn(w, where + ": unknown " + key + " '" + s + "'");
  return false;
}

/* 5 decimals: x/32767 survives the round trip exactly. */
double frac(double v, double scale) { return round(v / scale * 100000.0) / 100000.0; }

std::string raw_json(const cJSON *j) { return onion_cjson::Printed(j).str(); }

/* "" or absent: not chosen yet (the editor saves half-built items). */
bool unset(const char *name) { return !name || !*name; }

void parse_output(const cJSON *j, OutputDef *o, const std::string &where, Warnings *w) {
  const char *name = cJSON_IsString(j) ? j->valuestring : string_item(j, "out");
  if (!name || !ParseOutput(name, o)) {
    Warn(w, where + ": unknown output '" + (name ? name : "") + "', kept as is");
    o->raw = raw_json(j);
    return;
  }
  if (!cJSON_IsObject(j)) return; /* "pad:cross" shorthand */
  const cJSON *v = item(j, "value");
  if (cJSON_IsNumber(v)) {
    double x = fabs(v->valuedouble) * kUnit;
    if (o->kind == OutputKind::AxisDirection && o->value < 0) x = -x;
    x = round(x);
    o->value = static_cast<int16_t>(x > 32767 ? 32767 : x < -32767 ? -32767 : x);
  }
  get_num(j, "rise_ms", o->ramp.rise_ms);
  get_num(j, "fall_ms", o->ramp.fall_ms);
  get_num(j, "curve", o->ramp.curve_x100, 100.0);
  get_str(j, "layer", o->layer);
  get_str(j, "macro", o->macro);
}

/* An unknown input, modifier, gesture or mode keeps the whole binding as raw
 * JSON: dropping only the part would change what it does (a chord without
 * its modifier fires on the bare key). */
void parse_binding(const cJSON *j, BindingDef *b, const std::string &lwhere, Warnings *w) {
  const char *in = string_item(j, "input");
  const std::string where = lwhere + " " + (unset(in) ? "binding" : in);
  bool known = unset(in) || ParseInput(in, &b->input);
  if (!known) Warn(w, where + ": unknown input");
  int n = 0;
  const cJSON *m = nullptr;
  cJSON_ArrayForEach(m, item(j, "modifiers")) {
    Input mod = kInputNone;
    if (!cJSON_IsString(m) || !ParseInput(m->valuestring, &mod) || n == kMaxModifiers) {
      Warn(w, where + ": unknown modifier");
      known = false;
      break;
    }
    b->modifiers[n++] = mod;
  }
  known = get_enum(j, "gesture", ParseGesture, b->gesture, where, w) && known;
  known = get_enum(j, "mode", ParseMode, b->mode, where, w) && known;
  if (!known) {
    Warn(w, where + ": binding kept as is, not used");
    b->raw = raw_json(j);
    return;
  }
  get_num(j, "turbo_hz", b->turbo_hz);
  get_num(j, "turbo_duty", b->turbo_duty);
  get_num(j, "tap_ms", b->tap_ms);
  get_num(j, "long_ms", b->long_ms);
  get_num(j, "double_ms", b->double_ms);
  get_num(j, "pulse_ms", b->pulse_ms);
  get_str(j, "label", b->label);
  const cJSON *o = nullptr;
  cJSON_ArrayForEach(o, item(j, "outputs")) {
    OutputDef out;
    parse_output(o, &out, where, w);
    b->outputs.push_back(out);
  }
}

void parse_mouse(const cJSON *j, MouseParams &m, bool &inherit, const std::string &where,
                 Warnings *w) {
  if (!cJSON_IsObject(j)) return;
  get_bool(j, "inherit", inherit);
  get_enum(j, "mode", ParseMouseMode, m.mode, where, w);
  if (const char *s = string_item(j, "stick")) m.stick = strcasecmp(s, "left") == 0 ? 0 : 1;
  get_enum(j, "deadzone_shape", ParseDeadzoneShape, m.deadzone_shape, where, w);
  bool ix = m.invert & 1, iy = m.invert & 2;
  get_bool(j, "invert_x", ix);
  get_bool(j, "invert_y", iy);
  m.invert = static_cast<uint8_t>((ix ? 1 : 0) | (iy ? 2 : 0));
  get_num(j, "sensitivity", m.sensitivity_x100, 100.0);
  get_num(j, "yx_ratio", m.yx_ratio_x100, 100.0);
  get_num(j, "exponent", m.exponent_x100, 100.0);
  get_num(j, "deadzone", m.deadzone_x1000, 1000.0);
  get_num(j, "outer", m.outer_x1000, 1000.0);
  get_num(j, "smoothing_ms", m.smoothing_ms);
  get_num(j, "boost", m.boost_x100, 100.0);
  get_num(j, "position_range", m.position_range);
  get_num(j, "spring_per_s", m.spring_per_s_x10, 10.0);
}

cJSON *mouse_json(const MouseParams &m, bool inherit) {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddBoolToObject(j, "inherit", inherit);
  cJSON_AddStringToObject(j, "mode", MouseModeName(m.mode));
  cJSON_AddStringToObject(j, "stick", m.stick ? "right" : "left");
  cJSON_AddStringToObject(j, "deadzone_shape", DeadzoneShapeName(m.deadzone_shape));
  cJSON_AddBoolToObject(j, "invert_x", m.invert & 1);
  cJSON_AddBoolToObject(j, "invert_y", m.invert & 2);
  cJSON_AddNumberToObject(j, "sensitivity", frac(m.sensitivity_x100, 100.0));
  cJSON_AddNumberToObject(j, "yx_ratio", frac(m.yx_ratio_x100, 100.0));
  cJSON_AddNumberToObject(j, "exponent", frac(m.exponent_x100, 100.0));
  cJSON_AddNumberToObject(j, "deadzone", frac(m.deadzone_x1000, 1000.0));
  cJSON_AddNumberToObject(j, "outer", frac(m.outer_x1000, 1000.0));
  cJSON_AddNumberToObject(j, "smoothing_ms", m.smoothing_ms);
  cJSON_AddNumberToObject(j, "boost", frac(m.boost_x100, 100.0));
  cJSON_AddNumberToObject(j, "position_range", m.position_range);
  cJSON_AddNumberToObject(j, "spring_per_s", frac(m.spring_per_s_x10, 10.0));
  return j;
}

cJSON *output_json(const OutputDef &o) {
  if (!o.raw.empty()) return cJSON_Parse(o.raw.c_str());
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "out", OutputName(o).c_str());
  switch (o.kind) {
  case OutputKind::AxisDirection:
  case OutputKind::Trigger:
    cJSON_AddNumberToObject(j, "value", frac(o.value < 0 ? -o.value : o.value, kUnit));
    cJSON_AddNumberToObject(j, "rise_ms", o.ramp.rise_ms);
    cJSON_AddNumberToObject(j, "fall_ms", o.ramp.fall_ms);
    cJSON_AddNumberToObject(j, "curve", frac(o.ramp.curve_x100, 100.0));
    break;
  case OutputKind::StickScale:
    cJSON_AddNumberToObject(j, "value", frac(o.value, kUnit));
    break;
  case OutputKind::LayerHold:
  case OutputKind::LayerToggle:
    cJSON_AddStringToObject(j, "layer", o.layer.c_str());
    break;
  case OutputKind::Macro:
    cJSON_AddStringToObject(j, "macro", o.macro.c_str());
    break;
  default:
    break;
  }
  return j;
}

cJSON *binding_json(const BindingDef &b) {
  if (!b.raw.empty()) return cJSON_Parse(b.raw.c_str());
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "input", InputName(b.input).c_str());
  if (b.modifiers[0] != kInputNone) {
    cJSON *mods = cJSON_AddArrayToObject(j, "modifiers");
    for (Input m : b.modifiers)
      if (m != kInputNone)
        cJSON_AddItemToArray(mods, cJSON_CreateString(InputName(m).c_str()));
  }
  if (!b.label.empty()) cJSON_AddStringToObject(j, "label", b.label.c_str());
  cJSON_AddStringToObject(j, "gesture", GestureName(b.gesture));
  cJSON_AddStringToObject(j, "mode", ModeName(b.mode));
  cJSON_AddNumberToObject(j, "turbo_hz", b.turbo_hz);
  cJSON_AddNumberToObject(j, "turbo_duty", b.turbo_duty);
  cJSON_AddNumberToObject(j, "tap_ms", b.tap_ms);
  cJSON_AddNumberToObject(j, "long_ms", b.long_ms);
  cJSON_AddNumberToObject(j, "double_ms", b.double_ms);
  cJSON_AddNumberToObject(j, "pulse_ms", b.pulse_ms);
  cJSON *outs = cJSON_AddArrayToObject(j, "outputs");
  for (const OutputDef &o : b.outputs)
    if (cJSON *oj = output_json(o)) cJSON_AddItemToArray(outs, oj);
  return j;
}

const char *kAxisKeys[4] = {"lx", "ly", "rx", "ry"};

bool read_file(const std::string &path, std::string *out, std::string *error) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) {
    if (error && errno != ENOENT) *error = path + ": " + strerror(errno);
    return false;
  }
  out->clear();
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    out->append(buf, n);
    if (out->size() > kMaxProfileFileSize) {
      fclose(f);
      if (error) *error = path + ": larger than 1 MiB";
      return false;
    }
  }
  fclose(f);
  return true;
}

bool write_file(const std::string &path, const std::string &text) {
  if (text.empty()) return false; /* printing failed: keep the file there is */
  const size_t slash = path.rfind('/');
  if (slash != std::string::npos && slash > 0 && !mkdir_tree(path.substr(0, slash).c_str()))
    return false;
  return write_file_atomic(path.c_str(), text.data(), text.size());
}

std::string settings_path(const std::string &root) { return root + "/settings.json"; }

} // namespace

bool ProfileFromJson(const char *text, size_t length, Profile *out, Warnings *w,
                     std::string *error) {
  if (!out) return false;
  onion_cjson::Root root(text, length);
  if (!root || !cJSON_IsObject(root.get())) {
    if (error) *error = "not a JSON object";
    return false;
  }
  const cJSON *r = root.get();
  const int schema = onion_cjson::int_item(r, "schema", kProfileSchema);
  if (schema > kProfileSchema) {
    if (error) *error = "profile schema " + std::to_string(schema) + " needs a newer HEN";
    return false;
  }
  /* schema < kProfileSchema: migrate here, one step per version (none yet). */

  Profile p;
  get_str(r, "name", p.name);
  get_bool(r, "enabled", p.enabled);
  get_bool(r, "merge_real_pad", p.merge_real_pad);
  get_num(r, "trigger_button_threshold", p.trigger_button_threshold);

  const cJSON *lj = nullptr;
  cJSON_ArrayForEach(lj, item(r, "layers")) {
    LayerDef l;
    get_str(lj, "name", l.name);
    const std::string where = "layer '" + l.name + "'";
    parse_mouse(item(lj, "mouse"), l.mouse, l.mouse_inherit, where, w);
    get_enum(lj, "opposing", ParseOpposing, l.opposing, where, w);
    get_num(lj, "mouse_digital_threshold", l.mouse_digital_threshold);
    const cJSON *bj = nullptr;
    cJSON_ArrayForEach(bj, item(lj, "bindings")) {
      BindingDef b;
      parse_binding(bj, &b, where, w);
      l.bindings.push_back(b);
    }
    p.layers.push_back(l);
  }

  const cJSON *mj = nullptr;
  cJSON_ArrayForEach(mj, item(r, "macros")) {
    MacroDef m;
    get_str(mj, "name", m.name);
    get_bool(mj, "repeat_while_held", m.repeat_while_held);
    const cJSON *sj = nullptr;
    cJSON_ArrayForEach(sj, item(mj, "steps")) {
      MacroStep s{};
      for (int16_t &a : s.axes) a = INT16_MIN;
      s.duration_ms = 60; /* "ms" left out: one short step */
      const char *buttons = string_item(sj, "buttons");
      if (buttons && *buttons && !ParseButtons(buttons, &s.buttons))
        Warn(w, "macro '" + m.name + "': unknown buttons '" + buttons + "'");
      for (int a = 0; a < 4; ++a)
        if (cJSON_IsNumber(item(sj, kAxisKeys[a]))) {
          get_num(sj, kAxisKeys[a], s.axes[a], kUnit);
          if (s.axes[a] == INT16_MIN) s.axes[a] = -32767; /* -1.0 is not "untouched" */
        }
      get_num(sj, "l2", s.l2);
      get_num(sj, "r2", s.r2);
      get_num(sj, "ms", s.duration_ms);
      m.steps.push_back(s);
    }
    p.macros.push_back(m);
  }

  Validate(p, w);
  *out = p;
  return true;
}

std::string ProfileToJson(const Profile &p) {
  cJSON *r = cJSON_CreateObject();
  cJSON_AddNumberToObject(r, "schema", kProfileSchema);
  cJSON_AddStringToObject(r, "name", p.name.c_str());
  cJSON_AddBoolToObject(r, "enabled", p.enabled);
  cJSON_AddBoolToObject(r, "merge_real_pad", p.merge_real_pad);
  cJSON_AddNumberToObject(r, "trigger_button_threshold", p.trigger_button_threshold);
  cJSON *layers = cJSON_AddArrayToObject(r, "layers");
  for (const LayerDef &l : p.layers) {
    cJSON *lj = cJSON_CreateObject();
    cJSON_AddStringToObject(lj, "name", l.name.c_str());
    cJSON_AddItemToObject(lj, "mouse", mouse_json(l.mouse, l.mouse_inherit));
    cJSON_AddStringToObject(lj, "opposing", OpposingName(l.opposing));
    cJSON_AddNumberToObject(lj, "mouse_digital_threshold", l.mouse_digital_threshold);
    cJSON *bindings = cJSON_AddArrayToObject(lj, "bindings");
    for (const BindingDef &b : l.bindings)
      if (cJSON *bj = binding_json(b)) cJSON_AddItemToArray(bindings, bj);
    cJSON_AddItemToArray(layers, lj);
  }
  cJSON *macros = cJSON_AddArrayToObject(r, "macros");
  for (const MacroDef &m : p.macros) {
    cJSON *mj = cJSON_CreateObject();
    cJSON_AddStringToObject(mj, "name", m.name.c_str());
    cJSON_AddBoolToObject(mj, "repeat_while_held", m.repeat_while_held);
    cJSON *steps = cJSON_AddArrayToObject(mj, "steps");
    for (const MacroStep &s : m.steps) {
      cJSON *sj = cJSON_CreateObject();
      cJSON_AddNumberToObject(sj, "ms", s.duration_ms);
      if (s.buttons) cJSON_AddStringToObject(sj, "buttons", ButtonsName(s.buttons).c_str());
      for (int a = 0; a < 4; ++a)
        if (s.axes[a] != INT16_MIN)
          cJSON_AddNumberToObject(sj, kAxisKeys[a], frac(s.axes[a], kUnit));
      if (s.l2) cJSON_AddNumberToObject(sj, "l2", s.l2);
      if (s.r2) cJSON_AddNumberToObject(sj, "r2", s.r2);
      cJSON_AddItemToArray(steps, sj);
    }
    cJSON_AddItemToArray(macros, mj);
  }
  return onion_cjson::print_owned(r, true);
}

bool SettingsFromJson(const char *text, size_t length, Settings *out, Warnings *w) {
  onion_cjson::Root root(text, length);
  if (!out || !root || !cJSON_IsObject(root.get())) return false;
  Settings s;
  get_bool(root.get(), "enabled", s.enabled);
  if (const char *name = string_item(root.get(), "suspend_key")) {
    Input key = kInputNone;
    if (strcasecmp(name, "none") == 0)
      s.suspend_key = kInputNone;
    else if (ParseInput(name, &key) && IsValidSuspendKey(key))
      s.suspend_key = key;
    else
      Warn(w, std::string("suspend key '") + name + "' refused, using " +
                  InputName(kDefaultSuspendKey));
  }
  *out = s;
  return true;
}

std::string SettingsToJson(const Settings &s) {
  cJSON *r = cJSON_CreateObject();
  cJSON_AddNumberToObject(r, "schema", kProfileSchema);
  cJSON_AddBoolToObject(r, "enabled", s.enabled);
  cJSON_AddStringToObject(r, "suspend_key",
                          s.suspend_key == kInputNone ? "none" : InputName(s.suspend_key).c_str());
  return onion_cjson::print_owned(r, true);
}

bool IsValidTitleId(const std::string &id) {
  if (id.empty() || id.size() > 16) return false;
  for (char c : id)
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

std::string TitleProfilePath(const std::string &root, const std::string &id) {
  return IsValidTitleId(id) ? root + "/titles/" + id + ".json" : std::string();
}
std::string DefaultProfilePath(const std::string &root) { return root + "/default.json"; }

bool LoadProfile(const std::string &path, Profile *out, Warnings *w, std::string *error) {
  std::string text;
  if (!read_file(path, &text, error)) return false;
  std::string why;
  if (!ProfileFromJson(text.data(), text.size(), out, w, &why)) {
    if (error) *error = path + ": " + why;
    return false;
  }
  return true;
}

bool SaveProfile(const std::string &path, const Profile &p) {
  const std::string text = ProfileToJson(p);
  /* a file LoadProfile would refuse must never replace a good one */
  return !path.empty() && text.size() <= kMaxProfileFileSize && write_file(path, text);
}

bool DeleteProfile(const std::string &path) {
  return !path.empty() && (unlink(path.c_str()) == 0 || errno == ENOENT);
}

Profile ResolveProfile(const std::string &root, const std::string &title_id, Warnings *w) {
  Profile p;
  for (const std::string &path : {TitleProfilePath(root, title_id), DefaultProfilePath(root)}) {
    if (path.empty()) continue;
    std::string error;
    if (LoadProfile(path, &p, w, &error)) return p;
    if (!error.empty()) Warn(w, error); /* broken file: say so, fall back */
  }
  return DefaultProfile();
}

bool TitleEnabled(const std::string &root, const std::string &title_id) {
  Profile p;
  for (const std::string &path : {TitleProfilePath(root, title_id), DefaultProfilePath(root)})
    if (!path.empty() && LoadProfile(path, &p)) return p.enabled;
  return false; /* the built-in template never applies by itself */
}

std::vector<std::string> ListTitleProfiles(const std::string &root) {
  std::vector<std::string> ids;
  DIR *d = opendir((root + "/titles").c_str());
  if (!d) return ids;
  while (const dirent *e = readdir(d)) {
    const std::string name = e->d_name;
    if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
    const std::string id = name.substr(0, name.size() - 5);
    if (IsValidTitleId(id)) ids.push_back(id);
  }
  closedir(d);
  std::sort(ids.begin(), ids.end());
  return ids;
}

Settings LoadSettings(const std::string &root, Warnings *w) {
  Settings s;
  std::string text;
  if (read_file(settings_path(root), &text, nullptr))
    SettingsFromJson(text.data(), text.size(), &s, w);
  return s;
}

bool SaveSettings(const std::string &root, const Settings &s) {
  return write_file(settings_path(root), SettingsToJson(s));
}

} // namespace onion::kbm
