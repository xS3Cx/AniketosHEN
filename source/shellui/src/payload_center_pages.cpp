#include "payload_center.hpp"

#include "ps5_settings_ui.hpp"
#include "settings_page_refresh.hpp"
#include "shellui_payload_state.hpp"
#include "shellui_payloads.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/net.h>
#include <onion/payload_catalog_policy.hpp>
#include <onion/payload_config.h>

#include <onion_cjson.hpp>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace onion::shellui::payload_center {
namespace {

constexpr const char *kIconPlugins =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_plugins.png";
constexpr std::size_t kMaxMetaBytes = 128 * 1024;

bool marker_present(const PayloadEntry &entry) {
  return if_exists((entry.shellui_path + ".auto_start").c_str());
}

struct MetaView {
  std::string id;
  std::string name;
  std::string version;
  std::string source_name;
  std::string last_update;
  std::string category;
  std::string icon_key;
  bool present = false;
};

MetaView meta_view(const std::string &shellui_path) {
  MetaView view;
  FILE *file = std::fopen((shellui_path + ".meta.json").c_str(), "rb");
  if (!file)
    return view;
  struct stat st {};
  if (fstat(fileno(file), &st) != 0 || st.st_size < 0 ||
      static_cast<std::size_t>(st.st_size) > kMaxMetaBytes) {
    std::fclose(file);
    return view;
  }
  std::string raw;
  char buffer[4096];
  std::size_t read_bytes = 0;
  while ((read_bytes = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    raw.append(buffer, read_bytes);
  std::fclose(file);
  if (raw.empty())
    return view;
  onion_cjson::Root root(raw);
  if (!root)
    return view;
  view.id = onion_cjson::string_item(root.get(), "id", "");
  view.name = onion_cjson::string_item(root.get(), "name", "");
  view.version = onion_cjson::string_item(root.get(), "item_version", "");
  if (view.version.empty())
    view.version = onion_cjson::string_item(root.get(), "version", "");
  view.source_name = onion_cjson::string_item(root.get(), "source_name", "");
  view.last_update = onion_cjson::string_item(root.get(), "last_update", "");
  view.category = onion_cjson::string_item(root.get(), "category", "");
  view.icon_key = onion_cjson::string_item(root.get(), "icon_key", "");
  view.present = !view.id.empty();
  return view;
}

std::string display_name(const PayloadEntry &entry) {
  const std::string name = meta_view(entry.shellui_path).name;
  return name.empty() ? entry.name : name;
}

int enabled_position(const std::vector<PayloadEntry> &list,
                     const PayloadEntry &target) {
  int position = 0;
  for (const auto &entry : list) {
    if (!marker_present(entry))
      continue;
    ++position;
    if (entry.id == target.id)
      return position;
  }
  return 0;
}

std::string version_text(const Entry &entry) {
  if (entry.item.version.empty())
    return toolbox_i18n::tr("payload_center.version_unknown");
  return entry.item.version;
}

std::string status_text(const Entry &entry) {
  if (entry.update_available)
    return toolbox_i18n::tr("payload_center.status.update");
  if (entry.installed)
    return toolbox_i18n::tr("payload_center.status.installed");
  return toolbox_i18n::tr("payload_center.status.install");
}

std::string or_dash(const std::string &text) {
  return text.empty() ? "-" : text;
}

std::optional<std::string> icon_for_key(const std::string &icon_key) {
  if (icon_key.empty())
    return std::nullopt;
  const std::string name =
      onion::payload_catalog::icon_cache_name(icon_key) + ".png";
  for (const char *root : {ONION_DIST_USER_DATA_ROOT, ONION_DIST_DATA_ROOT}) {
    const std::string path =
        std::string(root) + "/payload_center/icons/" + name;
    if (access(path.c_str(), F_OK) == 0)
      return path;
  }
  return std::nullopt;
}

std::string row_icon(const std::string &icon_key) {
  const auto icon = icon_for_key(icon_key);
  return icon ? *icon : kIconPlugins;
}

bool unmanaged_target(const Entry &entry) {
  const std::string name =
      onion::payload_catalog::slug(entry.item.name) + ".elf";
  for (const char *root :
       {ONION_DIST_USER_DATA_ROOT, ONION_DIST_DATA_ROOT}) {
    const std::string path = std::string(root) + "/payloads/" + name;
    if (access(path.c_str(), F_OK) != 0)
      continue;
    if (access((path + ".meta.json").c_str(), F_OK) != 0)
      return true;
  }
  return false;
}

std::string install_confirm(const Entry &entry) {
  std::string text =
      toolbox_i18n::format("payload_center.confirm.install_fmt",
                           entry.item.name.c_str());
  if (!entry.item.has_checksum)
    text += std::string("\n") +
            toolbox_i18n::tr("payload_center.confirm.no_checksum");
  if (onion::payload_catalog::is_system_or_conflicting_name(entry.item.name))
    text += std::string("\n") +
            toolbox_i18n::format("payload_center.confirm.risk_fmt",
                                 entry.item.name.c_str());
  if (unmanaged_target(entry))
    text += std::string("\n") +
            toolbox_i18n::tr("payload_center.confirm.replace");
  return text;
}

struct InstalledRow {
  const PayloadEntry *entry = nullptr;
  std::string display;
  std::string version;
  std::string icon_key;
  int position = 0;
  bool enabled = false;
  bool running = false;
  bool update = false;
};

bool identity_has_update(const std::string &identity) {
  for (const auto &entry : entries())
    if (entry.identity == identity && entry.update_available)
      return true;
  return false;
}

std::string identity_icon_key(const std::string &identity) {
  std::string catalog_icon;
  for (const auto &entry : entries()) {
    if (entry.identity != identity)
      continue;
    if (!entry.icon_key.empty())
      return entry.icon_key;
    if (catalog_icon.empty())
      catalog_icon = entry.item.icon_key;
  }
  return catalog_icon;
}

std::vector<InstalledRow> installed_rows() {
  g_ui.payloads_list = shellui_scan_payloads();
  std::vector<InstalledRow> rows;
  rows.reserve(g_ui.payloads_list.size());
  for (const auto &entry : g_ui.payloads_list) {
    const MetaView meta = meta_view(entry.shellui_path);
    InstalledRow row;
    row.entry = &entry;
    row.display = meta.name.empty() ? entry.name : meta.name;
    row.version =
        meta.version.empty() ? toolbox_i18n::tr("payload_center.version_unknown")
                             : meta.version;
    row.icon_key = meta.icon_key.empty() ? identity_icon_key(entry.id)
                                         : meta.icon_key;
    row.enabled = marker_present(entry);
    row.running = shellui_payload_is_running(entry.tid.c_str());
    row.position = row.enabled ? enabled_position(g_ui.payloads_list, entry) : 0;
    row.update = identity_has_update(entry.id);
    rows.push_back(std::move(row));
  }
  std::sort(rows.begin(), rows.end(), [](const InstalledRow &a,
                                         const InstalledRow &b) {
    const std::string left = onion::payload_catalog::ascii_lower(a.display);
    const std::string right = onion::payload_catalog::ascii_lower(b.display);
    if (left != right)
      return left < right;
    return a.entry->shellui_path < b.entry->shellui_path;
  });
  return rows;
}

std::string installed_header() {
  std::string text = toolbox_i18n::tr("payload_center.installed.ftp_path");
  text += std::string("\n") + toolbox_i18n::tr("payload_center.installed.usb");
  return text;
}

std::string installed_subtitle(const InstalledRow &row) {
  std::string text =
      row.version + " \xC2\xB7 " +
      toolbox_i18n::tr(row.enabled ? "payload_center.enabled"
                                   : "payload_center.disabled") +
      " \xC2\xB7 " +
      toolbox_i18n::tr(row.running ? "payload_center.running"
                                   : "payload_center.stopped");
  if (row.position > 0)
    text += std::string(" \xC2\xB7 ") +
            toolbox_i18n::format("payload_center.position_fmt", row.position);
  if (row.update)
    text += std::string(" \xC2\xB7 ") + toolbox_i18n::tr("payload_center.update");
  return text;
}

std::string browse_subtitle(const Entry &entry) {
  const char *status = entry.update_available
                           ? "payload_center.update"
                           : entry.installed ? "payload_center.status.installed"
                                             : "payload_center.status.install";
  return or_dash(entry.item.category) + " \xC2\xB7 " + version_text(entry) +
         " \xC2\xB7 " + toolbox_i18n::tr(status);
}

bool installed_model(ps5ui::Node &model) {
  reload(false);
  /* The catalog search belongs to the Browse page visit. */
  g_ui.pldc_query.clear();
  const std::vector<InstalledRow> all = installed_rows();
  ps5ui::Page page("id_pldc_installed",
                   toolbox_i18n::tr("payload_center.manage"));
  page.label("id_pldc_installed_header", installed_header(),
             ps5ui::Style::Center)
      .button("id_pldc_refresh_list",
              toolbox_i18n::tr("payload_center.refresh_list"))
      .label("id_pldc_installed_summary",
             toolbox_i18n::format(
                 "payload_center.installed.summary_fmt",
                 static_cast<int>(all.size()),
                 static_cast<int>(std::count_if(
                     all.begin(), all.end(),
                     [](const InstalledRow &row) { return row.enabled; })),
                 updates_available()),
             ps5ui::Style::Center);

  for (const auto &row : all) {
    page.link("id_payload_item_" + row.entry->id, row.display,
              toolbox::payload_config_xml(row.entry->id),
              installed_subtitle(row), row_icon(row.icon_key));
  }
  if (all.empty()) {
    page.label("id_pldc_installed_empty",
               toolbox_i18n::tr("payload_center.manage.empty"),
               ps5ui::Style::Center)
        .link("id_pldc_empty_browse", toolbox_i18n::tr("payload_center.browse"),
              "pldc_browse.xml");
  }
  model = page.root();
  return true;
}

bool autostart_model(ps5ui::Node &model) {
  g_ui.payloads_list = shellui_scan_payloads();
  ps5ui::Page page("id_pldc_autostart",
                   toolbox_i18n::tr("payload_center.autostart"));
  page.toggle("id_pldc_master", toolbox_i18n::tr("payload_center.master"),
              g_settings.payload_autostart_enabled,
              toolbox_i18n::tr("payload_center.master.sub"))
      .link("id_pldc_add_payload",
            toolbox_i18n::tr("payload_center.add_payload"),
            "pldc_add_payload.xml",
            toolbox_i18n::tr("payload_center.add_payload.sub"))
      .label("id_pldc_autostart_hint",
             toolbox_i18n::tr("payload_center.autostart.order_hint"),
             ps5ui::Style::Center);
  int position = 0;
  for (const auto &entry : g_ui.payloads_list) {
    if (!marker_present(entry))
      continue;
    ++position;
    page.link("id_pldc_pick_order_" + entry.id,
              std::to_string(position) + ". " + display_name(entry),
              "pldc_order.xml");
  }
  if (position == 0)
    page.label("id_pldc_autostart_empty",
               toolbox_i18n::tr("payload_center.autostart.empty"),
               ps5ui::Style::Center);
  model = page.root();
  return true;
}

bool add_payload_model(ps5ui::Node &model) {
  g_ui.payloads_list = shellui_scan_payloads();
  ps5ui::Page page("id_pldc_add_payload",
                   toolbox_i18n::tr("payload_center.add_payload"));
  int available = 0;
  for (const auto &entry : g_ui.payloads_list) {
    if (marker_present(entry))
      continue;
    ++available;
    page.button("id_pldc_add_pick_" + entry.id, display_name(entry));
  }
  if (available == 0)
    page.label("id_pldc_add_empty",
               toolbox_i18n::tr("payload_center.add_payload.empty"),
               ps5ui::Style::Center);
  model = page.root();
  return true;
}

bool order_model(ps5ui::Node &model) {
  g_ui.payloads_list = shellui_scan_payloads();
  const PayloadEntry *selected = nullptr;
  for (const auto &entry : g_ui.payloads_list) {
    if (entry.id == g_ui.active_pldc_order && marker_present(entry)) {
      selected = &entry;
      break;
    }
  }
  if (!selected) {
    ps5ui::Page page("id_pldc_order_page",
                     toolbox_i18n::tr("payload_center.order.title"));
    page.label("id_pldc_order_missing",
               toolbox_i18n::tr("payload_center.order.missing"),
               ps5ui::Style::Center);
    model = page.root();
    return true;
  }

  std::vector<const PayloadEntry *> ordered;
  for (const auto &entry : g_ui.payloads_list)
    if (marker_present(entry))
      ordered.push_back(&entry);

  int current = 0;
  for (std::size_t i = 0; i < ordered.size(); ++i)
    if (ordered[i]->id == selected->id)
      current = static_cast<int>(i) + 1;

  const PayloadEntry &entry = *selected;
  OnionPayloadConfig config{};
  onion_payload_config_load(entry.shellui_path.c_str(), &config);
  ps5ui::Page page("id_pldc_order_page", display_name(entry));
  const int count = static_cast<int>(ordered.size());
  page.label("id_pldc_order_position",
             toolbox_i18n::format("payload_center.order.position_fmt", current,
                                  count),
             ps5ui::Style::Center)
      .list(
          "id_pldc_order", toolbox_i18n::tr("payload_center.order.position"),
          [count](ps5ui::ListBuilder &L) {
            for (int i = 1; i <= count; ++i)
              L.item("id_pldc_order_" + std::to_string(i), std::to_string(i),
                     std::to_string(i));
          },
          toolbox_i18n::tr("payload_center.order.sub"), std::to_string(current))
      .text_field("id_payload_delay_" + entry.id,
                  toolbox_i18n::tr("payload_center.order.delay"),
                  toolbox_i18n::tr("payload_center.order.delay.sub"), "number",
                  "1", "3", std::nullopt, std::nullopt, std::nullopt,
                  std::to_string(config.delay_seconds))
      .button("id_pldc_order_disable",
              toolbox_i18n::tr("payload_center.order.disable"));
  const bool removable =
      entry.path.rfind(ONION_DIST_DATA_ROOT "/payloads/", 0) == 0;
  if (removable) {
    page.button("id_pldc_order_delete", toolbox_i18n::tr("payload_center.remove"),
                std::nullopt, std::nullopt, std::nullopt, ps5ui::Style::None,
                toolbox_i18n::format("payload_center.confirm.remove_fmt",
                                     display_name(entry).c_str()),
                toolbox_i18n::confirm_phrase());
  }
  model = page.root();
  return true;
}

bool browse_model(ps5ui::Node &model) {
  reload(false);
  ps5ui::Page page("id_pldc_browse",
                   toolbox_i18n::tr("payload_center.browse"));
  page.button("id_pldc_search", toolbox_i18n::tr("payload_center.search"),
              toolbox_i18n::tr("payload_center.search.sub"))
      .button("id_pldc_refresh", toolbox_i18n::tr("payload_center.refresh"),
              std::nullopt);
  if (!g_ui.pldc_query.empty())
    page.button("id_pldc_clear_filter",
                toolbox_i18n::tr("payload_center.clear_filter"));
  const auto visible = visible_entries();
  for (std::size_t i = 0; i < visible.size(); ++i) {
    const Entry &entry = *visible[i];
    page.link("id_pldc_pick_" + std::to_string(i), entry.item.name,
              "pldc_detail.xml", browse_subtitle(entry),
              row_icon(entry.item.icon_key.empty() ? entry.icon_key
                                                   : entry.item.icon_key));
  }
  if (visible.empty())
    page.label("id_pldc_browse_empty",
               toolbox_i18n::tr("payload_center.list.empty"),
               ps5ui::Style::Center);
  page.link("id_pldc_settings",
            toolbox_i18n::tr("payload_center.catalog_settings"),
            "pldc_settings.xml",
            toolbox_i18n::tr("payload_center.catalog_settings.sub"));
  model = page.root();
  return true;
}

bool detail_model(ps5ui::Node &model) {
  reload(false);
  const Entry *entry = find_entry(g_ui.active_pldc_item);
  if (!entry) {
    ps5ui::Page page("id_pldc_detail",
                     toolbox_i18n::tr("payload_center.detail"));
    page.label("id_pldc_detail_missing",
               toolbox_i18n::tr("payload_center.detail.missing"),
               ps5ui::Style::Center);
    model = page.root();
    return true;
  }
  ps5ui::Page page("id_pldc_detail", entry->item.name);
  page.label("id_pldc_name", entry->item.name)
      .label("id_pldc_version", version_text(*entry))
      .label("id_pldc_category", or_dash(entry->item.category))
      .label("id_pldc_size", "-")
      .label("id_pldc_updated", or_dash(entry->item.last_update))
      .label("id_pldc_source", or_dash(entry->source_name));
  if (!entry->item.description.empty())
    page.label("id_pldc_description", entry->item.description,
               ps5ui::Style::Center);
  if (entry->item.insecure || entry->item.unsupported_binary) {
    page.label("id_pldc_unsupported",
               toolbox_i18n::tr("payload_center.detail.unsupported"),
               ps5ui::Style::Center);
  } else if (entry->update_available) {
    page.button("id_pldc_install",
                toolbox_i18n::tr("payload_center.update"), std::nullopt,
                status_text(*entry), std::nullopt, ps5ui::Style::None,
                install_confirm(*entry), toolbox_i18n::confirm_phrase());
  } else if (!entry->installed) {
    page.button("id_pldc_install",
                toolbox_i18n::tr("payload_center.install"), std::nullopt,
                status_text(*entry), std::nullopt, ps5ui::Style::None,
                install_confirm(*entry), toolbox_i18n::confirm_phrase());
  }
  if (entry->installed) {
    g_ui.payloads_list = shellui_scan_payloads();
    page.button("id_pldc_remove", toolbox_i18n::tr("payload_center.remove"),
                std::nullopt, entry->installed_path, std::nullopt,
                ps5ui::Style::None,
                toolbox_i18n::format("payload_center.confirm.remove_fmt",
                                     entry->item.name.c_str()),
                toolbox_i18n::confirm_phrase());
  }
  model = page.root();
  return true;
}

bool settings_model(ps5ui::Node &model) {
  reload(false);
  ps5ui::Page page("id_pldc_settings",
                   toolbox_i18n::tr("payload_center.catalog_settings"));
  page.label("id_pldc_last_refresh", last_refresh_text(), ps5ui::Style::Center)
      .button("id_pldc_refresh",
              toolbox_i18n::tr("payload_center.refresh"), std::nullopt)
      .button("id_pldc_clear_cache",
              toolbox_i18n::tr("payload_center.clear_cache"),
              toolbox_i18n::tr("payload_center.clear_cache.sub"), std::nullopt,
              std::nullopt, ps5ui::Style::None,
              toolbox_i18n::tr("payload_center.clear_cache.confirm"))
      .button("id_pldc_add_source",
              toolbox_i18n::tr("payload_center.add_source"), std::nullopt);
  for (const auto &source : sources()) {
    const std::string key =
        onion::payload_catalog::source_hash8(source.url);
    page.label("id_pldc_source_" + key, source.name, ps5ui::Style::Center)
        .label("id_pldc_source_url_" + key, source.url,
               ps5ui::Style::Center);
    if (!source.builtin) {
      page.button("id_pldc_remove_source_" + key,
                  toolbox_i18n::tr("payload_center.remove_source"),
                  source.name);
    }
  }
  model = page.root();
  return true;
}

bool post_install_model(ps5ui::Node &model) {
  ps5ui::Page page("id_pldc_post",
                   toolbox_i18n::tr("payload_center.post.title"));
  std::string name = post_install_name();
  if (name.empty())
    name = post_install_identity();
  page.label("id_pldc_post_installed",
             toolbox_i18n::format("payload_center.post.installed_fmt",
                                  name.c_str()),
             ps5ui::Style::Center)
      .toggle("id_pldc_post_autostart",
              toolbox_i18n::tr("payload_center.post.autostart"),
              post_install_autostart_enabled(),
              toolbox_i18n::tr("payload_center.post.autostart.sub"))
      .button("id_pldc_post_done",
              toolbox_i18n::tr("payload_center.post.done"))
      .button("id_pldc_post_installed",
              toolbox_i18n::tr("payload_center.post.installed"));
  model = page.root();
  return true;
}

} // namespace

void generate_center_xml(std::string &xml) {
  ps5ui::Page page("id_pldc_center",
                   toolbox_i18n::tr("payload_center.title"));
  page.link("id_pldc_installed", toolbox_i18n::tr("payload_center.manage"),
            "payloads.xml", toolbox_i18n::tr("payload_center.manage.sub"))
      .link("id_pldc_autostart", toolbox_i18n::tr("payload_center.autostart"),
            "pldc_autostart.xml",
            toolbox_i18n::tr("payload_center.autostart.sub"))
      .link("id_pldc_browse", toolbox_i18n::tr("payload_center.browse"),
            "pldc_browse.xml", toolbox_i18n::tr("payload_center.browse.sub"))
      .link("id_pldc_included", toolbox_i18n::tr("payload_center.included"),
            "pldc_included.xml",
            toolbox_i18n::tr("payload_center.included.sub"));
  xml = page.build();
}

bool included_model(ps5ui::Node &model) {
  ps5ui::Page page("id_pldc_included",
                   toolbox_i18n::tr("payload_center.included.title"));
  page.label("id_pldc_included_about",
             toolbox_i18n::tr("payload_center.included.about"),
             ps5ui::Style::Center)
      .toggle("id_plugin_kstuff_autoload",
              toolbox_i18n::tr("kstuff.autoload"), g_settings.kstuff_autoload,
              toolbox_i18n::tr("kstuff.autoload.sub"))
      .toggle("id_plugin_shadowmount_autoload",
              toolbox_i18n::tr("shadowmount.autoload"),
              g_settings.shadowmount_autoload,
              toolbox_i18n::tr("shadowmount.autoload.sub"));
  model = page.root();
  return true;
}

void generate_included_xml(std::string &xml) {
  ps5ui::Node model;
  included_model(model);
  xml = onion::shellui::settings::publish(model, included_model);
}

void generate_installed_xml(std::string &xml) {
  ps5ui::Node model;
  installed_model(model);
  xml = onion::shellui::settings::publish(model, installed_model);
}

void generate_autostart_xml(std::string &xml) {
  ps5ui::Node model;
  autostart_model(model);
  xml = onion::shellui::settings::publish(model, autostart_model);
}

void generate_add_payload_xml(std::string &xml) {
  ps5ui::Node model;
  add_payload_model(model);
  xml = onion::shellui::settings::publish(model, add_payload_model);
}

void generate_order_xml(std::string &xml) {
  ps5ui::Node model;
  order_model(model);
  xml = onion::shellui::settings::publish(model, order_model);
}

void generate_browse_xml(std::string &xml) {
  ps5ui::Node model;
  browse_model(model);
  xml = onion::shellui::settings::publish(model, browse_model);
}

void generate_detail_xml(std::string &xml) {
  ps5ui::Node model;
  detail_model(model);
  xml = onion::shellui::settings::publish(model, detail_model);
}

void generate_settings_xml(std::string &xml) {
  ps5ui::Node model;
  settings_model(model);
  xml = onion::shellui::settings::publish(model, settings_model);
}

void generate_post_install_xml(std::string &xml) {
  ps5ui::Node model;
  post_install_model(model);
  xml = onion::shellui::settings::publish(model, post_install_model);
}

} // namespace onion::shellui::payload_center
