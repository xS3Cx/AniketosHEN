#pragma once

#include "dynamic_ui_runtime.hpp"
#include "ps5_settings_ui.hpp"

#include <onion/distribution.h>
#include <onion/ipc_client.hpp>
#include <onion/plugin_folder.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace onion::shellui::plugin_pages {

inline constexpr std::string_view kPluginRunPrefix = "id_external_plugin_run_";
inline constexpr std::string_view kPluginDeletePrefix =
    "id_external_plugin_delete_";
inline constexpr std::string_view kPluginAutoStartPrefix =
    "id_external_plugin_autostart_";
inline constexpr std::string_view kPluginItemPrefix =
    "id_external_plugin_item_";
/* + title ID: a game of the Game plugins list / its Load on startup switch */
inline constexpr std::string_view kSprxItemPrefix = "id_external_sprx_item_";
inline constexpr std::string_view kSprxStartupPrefix = "id_external_sprx_on_";
/* + title ID (9) + '_' + the plugin's row in the game's list */
inline constexpr std::string_view kSprxPluginPrefix = "id_external_sprx_plugin_";
/* The plugin folders, as the player sees them (the daemon's path) and as
   ShellUI opens them. */
inline constexpr const char *kPluginFolder = ONION_DIST_DATA_ROOT "/pluginloader";
inline constexpr const char *kPluginFolderUser =
    ONION_DIST_USER_DATA_ROOT "/pluginloader";
inline constexpr const char *kSprxCatalogPath =
    ONION_DIST_DATA_ROOT "/sprx/catalog.ini";

struct SprxGame {
  std::string id;
  std::string name;
  std::string icon;
  size_t plugins = 0; /* folder plugins for it, and catalog/plugins.ini ones */
  bool load_on_startup = true;
};

/* A catalog.ini / plugins.ini entry that is on and loads into this title. */
bool legacy_loads_into(const SprxInventoryItem &entry, std::string_view title);
std::string sprx_plugin_control_id(std::string_view title, size_t row);
bool split_sprx_plugin_control_id(std::string_view control_id,
                                  std::string &title, size_t &row);

inline bool split_control_id(std::string_view control_id,
                             std::string_view prefix, std::string &id,
                             std::size_t min_len, std::size_t max_len) {
  if (!control_id.starts_with(prefix))
    return false;
  const std::string_view suffix = control_id.substr(prefix.size());
  if (suffix.size() < min_len || suffix.size() > max_len)
    return false;
  id.assign(suffix);
  return true;
}

std::string plugin_list_status(bool running, bool auto_start);

void append_plugin_list_links(
    ps5ui::Page &page, const std::vector<PluginInventoryItem> &plugins);
void fill_plugin_config(ps5ui::Page &page, const PluginInventoryItem &plugin,
                        const dynamic_ui::PluginSettingsLink *settings);

/* "SPRX · all games · Loaded at the last start" */
std::string sprx_plugin_detail(const pluginloader::Plugin &plugin,
                               const pluginloader::Status &status);
void append_sprx_folder_help(ps5ui::Page &page);
void append_sprx_game_links(ps5ui::Page &page,
                            const std::vector<SprxGame> &games);
/* Load on startup, the folder plugins as switches, then the catalog.ini /
   plugins.ini entries for this title, read-only. */
void fill_sprx_game(ps5ui::Page &page, std::string_view title,
                    const pluginloader::Config &config,
                    const std::vector<pluginloader::Plugin> &plugins,
                    const pluginloader::Status &status,
                    const std::vector<SprxInventoryItem> &legacy);

} // namespace onion::shellui::plugin_pages
