#include "onpress.hpp"
#include "payload_center.hpp"
#include "shellui_payload_state.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_navigation.hpp"

#include <onion/distribution.h>
#include <onion/payload_identity.h>

#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace {

bool enabled(const std::string &value) {
  return value == "1" || value == "true";
}

const onion::shellui::payload_center::Entry *active_entry() {
  return onion::shellui::payload_center::find_entry(g_ui.active_pldc_item);
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

OnPressResult add_url(OnPressContext &ctx) {
  ctx.dirty = false;
  onion::shellui::payload_center::request_install_url();
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
  g_ui.pldc_category.clear();
  toolbox_push_resource("pldc_list.xml");
  return OnPressResult::Consumed;
}

OnPressResult category(OnPressContext &ctx) {
  ctx.dirty = false;
  constexpr const char *kPrefix = "id_pldc_cat_";
  const std::string suffix = ctx.id.substr(std::string(kPrefix).size());
  if (suffix == "all") {
    g_ui.pldc_category.clear();
  } else {
    char *end = nullptr;
    const unsigned long index = std::strtoul(suffix.c_str(), &end, 10);
    if (!end || *end)
      return OnPressResult::Consumed;
    const auto categories =
        onion::shellui::payload_center::categories();
    if (index >= categories.size())
      return OnPressResult::Consumed;
    g_ui.pldc_category = categories[index];
  }
  return OnPressResult::Handled;
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

OnPressResult payload_autostart(OnPressContext &ctx) {
  ctx.dirty = false;
  constexpr const char *kPrefix = "id_payload_autostart_";
  const std::string identity = ctx.id.substr(std::string(kPrefix).size());
  for (const auto &entry : g_ui.payloads_list) {
    if (entry.id != identity)
      continue;
    const std::string marker = entry.shellui_path + ".auto_start";
    if (!enabled(ctx.value)) {
      unlink(marker.c_str());
    } else {
      const int fd = open(marker.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
      if (fd >= 0)
        close(fd);
      else
        notify("notify.payload.autostart_file");
    }
    return OnPressResult::Consumed;
  }
  return OnPressResult::Consumed;
}

const OnPressExactEntry kExact[] = {
    {"id_pldc_master", master_toggle},
    {"id_pldc_install", install},
    {"id_pldc_remove", remove},
    {"id_pldc_refresh", refresh},
    {"id_pldc_clear_cache", clear_cache},
    {"id_pldc_add_source", add_source},
    {"id_pldc_add_url", add_url},
    {"id_pldc_search", search},
    {"id_pldc_clear_filter", clear_filter},
};

const OnPressPrefixEntry kPrefix[] = {
    {"id_pldc_cat_", category},
    {"id_pldc_pick_", pick},
    {"id_pldc_remove_source_", remove_source},
    {"id_payload_autostart_", payload_autostart},
};

}

const OnPressExactEntry *onpress_payload_center_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}

const OnPressPrefixEntry *onpress_payload_center_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
