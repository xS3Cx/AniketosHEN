#include "onpress.hpp"
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

OnPressResult pick_order(OnPressContext &ctx) {
  ctx.dirty = false;
  constexpr const char *kPrefix = "id_pldc_pick_order_";
  if (ctx.id.rfind(kPrefix, 0) != 0)
    return OnPressResult::NotMine;
  g_ui.active_pldc_order = ctx.id.substr(std::string(kPrefix).size());
  return OnPressResult::Handled;
}

OnPressResult order_selected(OnPressContext &ctx) {
  ctx.dirty = false;
  char *end = nullptr;
  const long position = std::strtol(ctx.value.c_str(), &end, 10);
  if (ctx.value.empty() || !end || *end || position < 1)
    return OnPressResult::Consumed;
  if (g_ui.payloads_list.empty())
    g_ui.payloads_list = shellui_scan_payloads();

  std::vector<PayloadEntry *> ordered;
  for (auto &entry : g_ui.payloads_list)
    if (marker_present(entry))
      ordered.push_back(&entry);
  if (position > static_cast<long>(ordered.size()))
    return OnPressResult::Consumed;

  const auto selected = std::find_if(
      ordered.begin(), ordered.end(),
      [&](const PayloadEntry *entry) {
        return entry->id == g_ui.active_pldc_order;
      });
  if (selected == ordered.end())
    return OnPressResult::Consumed;

  PayloadEntry *moving = *selected;
  ordered.erase(selected);
  ordered.insert(ordered.begin() + (position - 1), moving);
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    OnionPayloadConfig config{};
    onion_payload_config_load(ordered[i]->shellui_path.c_str(), &config);
    config.priority = std::max(
        0, ONION_PAYLOAD_PRIORITY_MAX - static_cast<int>(i) * 10);
    if (!onion_payload_config_save(ordered[i]->shellui_path.c_str(), &config))
      notify("notify.payload.config_save_failed", ordered[i]->name.c_str());
    ordered[i]->config = config;
  }
  notify("notify.payload_center.order_saved");
  toolbox_pop_page();
  return OnPressResult::Consumed;
}

OnPressResult order_disable(OnPressContext &ctx) {
  ctx.dirty = false;
  if (g_ui.payloads_list.empty())
    g_ui.payloads_list = shellui_scan_payloads();
  for (const auto &entry : g_ui.payloads_list) {
    if (entry.id != g_ui.active_pldc_order)
      continue;
    unlink((entry.shellui_path + ".auto_start").c_str());
    notify("notify.payload_center.order_saved");
    toolbox_pop_page();
    return OnPressResult::Consumed;
  }
  return OnPressResult::Consumed;
}

OnPressResult order_delete(OnPressContext &ctx) {
  ctx.dirty = false;
  if (g_ui.payloads_list.empty())
    g_ui.payloads_list = shellui_scan_payloads();
  for (const auto &entry : g_ui.payloads_list) {
    if (entry.id != g_ui.active_pldc_order)
      continue;
    if (!onion::shellui::payload_center::remove_installed(entry.id, entry.name))
      notify("notify.payload_center.remove_failed");
    toolbox_pop_page();
    return OnPressResult::Consumed;
  }
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
    {"id_pldc_order", order_selected},
    {"id_pldc_order_disable", order_disable},
    {"id_pldc_order_delete", order_delete},
    {"id_pldc_post_autostart", post_autostart},
    {"id_pldc_post_done", post_done},
    {"id_pldc_post_installed", post_installed},
};

const OnPressPrefixEntry kPrefix[] = {
    {"id_pldc_pick_order_", pick_order},
    {"id_pldc_pick_", pick},
    {"id_pldc_remove_source_", remove_source},
};

} // namespace

const OnPressExactEntry *onpress_payload_center_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}

const OnPressPrefixEntry *onpress_payload_center_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
