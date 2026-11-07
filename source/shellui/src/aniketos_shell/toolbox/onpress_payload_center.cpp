#include "onpress.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "hooked_funcs.hpp"
#include "payload_center.hpp"
#include "shellui_payload_state.hpp"
#include "shellui_payloads.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_navigation.hpp"

#include <onion/distribution.h>
#include <onion/payload_config.h>
#include <onion/payload_identity.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

bool enabled(const std::string &value) {
  return value == "1" || value == "true";
}

bool marker_present(const PayloadEntry &entry) {
  return access((entry.shellui_path + ".auto_start").c_str(), F_OK) == 0;
}

const onion::shellui::payload_center::Entry *active_entry() {
  return onion::shellui::payload_center::find_entry(g_ui.active_pldc_item);
}

PayloadEntry *entry_by_identity(const std::string &identity) {
  for (auto &entry : g_ui.payloads_list)
    if (entry.id == identity)
      return &entry;
  return nullptr;
}

/* Writes priorities so `ordered` is the load order, first loads first. */
void save_order(const std::vector<PayloadEntry *> &ordered) {
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    OnionPayloadConfig config{};
    onion_payload_config_load(ordered[i]->shellui_path.c_str(), &config);
    config.priority = std::max(
        0, ONION_PAYLOAD_PRIORITY_MAX - static_cast<int>(i) * 10);
    if (!onion_payload_config_save(ordered[i]->shellui_path.c_str(), &config))
      notify("notify.payload.config_save_failed", ordered[i]->name.c_str());
    ordered[i]->config = config;
  }
}

/* Swaps two payloads' places by trading their priorities: two files written
   instead of every autostart payload's. */
void swap_priorities(PayloadEntry &first, PayloadEntry &second) {
  OnionPayloadConfig a{}, b{};
  onion_payload_config_load(first.shellui_path.c_str(), &a);
  onion_payload_config_load(second.shellui_path.c_str(), &b);
  std::swap(a.priority, b.priority);
  if (!onion_payload_config_save(first.shellui_path.c_str(), &a))
    notify("notify.payload.config_save_failed", first.name.c_str());
  if (!onion_payload_config_save(second.shellui_path.c_str(), &b))
    notify("notify.payload.config_save_failed", second.name.c_str());
  first.config = a;
  second.config = b;
}

OnPressResult master_toggle(OnPressContext &ctx) {
  const bool on = enabled(ctx.value);
  if (on == g_settings.payload_autostart_enabled)
    return OnPressResult::EarlyReturn;
  g_settings.payload_autostart_enabled = on;
  ctx.reload_main = true;
  notify("notify.payload_center.master_saved");
  return OnPressResult::Handled;
}

OnPressResult install(OnPressContext &ctx) {
  ctx.dirty = false;
  const auto *entry = active_entry();
  if (!entry || entry->item.insecure || entry->item.unsupported_binary) {
    notify("notify.payload_center.not_installable");
    return OnPressResult::Consumed;
  }
  (void)onion::shellui::payload_center::start_install(*entry, true);
  return OnPressResult::Consumed;
}

OnPressResult remove(OnPressContext &ctx) {
  ctx.dirty = false;
  const auto *entry = active_entry();
  if (!entry || entry->identity.empty()) {
    notify("notify.payload_center.remove_failed");
    return OnPressResult::Consumed;
  }
  std::string reply;
  if (!IPC_Client::getInstance(true).RemovePayload(entry->identity, reply)) {
    notify("notify.payload_center.remove_failed");
    return OnPressResult::Consumed;
  }
  notify("notify.payload_center.removed", entry->item.name.c_str());
  onion::shellui::payload_center::reload(true);
  return OnPressResult::Consumed;
}

OnPressResult refresh(OnPressContext &ctx) {
  ctx.dirty = false;
  (void)onion::shellui::payload_center::start_refresh();
  return OnPressResult::Consumed;
}

OnPressResult refresh_list(OnPressContext &ctx) {
  ctx.dirty = false;
  (void)onion::shellui::payload_center::refresh_installed_page();
  return OnPressResult::Consumed;
}

OnPressResult clear_cache(OnPressContext &ctx) {
  ctx.dirty = false;
  onion::shellui::payload_center::clear_cache();
  notify("notify.payload_center.cache_cleared");
  return OnPressResult::Consumed;
}

