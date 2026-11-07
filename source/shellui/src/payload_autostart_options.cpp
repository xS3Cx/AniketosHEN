/* The OPTIONS menu of a payload on the Autostart page: the same native
 * pop-up as the HEN menu, with the load delay (a submenu of values) and
 * removal (a submenu that confirms). */

#include "payload_center.hpp"

#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "hooked_funcs.hpp"
#include "shellui_payloads.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"

#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/payload_config.h>

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

namespace onion::shellui::payload_center {
namespace {

using SystemDialogs::MenuEntry;

/* Nobody waits half a minute for a payload: whole seconds up to 10, then 15. */
constexpr int kDelays[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15};
/* No system icon id for a clock shows in this menu: the font's clock glyph
   (U+F876) leads the row instead. */
constexpr const char *kClockGlyph = "\xEF\xA1\xB6  ";
constexpr const char *kCheckedIcon = "cxml://ui3/iconid_done_checked";
constexpr const char *kRemoveIcon = "cxml://ui3/iconid_delete";
constexpr const char *kDelayId = "pldc_delay:";
constexpr const char *kRemoveId = "pldc_remove";

struct Menu {
  bool up = false;
  uint32_t serial = 0; /* PopupMenuSerial() of our menu */
  std::string id;      /* the payload's id */
};
Menu s_menu;

const PayloadEntry *find_enabled(const std::vector<PayloadEntry> &list,
                                 const std::string &id) {
  for (const auto &entry : list)
    if (entry.id == id &&
        access((entry.shellui_path + ".auto_start").c_str(), F_OK) == 0)
      return &entry;
  return nullptr;
}

bool removable(const PayloadEntry &entry) {
  return entry.path.rfind(ONION_DIST_DATA_ROOT "/payloads/", 0) == 0;
}

std::string delay_text(int seconds) {
  return seconds > 0 ? toolbox_i18n::format("payload_center.options.delay_choice_fmt", seconds)
                     : toolbox_i18n::tr("payload_center.delay_none");
}

std::vector<MenuEntry> rows(const PayloadEntry &entry) {
  const int delay = entry.config.delay_seconds;
  MenuEntry delay_row{
      kClockGlyph +
          (delay > 0 ? toolbox_i18n::format("payload_center.options.delay_fmt", delay)
                     : std::string(toolbox_i18n::tr("payload_center.delay_none"))),
      ""};
  for (const int seconds : kDelays) {
    MenuEntry choice{delay_text(seconds), seconds == delay ? kCheckedIcon : ""};
    choice.id = kDelayId + std::to_string(seconds);
    delay_row.children.push_back(choice);
  }
  delay_row.children.front().header =
      toolbox_i18n::tr("payload_center.options.delay_title");

  const bool deletes = removable(entry);
  MenuEntry remove_row{
      toolbox_i18n::tr(deletes ? "payload_center.options.remove"
                               : "payload_center.options.remove_autostart"),
      kRemoveIcon};
  MenuEntry confirm{
      deletes ? toolbox_i18n::format("payload_center.options.remove_confirm_fmt",
                                     entry.name.c_str())
              : toolbox_i18n::tr("payload_center.options.remove_autostart"),
      kRemoveIcon};
  confirm.id = kRemoveId;
  confirm.header = toolbox_i18n::tr(
      deletes ? "payload_center.options.remove_title"
              : "payload_center.options.remove_autostart_title");
  remove_row.children.push_back(confirm);
  return {delay_row, remove_row};
}

void set_delay(const PayloadEntry &entry, int seconds) {
  OnionPayloadConfig config{};
  onion_payload_config_load(entry.shellui_path.c_str(), &config);
  config.delay_seconds = seconds;
  if (!onion_payload_config_save(entry.shellui_path.c_str(), &config))
    notify("notify.payload.config_save_failed", entry.name.c_str());
}

void remove(const PayloadEntry &entry) {
  if (removable(entry)) {
    if (!remove_installed(entry.id, entry.name))
      notify("notify.payload_center.remove_failed");
  } else {
    unlink((entry.shellui_path + ".auto_start").c_str());
  }
}

void close_menu() {
  if (s_menu.up && SystemDialogs::PopupMenuSerial() == s_menu.serial)
    SystemDialogs::ClosePopupMenu();
  s_menu = {};
}

} // namespace

bool open_autostart_options(const std::string &id) {
  close_menu();
  const auto list = shellui_scan_payloads();
  const PayloadEntry *entry = find_enabled(list, id);
  if (!entry)
    return false;
  std::string failure;
  if (!SystemDialogs::ShowPopupMenu(rows(*entry), entry->name, &failure,
                                    SystemDialogs::FocusedScene(),
                                    /*stay_open=*/true)) {
    LOG_WARN("[payload-center] autostart options: %s", failure.c_str());
    return false;
  }
  s_menu = {true, SystemDialogs::PopupMenuSerial(), id};
  return true;
}

void tick_autostart_options() {
  if (!s_menu.up)
    return;
  /* Another menu (the HEN menu) took the screen: its choices are not ours. */
  if (SystemDialogs::PopupMenuSerial() != s_menu.serial) {
    s_menu = {};
    return;
  }
  if (SystemDialogs::PopupMenuDismissed()) {
    s_menu = {};
    return;
  }
  std::string chosen;
  if (!SystemDialogs::PollPopupSubmenu(&chosen))
    return;
  const std::string id = s_menu.id;
  close_menu();
  const auto list = shellui_scan_payloads();
  const PayloadEntry *entry = find_enabled(list, id);
  if (!entry)
    return;
  const std::string delay_prefix = kDelayId;
  if (chosen.rfind(delay_prefix, 0) == 0)
    set_delay(*entry, std::atoi(chosen.c_str() + delay_prefix.size()));
  else if (chosen == kRemoveId)
    remove(*entry);
  request_autostart_refresh("id_pldc_move_" + id);
}

} // namespace onion::shellui::payload_center
