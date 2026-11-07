#include "plugin_sprx_pages.hpp"

#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"

#include <algorithm>

namespace onion::shellui::plugin_pages {

std::string plugin_list_status(bool running, bool auto_start) {
  if (running && auto_start)
    return toolbox_i18n::tr("plugins.external.status.running_autostart");
  if (running)
    return toolbox_i18n::tr("plugins.external.status.running");
  if (auto_start)
    return toolbox_i18n::tr("plugins.external.status.stopped_autostart");
  return toolbox_i18n::tr("plugins.external.status.stopped");
}

bool legacy_loads_into(const SprxInventoryItem &entry, std::string_view title) {
  if (!entry.enabled)
    return false;
  for (const std::string &id : entry.titles)
    if (id == title)
      return true;
  for (const std::string &prefix : entry.prefixes)
    if (title.starts_with(prefix))
      return true;
  return false;
}

std::string sprx_plugin_control_id(std::string_view title, size_t row) {
  return std::string(kSprxPluginPrefix) + std::string(title) + "_" +
         std::to_string(row);
}

bool split_sprx_plugin_control_id(std::string_view control_id,
                                  std::string &title, size_t &row) {
  std::string rest;
  if (!split_control_id(control_id, kSprxPluginPrefix, rest, 11, 9 + 1 + 3) ||
      rest[9] != '_')
    return false;
  row = 0;
  for (const char c : rest.substr(10)) {
    if (c < '0' || c > '9')
      return false;
    row = row * 10 + static_cast<size_t>(c - '0');
  }
  title = rest.substr(0, 9);
  return true;
}

void append_plugin_list_links(
    ps5ui::Page &page, const std::vector<PluginInventoryItem> &plugins) {
  for (const PluginInventoryItem &plugin : plugins)
    page.link(std::string(kPluginItemPrefix) + plugin.plugin_id, plugin.name,
              toolbox::external_plugin_config_xml(plugin.plugin_id),
              plugin_list_status(plugin.running, plugin.auto_start));
}

void fill_plugin_config(ps5ui::Page &page, const PluginInventoryItem &plugin,
                        const dynamic_ui::PluginSettingsLink *settings) {
  page.toggle(std::string(kPluginRunPrefix) + plugin.plugin_id,
              toolbox_i18n::tr("plugins.external.run"), plugin.running,
              toolbox_i18n::tr("plugins.external.run.sub"))
      .toggle(std::string(kPluginAutoStartPrefix) + plugin.plugin_id,
              toolbox_i18n::tr("plugins.external.autostart"), plugin.auto_start,
              toolbox_i18n::tr("plugins.external.autostart.sub"));
  if (settings)
    page.link("id_external_plugin_settings_" + plugin.plugin_id, settings->title,
              settings->resource,
              settings->description.empty()
                  ? std::optional<std::string>{}
                  : std::optional<std::string>{settings->description});
  page.button(std::string(kPluginDeletePrefix) + plugin.plugin_id,
              toolbox_i18n::tr("plugins.external.delete"), std::nullopt,
              std::nullopt, std::nullopt, ps5ui::Style::None,
              toolbox_i18n::tr("plugins.external.delete.confirm"),
              toolbox_i18n::tr("account.activate.confirm_phrase"));
}

std::string sprx_plugin_detail(const pluginloader::Plugin &plugin,
                               const pluginloader::Status &status) {
  std::string text = pluginloader::kind_name(plugin.kind);
  if (plugin.all_games())
    text += std::string(" · ") + toolbox_i18n::tr("sprx.menu.all_games");
  const auto found = status.find(plugin.name);
  if (found == status.end() || found->second.empty())
    return text;
  const std::string &raw = found->second;
  text += " · ";
  if (raw == "loaded")
    text += toolbox_i18n::tr("sprx.status.loaded");
  else if (raw == "started")
    text += toolbox_i18n::tr("sprx.status.started");
  else if (raw == "running")
    text += toolbox_i18n::tr("sprx.status.running");
  else
    text += toolbox_i18n::format("sprx.status.error_fmt", raw.c_str());
  return text;
}

void append_sprx_folder_help(ps5ui::Page &page) {
  page.label("id_external_sprx_folder",
             toolbox_i18n::format("sprx.folder_fmt", kPluginFolder,
                                  kPluginFolder));
}

void append_sprx_game_links(ps5ui::Page &page,
                            const std::vector<SprxGame> &games) {
  for (const SprxGame &game : games) {
    const std::string status =
        game.plugins == 0 ? std::string(toolbox_i18n::tr("sprx.game.none"))
                          : toolbox_i18n::format(game.load_on_startup
                                                     ? "sprx.game.on_fmt"
                                                     : "sprx.game.off_fmt",
                                                 static_cast<unsigned>(game.plugins));
    page.link(std::string(kSprxItemPrefix) + game.id, game.name,
              toolbox::sprx_config_xml(game.id), status,
              game.icon.empty() ? std::optional<std::string>{}
                                : std::optional<std::string>{game.icon});
  }
}

void fill_sprx_game(ps5ui::Page &page, std::string_view title,
                    const pluginloader::Config &config,
                    const std::vector<pluginloader::Plugin> &plugins,
                    const pluginloader::Status &status,
                    const std::vector<SprxInventoryItem> &legacy) {
  const std::string id(title);
  page.toggle(std::string(kSprxStartupPrefix) + id,
              toolbox_i18n::tr("sprx.load_on_startup"), config.load_on_startup,
              toolbox_i18n::tr("sprx.load_on_startup.sub"));
  if (plugins.empty())
    page.label("id_external_sprx_empty",
               toolbox_i18n::format("sprx.menu.empty_fmt",
                                    (std::string(kPluginFolder) + "/" + id).c_str()));
  for (size_t row = 0; row < plugins.size(); ++row) {
    const pluginloader::Plugin &plugin = plugins[row];
    page.toggle(sprx_plugin_control_id(title, row),
                plugin.name.substr(plugin.all_games() ? 4 : 0), plugin.enabled,
                sprx_plugin_detail(plugin, status));
  }
  /* catalog.ini and plugins.ini decide these; the page only shows them. */
  size_t row = 0;
  for (const SprxInventoryItem &entry : legacy) {
    if (!legacy_loads_into(entry, title))
      continue;
    const std::string &source =
        entry.source.empty() ? std::string(kSprxCatalogPath) : entry.source;
    page.label("id_external_sprx_legacy_" + std::to_string(row++),
               entry.id + " · " +
                   toolbox_i18n::format("sprx.imported_fmt", source.c_str()));
  }
}

} // namespace onion::shellui::plugin_pages