OnPressResult add_source(OnPressContext &ctx) {
  ctx.dirty = false;
  onion::shellui::payload_center::request_add_source();
  return OnPressResult::Consumed;
}

OnPressResult search(OnPressContext &ctx) {
  ctx.dirty = false;
  onion::shellui::payload_center::request_search();
  return OnPressResult::Consumed;
}

OnPressResult clear_filter(OnPressContext &ctx) {
  ctx.dirty = false;
  g_ui.pldc_query.clear();
  toolbox_push_resource("pldc_browse.xml");
  return OnPressResult::Consumed;
}

OnPressResult pick(OnPressContext &ctx) {
  ctx.dirty = false;
  constexpr const char *kPrefix = "id_pldc_pick_";
  const std::string suffix = ctx.id.substr(std::string(kPrefix).size());
  char *end = nullptr;
  const unsigned long index = std::strtoul(suffix.c_str(), &end, 10);
  if (!end || *end)
    return OnPressResult::Consumed;
  onion::shellui::payload_center::select_visible_index(index);
  return OnPressResult::Handled;
}

OnPressResult remove_source(OnPressContext &ctx) {
  ctx.dirty = false;
  constexpr const char *kPrefix = "id_pldc_remove_source_";
  const std::string source_id = ctx.id.substr(std::string(kPrefix).size());
  if (onion::shellui::payload_center::remove_source(source_id))
    notify("notify.payload_center.source_removed");
  else
    notify("notify.payload_center.save_failed");
  return OnPressResult::Consumed;
}

OnPressResult add_pick(OnPressContext &ctx) {
  ctx.dirty = false;
  constexpr const char *kPrefix = "id_pldc_add_pick_";
  if (ctx.id.rfind(kPrefix, 0) != 0)
    return OnPressResult::NotMine;
  const std::string identity = ctx.id.substr(std::string(kPrefix).size());
  if (g_ui.payloads_list.empty())
    g_ui.payloads_list = shellui_scan_payloads();
  PayloadEntry *entry = entry_by_identity(identity);
  if (!entry)
    return OnPressResult::Consumed;
  (void)shellui_payload_set_autostart(*entry, true);
  notify("notify.payload_center.autostart_added", entry->name.c_str());
  toolbox_pop_page();
  return OnPressResult::Consumed;
}

/* X on a row picks the payload up; from then on filter_autostart_pad has the
   controller until X puts it down. */
OnPressResult reorder_pick(OnPressContext &ctx) {
  ctx.dirty = false;
  constexpr const char *kPrefix = "id_pldc_move_";
  if (ctx.id.rfind(kPrefix, 0) != 0)
    return OnPressResult::NotMine;
  if (!g_ui.pldc_held.empty() ||
      !onion::shellui::payload_center::autostart_pick_allowed())
    return OnPressResult::Consumed;
  g_ui.pldc_held = ctx.id.substr(std::string(kPrefix).size());
  onion::shellui::payload_center::request_page_refresh(ctx.instance, ctx.id);
  return OnPressResult::Consumed;
}

OnPressResult post_autostart(OnPressContext &ctx) {
  ctx.dirty = false;
  const std::string identity =
      onion::shellui::payload_center::post_install_identity();
  if (identity.empty())
    return OnPressResult::Consumed;
  if (g_ui.payloads_list.empty())
    g_ui.payloads_list = shellui_scan_payloads();
  PayloadEntry *entry = entry_by_identity(identity);
  if (!entry) {
    g_ui.payloads_list = shellui_scan_payloads();
    entry = entry_by_identity(identity);
  }
  if (entry)
    (void)shellui_payload_set_autostart(*entry, enabled(ctx.value));
  return OnPressResult::Consumed;
}

OnPressResult post_done(OnPressContext &ctx) {
  ctx.dirty = false;
  onion::shellui::payload_center::clear_post_install_page();
  toolbox_pop_page();
  return OnPressResult::Consumed;
}

OnPressResult post_installed(OnPressContext &ctx) {
  ctx.dirty = false;
  onion::shellui::payload_center::clear_post_install_page();
  onion::shellui::payload_center::request_navigate_installed();
  toolbox_pop_page();
  return OnPressResult::Consumed;
}

