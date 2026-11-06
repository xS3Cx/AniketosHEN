#include "payload_center.hpp"

#include "shellui_payloads.hpp"
#include "shellui_state.hpp"
#include "ps5_settings_ui.hpp"
#include "toolbox_i18n.hpp"

#include <onion/distribution.h>
#include <onion/payload_catalog_policy.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

namespace onion::shellui::payload_center {
namespace {

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
  return toolbox_i18n::tr("payload_center.status.available");
}

std::string or_dash(const std::string &text) {
  return text.empty() ? "-" : text;
}

std::optional<std::string> icon_for(const Entry &entry) {
  if (entry.item.icon_key.empty())
    return std::nullopt;
  const std::string path =
      std::string(ONION_DIST_USER_DATA_ROOT) + "/payload_center/icons/" +
      onion::payload_catalog::icon_cache_name(entry.item.icon_key) + ".png";
  if (access(path.c_str(), F_OK) != 0)
    return std::nullopt;
  return path;
}

std::string subtitle_for(const Entry &entry) {
  return version_text(entry) + " · " + status_text(entry);
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

}

void generate_center_xml(std::string &xml) {
  reload(false);
  ps5ui::Page page("id_pldc_center",
                   toolbox_i18n::tr("payload_center.title"));
  const std::string hint = ftp_hint();
  if (!hint.empty())
    page.label("id_pldc_ftp", hint, ps5ui::Style::Center);
  page.link("id_pldc_autostart", toolbox_i18n::tr("payload_center.autostart"),
            "pldc_autostart.xml",
            toolbox_i18n::tr("payload_center.autostart.sub"))
      .link("id_pldc_browse", toolbox_i18n::tr("payload_center.browse"),
            "pldc_browse.xml", toolbox_i18n::tr("payload_center.browse.sub"))
      .button("id_pldc_add_url", toolbox_i18n::tr("payload_center.add_url"),
              toolbox_i18n::tr("payload_center.add_url.sub"), std::nullopt,
              std::nullopt, ps5ui::Style::None,
              toolbox_i18n::tr("payload_center.add_url.confirm"))
      .link("id_pldc_settings", toolbox_i18n::tr("payload_center.settings"),
            "pldc_settings.xml",
            toolbox_i18n::tr("payload_center.settings.sub"));
  xml = page.build();
}

void generate_autostart_xml(std::string &xml) {
  g_ui.payloads_list = shellui_scan_payloads();
  ps5ui::Page page("id_pldc_autostart",
                   toolbox_i18n::tr("payload_center.autostart"));
  page.toggle("id_pldc_master", toolbox_i18n::tr("payload_center.master"),
              g_settings.payload_autostart_enabled,
              toolbox_i18n::tr("payload_center.master.sub"));
  if (g_ui.payloads_list.empty()) {
    page.label("id_pldc_autostart_empty",
               toolbox_i18n::tr("payload_center.autostart.empty"),
               ps5ui::Style::Center);
  } else {
    for (const auto &entry : g_ui.payloads_list) {
      const bool automatic =
          access((entry.shellui_path + ".auto_start").c_str(), F_OK) == 0;
      page.toggle("id_payload_autostart_" + entry.id, entry.name, automatic,
                  entry.name);
    }
  }
  xml = page.build();
}

void generate_browse_xml(std::string &xml) {
  reload(false);
  ps5ui::Page page("id_pldc_browse",
                   toolbox_i18n::tr("payload_center.browse"));
  page.link("id_pldc_cat_all", toolbox_i18n::tr("payload_center.all"),
            "pldc_list.xml")
      .button("id_pldc_search", toolbox_i18n::tr("payload_center.search"),
              toolbox_i18n::tr("payload_center.search.sub"));
  const auto category_list = categories();
  for (std::size_t i = 0; i < category_list.size(); ++i) {
    page.link("id_pldc_cat_" + std::to_string(i), category_list[i],
              "pldc_list.xml");
  }
  if (category_list.empty())
    page.label("id_pldc_browse_empty",
               toolbox_i18n::tr("payload_center.browse.empty"),
               ps5ui::Style::Center);
  xml = page.build();
}

void generate_list_xml(std::string &xml) {
  reload(false);
  const auto visible = visible_entries();
  ps5ui::Page page("id_pldc_list",
                   toolbox_i18n::tr("payload_center.list"));
  page.label("id_pldc_list_count",
             toolbox_i18n::format("payload_center.list_count_fmt",
                                  static_cast<int>(visible.size())),
             ps5ui::Style::Center);
  if (!g_ui.pldc_query.empty())
    page.button("id_pldc_clear_filter",
                toolbox_i18n::tr("payload_center.clear_filter"), std::nullopt);
  for (std::size_t i = 0; i < visible.size(); ++i) {
    page.link("id_pldc_pick_" + std::to_string(i), visible[i]->item.name,
              "pldc_detail.xml", subtitle_for(*visible[i]),
              icon_for(*visible[i]));
  }
  if (visible.empty())
    page.label("id_pldc_list_empty",
               toolbox_i18n::tr("payload_center.list.empty"),
               ps5ui::Style::Center);
  xml = page.build();
}

void generate_detail_xml(std::string &xml) {
  reload(false);
  const Entry *entry = find_entry(g_ui.active_pldc_item);
  if (!entry) {
    ps5ui::Page page("id_pldc_detail",
                     toolbox_i18n::tr("payload_center.detail"));
    page.label("id_pldc_detail_missing",
               toolbox_i18n::tr("payload_center.detail.missing"),
               ps5ui::Style::Center);
    xml = page.build();
    return;
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
                install_confirm(*entry), toolbox_i18n::tr("pkg.msg.ok"));
  } else if (!entry->installed) {
    page.button("id_pldc_install",
                toolbox_i18n::tr("payload_center.install"), std::nullopt,
                status_text(*entry), std::nullopt, ps5ui::Style::None,
                install_confirm(*entry), toolbox_i18n::tr("pkg.msg.ok"));
  }
  if (entry->installed) {
    g_ui.payloads_list = shellui_scan_payloads();
    page.button("id_pldc_remove", toolbox_i18n::tr("payload_center.remove"),
                std::nullopt, entry->installed_path, std::nullopt,
                ps5ui::Style::None,
                toolbox_i18n::format("payload_center.confirm.remove_fmt",
                                     entry->item.name.c_str()),
                toolbox_i18n::tr("pkg.msg.ok"));
    if (!entry->identity.empty()) {
      page.toggle("id_payload_autostart_" + entry->identity,
                  toolbox_i18n::tr("payload_center.autostart.toggle"),
                  entry->autostart,
                  toolbox_i18n::tr("payload_center.autostart.toggle.sub"));
    }
  }
  xml = page.build();
}

void generate_settings_xml(std::string &xml) {
  reload(false);
  ps5ui::Page page("id_pldc_settings",
                   toolbox_i18n::tr("payload_center.settings"));
  page.toggle("id_pldc_master", toolbox_i18n::tr("payload_center.master"),
              g_settings.payload_autostart_enabled,
              toolbox_i18n::tr("payload_center.master.sub"))
      .label("id_pldc_last_refresh", last_refresh_text(), ps5ui::Style::Center)
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
  xml = page.build();
}

}
