#include "external_sprx_ui.hpp"

#include "aniketos_shell/toolbox/kbm_toolbox_internal.hpp"
#include "plugin_sprx_pages.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"
#include "settings_page_refresh.hpp"

#include <onion/ipc_client.hpp>
#include <onion/log.h>

#include <algorithm>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace onion::shellui::external_sprx {
namespace {

namespace pl = onion::pluginloader;
using plugin_pages::kPluginFolderUser;
using plugin_pages::kSprxStartupPrefix;
using plugin_pages::split_control_id;

/* The game page on screen: its switches index into s_plugins. */
std::string s_title;
pl::Config s_config;
std::vector<pl::Plugin> s_plugins;

/* catalog.ini / plugins.ini entries; empty when the daemon does not answer. */
void refresh_legacy() {
  std::vector<SprxInventoryItem> sprx;
  if (IPC_Client::getInstance(false).ListSprx(sprx))
    g_ui.external_sprx = std::move(sprx);
  else
    g_ui.external_sprx.clear();
}

std::string game_icon(const std::string &title) {
  const std::string icon = "/user/appmeta/" + title + "/icon0.png";
  return access(icon.c_str(), R_OK) == 0 ? icon : std::string();
}

} // namespace

bool append_inventory(ps5ui::Page &page) {
  plugin_pages::append_sprx_folder_help(page);
  kbm_toolbox::scan_games();
  refresh_legacy();
  if (kbm_toolbox::s_games.empty())
    page.label("id_external_sprx_empty", toolbox_i18n::tr("kbm.games.none"));
  std::vector<plugin_pages::SprxGame> games;
  for (const kbm_toolbox::Game &game : kbm_toolbox::s_games) {
    plugin_pages::SprxGame row{game.id, game.name, game_icon(game.id)};
    const pl::Config config = pl::load_config(kPluginFolderUser, game.id);
    row.load_on_startup = config.load_on_startup;
    row.plugins = pl::list(kPluginFolderUser, game.id, config).size() +
                  static_cast<size_t>(std::ranges::count_if(g_ui.external_sprx, [&](const auto &e) {
                    return plugin_pages::legacy_loads_into(e, game.id);
                  }));
    games.push_back(std::move(row));
  }
  plugin_pages::append_sprx_game_links(page, games);
  return true;
}

static bool config_model(ps5ui::Node &model, std::string_view id) {
  s_title = std::string(id);
  s_config = pl::load_config(kPluginFolderUser, s_title);
  s_plugins = pl::list(kPluginFolderUser, s_title, s_config);
  refresh_legacy();
  ps5ui::Page page("id_sprx_config", kbm_toolbox::game_name(s_title));
  plugin_pages::fill_sprx_game(page, s_title, s_config, s_plugins,
                               pl::load_status(kPluginFolderUser, s_title),
                               g_ui.external_sprx);
  model = page.root();
  return true;
}

void generate_config_xml(std::string &xml_buffer, std::string_view id) {
  ps5ui::Node model;
  config_model(model, id);
  xml_buffer = settings::publish(model, [key = std::string(id)](ps5ui::Node &next) {
    return config_model(next, key);
  });
}

bool control_value(std::string_view control_id, std::string &out) {
  std::string title;
  size_t row = 0;
  if (split_control_id(control_id, kSprxStartupPrefix, title, 9, 9)) {
    if (title != s_title)
      return false;
    out = s_config.load_on_startup ? "1" : "0";
    return true;
  }
  if (!plugin_pages::split_sprx_plugin_control_id(control_id, title, row) ||
      title != s_title || row >= s_plugins.size())
    return false;
  out = s_plugins[row].enabled ? "1" : "0";
  return true;
}

DispatchResult dispatch(std::string_view control_id, std::string_view value) {
  DispatchResult result;
  const bool on = value == "1" || value == "true";
  size_t row = 0;
  pl::Config next = s_config;
  if (split_control_id(control_id, kSprxStartupPrefix, result.id, 9, 9)) {
    next.load_on_startup = on;
  } else if (plugin_pages::split_sprx_plugin_control_id(control_id, result.id,
                                                        row)) {
    if (row >= s_plugins.size() || result.id != s_title) {
      result.owned = true;
      return result;
    }
    std::erase(next.disabled, s_plugins[row].name);
    if (!on)
      next.disabled.push_back(s_plugins[row].name);
  } else {
    return result;
  }
  result.owned = true;
  result.success =
      result.id == s_title && pl::save_config(kPluginFolderUser, s_title, next);
  if (result.success) {
    s_config = std::move(next);
    for (pl::Plugin &plugin : s_plugins)
      plugin.enabled = std::ranges::find(s_config.disabled, plugin.name) ==
                       s_config.disabled.end();
  }
  LOG_INFO("[pluginloader] %.*s %.*s=%d -> %d",
           static_cast<int>(result.id.size()), result.id.data(),
           static_cast<int>(control_id.size()), control_id.data(), on ? 1 : 0,
           result.success ? 1 : 0);
  return result;
}

} // namespace onion::shellui::external_sprx