const OnPressExactEntry kExact[] = {
    {"id_pldc_master", master_toggle},
    {"id_pldc_install", install},
    {"id_pldc_remove", remove},
    {"id_pldc_refresh", refresh},
    {"id_pldc_refresh_list", refresh_list},
    {"id_pldc_clear_cache", clear_cache},
    {"id_pldc_add_source", add_source},
    {"id_pldc_search", search},
    {"id_pldc_clear_filter", clear_filter},
    {"id_pldc_post_autostart", post_autostart},
    {"id_pldc_post_done", post_done},
    {"id_pldc_post_installed", post_installed},
};

const OnPressPrefixEntry kPrefix[] = {
    {"id_pldc_add_pick_", add_pick},
    {"id_pldc_move_", reorder_pick},
    {"id_pldc_pick_", pick},
    {"id_pldc_remove_source_", remove_source},
};

} // namespace

const OnPressExactEntry *onpress_payload_center_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}

namespace onion::shellui::payload_center {

/* Buttons whose press was taken here: kept from ShellUI, ButtonsPrev
   included, until let go, so their release cannot select anything either. */
static uint32_t s_taken = 0;
/* When X last put a payload down: the press that did it must not pick it up
   again on its way back up. */
static uint64_t s_dropped_ms = 0;
constexpr uint64_t kPickAfterDropMs = 400;

static void take(GamePadData &data, uint32_t buttons) {
  data.Buttons = GamePadButtons(data.Buttons & ~buttons);
  data.ButtonsPrev = GamePadButtons(data.ButtonsPrev & ~buttons);
  data.ButtonsDown = GamePadButtons(data.ButtonsDown & ~buttons);
  data.ButtonsUp = GamePadButtons(data.ButtonsUp & ~buttons);
}

bool autostart_pick_allowed() {
  return AniketosUI::MonotonicMs() - s_dropped_ms >= kPickAfterDropMs;
}

bool filter_autostart_pad(GamePadData &data) {
  bool changed = false;
  if (s_taken) {
    const uint32_t still = static_cast<uint32_t>(data.Buttons) & s_taken;
    take(data, s_taken);
    s_taken = still;
    changed = true;
  }
  if (!autostart_in_front()) {
    g_ui.pldc_held.clear();
    return changed;
  }
  const uint32_t down = data.ButtonsDown;
  if (g_ui.pldc_held.empty()) {
    /* A menu on screen (ours or the HEN menu) has OPTIONS to itself. */
    if (!(down & Option) || SystemDialogs::PopupMenuOpen())
      return changed;
    s_taken |= Option;
    take(data, Option);
    request_autostart_options({});
    return true;
  }

  constexpr uint32_t kMoving = Left | Up | Right | Down | Cross | Circle |
                               Option | Enter | Back | TouchPad;
  /* The sticks would move the focus off the held row. */
  data.AnalogLeftX = data.AnalogLeftY = 0.0f;
  data.AnalogRightX = data.AnalogRightY = 0.0f;
  const std::string held = g_ui.pldc_held;
  if (down & (Up | Down)) {
    g_ui.payloads_list = shellui_scan_payloads();
    std::vector<PayloadEntry *> ordered;
    for (auto &entry : g_ui.payloads_list)
      if (marker_present(entry))
        ordered.push_back(&entry);
    const auto at = std::find_if(ordered.begin(), ordered.end(),
        [&](const PayloadEntry *e) { return e->id == held; });
    if (at == ordered.end()) {
      g_ui.pldc_held.clear();
    } else {
      const auto to = (down & Up) ? at - (at != ordered.begin())
                                  : at + (at + 1 != ordered.end());
      if (to != at) {
        /* Equal priorities (a tie, or a drive that was away during a
           renumbering) can't be traded: number the list afresh. */
        if ((*at)->config.priority != (*to)->config.priority) {
          swap_priorities(**at, **to);
        } else {
          std::iter_swap(at, to);
          save_order(ordered);
        }
      }
    }
  } else if (down & (Cross | Circle | Enter | Back | Option)) {
    g_ui.pldc_held.clear();
    s_dropped_ms = AniketosUI::MonotonicMs();
    if (down & Option)
      request_autostart_options(held);
  }
  if (down & kMoving)
    request_autostart_refresh("id_pldc_move_" + held);
  s_taken |= down & kMoving;
  take(data, kMoving);
  return true;
}

} // namespace onion::shellui::payload_center

const OnPressPrefixEntry *onpress_payload_center_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
