/* Copyright (C) 2026 AniketosHEN
 *
 * Toolbox "Keyboard and Mouse": the pages and the values their controls show.
 * Presses are handled in onpress_kbm.cpp.
 *
 * Pages whose rows change (lists of games, layers, bindings, outputs,
 * macros, steps) hold only links, toggles, labels and buttons, so the
 * Settings page refresher can update them when the user comes back to them.
 * Pickers (lists, text fields) sit on pages of their own, as PS5 Settings
 * does, and never change shape while open.
 */
#include "kbm_toolbox_internal.hpp"

#include "ps5_settings_ui.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_route.hpp"

#include <onion/platform.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <utility>

namespace kbm_toolbox {
namespace {

constexpr const char *kIconGame =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_game.png";

using Choices = std::vector<std::pair<std::string, std::string>>;

ps5ui::Page new_page(const Path &p, const char *page, std::string title) {
  return ps5ui::Page("id_kbmpage-" + path_text(p) + (page ? std::string("-") + page : ""),
                     std::move(title));
}

std::optional<std::string> opt_tr(const char *k) {
  return k ? std::optional<std::string>(tr(k)) : std::nullopt;
}

using Fmt = std::string (*)(long);

void num_list(ps5ui::Page &g, const Path &base, const char *f, const char *title_key,
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

void choice_list(ps5ui::Page &g, const Path &base, const char *f, const char *title_key,
                 const char *sub_key, const Choices &items) {
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

template <size_t N> Choices enum_items(const char *const (&keys)[N]) {
  Choices items;
  for (size_t i = 0; i < N; ++i)
    items.emplace_back(std::to_string(i), tr(keys[i]));
  return items;
}

void toggle(ps5ui::Page &g, const Path &base, const char *f, std::string title,
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
  g.link("id_kbmnav-" + path_text(p) + (page ? std::string("-") + page : ""),
         std::move(title), resource(p, page), std::move(sub));
}

void text(ps5ui::Page &g, const std::string &id, const char *k) {
  g.label(id, tr(k), ps5ui::Style::Left);
}
void heading(ps5ui::Page &g, const std::string &id, const char *k) {
  g.label(id, tr(k), ps5ui::Style::Center);
}

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
  ps5ui::Page page = new_page(p, nullptr, game_name(p.title));
  if (!s_edit.own) {
    text(page, "id_kbm_uses_default", "kbm.profile.uses_default");
    action(page, p, "newtpl", "kbm.profile.create_template",
           "kbm.profile.create_template.sub");
    action(page, p, "newempty", "kbm.profile.create_empty",
           "kbm.profile.create_empty.sub");
    return page.root();
  }
  toggle(page, p, "on", tr(p.title == "default" ? "kbm.profile.enabled_default"
                                             : "kbm.profile.enabled"));
  toggle(page, p, "merge", tr("kbm.profile.merge"), "kbm.profile.merge.sub");

  heading(page, "id_kbm_layers", "kbm.layers");
  const kb::Profile &pr = s_edit.profile;
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
  if (p.title == "default")
    action(page, p, "delete", "kbm.profile.delete", "kbm.profile.delete_default.sub",
           "kbm.profile.delete_default.confirm");
  else
    action(page, p, "delete", "kbm.profile.delete", "kbm.profile.delete.sub",
           "kbm.profile.delete.confirm");
  return page.root();
}

ps5ui::Node layer_page(const Path &p) {
  const kb::LayerDef &l = layer(p);
  ps5ui::Page page = new_page(p, nullptr, layer_name(p.layer));
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

bool analog(const kb::OutputDef &o) {
  return o.kind == kb::OutputKind::AxisDirection ||
         o.kind == kb::OutputKind::Trigger;
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
  if (s_edit.profile.macros.empty())
    text(page, "id_kbm_macros_none", "kbm.macros.none");
  for (size_t i = 0; i < s_edit.profile.macros.size(); ++i)
    nav(page, with(p, 'm', static_cast<int>(i)), nullptr, s_edit.profile.macros[i].name,
        format("kbm.macro.steps_fmt", static_cast<int>(s_edit.profile.macros[i].steps.size())));
  if (s_edit.profile.macros.size() < static_cast<size_t>(kb::kMaxMacros))
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

ps5ui::Node layer_options_page(const Path &p) {
  ps5ui::Page page = new_page(p, "opts", tr("kbm.layer.options"));
  if (p.layer > 0)
    page.text_field(element_id(p, "name"), tr("kbm.layer.name"),
                    tr("kbm.layer.name.sub"), "basic_latin", "1", "23",
                    std::nullopt, std::nullopt, std::nullopt, layer(p).name);
  choice_list(page, p, "socd", "kbm.socd", "kbm.socd.sub", enum_items(kOpposingKeys));
  num_list(page, p, "move", "kbm.move_threshold", "kbm.move_threshold.sub",
           {500, 1000, 2000, 3000, 5000, 8000, 12000}, fmt_counts);
  if (p.layer > 0)
    action(page, p, "dellayer", "kbm.layer.delete", nullptr, "kbm.layer.delete.confirm");
  return page.root();
}

ps5ui::Node mouse_page(const Path &p) {
  ps5ui::Page page = new_page(p, "mouse", tr("kbm.mouse"));
  if (p.layer > 0)
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

void pulse_row(ps5ui::Page &page, const Path &p) {
  num_list(page, p, "pulse", "kbm.timing.pulse", "kbm.timing.pulse.sub",
           {16, 33, 50, 60, 80, 100, 150, 200, 300, 500}, fmt_ms);
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
  pulse_row(page, p);
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
  pulse_row(page, p);
  return page.root();
}

void ramp_rows(ps5ui::Page &page, const Path &p) {
  text(page, "id_kbm_ramp_hint", "kbm.ramp.hint");
  const std::vector<long> times = {0, 50, 100, 150, 200, 300, 400, 500, 750,
                                   1000, 1500, 2000, 3000};
  num_list(page, p, "rise", "kbm.ramp.rise", "kbm.ramp.rise.sub", times, fmt_ms);
  num_list(page, p, "fall", "kbm.ramp.fall", "kbm.ramp.fall.sub", times, fmt_ms);
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
    Choices dirs;
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
    if (s_edit.profile.layers.size() < 2) {
      text(page, "id_kbm_no_layers", "kbm.out.layer.none");
      break;
    }
    Choices names;
    for (size_t i = 1; i < s_edit.profile.layers.size(); ++i)
      names.emplace_back(std::to_string(i), s_edit.profile.layers[i].name);
    choice_list(page, p, "layer", "kbm.kind.layer", nullptr, names);
    choice_list(page, p, "how", "kbm.out.layer_how", nullptr,
                {{"0", tr("kbm.layer_how.hold")}, {"1", tr("kbm.layer_how.toggle")}});
    break;
  }
  case kb::OutputKind::Macro: {
    if (s_edit.profile.macros.empty()) {
      text(page, "id_kbm_no_macros", "kbm.out.macro.none");
      break;
    }
    Choices names;
    for (size_t i = 0; i < s_edit.profile.macros.size(); ++i)
      names.emplace_back(std::to_string(i), s_edit.profile.macros[i].name);
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
  page.text_field(element_id(p, "name"), tr("kbm.layer.name"),
                  tr("kbm.macro.name.sub"), "basic_latin", "1", "32", std::nullopt,
                  std::nullopt, std::nullopt, macro(p).name);
  action(page, p, "delmacro", "kbm.macro.delete", nullptr, "kbm.macro.delete.confirm");
  return page.root();
}

ps5ui::Node step_page(const Path &p) {
  ps5ui::Page page = new_page(p, nullptr, format("kbm.step.title_fmt", p.step + 1));
  num_list(page, p, "dur", "kbm.step.duration", "kbm.step.duration.sub",
           {16, 33, 50, 75, 100, 150, 200, 300, 500, 750, 1000, 1500, 2000, 3000, 5000},
           fmt_ms);
  text(page, "id_kbm_step_hint", "kbm.step.hint");
  for (const PadName &n : kPadNames)
    toggle(page, p, n.field, pad_name(n));
  const char *axes[][3] = {{"lx", "kbm.step.lx", "h"}, {"ly", "kbm.step.ly", "v"},
                           {"rx", "kbm.step.rx", "h"}, {"ry", "kbm.step.ry", "v"}};
  for (const auto &a : axes) {
    Choices items = {{"u", tr("kbm.step.untouched")}};
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
  if (p.title == "all") {
    out = main_page();
    return true;
  }
  if (p.title == "help") {
    out = help_page();
    return true;
  }
  load(p.title);
  if (p.layer < 0 && p.macro < 0 && w.empty()) {
    out = profile_page(p);
    return true;
  }
  if (!s_edit.own || under_deleted(p) || !in_range(p)) {
    out = deleted_page(p, w.empty() ? nullptr : w.c_str());
    return true;
  }
  if (p.layer >= 0) {
    *flat = false;
    if (p.output >= 0) out = output_page(p);
    else if (p.binding >= 0 && w == "gesture") out = gesture_page(p);
    else if (p.binding >= 0 && w == "mode") out = mode_page(p);
    else if (w == "mouse") out = mouse_page(p);
    else if (w == "opts") out = layer_options_page(p);
    else if (!w.empty()) return false;
    else {
      *flat = true;
      out = p.binding >= 0 ? binding_page(p) : layer_page(p);
    }
    return true;
  }
  if (p.macro >= 0) {
    *flat = p.step < 0 && w.empty();
    if (p.step >= 0) out = step_page(p);
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

} // namespace

void generate_xml(std::string &xml, const std::string &resource_name) {
  std::string_view relative;
  if (!toolbox::settings_plugins_relative(resource_name, &relative))
    relative = resource_name;
  Path p;
  bool parsed = false;
  s_deleted = {}; /* a page opened anew is never the one deleted */
  if (relative == "kbm.xml") {
    p.title = "all";
    parsed = true;
    s_edit.title.clear(); /* coming in fresh: read the files again */
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
