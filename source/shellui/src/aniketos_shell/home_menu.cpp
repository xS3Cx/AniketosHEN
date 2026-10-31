#include "aniketos_shell/home_menu.hpp"
#include "aniketos_shell/cheats_menu.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "shellui_payload_state.hpp"
#include "shellui_payloads.hpp"
#include "aniketos_shell/system_dialogs.hpp"
#include "toolbox_i18n.hpp"

#include <onion/distribution.h>
#include <onion/log.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <vector>
#include <initializer_list>
#include <string>

bool shellui_open_toolbox();

namespace HomeMenu {
namespace {

/* What a row of the menu is. */
enum class Row { Toolbox, Packages, Payloads, Cheats, Internet, Unload };
struct RowInfo {
  Row kind;
  int index;
};



constexpr const char *kUnloadIcon = "cxml://ui3/iconid_no_operation_allowed";

std::atomic<bool> s_requested{false};
uint32_t s_scene_handle = 0; /* where the menu and its dialogs open */
bool s_confirming_unload = false;
bool s_switch_shown = false; /* the menu on screen has the Internet switch */
bool s_switch_value = true;
int s_unload_countdown = 0;
/* "Install packages" is a submenu row: PUI opens the places packages can be
   in beside the menu, and each place opens its packages beside that. The
   whole tree is read when the menu opens; a package row's id is its number
   in s_packages. */
bool s_menu_up = false;       /* our menu is the one on screen */
std::vector<std::string> s_packages; /* every package listed, by row id */
constexpr const char *kPackageIcon =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_package.png";
constexpr size_t kMaxPackages = 40; /* in one place */
constexpr const char *kDriveIcon =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_hardrive.png";
/* "Payloads" is a submenu row too: every payload of the payloads folders,
   read when the menu opens, with whether it runs; choosing one starts or
   stops it. A payload row's id is kPayloadId and its number in
   s_payloads. */
std::vector<PayloadEntry> s_payloads;
constexpr const char *kPayloadId = "payload:";
constexpr const char *kPayloadIcon =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_plugins.png";
/* "Cheats" opens the running game's cheats as a card of switches. */
constexpr const char *kCheatsIcon =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_cheats.png";

const char *tr(const char *key) { return toolbox_i18n::tr(key); }

/* ---- the console's "Connect to the Internet" switch -------------------
   The same registry flag Settings > Network writes (found by the name of
   its key constant), followed by the same connect or disconnect request.
   Everything is looked up by name in ShellUI's managed assemblies. */

MonoMethod *static_method(const char *dll, const char *space, const char *klass,
                          const char *method, int parameters) {
  MonoImage *image = getDLLimage(dll);
  MonoClass *type = image ? mono_class_from_name(image, space, klass) : nullptr;
  return type ? mono_class_get_method_from_name(type, method, parameters)
              : nullptr;
}

bool internet_flag_key(int32_t *key) {
  MonoImage *image = getDLLimage("Sce.Vsh.Registry.dll");
  MonoClass *registry =
      image ? mono_class_from_name(image, "Sce.Vsh", "Registry") : nullptr;
  MonoClassField *field =
      registry ? mono_class_get_field_from_name(
                     registry, "SCE_REGMGR_ENT_KEY_NET_COMMON_net_flag")
               : nullptr;
  if (!field)
    return false;
  mono_field_static_get_value(mono_class_vtable(mono_domain_get(), registry),
                              field, key);
  return *key != 0;
}

bool internet_enabled(bool *enabled) {
  int32_t key = 0;
  MonoMethod *get = static_method("ReactNative.PUI.dll",
                                  "ReactNative.SystemMonitor",
                                  "SystemMonitorServer", "SceRegMgrGetInt", 2);
  if (!get || !internet_flag_key(&key))
    return false;
  int32_t value = 0;
  void *args[2] = {&key, &value};
  MonoObject *exception = nullptr;
  MonoObject *result = mono_runtime_invoke(get, nullptr, args, &exception);
  if (exception || !result ||
      *static_cast<int32_t *>(mono_object_unbox(result)) != 0)
    return false;
  *enabled = value != 0;
  return true;
}

bool set_internet_enabled(bool enabled) {
  int32_t key = 0;
  MonoMethod *set = static_method("ReactNative.PUI.dll",
                                  "ReactNative.SystemMonitor",
                                  "SystemMonitorServer", "SceRegMgrSetInt", 2);
  if (!set || !internet_flag_key(&key))
    return false;
  int32_t value = enabled ? 1 : 0;
  void *args[2] = {&key, &value};
  MonoObject *exception = nullptr;
  MonoObject *result = mono_runtime_invoke(set, nullptr, args, &exception);
  const int32_t set_result =
      !exception && result ? *static_cast<int32_t *>(mono_object_unbox(result))
                           : -1;
  /* The flag alone is only read at the next connection attempt; ask for the
     change now, as Settings does. */
  int32_t request_result = -1;
  for (const char *name :
       enabled ? std::initializer_list<const char *>{"NetCtlConnectWithRetry",
                                                     "NetCtlConnect",
                                                     "NetCtlConnectNative"}
               : std::initializer_list<const char *>{"NetCtlDisconnect",
                                                     "NetCtlDisconnectNative"}) {
    MonoMethod *request =
        static_method("Sce.Vsh.ShellUI.Legacy.dll",
                      "Sce.Vsh.ShellUI.Settings.NetCtl", "NetCtrlWrapper",
                      name, 0);
    if (!request)
      continue;
    MonoObject *thrown = nullptr;
    MonoObject *answer = mono_runtime_invoke(request, nullptr, nullptr, &thrown);
    if (!thrown && answer)
      request_result = *static_cast<int32_t *>(mono_object_unbox(answer));
    LOG_INFO("[HOMEMENU] %s -> 0x%x%s", name,
             static_cast<unsigned>(request_result), thrown ? " (threw)" : "");
    if (!thrown)
      break;
  }
  LOG_INFO("[HOMEMENU] internet flag set to %d: registry=0x%x", value,
           static_cast<unsigned>(set_result));
  return set_result == 0;
}

/* ---- packages -----------------------------------------------------------
   The places packages can be in (/data and the external drives that are
   plugged in), the .pkg files of one of them, and the system's own
   "install this package file" call, the one its
   Package Installer screen makes: Sce.Vsh.AppInstUtilWrapper, found by
   name. The system does the rest, and shows the progress in Downloads. */

bool is_package(const char *name) {
  const size_t length = std::strlen(name);
  return length > 4 && name[0] != '.' &&
         strcasecmp(name + length - 4, ".pkg") == 0;
}

/* A place packages can be in. ShellUI has its own view of the file system:
   /data is /user/data there and the USB drives are /usb0 to /usb7, so a
   place has the folder as this process reaches it and the name it is known
   by everywhere else, which is what is shown. */
struct Place {
  std::string folder; /* as ShellUI sees it */
  std::string shown;
  int depth;          /* how many folders down packages are looked for */
  bool drive;
  std::vector<int> packages; /* their numbers in s_packages */
};
std::vector<Place> s_place_list;

bool has_entries(const std::string &folder) {
  DIR *dir = opendir(folder.c_str());
  if (!dir)
    return false;
  bool any = false;
  while (const dirent *entry = readdir(dir)) {
    if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, "..")) {
      any = true;
      break;
    }
  }
  closedir(dir);
  return any;
}

