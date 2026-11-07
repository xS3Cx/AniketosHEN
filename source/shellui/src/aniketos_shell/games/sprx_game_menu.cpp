#include "aniketos_shell/games/sprx_game_menu.hpp"

#include "aniketos_shell/home/title_info.hpp"
#include "aniketos_shell/system_ui/pin_lock.hpp"
#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "hooked_funcs.hpp"
#include "plugin_sprx_pages.hpp"
#include "toolbox_i18n.hpp"

#include <onion/ipc_client.hpp>
#include <onion/log.h>

#include <algorithm>
#include <mutex>
#include <vector>

namespace SprxGameMenu {
namespace {

using onion::shellui::plugin_pages::sprx_loads_into;

std::mutex s_request_mutex;
std::string s_request; /* title waiting for its menu */

bool s_up = false;
uint32_t s_serial = 0;
bool s_changed = false;
std::string s_title;
std::vector<SprxInventoryItem> s_items; /* one per row, when there are any */
std::vector<bool> s_values;             /* each switch as we last saw it */

const char *tr(const char *key) { return toolbox_i18n::tr(key); }

void open(const std::string &title) {
  std::vector<SprxInventoryItem> items;
  if (!IPC_Client::getInstance(false).ListSprx(items)) {
    notify("%s", tr("sprx.unavailable"));
    return;
  }
  std::vector<SystemDialogs::MenuEntry> rows;
  std::vector<bool> values;
  for (const SprxInventoryItem &item : items) {
    SystemDialogs::MenuEntry row{item.id, ""};
    row.is_switch = true;
    row.value = sprx_loads_into(item, title);
    if (!item.source.empty()) { /* its plugins.ini decides */
      row.title += "  ·  plugins.ini";
      row.enabled = false;
    } else if (item.all_games) {
      row.title += std::string("  ·  ") + tr("sprx.menu.all_games");
      row.enabled = false;
    } else if (!item.error.empty()) {
      row.title += std::string("  ·  ") + tr("sprx.menu.failed");
    }
    rows.push_back(row);
    values.push_back(row.value);
  }
  if (rows.empty()) {
    SystemDialogs::MenuEntry row{
        toolbox_i18n::format("sprx.menu.empty_fmt",
                             onion::shellui::plugin_pages::kSprxFolder),
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
  s_items = std::move(items);
  s_values = std::move(values);
  LOG_INFO("[SPRXMENU] menu for %s, %zu plugins", title.c_str(),
           s_items.size());
}

/* A switch was flipped: the plugin loads into this game, or no longer. */
void apply(size_t row, bool on) {
  SprxInventoryItem &item = s_items[row];
  std::vector<std::string> titles = item.titles;
  std::erase(titles, s_title);
  if (on)
    titles.push_back(s_title);
  IPC_Client &client = IPC_Client::getInstance(false);
  /* An off plugin switched on for a game is meant to load there. */
  const bool ok = client.SetSprxTargets(item.id, titles, item.all_games) &&
                  (!on || item.enabled || client.SetSprxEnabled(item.id, true));
  LOG_INFO("[SPRXMENU] %s %s for %s -> %d", item.id.c_str(), on ? "on" : "off",
           s_title.c_str(), ok ? 1 : 0);
  if (!ok) {
    /* refused: the switch goes back to what is really set */
    SystemDialogs::SetPopupMenuSwitch(static_cast<int>(row), s_values[row]);
    notify("%s", toolbox_i18n::format("sprx.operation_failed_fmt",
                                      item.id.c_str())
                     .c_str());
    return;
  }
  s_values[row] = on;
  item.titles = std::move(titles);
  item.enabled = item.enabled || on;
  s_changed = true;
}

void menu_tick() {
  /* Another menu took the screen: its switches are not ours. */
  if (SystemDialogs::PopupMenuSerial() != s_serial) {
    s_up = false;
    return;
  }
  for (size_t row = 0; row < s_items.size(); ++row) {
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
