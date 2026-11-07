#include "aniketos_shell/home/hen_menu.hpp"

#include "aniketos_shell/core/boot_uri.hpp"
#include "aniketos_shell/core/embedded_assets.hpp"
#include "aniketos_shell/games/cheats_menu.hpp"
#include "aniketos_shell/home/internet_switch.hpp"
#include "aniketos_shell/home/package_files.hpp"
#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/small_file.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "shellui_payload_state.hpp"
#include "shellui_payloads.hpp"
#include "toolbox_i18n.hpp"

#include <onion/distribution.h>
#include <onion/log.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Reflect = AniketosUI::Reflect;

namespace HenMenu {
namespace {

using SystemDialogs::MenuEntry;

/* What a row of the menu is. */
enum class Row { Toolbox, Packages, Payloads, Cheats, Internet, Unload };

constexpr const char *kUnloadIcon = "cxml://ui3/iconid_no_operation_allowed";
constexpr const char *kPackageIcon =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_package.png";
constexpr const char *kDriveIcon =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_hardrive.png";
constexpr const char *kPayloadIcon =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_plugins.png";
constexpr const char *kCheatsIcon =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_cheats.png";
constexpr const char *kPayloadId = "payload:";

/* What Request hands to Tick: a weak handle to the scene, 0 for Home's. */
constexpr int64_t kNoRequest = -1;
std::atomic<int64_t> s_request{kNoRequest};
uint32_t s_scene_handle = 0; /* where the menu and its dialogs open (UI thread) */
bool s_confirming_unload = false;
bool s_switch_shown = false; /* the menu on screen has the Internet switch */
bool s_switch_value = true;
int s_unload_countdown = 0;
bool s_menu_up = false; /* our menu is the one on screen */
uint32_t s_menu_serial = 0; /* PopupMenuSerial() of our menu */
/* "Install packages" is a submenu row: PUI opens the places packages can be
   in beside the menu, and each place opens its packages beside that. The
   whole tree is read when the menu opens; a package row's id is its number
   in the listing. */
PackageFiles::Listing s_listing;
/* "Payloads" is a submenu row too: every payload of the payloads folders,
   read when the menu opens, with whether it runs; choosing one starts or
   stops it. A payload row's id is kPayloadId and its number in
   s_payloads. */
std::vector<PayloadEntry> s_payloads;
/* The menu's rows as they stand now, and what each of them is. */
std::vector<Row> s_rows;
int s_internet_row = -1;

const char *tr(const char *key) { return toolbox_i18n::tr(key); }

void close_menu() {
  SystemDialogs::ClosePopupMenu();
  s_menu_up = false;
}

/* A submenu needs a row: this one says there is nothing to choose. */
MenuEntry nothing_row(const char *key, const char *where) {
  char text[200];
  std::snprintf(text, sizeof(text), tr(key), where);
  MenuEntry row{text, ""};
  row.enabled = false;
  return row;
}

MenuEntry packages_row() {
  MenuEntry packages{tr("home_menu.packages"), kPackageIcon};
  for (const PackageFiles::Place &place : s_listing.places) {
    MenuEntry where{place.shown, place.drive ? kDriveIcon : kPackageIcon};
    for (const int number : place.packages) {
      MenuEntry file{
          PackageFiles::Label(
              place.folder, s_listing.packages[static_cast<size_t>(number)]),
          kPackageIcon};
      file.id = std::to_string(number);
      where.children.push_back(file);
    }
    if (where.children.empty())
      where.children.push_back(
          nothing_row("home_menu.packages.none", place.shown.c_str()));
    where.children.front().header = place.shown;
    packages.children.push_back(where);
  }
  if (!packages.children.empty())
    packages.children.front().header = tr("home_menu.packages.title");
  return packages;
}

MenuEntry payloads_row() {
  MenuEntry payloads{tr("home_menu.payloads"), kPayloadIcon};
  for (size_t number = 0; number < s_payloads.size(); ++number) {
    const PayloadEntry &payload = s_payloads[number];
    const bool running = shellui_payload_is_running(payload.tid.c_str());
    MenuEntry row{
        payload.name + "  ·  " +
            tr(running ? "payload.status.running" : "payload.status.stopped"),
        kPayloadIcon};
    row.id = kPayloadId + std::to_string(number);
    payloads.children.push_back(row);
  }
  if (payloads.children.empty())
    payloads.children.push_back(nothing_row(
        "home_menu.payloads.none", "/data/" ONION_DIST_DIR_NAME "/payloads"));
  payloads.children.front().header = tr("home_menu.payloads.title");
  return payloads;
}

std::vector<MenuEntry> build_rows() {
  std::vector<MenuEntry> entries;
  s_rows.clear();
  entries.push_back({tr("home_menu.toolbox"), AniketosIconPath()});
  s_rows.push_back(Row::Toolbox);
  entries.push_back(packages_row());
  s_rows.push_back(Row::Packages);
  entries.push_back(payloads_row());
  s_rows.push_back(Row::Payloads);
  if (s_scene_handle) { /* opened from the Control Center */
    entries.push_back({tr("home_menu.cheats"), kCheatsIcon});
    s_rows.push_back(Row::Cheats);
  }
  s_internet_row = static_cast<int>(entries.size());
  entries.push_back({tr("home_menu.internet"), InternetIconPath(s_switch_value),
                     /*is_switch=*/s_switch_shown, s_switch_value});
  s_rows.push_back(Row::Internet);
  entries.push_back({tr("home_menu.unload"), kUnloadIcon});
  s_rows.push_back(Row::Unload);
  return entries;
}

void open_menu(MonoObject *scene) {
  /* The switch shows the flag as it is now. */
  bool internet = true;
  const bool internet_known = InternetSwitch::Read(&internet);
  LOG_INFO("[HOMEMENU] internet flag known=%d enabled=%d",
           internet_known ? 1 : 0, internet ? 1 : 0);
  s_switch_shown = internet_known;
  s_switch_value = internet;
  s_listing = PackageFiles::Find();
  s_payloads = shellui_scan_payloads();
  LOG_INFO("[HOMEMENU] %zu places, %zu packages, %zu payloads",
           s_listing.places.size(), s_listing.packages.size(),
           s_payloads.size());
  close_menu();
  std::string failure;
  s_menu_up = SystemDialogs::ShowPopupMenu(build_rows(), ONION_DIST_NAME,
                                           &failure, scene, /*stay_open=*/true);
  s_menu_serial = SystemDialogs::PopupMenuSerial();
  if (!s_menu_up) {
    LOG_WARN("[HOMEMENU] %s", failure.c_str());
    /* Never leave the icon dead: fall back to what it always did. */
    LOG_WARN("[HOMEMENU] no menu; opening the Toolbox instead");
    shellui_open_toolbox();
  }
}

/* The Internet switch is flipped in the menu itself; follow it. */
void follow_internet_switch() {
  bool wanted = s_switch_value;
  if (!s_menu_up || !s_switch_shown || s_internet_row < 0 ||
      !SystemDialogs::PopupMenuSwitchValue(s_internet_row, &wanted) ||
      wanted == s_switch_value)
    return;
  s_switch_value = wanted;
  if (InternetSwitch::Set(wanted)) {
    SystemDialogs::SetPopupMenuEntryIcon(s_internet_row,
                                         InternetIconPath(wanted));
    notify("%s", tr(wanted ? "home_menu.internet.enabled"
                           : "home_menu.internet.disabled"));
  }
}

void toggle_payload(int number) {
  if (number < 0 || static_cast<size_t>(number) >= s_payloads.size())
    return;
  const PayloadEntry payload = s_payloads[static_cast<size_t>(number)];
  close_menu();
  const bool stopped = shellui_payload_stop(payload);
  if (!stopped)
    shellui_payload_start(payload);
  LOG_INFO("[HOMEMENU] payload '%s' %s", payload.name.c_str(),
           stopped ? "stopped" : "started");
}

void install_package(int number) {
  if (number < 0 || static_cast<size_t>(number) >= s_listing.packages.size())
    return;
  const std::string path = s_listing.packages[static_cast<size_t>(number)];
  close_menu();
  const int result = PackageFiles::Install(path);
  LOG_INFO("[HOMEMENU] install '%s' -> 0x%x", path.c_str(),
           static_cast<unsigned>(result));
  const std::string name = AniketosUI::FileName(path);
  if (result == 0)
    notify(tr("home_menu.packages.started"), name.c_str());
  else
    notify(tr("home_menu.packages.failed"), name.c_str(),
           static_cast<unsigned>(result));
}

/* A package chosen in a place's submenu: the row's id is its number. A
   payload: kPayloadId and its number. */
void handle_submenu_choice() {
  std::string chosen;
  if (!s_menu_up || !SystemDialogs::PollPopupSubmenu(&chosen))
    return;
  const size_t payload_id = std::strlen(kPayloadId);
  if (chosen.compare(0, payload_id, kPayloadId) == 0)
    toggle_payload(std::atoi(chosen.c_str() + payload_id));
  else
    install_package(std::atoi(chosen.c_str()));
}

void confirm_unload(MonoObject *scene) {
  SystemDialogs::AlertOptions confirm;
  confirm.style = SystemDialogs::AlertStyle::Popup;
  confirm.title = tr("home_menu.unload");
  confirm.message = tr("home_menu.unload.confirm");
  confirm.positive = tr("home_menu.unload.yes");
  confirm.negative = tr("home_menu.unload.no");
  s_confirming_unload = SystemDialogs::ShowAlert(confirm, nullptr, scene);
}

void handle_row_choice(MonoObject *scene) {
  int choice = -1;
  if (!s_menu_up || !SystemDialogs::PollPopupMenu(&choice) || choice < 0 ||
      static_cast<size_t>(choice) >= s_rows.size())
    return;
  const Row row = s_rows[static_cast<size_t>(choice)];
  LOG_INFO("[HOMEMENU] selected row=%d kind=%d", choice,
           static_cast<int>(row));
  switch (row) {
  case Row::Toolbox:
    close_menu();
    shellui_open_toolbox();
    break;
  case Row::Packages:
  case Row::Payloads:
    break; /* submenu rows: PUI opens their rows beside the menu */
  case Row::Cheats:
    close_menu();
    AniketosShell::CheatsMenu::OpenFromControlCenter(scene);
    break;
  case Row::Internet:
    break; /* a switch: followed in Tick */
  case Row::Unload:
    close_menu();
    confirm_unload(scene);
    break;
  }
}

} // namespace

bool Available() { return SystemDialogs::PopupMenuAvailable(); }

void Request(MonoObject *scene) {
  uint32_t handle = 0;
  Reflect::Hold(handle, scene);
  const int64_t replaced = s_request.exchange(handle, std::memory_order_acq_rel);
  if (replaced > 0) {
    uint32_t stale = static_cast<uint32_t>(replaced);
    Reflect::Release(stale);
  }
}

void Tick() {
  const int64_t request =
      s_request.exchange(kNoRequest, std::memory_order_acq_rel);
  if (request != kNoRequest) {
    Reflect::Release(s_scene_handle);
    s_scene_handle = static_cast<uint32_t>(request);
  }
  MonoObject *scene = Reflect::Target(s_scene_handle);
  if (request != kNoRequest)
    open_menu(scene);

  follow_internet_switch();

  /* Another menu (a payload's OPTIONS menu) took the screen: its choices
     are not ours. Then backed out of (the submenus are PUI's own and go
     back by themselves). */
  if (s_menu_up && SystemDialogs::PopupMenuSerial() != s_menu_serial)
    s_menu_up = false;
  if (s_menu_up && SystemDialogs::PopupMenuDismissed())
    s_menu_up = false;

  handle_submenu_choice();
  handle_row_choice(scene);

  int button = -1;
  if (s_confirming_unload && SystemDialogs::PollAlert(&button)) {
    s_confirming_unload = false;
    LOG_INFO("[HOMEMENU] unload confirm button=%d", button);
    if (button == 0)
      s_unload_countdown = 30; /* let the dialog close first */
  }
  if (s_unload_countdown > 0 && --s_unload_countdown == 0) {
    LOG_INFO("[HOMEMENU] unloading the stack");
    IPC_Client::getInstance(false).ShutdownStack();
  }
}

} // namespace HenMenu