/* "931 GB exFAT", from the drive itself; empty if it will not say. */
std::string drive_summary(const std::string &folder) {
  struct statfs info {};
  if (statfs(folder.c_str(), &info) != 0 || info.f_blocks == 0)
    return {};
  const double gigabytes = static_cast<double>(info.f_blocks) *
                           static_cast<double>(info.f_bsize) /
                           (1024.0 * 1024.0 * 1024.0);
  char text[64];
  std::snprintf(text, sizeof(text), gigabytes >= 100.0 ? "%.0f GB %s"
                                                         : "%.1f GB %s",
                gigabytes, info.f_fstypename);
  return text;
}

/* /data and /data/pkg, and every external drive that is plugged in: a
   drive's mount point is always there, and empty until something is
   mounted on it. /data holds a great many folders of other tools, so only
   its own files are looked at; /data/pkg one folder down too; a drive, the
   files at its root. */
void find_places() {
  s_place_list.clear();
  s_place_list.push_back({"/user/data", "/data", 0, false, {}});
  s_place_list.push_back({"/user/data/pkg", "/data/pkg", 1, false, {}});
  for (int drive = 0; drive < 8; ++drive) {
    const std::string folder = "/usb" + std::to_string(drive);
    if (!has_entries(folder))
      continue;
    std::string shown = "USB " + std::to_string(drive);
    const std::string summary = drive_summary(folder);
    if (!summary.empty())
      shown += "  (" + summary + ")";
    s_place_list.push_back({folder, shown, 0, true, {}});
  }
  for (int drive = 0; drive < 2; ++drive) {
    const std::string folder = "/mnt/ext" + std::to_string(drive);
    if (!has_entries(folder))
      continue;
    std::string shown = "M.2 / HDD " + std::to_string(drive);
    const std::string summary = drive_summary(folder);
    if (!summary.empty())
      shown += "  (" + summary + ")";
    s_place_list.push_back({folder, shown, 0, true, {}});
  }
}

