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

bool sprx_loads_into(const SprxInventoryItem &entry, std::string_view title) {
  if (!entry.enabled)
    return false;
  if (entry.all_games)
    return true;
  for (const std::string &id : entry.titles)
    if (id == title)
      return true;
  return false;
}

std::string sprx_game_control_id(std::string_view title, std::string_view id) {
  return std::string(kSprxGamePrefix) + std::string(title) + "_" +
         std::string(id);
}

bool split_sprx_game_control_id(std::string_view control_id, std::string &title,
                                std::string &id) {
  std::string rest;
  if (!split_control_id(control_id, kSprxGamePrefix, rest, 11, 9 + 1 + 31) ||
      rest[9] != '_')
    return false;
  title = rest.substr(0, 9);
  id = rest.substr(10);
  return true;
}

std::string sprx_list_status(const SprxInventoryItem &entry) {
  if (entry.loaded_for_current_target)
    return toolbox_i18n::tr("sprx.status.loaded");
  if (!entry.error.empty())
    return toolbox_i18n::format("sprx.status.error_fmt", entry.error.c_str());
  if (!sprx_set_up(entry))
    return toolbox_i18n::tr("sprx.status.not_set_up");
  if (!entry.enabled)
    return toolbox_i18n::tr("sprx.status.off");
  if (entry.matches_current_target)
    return toolbox_i18n::tr("sprx.status.matches");
  return toolbox_i18n::tr(entry.all_games ? "sprx.status.ready_all"
                                          : "sprx.status.ready");
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

void append_sprx_list_links(ps5ui::Page &page,
                            const std::vector<SprxInventoryItem> &sprx) {
  for (const SprxInventoryItem &entry : sprx)
    page.link(std::string(kSprxItemPrefix) + entry.id, entry.id,
              toolbox::sprx_config_xml(entry.id), sprx_list_status(entry));
}

void append_sprx_folder_help(ps5ui::Page &page) {
  page.label("id_external_sprx_folder",
             toolbox_i18n::format("sprx.folder_fmt", kSprxFolder));
}

void fill_sprx_config(ps5ui::Page &page, const SprxInventoryItem &entry,
                      const std::vector<SprxGame> &games) {
  page.label("id_external_sprx_status_" + entry.id, sprx_list_status(entry))
      .label("id_external_sprx_path_" + entry.id, entry.path);
  if (sprx_set_up(entry))
    page.toggle(std::string(kSprxEnabledPrefix) + entry.id,
                toolbox_i18n::tr("sprx.enabled"), entry.enabled,
                toolbox_i18n::tr("sprx.enabled.sub"));
  page.toggle(std::string(kSprxAllGamesPrefix) + entry.id,
              toolbox_i18n::tr("sprx.all_games"), entry.all_games,
              toolbox_i18n::tr("sprx.all_games.sub"));
  if (!entry.all_games) {
    page.label("id_external_sprx_games_" + entry.id,
               toolbox_i18n::tr("sprx.games"));
    std::vector<SprxGame> shown = games;
    for (const std::string &title : entry.titles)
      if (std::ranges::find(shown, title, &SprxGame::id) == shown.end())
        shown.push_back({title, title});
    for (const SprxGame &game : shown)
      page.toggle(sprx_game_control_id(game.id, entry.id), game.name,
                  std::ranges::find(entry.titles, game.id) != entry.titles.end(),
                  game.id);
  }
  page.button(std::string(kSprxDeletePrefix) + entry.id,
              toolbox_i18n::tr("sprx.delete"), std::nullopt,
              toolbox_i18n::tr("sprx.delete.sub"), std::nullopt,
              ps5ui::Style::None, toolbox_i18n::tr("sprx.delete.confirm"));
}

} // namespace onion::shellui::plugin_pages
