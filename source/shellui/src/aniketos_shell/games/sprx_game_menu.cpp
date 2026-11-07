#include "aniketos_shell/games/sprx_game_menu.hpp"

#include "aniketos_shell/home/title_info.hpp"
#include "aniketos_shell/system_ui/pin_lock.hpp"
#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "hooked_funcs.hpp"
#include "plugin_sprx_pages.hpp"
#include "toolbox_i18n.hpp"

#include <onion/ipc_client.hpp>
#include <onion/log.h>

#include <onion/plugin_folder.hpp>

#include <algorithm>
#include <mutex>
#include <vector>

namespace SprxGameMenu {
namespace {

namespace pl = onion::pluginloader;
namespace pages = onion::shellui::plugin_pages;

std::mutex s_request_mutex;
std::string s_request; /* title waiting for its menu */

bool s_up = false;
uint32_t s_serial = 0;
bool s_changed = false;
std::string s_title;
pl::Config s_config;
std::vector<pl::Plugin> s_plugins; /* rows 1.., row 0 is Load on startup */
std::vector<bool> s_values;        /* each switch as we last saw it */

const char *tr(const char *key) { return toolbox_i18n::tr(key); }

SystemDialogs::MenuEntry switch_row(std::string title, bool on) {
  SystemDialogs::MenuEntry row{std::move(title), ""};
  row.is_switch = true;
  row.value = on;
  return row;
}

void open(const std::string &title) {
  s_config = pl::load_config(pages::kPluginFolderUser, title);
  s_plugins = pl::list(pages::kPluginFolderUser, title, s_config);
  const pl::Status status = pl::load_status(pages::kPluginFolderUser, title);
  std::vector<SystemDialogs::MenuEntry> rows;
  rows.push_back(switch_row(tr("sprx.load_on_startup"), s_config.load_on_startup));
  for (const pl::Plugin &plugin : s_plugins)
    rows.push_back(switch_row(plugin.name.substr(plugin.all_games() ? 4 : 0) +
                                  "  ·  " + pages::sprx_plugin_detail(plugin, status),
                              plugin.enabled));
  std::vector<bool> values;
  for (const auto &row : rows)
    values.push_back(row.value);
  if (s_plugins.empty()) {
    SystemDialogs::MenuEntry row{
        toolbox_i18n::format("sprx.menu.empty_fmt",
                             (std::string(pages::kPluginFolder) + "/" + title).c_str()),
        ""};
    row.enabled = false;
    rows.push_back(row);
  }
  /* catalog.ini / plugins.ini: that file decides */
  std::vector<SprxInventoryItem> legacy;
  (void)IPC_Client::getInstance(false).ListSprx(legacy);
  for (const SprxInventoryItem &item : legacy) {
    if (!pages::legacy_loads_into(item, title))
      continue;
    SystemDialogs::MenuEntry row{
        item.id + "  ·  " +
            (item.source.empty() ? std::string("catalog.ini") : item.source),
        ""};
    row.enabled = false;
    rows.push_back(row);
  }
  std::string failure;
  SystemDialogs::ClosePopupMenu();
  if (!SystemDialogs::ShowPopupMenu(rows, tr("sprx.menu.title"), &failure,
                                    nullptr, /*stay_open=*/true)) {
    LOG_WARN("[SPRXMENU] menu: %s", failure.c_str());
    return;
  }
  s_up = true;
  s_serial = SystemDialogs::PopupMenuSerial();
  s_changed = false;
  s_title = title;
  s_values = std::move(values);
  LOG_INFO("[SPRXMENU] menu for %s, %zu plugins", title.c_str(),
           s_plugins.size());
}

/* A switch was flipped: Load on startup (row 0) or one plugin. */
void apply(size_t row, bool on) {
  pl::Config next = s_config;
  if (row == 0) {
    next.load_on_startup = on;
  } else {
    const std::string &name = s_plugins[row - 1].name;
    std::erase(next.disabled, name);
    if (!on)
      next.disabled.push_back(name);
  }
  const bool ok = pl::save_config(pages::kPluginFolderUser, s_title, next);
  LOG_INFO("[SPRXMENU] row %zu %s for %s -> %d", row, on ? "on" : "off",
           s_title.c_str(), ok ? 1 : 0);
  if (!ok) {
    /* refused: the switch goes back to what is really set */
    SystemDialogs::SetPopupMenuSwitch(static_cast<int>(row), s_values[row]);
    notify("%s", toolbox_i18n::format("sprx.operation_failed_fmt",
                                      s_title.c_str())
                     .c_str());
    return;
  }
  s_config = std::move(next);
  s_values[row] = on;
  s_changed = true;
}

void menu_tick() {
  /* Another menu took the screen: its switches are not ours. */
  if (SystemDialogs::PopupMenuSerial() != s_serial) {
    s_up = false;
    return;
  }
  for (size_t row = 0; row < s_values.size(); ++row) {
    bool on = s_values[row];
    if (SystemDialogs::PopupMenuSwitchValue(static_cast<int>(row), &on) &&
        on != s_values[row])
      apply(row, on);
  }
  int chosen = -1;
  (void)SystemDialogs::PollPopupMenu(&chosen); /* switches flip in place */
  if (SystemDialogs::PopupMenuDismissed()) {
    s_up = false;
    if (s_changed)
      notify("%s", tr("sprx.menu.next_start"));
  }
}

} // namespace

std::string s_locked_title; /* waiting for the PIN, UI thread */

void Request(const std::string &title_id) {
  if (!AniketosShell::TitleInfo::Valid(title_id)) {
    LOG_WARN("[SPRXMENU] no title ('%s')", title_id.c_str());
    return;
  }
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = title_id;
}

void Tick() {
  if (s_up)
    menu_tick();
  std::string title;
  {
    std::lock_guard<std::mutex> lock(s_request_mutex);
    title.swap(s_request);
  }
  if (!title.empty()) {
    /* the same lock as the Toolbox's Game plugins page */
    s_locked_title = std::move(title);
    PinLock::Require([] { open(s_locked_title); });
  }
}

} // namespace SprxGameMenu