/* Every .pkg under a folder, `depth` folders down at most; hidden ones are
   passed over. */
void collect_packages(const std::string &folder, int depth,
                      std::vector<std::string> &out) {
  DIR *dir = opendir(folder.c_str());
  if (!dir)
    return;
  std::vector<std::string> files, folders;
  while (const dirent *entry = readdir(dir)) {
    if (entry->d_name[0] == '.')
      continue;
    const std::string path = folder + "/" + entry->d_name;
    if (is_package(entry->d_name)) {
      files.push_back(path);
    } else if (depth > 0) {
      bool is_folder = entry->d_type == DT_DIR;
      if (entry->d_type == DT_UNKNOWN) { /* a file system that does not say */
        struct stat info {};
        is_folder = stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
      }
      if (is_folder)
        folders.push_back(path);
    }
  }
  closedir(dir);
  std::sort(files.begin(), files.end());
  std::sort(folders.begin(), folders.end());
  for (const std::string &path : files)
    if (out.size() < kMaxPackages)
      out.push_back(path);
  for (const std::string &path : folders)
    if (out.size() < kMaxPackages)
      collect_packages(path, depth - 1, out);
}

/* The places and the packages in each, as the menu will show them. */
void find_packages() {
  s_packages.clear();
  find_places();
  for (Place &place : s_place_list) {
    std::vector<std::string> found;
    collect_packages(place.folder, place.depth, found);
    for (const std::string &path : found) {
      place.packages.push_back(static_cast<int>(s_packages.size()));
      s_packages.push_back(path);
    }
  }
}

/* The file's path inside its place: "pkg/Name.pkg", "Name.pkg". */
std::string package_label(const std::string &place, const std::string &path) {
  return path.size() > place.size() + 1 ? path.substr(place.size() + 1) : path;
}

/* The system's result, 0 when it took the package. */
int install_package(const std::string &path) {
  MonoMethod *initialize =
      static_method("Sce.Vsh.AppInstUtilWrapper.dll", "Sce.Vsh",
                    "AppInstUtilWrapper", "Initialize", 0);
  MonoMethod *install =
      static_method("Sce.Vsh.AppInstUtilWrapper.dll", "Sce.Vsh",
                    "AppInstUtilWrapper", "AppInstallPkg", 2);
  if (!install)
    return -1;
  MonoObject *exception = nullptr;
  if (initialize)
    mono_runtime_invoke(initialize, nullptr, nullptr, &exception);
  exception = nullptr;
  void *nothing = nullptr; /* IntPtr.Zero: no extra parameters */
  void *args[2] = {mono_string_new(mono_domain_get(), path.c_str()), &nothing};
  MonoObject *result = mono_runtime_invoke(install, nullptr, args, &exception);
  if (exception || !result)
    return -2;
  return *static_cast<int32_t *>(mono_object_unbox(result));
}

} // namespace

bool Available() { return SystemDialogs::PopupMenuAvailable(); }

void Request(MonoObject *scene) {
  if (s_scene_handle) {
    mono_gchandle_free(s_scene_handle);
    s_scene_handle = 0;
  }
  if (scene)
    s_scene_handle = mono_gchandle_new_weakref(scene, 0);
  s_requested.store(true, std::memory_order_relaxed);
}

