/* Copyright (C) 2026 AniketosHEN
 *
 * Toolbox "Keyboard and Mouse": the pages, the profile being edited and the
 * values the controls show. Presses are handled in onpress_kbm.cpp.
 *
 * Pages whose rows change (lists of games, layers, bindings, outputs,
 * macros, steps) hold only links, toggles, labels and buttons, so the
 * Settings page refresher can update them when the user comes back to them.
 * Pickers (lists, text fields) sit on pages of their own, as PS5 Settings
 * does, and never change shape while open.
 */
#include "aniketos_shell/toolbox/kbm_toolbox.hpp"

#include "hooked_funcs.hpp"
#include "ps5_settings_ui.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"
#include "onion_cjson.hpp"

#include <msg.hpp>
#include <onion/ipc_client.hpp>
#include <onion/kbm_names.hpp>
#include <onion/kbm_profile.hpp>
#include <onion/kbm_store.hpp>
#include <onion/platform.h>

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <pthread.h>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

bool Get_Running_App_TID(std::string &title_id, int &BigAppid);

namespace kbm_toolbox {
namespace kb = onion::kbm;
using toolbox_i18n::format;
using toolbox_i18n::tr;

namespace {

constexpr const char *kRoot = kb::kStoreRootUser;
constexpr const char *kIconGame =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_game.png";

/* ---- paths ------------------------------------------------------------- */

bool is_word(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

/* "l3" → 3 for the index letters; -1 otherwise. */
int index_token(std::string_view s, char letter) {
  if (s.size() < 2 || s.size() > 4 || s[0] != letter)
    return -1;
  int n = 0;
  for (char c : s.substr(1)) {
    if (c < '0' || c > '9')
      return -1;
    n = n * 10 + (c - '0');
  }
  return n;
}

/* "<T>[-l0][-b1]...[-word]": the word, if any, comes last. */
bool parse_tokens(std::string_view text, Path *out) {
  Path p;
  size_t at = 0;
  for (int n = 0; at <= text.size(); ++n) {
    const size_t dash = text.find('-', at);
    const std::string_view tok = text.substr(
        at, dash == std::string_view::npos ? std::string_view::npos : dash - at);
    at = dash == std::string_view::npos ? text.size() + 1 : dash + 1;
    if (n == 0) {
      p.t = std::string(tok);
      if (p.t != "all" && p.t != "default" && p.t != "help" &&
          !kb::IsValidTitleId(p.t))
        return false;
      continue;
    }
    if (!p.word.empty())
      return false;
    int *slot = nullptr;
    int value = -1;
    const std::pair<char, int *> slots[] = {
        {'l', &p.l}, {'b', &p.b}, {'o', &p.o}, {'m', &p.m}, {'s', &p.s}};
    for (const auto &[letter, target] : slots)
      if ((value = index_token(tok, letter)) >= 0) {
        slot = target;
        break;
      }
    if (slot) {
      if (*slot >= 0)
        return false;
      *slot = value;
    } else if (is_word(tok)) {
      p.word = std::string(tok);
    } else {
      return false;
    }
  }
  *out = std::move(p);
  return true;
}

std::string key(const Path &p) {
  std::string k = p.t;
  const std::pair<char, int> parts[] = {
      {'l', p.l}, {'b', p.b}, {'o', p.o}, {'m', p.m}, {'s', p.s}};
  for (const auto &[letter, v] : parts)
    if (v >= 0)
      k += std::string("-") + letter + std::to_string(v);
  return k;
}

std::string element_id(const Path &p, const char *field) {
  return "id_kbm-" + key(p) + "-" + field;
}

Path with(Path p, char letter, int v) {
  (letter == 'l' ? p.l : letter == 'b' ? p.b : letter == 'o' ? p.o
                       : letter == 'm' ? p.m : p.s) = v;
  p.word.clear();
  return p;
}

Path field(Path p, const char *f) {
  p.word = f;
  return p;
}

/* ---- the profile being edited ----------------------------------------- */

struct Edit {
  std::string t;
  bool own = false;
  kb::Profile p;
};
Edit s_edit;
Path s_deleted; /* t empty: nothing deleted */

std::string file_for(const std::string &t) {
  return t == "default" ? kb::DefaultProfilePath(kRoot)
                        : kb::TitleProfilePath(kRoot, t);
}

void load(const std::string &t) {
  if (s_edit.t == t)
    return;
  s_edit = {};
  s_edit.t = t;
  std::string error;
  s_edit.own = kb::LoadProfile(file_for(t), &s_edit.p, nullptr, &error);
  if (!error.empty())
    LOG_WARN("[kbm] %s", error.c_str());
  if (!s_edit.own) {
    /* the default profile always exists: the template until saved */
    s_edit.own = t == "default";
    s_edit.p = t == "default" ? kb::DefaultProfile()
                              : kb::ResolveProfile(kRoot, t);
  }
}

bool under_deleted(const Path &p) {
  if (s_deleted.t.empty() || s_deleted.t != p.t)
    return false;
  const std::pair<int, int> pairs[] = {{s_deleted.l, p.l}, {s_deleted.b, p.b},
                                       {s_deleted.o, p.o}, {s_deleted.m, p.m},
                                       {s_deleted.s, p.s}};
  for (const auto &[d, v] : pairs)
    if (d >= 0 && d != v)
      return false;
  return true;
}

template <typename V> bool in(const V &v, int i) {
  return i < 0 || static_cast<size_t>(i) < v.size();
}

kb::LayerDef &layer(const Path &p) { return s_edit.p.layers[p.l]; }
kb::BindingDef &binding(const Path &p) { return layer(p).bindings[p.b]; }
kb::OutputDef &output(const Path &p) { return binding(p).outputs[p.o]; }
kb::MacroDef &macro(const Path &p) { return s_edit.p.macros[p.m]; }
kb::MacroStep &step(const Path &p) { return macro(p).steps[p.s]; }

/* Every index in the path points at something that exists. */
bool in_range(const Path &p) {
  const kb::Profile &pr = s_edit.p;
  return in(pr.layers, p.l) && in(pr.macros, p.m) &&
         (p.b < 0 || (p.l >= 0 && in(layer(p).bindings, p.b))) &&
         (p.o < 0 || (p.b >= 0 && in(binding(p).outputs, p.o))) &&
         (p.s < 0 || (p.m >= 0 && in(macro(p).steps, p.s)));
}

/* ---- names --------------------------------------------------------------- */

struct PadName {
  uint32_t mask;
  const char *field;
  const char *key;     /* translated */
  const char *literal; /* or as printed on the pad */
};
constexpr PadName kPadNames[] = {
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
std::string pad_name(const PadName &n) { return n.key ? tr(n.key) : n.literal; }
const PadName *pad_field(const std::string &f) {
  for (const PadName &n : kPadNames)
    if (f == n.field)
      return &n;
  return nullptr;
}

std::string buttons_name(uint32_t mask) {
  std::string out;
  for (const PadName &n : kPadNames)
    if (mask & n.mask)
      out += (out.empty() ? "" : " + ") + pad_name(n);
  return out;
}

/* Axis directions as the editor offers them: d/2 picks the axis, odd d is
   the positive way (right, down). */
constexpr struct {
  uint8_t axis;
  bool positive;
  const char *key;
} kDirections[] = {
    {kb::kAxisLY, false, "kbm.dir.ls_up"},   {kb::kAxisLY, true, "kbm.dir.ls_down"},
    {kb::kAxisLX, false, "kbm.dir.ls_left"}, {kb::kAxisLX, true, "kbm.dir.ls_right"},
    {kb::kAxisRY, false, "kbm.dir.rs_up"},   {kb::kAxisRY, true, "kbm.dir.rs_down"},
    {kb::kAxisRX, false, "kbm.dir.rs_left"}, {kb::kAxisRX, true, "kbm.dir.rs_right"},
};
int direction_of(const kb::OutputDef &o) {
  for (int d = 0; d < 8; ++d)
    if (kDirections[d].axis == o.axis && kDirections[d].positive == (o.value > 0))
      return d;
  return 0;
}

/* between a binding's input and its outputs in rows */
constexpr const char *kArrow = " → ";

constexpr const char *kStepAxes[] = {"lx", "ly", "rx", "ry"};

constexpr const char *kGestureKeys[] = {"kbm.gesture.press", "kbm.gesture.tap",
                                        "kbm.gesture.long", "kbm.gesture.double",
                                        "kbm.gesture.release"};
constexpr const char *kModeKeys[] = {"kbm.mode.hold", "kbm.mode.toggle",
                                     "kbm.mode.turbo", "kbm.mode.pulse"};
constexpr const char *kMouseModeKeys[] = {"kbm.mouse.mode.off",
                                          "kbm.mouse.mode.velocity",
                                          "kbm.mouse.mode.position"};
constexpr const char *kOpposingKeys[] = {"kbm.socd.last", "kbm.socd.neutral",
                                         "kbm.socd.first"};

template <size_t N> const char *pick(const char *const (&keys)[N], long i) {
  return tr(keys[i >= 0 && static_cast<size_t>(i) < N ? i : 0]);
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
  return i == 0 ? tr("kbm.layer.base") : s_edit.p.layers[i].name;
}

std::string output_summary(const kb::OutputDef &o) {
  switch (o.kind) {
  case kb::OutputKind::None:
    return tr("kbm.sum.block");
  case kb::OutputKind::Button:
    return o.buttons ? buttons_name(o.buttons) : tr("kbm.sum.no_buttons");
  case kb::OutputKind::AxisDirection: {
    std::string s = tr(kDirections[direction_of(o)].key);
    const int p = pct(o.value, 32767);
    return p < 100 ? s + " " + fmt_pct(p) : s;
  }
  case kb::OutputKind::Trigger: {
    std::string s = o.trigger == kb::kTriggerL2 ? "L2" : "R2";
    const int p = pct(o.value, 32767);
    return p < 100 ? s + " " + fmt_pct(p) : s;
  }
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

bool analog(const kb::OutputDef &o) {
  return o.kind == kb::OutputKind::AxisDirection ||
         o.kind == kb::OutputKind::Trigger;
}

std::string chord_name(const kb::BindingDef &b) {
  std::string s;
  for (kb::Input m : b.modifiers)
    if (m != kb::kInputNone)
      s += input_name(m) + " + ";
  return s + (b.input == kb::kInputNone ? tr("kbm.input.none") : input_name(b.input));
}

/* "Left Shift + W → Left stick up, Cross" */
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
  if (&l != &s_edit.p.layers[0] && l.mouse_inherit)
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

/* ---- installed games ------------------------------------------------------- */

struct Game {
  std::string id, name;
};
std::vector<Game> s_games;

std::string read_small_file(const std::string &path) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return {};
  std::string text(256 * 1024, '\0');
  text.resize(std::fread(text.data(), 1, text.size(), file));
  std::fclose(file);
  return text;
}

/* PS5 titles: param.json localizedParameters[defaultLanguage].titleName. */
std::string name_from_json(const std::string &text) {
  onion_cjson::Root root(text);
  if (!root)
    return {};
  cJSON *loc = onion_cjson::item(root.get(), "localizedParameters");
  const char *lang = onion_cjson::string_item(loc, "defaultLanguage", "en-US");
  return onion_cjson::string_item(onion_cjson::item(loc, lang), "titleName", "");
}

/* PS4 titles: the TITLE entry of param.sfo. */
std::string name_from_sfo(const std::string &d) {
  auto u32 = [&](size_t at) -> uint32_t {
    return at + 4 > d.size() ? 0 : static_cast<uint32_t>(
        static_cast<uint8_t>(d[at]) | static_cast<uint8_t>(d[at + 1]) << 8 |
        static_cast<uint8_t>(d[at + 2]) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(d[at + 3])) << 24);
  };
  if (d.size() < 20 || d.compare(0, 4, std::string("\0PSF", 4)) != 0)
    return {};
  const uint32_t keys = u32(8), data = u32(12), count = u32(16);
  for (uint32_t i = 0; i < count && 20 + i * 16 + 16 <= d.size(); ++i) {
    const size_t e = 20 + i * 16;
    const size_t k = keys + (u32(e) & 0xffff);
    const size_t v = data + u32(e + 12);
    if (k >= d.size() || d.compare(k, 6, std::string("TITLE\0", 6)) != 0)
      continue;
    const uint32_t len = u32(e + 4);
    if (v + len > d.size())
      return {};
    return std::string(d.c_str() + v, strnlen(d.c_str() + v, len));
  }
  return {};
}

/* Valid UTF-8 without control characters: a homebrew title's bytes end up
   in the page XML. */
std::string clean_text(const std::string &in) {
  std::string out;
  for (size_t i = 0; i < in.size();) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3
               : (c >> 3) == 30 ? 4 : 0;
    bool ok = len && i + len <= in.size();
    for (size_t k = 1; ok && k < len; ++k)
      ok = (static_cast<unsigned char>(in[i + k]) & 0xc0) == 0x80;
    if (!ok) {
      ++i;
      continue;
    }
    if (len > 1 || (c >= 0x20 && c != 0x7f))
      out.append(in, i, len);
    i += len;
  }
  return out;
}

std::string title_name(const std::string &id) {
  std::string name = name_from_json(read_small_file("/user/appmeta/" + id + "/param.json"));
  if (name.empty())
    name = name_from_json(read_small_file("/user/app/" + id + "/sce_sys/param.json"));
  if (name.empty())
    name = name_from_sfo(read_small_file("/user/appmeta/" + id + "/param.sfo"));
  name = clean_text(name);
  return name.empty() ? id : name;
}

std::string lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

void scan_games() {
  s_games.clear();
  DIR *dir = opendir("/user/app");
  if (!dir)
    return;
  while (dirent *entry = readdir(dir)) {
    const std::string id = entry->d_name;
    if (id.size() == 9 && kb::IsValidTitleId(id))
      s_games.push_back({id, title_name(id)});
  }
  closedir(dir);
  std::sort(s_games.begin(), s_games.end(), [](const Game &a, const Game &b) {
    return lower(a.name) < lower(b.name);
  });
}

std::string game_name(const std::string &t) {
  if (t == "default")
    return tr("kbm.default");
  for (const Game &g : s_games)
    if (g.id == t)
      return g.name;
  return title_name(t);
}

/* ---- page helpers ------------------------------------------------------------ */

ps5ui::Page new_page(const Path &p, const char *page, std::string title) {
  return ps5ui::Page("id_kbmpage-" + key(p) + (page ? std::string("-") + page : ""),
                     std::move(title));
}

std::optional<std::string> opt_tr(const char *k) {
  return k ? std::optional<std::string>(tr(k)) : std::nullopt;
}

using Fmt = std::string (*)(long);

template <typename G>
void num_list(G &g, const Path &base, const char *f, const char *title_key,
              const char *sub_key, std::vector<long> values, Fmt fmt) {
  const Path p = field(base, f);
  std::string current;
  get_value(p, current);
  const long cur = std::atol(current.c_str());
  if (!current.empty() && std::find(values.begin(), values.end(), cur) == values.end()) {
    values.push_back(cur);
    std::sort(values.begin(), values.end());
  }
  const std::string id = element_id(p, f);
  g.list(id, tr(title_key),
         [&](ps5ui::ListBuilder &L) {
           for (long v : values)
             L.item(id + "." + std::to_string(v), fmt(v), std::to_string(v));
         },
         opt_tr(sub_key), current);
}

template <typename G>
void choice_list(G &g, const Path &base, const char *f, const char *title_key,
                 const char *sub_key,
                 const std::vector<std::pair<std::string, std::string>> &items) {
  const Path p = field(base, f);
  std::string current;
  get_value(p, current);
  const std::string id = element_id(p, f);
  g.list(id, tr(title_key),
         [&](ps5ui::ListBuilder &L) {
           for (const auto &[value, title] : items)
             L.item(id + "." + value, title, value);
         },
         opt_tr(sub_key), current);
}

template <size_t N>
std::vector<std::pair<std::string, std::string>> enum_items(const char *const (&keys)[N]) {
  std::vector<std::pair<std::string, std::string>> items;
  for (size_t i = 0; i < N; ++i)
    items.emplace_back(std::to_string(i), tr(keys[i]));
  return items;
}

template <typename G>
void toggle(G &g, const Path &base, const char *f, std::string title,
            const char *sub_key = nullptr) {
  const Path p = field(base, f);
  std::string v;
  get_value(p, v);
  g.toggle(element_id(p, f), std::move(title), v == "1", opt_tr(sub_key));
}

void action(ps5ui::Page &g, const Path &p, const char *f, const char *title_key,
            const char *sub_key = nullptr, const char *confirm_key = nullptr) {
  g.button(element_id(p, f), tr(title_key), opt_tr(sub_key), std::nullopt,
           std::nullopt, ps5ui::Style::None, opt_tr(confirm_key),
           confirm_key ? opt_tr("kbm.confirm_phrase") : std::nullopt);
}

void nav(ps5ui::Page &g, const Path &p, const char *page, std::string title,
         std::optional<std::string> sub) {
  g.link("id_kbmnav-" + key(p) + (page ? std::string("-") + page : ""),
         std::move(title), resource(p, page), std::move(sub));
}

void text(ps5ui::Page &g, const std::string &id, const char *k) {
  g.label(id, tr(k), ps5ui::Style::Left);
}
void heading(ps5ui::Page &g, const std::string &id, const char *k) {
  g.label(id, tr(k), ps5ui::Style::Center);
}

/* ---- pages that refresh (links, toggles, labels, buttons only) --------- */

ps5ui::Node main_page() {
  const Path all{"all"};
  ps5ui::Page page = new_page(all, nullptr, tr("kbm.link"));
  const kb::Settings settings = kb::LoadSettings(kRoot);
  toggle(page, all, "on", tr("kbm.enabled"), "kbm.enabled.sub");
  page.button(element_id(all, "suspend"), tr("kbm.suspend"),
              format("kbm.suspend.sub_fmt", input_name(settings.suspend_key).c_str()),
              tr("kbm.suspend.desc"));
  page.link("id_kbmnav-help", tr("kbm.help"), "kbm-help.xml", tr("kbm.help.sub"));
  page.link("id_kbmnav-default", tr("kbm.default"), "kbm-default.xml",
            tr("kbm.default.sub"));
  heading(page, "id_kbm_games", "kbm.games");
  if (s_games.empty())
    text(page, "id_kbm_games_none", "kbm.games.none");

  const std::vector<std::string> own = kb::ListTitleProfiles(kRoot);
  for (const Game &g : s_games) {
    const char *k = "kbm.game.default";
    if (std::find(own.begin(), own.end(), g.id) != own.end()) {
      kb::Profile p;
      k = kb::LoadProfile(kb::TitleProfilePath(kRoot, g.id), &p) && !p.enabled
              ? "kbm.game.own_off"
              : "kbm.game.own";
    }
    page.link("id_kbmnav-" + g.id, g.name, "kbm-" + g.id + ".xml",
              format(k, g.id.c_str()), kIconGame);
  }
  return page.root();
}

ps5ui::Node help_page() {
  ps5ui::Page page = new_page(Path{"help"}, nullptr, tr("kbm.help"));
  for (const char *k : {"kbm.help.1", "kbm.help.2", "kbm.help.3", "kbm.help.4",
                        "kbm.help.5", "kbm.help.6"})
    text(page, std::string("id_kbm_") + (k + 4), k);
  return page.root();
}

ps5ui::Node deleted_page(const Path &p, const char *page_name) {
  ps5ui::Page page = new_page(p, page_name, tr("kbm.link"));
  text(page, "id_kbm_deleted", "kbm.deleted");
  return page.root();
}

ps5ui::Node profile_page(const Path &p) {
  load(p.t);
  ps5ui::Page page = new_page(p, nullptr, game_name(p.t));
  if (!s_edit.own) {
    text(page, "id_kbm_uses_default", "kbm.profile.uses_default");
    action(page, p, "newtpl", "kbm.profile.create_template",
           "kbm.profile.create_template.sub");
    action(page, p, "newempty", "kbm.profile.create_empty",
           "kbm.profile.create_empty.sub");
    return page.root();
  }
  toggle(page, p, "on", tr(p.t == "default" ? "kbm.profile.enabled_default"
                                             : "kbm.profile.enabled"));
  toggle(page, p, "merge", tr("kbm.profile.merge"), "kbm.profile.merge.sub");

  heading(page, "id_kbm_layers", "kbm.layers");
  const kb::Profile &pr = s_edit.p;
  for (size_t i = 0; i < pr.layers.size(); ++i)
    nav(page, with(p, 'l', static_cast<int>(i)), nullptr, layer_name(static_cast<int>(i)),
        format("kbm.layer.summary_fmt", static_cast<int>(pr.layers[i].bindings.size()),
               mouse_summary(pr.layers[i]).c_str()));
  if (pr.layers.size() < static_cast<size_t>(kb::kMaxLayers))
    action(page, p, "addlayer", "kbm.layer.add", "kbm.layer.add.sub");

  heading(page, "id_kbm_more", "kbm.more");
  nav(page, p, "macros", tr("kbm.macros"),
      format("kbm.macros.count_fmt", static_cast<int>(pr.macros.size())));
  action(page, p, "reset", "kbm.profile.reset", "kbm.profile.reset.sub",
         "kbm.profile.reset.confirm");
  action(page, p, "clear", "kbm.profile.clear", "kbm.profile.clear.sub",
         "kbm.profile.clear.confirm");
  if (p.t == "default")
    action(page, p, "delete", "kbm.profile.delete", "kbm.profile.delete_default.sub",
           "kbm.profile.delete_default.confirm");
  else
    action(page, p, "delete", "kbm.profile.delete", "kbm.profile.delete.sub",
           "kbm.profile.delete.confirm");
  return page.root();
}

ps5ui::Node layer_page(const Path &p) {
  const kb::LayerDef &l = layer(p);
  ps5ui::Page page = new_page(p, nullptr, layer_name(p.l));
  nav(page, p, "mouse", tr("kbm.mouse"), mouse_summary(l));
  nav(page, p, "opts", tr("kbm.layer.options"),
      std::string(tr("kbm.socd")) + ": " + pick(kOpposingKeys, static_cast<long>(l.opposing)));
  heading(page, "id_kbm_bindings", "kbm.bindings");
  if (l.bindings.empty())
    text(page, "id_kbm_bindings_none", "kbm.bindings.none");
  for (size_t i = 0; i < l.bindings.size(); ++i) {
    const kb::BindingDef &b = l.bindings[i];
    nav(page, with(p, 'b', static_cast<int>(i)), nullptr, binding_title(b),
        gesture_summary(b) + " · " + mode_summary(b));
  }
  action(page, p, "addbind", "kbm.binding.add", "kbm.binding.add.sub");
  return page.root();
}

ps5ui::Node binding_page(const Path &p) {
  const kb::BindingDef &b = binding(p);
  ps5ui::Page page = new_page(p, nullptr, tr("kbm.binding.title"));
  page.button(element_id(p, "input"), tr("kbm.binding.input"), chord_name(b),
              tr("kbm.binding.input.sub"));
  if (b.modifiers[0] != kb::kInputNone || b.modifiers[1] != kb::kInputNone)
    action(page, p, "nomods", "kbm.binding.nomods");
  nav(page, p, "gesture", tr("kbm.binding.gesture"), gesture_summary(b));
  nav(page, p, "mode", tr("kbm.binding.mode"), mode_summary(b));

  heading(page, "id_kbm_outputs", "kbm.binding.outputs");
  if (b.outputs.empty())
    text(page, "id_kbm_outputs_none", "kbm.binding.outputs.none");
  for (size_t i = 0; i < b.outputs.size(); ++i) {
    const kb::OutputDef &o = b.outputs[i];
    const bool ramp = analog(o) && (o.ramp.rise_ms || o.ramp.fall_ms);
    nav(page, with(p, 'o', static_cast<int>(i)), nullptr, output_summary(o),
        ramp ? format("kbm.sum.ramp_fmt", static_cast<int>(o.ramp.rise_ms),
                      static_cast<int>(o.ramp.fall_ms))
             : std::string(tr(kind_key(o.kind))));
  }
  if (b.outputs.size() < static_cast<size_t>(kb::kMaxOutputsPerBinding)) {
    heading(page, "id_kbm_add_output", "kbm.binding.add_output");
    for (const char *k : {"button", "axis", "trigger", "layer", "macro", "scale",
                          "suspend", "block"}) {
      const std::string kk = std::string("kbm.kind.") + k;
      action(page, p, (std::string("add") + k).c_str(), kk.c_str(),
             (kk + ".sub").c_str());
    }
  }
  action(page, p, "delbind", "kbm.binding.delete", nullptr, "kbm.binding.delete.confirm");
  return page.root();
}

ps5ui::Node macros_page(const Path &p) {
  ps5ui::Page page = new_page(p, "macros", tr("kbm.macros"));
  text(page, "id_kbm_macros_hint", "kbm.macros.hint");
  if (s_edit.p.macros.empty())
    text(page, "id_kbm_macros_none", "kbm.macros.none");
  for (size_t i = 0; i < s_edit.p.macros.size(); ++i)
    nav(page, with(p, 'm', static_cast<int>(i)), nullptr, s_edit.p.macros[i].name,
        format("kbm.macro.steps_fmt", static_cast<int>(s_edit.p.macros[i].steps.size())));
  if (s_edit.p.macros.size() < static_cast<size_t>(kb::kMaxMacros))
    action(page, p, "addmacro", "kbm.macro.add");
  return page.root();
}

ps5ui::Node macro_page(const Path &p) {
  const kb::MacroDef &m = macro(p);
  ps5ui::Page page = new_page(p, nullptr, m.name);
  toggle(page, p, "repeat", tr("kbm.macro.repeat"), "kbm.macro.repeat.sub");
  nav(page, p, "opts", tr("kbm.macro.options"), m.name);
  heading(page, "id_kbm_steps", "kbm.macro.steps");
  for (size_t i = 0; i < m.steps.size(); ++i)
    nav(page, with(p, 's', static_cast<int>(i)), nullptr,
        std::to_string(i + 1) + ". " + step_summary(m.steps[i]),
        fmt_ms(m.steps[i].duration_ms));
  action(page, p, "addstep", "kbm.macro.add_step");
  return page.root();
}

/* ---- picker pages ------------------------------------------------------------ */

ps5ui::Node layer_options_page(const Path &p) {
  ps5ui::Page page = new_page(p, "opts", tr("kbm.layer.options"));
  if (p.l > 0)
    page.text_field(element_id(field(p, "name"), "name"), tr("kbm.layer.name"),
                    tr("kbm.layer.name.sub"), "basic_latin", "1", "23",
                    std::nullopt, std::nullopt, std::nullopt, layer(p).name);
  choice_list(page, p, "socd", "kbm.socd", "kbm.socd.sub", enum_items(kOpposingKeys));
  num_list(page, p, "move", "kbm.move_threshold", "kbm.move_threshold.sub",
           {500, 1000, 2000, 3000, 5000, 8000, 12000}, fmt_counts);
  if (p.l > 0)
    action(page, p, "dellayer", "kbm.layer.delete", nullptr, "kbm.layer.delete.confirm");
  return page.root();
}

ps5ui::Node mouse_page(const Path &p) {
  ps5ui::Page page = new_page(p, "mouse", tr("kbm.mouse"));
  if (p.l > 0)
    toggle(page, p, "inherit", tr("kbm.mouse.inherit"), "kbm.mouse.inherit.sub");
  choice_list(page, p, "mmode", "kbm.binding.mode", "kbm.mouse.mode.sub",
              enum_items(kMouseModeKeys));
  choice_list(page, p, "mstick", "kbm.mouse.stick", nullptr,
              {{"0", tr("kbm.stick.left")}, {"1", tr("kbm.stick.right")}});
  num_list(page, p, "sens", "kbm.mouse.sens", "kbm.mouse.sens.sub",
           {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 175,
            200, 250, 300, 350, 400, 500, 700, 1000},
           fmt_x100);
  num_list(page, p, "yx", "kbm.mouse.yx", "kbm.mouse.yx.sub",
           {25, 50, 60, 70, 75, 80, 90, 100, 110, 120, 125, 130, 140, 150, 175, 200, 250, 300},
           fmt_pct);
  num_list(page, p, "expo", "kbm.mouse.curve", "kbm.mouse.curve.sub",
           {50, 60, 70, 75, 80, 90, 100, 110, 120, 125, 130, 140, 150, 175, 200, 250, 300},
           fmt_x100);
  num_list(page, p, "dz", "kbm.mouse.deadzone", "kbm.mouse.deadzone.sub",
           {0, 20, 40, 60, 80, 100, 120, 140, 160, 180, 200, 220, 240, 260, 280, 300, 350, 400},
           fmt_permille);
  choice_list(page, p, "shape", "kbm.mouse.shape", "kbm.mouse.shape.sub",
              {{"0", tr("kbm.shape.circle")}, {"1", tr("kbm.shape.square")}});
  num_list(page, p, "outer", "kbm.mouse.outer", "kbm.mouse.outer.sub",
           {500, 600, 700, 800, 850, 900, 950, 1000}, fmt_permille);
  num_list(page, p, "smooth", "kbm.mouse.smoothing", "kbm.mouse.smoothing.sub",
           {4, 8, 12, 16, 20, 25, 30, 40, 50, 60, 80, 100}, fmt_ms);
  num_list(page, p, "boost", "kbm.mouse.boost", "kbm.mouse.boost.sub",
           {100, 110, 125, 150, 175, 200, 250, 300, 400}, fmt_x100);
  toggle(page, p, "ix", tr("kbm.mouse.invert_x"));
  toggle(page, p, "iy", tr("kbm.mouse.invert_y"));
  num_list(page, p, "range", "kbm.mouse.range", "kbm.mouse.range.sub",
           {50, 100, 200, 300, 400, 600, 800, 1000, 1500, 2000, 3000, 5000, 10000},
           fmt_counts);
  num_list(page, p, "spring", "kbm.mouse.spring", "kbm.mouse.spring.sub",
           {0, 5, 10, 20, 30, 50, 80, 120, 200, 400}, fmt_spring);
  return page.root();
}

ps5ui::Node gesture_page(const Path &p) {
  ps5ui::Page page = new_page(p, "gesture", tr("kbm.binding.gesture"));
  choice_list(page, p, "gesture", "kbm.binding.gesture", "kbm.binding.gesture.sub",
              enum_items(kGestureKeys));
  num_list(page, p, "tap", "kbm.timing.tap", "kbm.timing.tap.sub",
           {100, 150, 200, 250, 300, 400, 500}, fmt_ms);
  num_list(page, p, "long", "kbm.timing.long", "kbm.timing.long.sub",
           {200, 300, 400, 500, 600, 800, 1000, 1500, 2000}, fmt_ms);
  num_list(page, p, "double", "kbm.timing.double", "kbm.timing.double.sub",
           {150, 200, 250, 300, 400, 500}, fmt_ms);
  num_list(page, p, "pulse", "kbm.timing.pulse", "kbm.timing.pulse.sub",
           {16, 33, 50, 60, 80, 100, 150, 200, 300, 500}, fmt_ms);
  return page.root();
}

ps5ui::Node mode_page(const Path &p) {
  ps5ui::Page page = new_page(p, "mode", tr("kbm.binding.mode"));
  choice_list(page, p, "mode", "kbm.binding.mode", "kbm.binding.mode.sub",
              enum_items(kModeKeys));
  num_list(page, p, "hz", "kbm.turbo.rate", "kbm.turbo.rate.sub",
           {2, 4, 5, 6, 8, 10, 12, 15, 20, 25, 30}, fmt_hz);
  num_list(page, p, "duty", "kbm.turbo.duty", "kbm.turbo.duty.sub",
           {10, 20, 30, 40, 50, 60, 70, 80, 90}, fmt_pct);
  num_list(page, p, "pulse", "kbm.timing.pulse", "kbm.timing.pulse.sub",
           {16, 33, 50, 60, 80, 100, 150, 200, 300, 500}, fmt_ms);
  return page.root();
}

void ramp_rows(ps5ui::Page &page, const Path &p) {
  text(page, "id_kbm_ramp_hint", "kbm.ramp.hint");
  num_list(page, p, "rise", "kbm.ramp.rise", "kbm.ramp.rise.sub",
           {0, 50, 100, 150, 200, 300, 400, 500, 750, 1000, 1500, 2000, 3000}, fmt_ms);
  num_list(page, p, "fall", "kbm.ramp.fall", "kbm.ramp.fall.sub",
           {0, 50, 100, 150, 200, 300, 400, 500, 750, 1000, 1500, 2000, 3000}, fmt_ms);
  num_list(page, p, "rcurve", "kbm.ramp.curve", "kbm.ramp.curve.sub",
           {50, 70, 100, 150, 200, 250, 300}, fmt_x100);
}

ps5ui::Node output_page(const Path &p) {
  const kb::OutputDef &o = output(p);
  ps5ui::Page page = new_page(p, nullptr, tr(kind_key(o.kind)));
  const std::vector<long> strengths = {5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 75,
                                       80, 90, 100};
  switch (o.kind) {
  case kb::OutputKind::Button:
    text(page, "id_kbm_buttons_hint", "kbm.out.buttons.hint");
    for (const PadName &n : kPadNames)
      toggle(page, p, n.field, pad_name(n));
    break;
  case kb::OutputKind::AxisDirection: {
    std::vector<std::pair<std::string, std::string>> dirs;
    for (int d = 0; d < 8; ++d)
      dirs.emplace_back(std::to_string(d), tr(kDirections[d].key));
    choice_list(page, p, "dir", "kbm.out.direction", nullptr, dirs);
    num_list(page, p, "str", "kbm.out.strength", "kbm.out.strength.sub", strengths, fmt_pct);
    ramp_rows(page, p);
    break;
  }
  case kb::OutputKind::Trigger:
    choice_list(page, p, "trg", "kbm.out.trigger", nullptr, {{"0", "L2"}, {"1", "R2"}});
    num_list(page, p, "str", "kbm.out.strength", "kbm.out.strength.sub", strengths, fmt_pct);
    ramp_rows(page, p);
    break;
  case kb::OutputKind::LayerHold:
  case kb::OutputKind::LayerToggle: {
    if (s_edit.p.layers.size() < 2) {
      text(page, "id_kbm_no_layers", "kbm.out.layer.none");
      break;
    }
    std::vector<std::pair<std::string, std::string>> names;
    for (size_t i = 1; i < s_edit.p.layers.size(); ++i)
      names.emplace_back(std::to_string(i), s_edit.p.layers[i].name);
    choice_list(page, p, "layer", "kbm.kind.layer", nullptr, names);
    choice_list(page, p, "how", "kbm.out.layer_how", nullptr,
                {{"0", tr("kbm.layer_how.hold")}, {"1", tr("kbm.layer_how.toggle")}});
    break;
  }
  case kb::OutputKind::Macro: {
    if (s_edit.p.macros.empty()) {
      text(page, "id_kbm_no_macros", "kbm.out.macro.none");
      break;
    }
    std::vector<std::pair<std::string, std::string>> names;
    for (size_t i = 0; i < s_edit.p.macros.size(); ++i)
      names.emplace_back(std::to_string(i), s_edit.p.macros[i].name);
    choice_list(page, p, "macro", "kbm.kind.macro", nullptr, names);
    break;
  }
  case kb::OutputKind::StickScale:
    choice_list(page, p, "stick", "kbm.mouse.stick", nullptr,
                {{"0", tr("kbm.stick.left")}, {"1", tr("kbm.stick.right")}});
    num_list(page, p, "scale", "kbm.out.scale", "kbm.out.scale.sub",
             {10, 20, 25, 30, 40, 50, 60, 70, 75, 80, 90}, fmt_pct);
    break;
  case kb::OutputKind::MappingToggle:
    text(page, "id_kbm_kind_hint", "kbm.kind.suspend.sub");
    break;
  case kb::OutputKind::None:
    text(page, "id_kbm_kind_hint", "kbm.kind.block.sub");
    break;
  }
  action(page, p, "delout", "kbm.out.delete", nullptr, "kbm.out.delete.confirm");
  return page.root();
}

ps5ui::Node macro_options_page(const Path &p) {
  ps5ui::Page page = new_page(p, "opts", tr("kbm.macro.options"));
  page.text_field(element_id(field(p, "name"), "name"), tr("kbm.layer.name"),
                  tr("kbm.macro.name.sub"), "basic_latin", "1", "32", std::nullopt,
                  std::nullopt, std::nullopt, macro(p).name);
  action(page, p, "delmacro", "kbm.macro.delete", nullptr, "kbm.macro.delete.confirm");
  return page.root();
}

ps5ui::Node step_page(const Path &p) {
  ps5ui::Page page = new_page(p, nullptr, format("kbm.step.title_fmt", p.s + 1));
  num_list(page, p, "dur", "kbm.step.duration", "kbm.step.duration.sub",
           {16, 33, 50, 75, 100, 150, 200, 300, 500, 750, 1000, 1500, 2000, 3000, 5000},
           fmt_ms);
  text(page, "id_kbm_step_hint", "kbm.step.hint");
  for (const PadName &n : kPadNames)
    toggle(page, p, n.field, pad_name(n));
  const char *axes[][3] = {{"lx", "kbm.step.lx", "h"}, {"ly", "kbm.step.ly", "v"},
                           {"rx", "kbm.step.rx", "h"}, {"ry", "kbm.step.ry", "v"}};
  for (const auto &a : axes) {
    std::vector<std::pair<std::string, std::string>> items = {
        {"u", tr("kbm.step.untouched")}};
    const bool h = a[2][0] == 'h';
    for (int v = -100; v <= 100; v += 25)
      items.emplace_back(std::to_string(v),
                         v == 0 ? std::string(tr("kbm.centre"))
                                : format(v < 0 ? (h ? "kbm.dir.left_fmt" : "kbm.dir.up_fmt")
                                               : (h ? "kbm.dir.right_fmt" : "kbm.dir.down_fmt"),
                                         std::abs(v)));
    choice_list(page, p, a[0], a[1], nullptr, items);
  }
  for (const char *t : {"tl2", "tr2"}) {
    const Path fp = field(p, t);
    std::string current;
    get_value(fp, current);
    const std::string id = element_id(fp, t);
    page.list(id, t[1] == 'l' ? "L2" : "R2",
              [&](ps5ui::ListBuilder &L) {
                for (int v = 0; v <= 100; v += 25)
                  L.item(id + "." + std::to_string(v), fmt_pct(v), std::to_string(v));
              },
              tr("kbm.out.strength.sub"), current);
  }
  action(page, p, "delstep", "kbm.step.delete", nullptr, "kbm.step.delete.confirm");
  return page.root();
}

/* The page for a resource path; flat pages can refresh. */
bool build(const Path &p, ps5ui::Node &out, bool *flat) {
  const std::string &w = p.word;
  *flat = true;
  if (p.t == "all") {
    out = main_page();
    return true;
  }
  if (p.t == "help") {
    out = help_page();
    return true;
  }
  load(p.t);
  if (p.l < 0 && p.m < 0 && w.empty()) {
    out = profile_page(p);
    return true;
  }
  if (!s_edit.own || under_deleted(p) || !in_range(p)) {
    out = deleted_page(p, w.empty() ? nullptr : w.c_str());
    return true;
  }
  if (p.l >= 0) {
    *flat = false;
    if (p.o >= 0) out = output_page(p);
    else if (p.b >= 0 && w == "gesture") out = gesture_page(p);
    else if (p.b >= 0 && w == "mode") out = mode_page(p);
    else if (w == "mouse") out = mouse_page(p);
    else if (w == "opts") out = layer_options_page(p);
    else if (!w.empty()) return false;
    else {
      *flat = true;
      out = p.b >= 0 ? binding_page(p) : layer_page(p);
    }
    return true;
  }
  if (p.m >= 0) {
    *flat = p.s < 0 && w.empty();
    if (p.s >= 0) out = step_page(p);
    else if (w == "opts") out = macro_options_page(p);
    else if (w.empty()) out = macro_page(p);
    else return false;
    return true;
  }
  if (w == "macros") {
    out = macros_page(p);
    return true;
  }
  return false;
}

/* ---- values ---------------------------------------------------------------- */

/* Every plain number or enum, for reading and writing alike. */
template <typename F> bool number(const Path &p, F &&fn) {
  const std::string &f = p.word;
  if (p.s >= 0) {
    if (f == "dur") return fn(step(p).duration_ms), true;
    return false;
  }
  if (p.m >= 0) {
    if (f == "repeat") return fn(macro(p).repeat_while_held), true;
    return false;
  }
  if (p.o >= 0) {
    kb::OutputDef &o = output(p);
    if (f == "trg") return fn(o.trigger), true;
    if (f == "stick") return fn(o.axis), true;
    if (f == "rise") return fn(o.ramp.rise_ms), true;
    if (f == "fall") return fn(o.ramp.fall_ms), true;
    if (f == "rcurve") return fn(o.ramp.curve_x100), true;
    return false;
  }
  if (p.b >= 0) {
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
  if (p.l >= 0) {
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
  if (f == "on") return fn(s_edit.p.enabled), true;
  if (f == "merge") return fn(s_edit.p.merge_real_pad), true;
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

int find_layer(const std::string &name) {
  for (size_t i = 0; i < s_edit.p.layers.size(); ++i)
    if (s_edit.p.layers[i].name == name)
      return static_cast<int>(i);
  return -1;
}
int find_macro(const std::string &name) {
  for (size_t i = 0; i < s_edit.p.macros.size(); ++i)
    if (s_edit.p.macros[i].name == name)
      return static_cast<int>(i);
  return -1;
}

/* Names are what outputs refer to: printable ASCII, unique, no edges. */
bool clean_name(std::string name, std::string *out) {
  while (!name.empty() && name.back() == ' ')
    name.pop_back();
  while (!name.empty() && name.front() == ' ')
    name.erase(0, 1);
  if (name.empty() || name.size() > 32 ||
      !std::all_of(name.begin(), name.end(),
                   [](char c) { return c >= 0x20 && c < 0x7f; }))
    return false;
  *out = name;
  return true;
}

void rename_refs(bool layers, const std::string &from, const std::string &to) {
  for (kb::LayerDef &l : s_edit.p.layers)
    for (kb::BindingDef &b : l.bindings)
      for (kb::OutputDef &o : b.outputs) {
        std::string &ref = layers ? o.layer : o.macro;
        const bool kind = layers ? (o.kind == kb::OutputKind::LayerHold ||
                                    o.kind == kb::OutputKind::LayerToggle)
                                 : o.kind == kb::OutputKind::Macro;
        if (kind && ref == from)
          ref = to;
      }
  if (to.empty()) /* the target is being deleted: drop what pointed at it */
    for (kb::LayerDef &l : s_edit.p.layers)
      for (kb::BindingDef &b : l.bindings)
        std::erase_if(b.outputs, [&](const kb::OutputDef &o) {
          return (layers ? (o.kind == kb::OutputKind::LayerHold ||
                            o.kind == kb::OutputKind::LayerToggle)
                         : o.kind == kb::OutputKind::Macro) &&
                 (layers ? o.layer : o.macro).empty();
        });
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

} // namespace

/* ---- shared API ------------------------------------------------------------ */

bool parse_id(std::string_view id, Path *out) {
  constexpr std::string_view kPrefix = "id_kbm-";
  return id.starts_with(kPrefix) && parse_tokens(id.substr(kPrefix.size()), out) &&
         !out->word.empty();
}

std::string resource(const Path &p, const char *page) {
  return "kbm-" + key(p) + (page ? std::string("-") + page : "") + ".xml";
}

static kb::Profile *editable(const Path &p) {
  if (p.t == "all" || p.t == "help")
    return nullptr;
  load(p.t);
  return s_edit.own && !under_deleted(p) && in_range(p) ? &s_edit.p : nullptr;
}

/* The daemon is told off the UI thread (its reply may take long); changes
   made meanwhile ride along with the next send. */
std::atomic<bool> s_reload_wanted{false};
std::atomic<bool> s_reload_running{false};

void *send_reloads(void *) {
  while (s_reload_wanted.exchange(false)) {
    std::string reply;
    if (!IPC_Client::getInstance(false).IPCSendCommand(BREW_KBM_RELOAD, reply))
      LOG_WARN("[kbm] daemon did not take the reload");
  }
  s_reload_running.store(false);
  /* a change that came in just as the loop ended */
  if (s_reload_wanted.load() && !s_reload_running.exchange(true)) {
    pthread_t thread;
    if (pthread_create(&thread, nullptr, send_reloads, nullptr) == 0)
      pthread_detach(thread);
    else
      s_reload_running.store(false);
  }
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

bool save(const Path &p) {
  if (p.t == "all")
    return true; /* settings are saved where they change */
  if (!kb::SaveProfile(file_for(p.t), s_edit.p)) {
    notify("notify.kbm.save_failed");
    return false;
  }
  s_edit.own = true;
  tell_daemon();
  return true;
}

void create_profile(const Path &p, bool from_template) {
  load(p.t);
  kb::Profile fresh;
  if (from_template) {
    fresh = kb::DefaultProfile();
  } else {
    fresh.layers.emplace_back();
    fresh.layers[0].name = "base";
  }
  fresh.name = game_name(p.t);
  s_edit.p = std::move(fresh);
  s_edit.own = true;
  s_deleted = {};
  save(p);
}

void delete_profile(const Path &p) {
  kb::DeleteProfile(file_for(p.t));
  s_edit.t.clear();
  load(p.t);
  tell_daemon();
}

/* A new output of the given kind on the binding at p. */
static bool add_output(const Path &p, const std::string &what, Path *created) {
  if (p.o >= 0)
    return false;
  const kb::Profile *pr = &s_edit.p;
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

bool add_item(const Path &p, const std::string &what, Path *created) {
  kb::Profile *pr = editable(p);
  if (!pr)
    return false;
  if (p.b >= 0) {
    if (!add_output(p, what, created))
      return false;
  } else if (what == "layer" && p.l < 0 && p.m < 0) {
    if (pr->layers.size() >= static_cast<size_t>(kb::kMaxLayers))
      return false;
    kb::LayerDef l;
    l.name = free_name(true);
    l.mouse_inherit = true; /* a new layer starts with the mouse below it */
    pr->layers.push_back(std::move(l));
    *created = with(p, 'l', static_cast<int>(pr->layers.size()) - 1);
  } else if (what == "bind" && p.l >= 0 && p.b < 0) {
    size_t total = 0;
    for (const kb::LayerDef &l : pr->layers)
      total += l.bindings.size();
    if (total >= static_cast<size_t>(kb::kMaxBindings))
      return false;
    layer(p).bindings.emplace_back(); /* no input yet: the editor asks for it */
    *created = with(p, 'b', static_cast<int>(layer(p).bindings.size()) - 1);
  } else if (what == "macro" && p.l < 0 && p.m < 0) {
    if (pr->macros.size() >= static_cast<size_t>(kb::kMaxMacros))
      return false;
    kb::MacroDef m;
    m.name = free_name(false);
    m.steps.push_back(new_step()); /* a macro without steps is dropped */
    pr->macros.push_back(std::move(m));
    *created = with(p, 'm', static_cast<int>(pr->macros.size()) - 1);
  } else if (what == "step" && p.m >= 0 && p.s < 0) {
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
  if (p.s >= 0) {
    macro(p).steps.erase(macro(p).steps.begin() + p.s);
  } else if (p.m >= 0) {
    rename_refs(false, macro(p).name, std::string());
    pr->macros.erase(pr->macros.begin() + p.m);
    pages = 2; /* macro options and the macro */
  } else if (p.o >= 0) {
    binding(p).outputs.erase(binding(p).outputs.begin() + p.o);
  } else if (p.b >= 0) {
    layer(p).bindings.erase(layer(p).bindings.begin() + p.b);
  } else if (p.l > 0) {
    rename_refs(true, layer(p).name, std::string());
    pr->layers.erase(pr->layers.begin() + p.l);
    pages = 2; /* layer options and the layer */
  } else {
    return 0;
  }
  s_deleted = p;
  s_deleted.word.clear();
  return save(p) ? pages : 0;
}

bool clear_modifiers(const Path &p) {
  if (!editable(p) || p.b < 0 || p.o >= 0)
    return false;
  for (kb::Input &m : binding(p).modifiers)
    m = kb::kInputNone;
  return save(p);
}

SetInput set_input(const Path &p, kb::Input input, const kb::Input *mods) {
  kb::Settings settings = kb::LoadSettings(kRoot);
  if (p.t == "all") {
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
  if (!editable(p) || p.b < 0 || p.o >= 0)
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
  if (p.t == "all") {
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
    const uint32_t mask = p.s >= 0 ? step(p).buttons : p.o >= 0 ? output(p).buttons : 0;
    out = (mask & n->mask) ? "1" : "0";
    return p.s >= 0 || p.o >= 0;
  }
  if (p.s >= 0) {
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
  if (p.o >= 0) {
    const kb::OutputDef &o = output(p);
    if (f == "dir") out = std::to_string(direction_of(o));
    else if (f == "str" || f == "scale") out = std::to_string(pct(o.value, 32767));
    else if (f == "layer") out = std::to_string(std::max(find_layer(o.layer), 1));
    else if (f == "how") out = o.kind == kb::OutputKind::LayerToggle ? "1" : "0";
    else if (f == "macro") out = std::to_string(std::max(find_macro(o.macro), 0));
    else return false;
    return true;
  }
  if (p.m >= 0 && f == "name") {
    out = macro(p).name;
    return true;
  }
  if (p.l >= 0) {
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
  if (p.t == "all") {
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
    if (p.s < 0 && p.o < 0)
      return false;
    uint32_t &mask = p.s >= 0 ? step(p).buttons : output(p).buttons;
    mask = value == "1" ? (mask | pn->mask) : (mask & ~pn->mask);
    return true;
  }
  if (p.s >= 0) {
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
  if (p.o >= 0) {
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
    if (f == "layer" && numeric && n >= 1 && n < static_cast<long>(s_edit.p.layers.size())) {
      o.layer = s_edit.p.layers[n].name;
      return true;
    }
    if (f == "how" && numeric && (o.kind == kb::OutputKind::LayerHold ||
                                  o.kind == kb::OutputKind::LayerToggle)) {
      o.kind = n ? kb::OutputKind::LayerToggle : kb::OutputKind::LayerHold;
      return true;
    }
    if (f == "macro" && numeric && n >= 0 && n < static_cast<long>(s_edit.p.macros.size())) {
      o.macro = s_edit.p.macros[n].name;
      return true;
    }
    return false;
  }
  if (f == "name" && (p.m >= 0 || p.l > 0)) {
    std::string name;
    const bool is_layer = p.m < 0;
    std::string &current = is_layer ? layer(p).name : macro(p).name;
    if (!clean_name(value, &name) || (is_layer && name.size() > 23)) {
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
  if (p.l >= 0 && (f == "ix" || f == "iy")) {
    uint8_t &inv = layer(p).mouse.invert;
    const uint8_t bit = f == "ix" ? 1 : 2;
    inv = value == "1" ? (inv | bit) : (inv & ~bit);
    return true;
  }
  return false;
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

/* ---- Toolbox hooks ----------------------------------------------------------- */

void generate_xml(std::string &xml, const std::string &resource_name) {
  std::string_view relative;
  if (!toolbox::settings_plugins_relative(resource_name, &relative))
    relative = resource_name;
  Path p;
  bool parsed = false;
  s_deleted = {}; /* a page opened anew is never the one deleted */
  if (relative == "kbm.xml") {
    p.t = "all";
    parsed = true;
    s_edit.t.clear(); /* coming in fresh: read the files again */
    scan_games();
  } else if (relative.starts_with("kbm-") && relative.ends_with(".xml")) {
    parsed = parse_tokens(relative.substr(4, relative.size() - 8), &p);
  }

  ps5ui::Node model;
  bool flat = false;
  if (!parsed || !build(p, model, &flat)) {
    LOG_WARN("[kbm] unknown page %.*s", static_cast<int>(relative.size()),
             relative.data());
    model = deleted_page(Path{"help"}, "unknown");
    flat = false;
  }
  if (!flat) {
    xml = ps5ui::build_document(model);
    return;
  }
  xml = onion::shellui::settings::publish(model, [p](ps5ui::Node &next) {
    bool next_flat = false;
    return build(p, next, &next_flat) && next_flat;
  });
}

bool control_value(const std::string &id, std::string &out) {
  Path p;
  return parse_id(id, &p) && get_value(p, out);
}

} // namespace kbm_toolbox