namespace {


/* The menu's rows as they stand now, and what each of them is. */
std::vector<RowInfo> s_rows;
int s_internet_row = -1;

std::vector<SystemDialogs::MenuEntry> build_rows() {
  std::vector<SystemDialogs::MenuEntry> entries;
  s_rows.clear();
  entries.push_back({tr("home_menu.toolbox"), AniketosIconPath()});
  s_rows.push_back({Row::Toolbox, 0});
  SystemDialogs::MenuEntry packages{tr("home_menu.packages"), kPackageIcon};
  for (const Place &place : s_place_list) {
    SystemDialogs::MenuEntry where{place.shown,
                                   place.drive ? kDriveIcon : kPackageIcon};
    for (const int number : place.packages) {
      SystemDialogs::MenuEntry file{
          package_label(place.folder, s_packages[static_cast<size_t>(number)]),
          kPackageIcon};
      file.id = std::to_string(number);
      where.children.push_back(file);
    }
    if (where.children.empty()) { /* a submenu needs a row: say so */
      char none[160];
      std::snprintf(none, sizeof(none), tr("home_menu.packages.none"),
                    place.shown.c_str());
      SystemDialogs::MenuEntry empty{none, ""};
      empty.enabled = false;
      where.children.push_back(empty);
    }
    where.children.front().header = place.shown;
    packages.children.push_back(where);
  }
  if (!packages.children.empty())
    packages.children.front().header = tr("home_menu.packages.title");
  entries.push_back(packages);
  s_rows.push_back({Row::Packages, 0});
  SystemDialogs::MenuEntry payloads{tr("home_menu.payloads"), kPayloadIcon};
  for (size_t number = 0; number < s_payloads.size(); ++number) {
    const PayloadEntry &payload = s_payloads[number];
    const bool running = shellui_payload_is_running(payload.tid.c_str());
    SystemDialogs::MenuEntry row{
        payload.name + "  ·  " +
            tr(running ? "payload.status.running" : "payload.status.stopped"),
        kPayloadIcon};
    row.id = kPayloadId + std::to_string(number);
    payloads.children.push_back(row);
  }
  if (payloads.children.empty()) {
    char none[200];
    std::snprintf(none, sizeof(none), tr("home_menu.payloads.none"),
                  "/data/" ONION_DIST_DIR_NAME "/payloads");
    SystemDialogs::MenuEntry empty{none, ""};
    empty.enabled = false;
    payloads.children.push_back(empty);
  }
  payloads.children.front().header = tr("home_menu.payloads.title");
  entries.push_back(payloads);
  s_rows.push_back({Row::Payloads, 0});
  entries.push_back({tr("home_menu.cheats"), kCheatsIcon});
  s_rows.push_back({Row::Cheats, 0});
  s_internet_row = static_cast<int>(entries.size());
  entries.push_back({tr("home_menu.internet"), InternetIconPath(s_switch_value),
                     /*is_switch=*/s_switch_shown, s_switch_value});
  s_rows.push_back({Row::Internet, 0});
  entries.push_back({tr("home_menu.unload"), kUnloadIcon});
  s_rows.push_back({Row::Unload, 0});
  return entries;
}

/* Shows the rows as they stand: in the menu that is up, or in a new one. */
void show_rows(MonoObject *scene, int focus) {
  const std::vector<SystemDialogs::MenuEntry> entries = build_rows();
  std::string failure;
  if (s_menu_up &&
      SystemDialogs::UpdatePopupMenu(entries, ONION_DIST_NAME, focus, &failure))
    return;
  s_menu_up = SystemDialogs::ShowPopupMenu(entries, ONION_DIST_NAME, &failure,
                                           scene, /*stay_open=*/true, focus);
  if (!s_menu_up)
    LOG_WARN("[HOMEMENU] %s", failure.c_str());
}

} // namespace

void Tick() {
  MonoObject *scene =
      s_scene_handle ? mono_gchandle_get_target(s_scene_handle) : nullptr;
  if (s_requested.exchange(false, std::memory_order_relaxed)) {
    /* Shown as it is now; choosing the entry flips it. */
    bool internet = true;
    const bool internet_known = internet_enabled(&internet);
    LOG_INFO("[HOMEMENU] internet flag known=%d enabled=%d",
             internet_known ? 1 : 0, internet ? 1 : 0);
    s_switch_shown = internet_known;
    s_switch_value = internet;
    find_packages();
    s_payloads = shellui_scan_payloads();
    LOG_INFO("[HOMEMENU] %zu places, %zu packages, %zu payloads",
             s_place_list.size(), s_packages.size(), s_payloads.size());
    SystemDialogs::ClosePopupMenu();
    s_menu_up = false;
    show_rows(scene, 0);
    if (!s_menu_up) {
      /* Never leave the icon dead: fall back to what it always did. */
      LOG_WARN("[HOMEMENU] no menu; opening the Toolbox instead");
      shellui_open_toolbox();
    }
  }

  /* The Internet switch is flipped in the menu itself; follow it. */
  bool wanted = s_switch_value;
  if (s_menu_up && s_switch_shown && s_internet_row >= 0 &&
      SystemDialogs::PopupMenuSwitchValue(s_internet_row, &wanted) &&
      wanted != s_switch_value) {
    s_switch_value = wanted;
    if (set_internet_enabled(wanted)) {
      SystemDialogs::SetPopupMenuEntryIcon(s_internet_row,
                                           InternetIconPath(wanted));
      notify("%s", tr(wanted ? "home_menu.internet.enabled"
                             : "home_menu.internet.disabled"));
    }
  }

  /* Backed out of (the submenus are PUI's own and go back by themselves). */
  if (s_menu_up && SystemDialogs::PopupMenuDismissed())
    s_menu_up = false;

  /* A package chosen in a place's submenu: the row's id is its number. A
     payload: kPayloadId and its number. */
  std::string chosen;
  const size_t payload_id = std::strlen(kPayloadId);
  if (s_menu_up && SystemDialogs::PollPopupSubmenu(&chosen) &&
      chosen.compare(0, payload_id, kPayloadId) == 0) {
    const int number = std::atoi(chosen.c_str() + payload_id);
    if (number >= 0 && static_cast<size_t>(number) < s_payloads.size()) {
      const PayloadEntry payload = s_payloads[static_cast<size_t>(number)];
      SystemDialogs::ClosePopupMenu();
      s_menu_up = false;
      const bool stopped = shellui_payload_stop(payload);
      if (!stopped)
        shellui_payload_start(payload);
      LOG_INFO("[HOMEMENU] payload '%s' %s", payload.name.c_str(),
               stopped ? "stopped" : "started");
    }
  } else if (s_menu_up && !chosen.empty()) {
    const int number = std::atoi(chosen.c_str());
    if (!chosen.empty() && number >= 0 &&
        static_cast<size_t>(number) < s_packages.size()) {
      const std::string path = s_packages[static_cast<size_t>(number)];
      SystemDialogs::ClosePopupMenu();
      s_menu_up = false;
      const int result = install_package(path);
      LOG_INFO("[HOMEMENU] install '%s' -> 0x%x", path.c_str(),
               static_cast<unsigned>(result));
      const std::string name = path.substr(path.rfind('/') + 1);
      if (result == 0)
        notify(tr("home_menu.packages.started"), name.c_str());
      else
        notify(tr("home_menu.packages.failed"), name.c_str(),
               static_cast<unsigned>(result));
    }
  }

  int choice = -1;
  if (s_menu_up && SystemDialogs::PollPopupMenu(&choice) && choice >= 0 &&
      static_cast<size_t>(choice) < s_rows.size()) {
    const RowInfo row = s_rows[static_cast<size_t>(choice)];
    LOG_INFO("[HOMEMENU] selected row=%d kind=%d index=%d", choice,
             static_cast<int>(row.kind), row.index);
    switch (row.kind) {
    case Row::Toolbox:
      SystemDialogs::ClosePopupMenu();
      s_menu_up = false;
      shellui_open_toolbox();
      break;
    case Row::Packages:
    case Row::Payloads:
      break; /* submenu rows: PUI opens their rows beside the menu */
    case Row::Cheats:
      SystemDialogs::ClosePopupMenu();
      s_menu_up = false;
      AniketosShell::CheatsMenu::OpenFromControlCenter(scene);
      break;
    case Row::Internet:
      break; /* a switch: followed above */
    case Row::Unload: {
      SystemDialogs::ClosePopupMenu();
      s_menu_up = false;
      SystemDialogs::AlertOptions confirm;
      confirm.style = SystemDialogs::AlertStyle::Popup;
      confirm.title = tr("home_menu.unload");
      confirm.message = tr("home_menu.unload.confirm");
      confirm.positive = tr("home_menu.unload.yes");
      confirm.negative = tr("home_menu.unload.no");
      s_confirming_unload = SystemDialogs::ShowAlert(confirm, nullptr, scene);
      break;
    }
    }
  }

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

} // namespace HomeMenu
