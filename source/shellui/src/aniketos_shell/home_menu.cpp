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
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <ps5/kernel.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
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
   Package Installer screen makes: sceAppInstUtilInstallByPackage, found by
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

/* What sceAppInstUtilInstallByPackage reads. The call has no size argument:
   the system reads past the six pointers, and leftover bytes there made it
   take a full package for a patch (0x80B2116F, ps5upload), so all is zeroed. */
struct MetaInfo {
  const char *uri;
  const char *ex_uri;
  const char *playgo_scenario_id;
  const char *content_id;
  const char *content_name;
  const char *icon_url;
  uint32_t slot;
  uint32_t is_playgo;
  uint64_t pad[7];
};
struct SceAppInstallPkgInfo {
  char content_id[48];
  int content_type;
  int content_platform;
};
struct PlayGoInfo {
  char languages[30][8];
  char playgo_scenario_ids[64][3];
  char content_ids[64][48];
  uint8_t unknown[6480];
};
static_assert(offsetof(MetaInfo, slot) == 0x30, "MetaInfo layout");
static_assert(sizeof(MetaInfo) == 0x70, "MetaInfo layout");
static_assert(sizeof(SceAppInstallPkgInfo) == 56, "PkgInfo layout");
static_assert(sizeof(PlayGoInfo) == 0x2700, "PlayGoInfo layout");

void *find_symbol(uint32_t module, const char *name) {
  const intptr_t address = kernel_dynlib_dlsym(-1, module, name);
  return address > 0 ? reinterpret_cast<void *>(address) : nullptr;
}

/* The module of the install call, loaded if ShellUI has not; 0 when it can't
   be. */
uint32_t app_inst_util_module() {
  uint32_t module = 0;
  if (kernel_dynlib_handle(getpid(), "libSceAppInstUtil.sprx", &module) == 0)
    return module;
  uint32_t kernel = 0;
  if (kernel_dynlib_handle(getpid(), "libkernel_sys.sprx", &kernel) != 0 &&
      kernel_dynlib_handle(getpid(), "libkernel.sprx", &kernel) != 0)
    return 0;
  const auto load = reinterpret_cast<int (*)(const char *, size_t, const void *,
                                             uint32_t, void *, int *)>(
      find_symbol(kernel, "sceKernelLoadStartModule"));
  const int loaded =
      load ? load("/system/common/lib/libSceAppInstUtil.sprx", 0, nullptr, 0,
                  nullptr, nullptr)
           : -1;
  return loaded > 0 ? static_cast<uint32_t>(loaded) : 0;
}

/* ---- what a package says about itself -----------------------------------
   The install call shows the name and icon it is given, and takes neither
   from the package. Both come from the plain entries of the CNT table: at
   the start of a PS4 package, and in a PS5 one where the FIH header's
   little-endian offset at 0x58 says, with the entry offsets counted from
   there (ps5upload pkg_info.c). The file is untrusted: reads past its end
   fail, sizes are capped, and what a wild offset finds fails the magic checks.
   Whatever is missing leaves the name to the file and the icon empty. */

constexpr uint32_t kPackageSfo = 0x1000;
constexpr uint32_t kPackagePng = 0x1200;
constexpr uint32_t kPackageJson = 0x2000;
constexpr size_t kMaxPackageEntries = 1024;
constexpr size_t kMaxPackageMeta = 256 * 1024;
constexpr size_t kMaxPackageIcon = 1024 * 1024;
constexpr size_t kMaxPackageTitle = 127; /* APP_INSTALL_TITLE_SIZE with its end */
constexpr const char *kPackageIcons =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/PackageIcons";

constexpr uint32_t be32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) << 24 |
         static_cast<uint32_t>(bytes[1]) << 16 |
         static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
}
constexpr uint32_t le32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[3]) << 24 |
         static_cast<uint32_t>(bytes[2]) << 16 |
         static_cast<uint32_t>(bytes[1]) << 8 | bytes[0];
}
constexpr uint8_t kCntMagic[] = {0x7F, 'C', 'N', 'T'};
constexpr uint8_t kFihMagic[] = {0x7F, 'F', 'I', 'H'};
static_assert(be32(kCntMagic) == 0x7F434E54, "be32");
static_assert(le32(kCntMagic) == 0x544E437F, "le32");

bool read_at(int fd, uint64_t offset, void *out, size_t size) {
  return pread(fd, out, size, static_cast<off_t>(offset)) ==
         static_cast<ssize_t>(size);
}

/* The entry table, 0x20 bytes a row; empty if this is no package. `head`
   gets the CNT header and `base` where it starts. */
std::vector<uint8_t> package_table(int fd, uint8_t (&head)[0x70],
                                   uint64_t &base) {
  if (!read_at(fd, 0, head, sizeof(head)))
    return {};
  if (be32(head) == be32(kFihMagic)) {
    base = le32(head + 0x58) | static_cast<uint64_t>(le32(head + 0x5C)) << 32;
    if (!read_at(fd, base, head, sizeof(head)))
      return {};
  }
  const uint32_t count = be32(head + 0x10);
  if (be32(head) != be32(kCntMagic) || count > kMaxPackageEntries)
    return {};
  std::vector<uint8_t> table(count * size_t{0x20});
  if (!read_at(fd, base + be32(head + 0x18), table.data(), table.size()))
    return {};
  return table;
}

/* An entry's bytes; empty if it is missing, over `limit` or cut off. */
std::vector<uint8_t> package_entry(int fd, uint64_t base,
                                   const std::vector<uint8_t> &table,
                                   uint32_t id, size_t limit) {
  for (size_t row = 0; row + 0x20 <= table.size(); row += 0x20) {
    if (be32(&table[row]) != id)
      continue;
    const uint32_t size = be32(&table[row + 0x14]);
    if (size == 0 || size > limit)
      return {};
    std::vector<uint8_t> data(size);
    if (!read_at(fd, base + be32(&table[row + 0x10]), data.data(), size))
      return {};
    return data;
  }
  return {};
}

/* TITLE of a param.sfo, the name a PS4 package goes by. */
std::string sfo_title(const std::vector<uint8_t> &sfo) {
  if (sfo.size() < 20 || std::memcmp(sfo.data(), "\0PSF", 4) != 0)
    return {};
  const uint64_t keys = le32(&sfo[8]);
  const uint64_t values = le32(&sfo[12]);
  const uint32_t count = le32(&sfo[16]);
  for (uint32_t index = 0; index < count; ++index) {
    const size_t row = 20 + size_t{index} * 16;
    if (row + 16 > sfo.size())
      break;
    const uint64_t key = keys + (sfo[row] | sfo[row + 1] << 8);
    if (key + 6 > sfo.size() || std::memcmp(&sfo[key], "TITLE", 6) != 0)
      continue;
    const uint64_t value = values + le32(&sfo[row + 12]);
    const uint32_t length = le32(&sfo[row + 4]);
    if (value > sfo.size() || length > sfo.size() - value)
      return {};
    const char *text = reinterpret_cast<const char *>(sfo.data()) + value;
    return std::string(text, strnlen(text, length));
  }
  return {};
}

/* Where the value of `"key":` starts, from `from` on; npos if nowhere. */
size_t json_value(const std::string &json, const std::string &key,
                  size_t from) {
  const std::string quoted = "\"" + key + "\"";
  for (size_t at = json.find(quoted, from); at != std::string::npos;
       at = json.find(quoted, at + 1)) {
    const size_t colon = json.find_first_not_of(" \t\r\n", at + quoted.size());
    if (colon != std::string::npos && json[colon] == ':')
      return json.find_first_not_of(" \t\r\n", colon + 1);
  }
  return std::string::npos;
}

/* The JSON string at `at`, empty if there is none.
   shortcut: other escapes than \" \\ \/ give up on the name, the file name
   stands in; decode \uXXXX if a package turns out to need it. */
std::string json_string(const std::string &json, size_t at) {
  if (at >= json.size() || json[at] != '"')
    return {};
  std::string text;
  for (size_t index = at + 1; index < json.size(); ++index) {
    char letter = json[index];
    if (letter == '"')
      return text;
    if (letter == '\\') {
      if (++index == json.size() || !std::strchr("\"\\/", json[index]))
        return {};
      letter = json[index];
    }
    text += letter;
  }
  return {};
}

/* The default language's titleName of a param.json, the name a PS5 package
   goes by. */
std::string json_title(const std::string &json) {
  const size_t names = json_value(json, "localizedParameters", 0);
  if (names == std::string::npos)
    return {};
  const std::string language =
      json_string(json, json_value(json, "defaultLanguage", names));
  const size_t own =
      language.empty() ? std::string::npos : json_value(json, language, names);
  return json_string(
      json,
      json_value(json, "titleName", own == std::string::npos ? names : own));
}

/* Saves the icon where the installer can read it, for as long as Downloads
   shows the task: one file a title, so installs do not pile up. Returns its
   path, the same in ShellUI and in the installer, or empty. */
std::string store_package_icon(const uint8_t *content_id,
                               const std::vector<uint8_t> &png) {
  constexpr uint8_t kPng[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (png.size() < sizeof(kPng) ||
      std::memcmp(png.data(), kPng, sizeof(kPng)) != 0)
    return {};
  /* "EP1004-CUSA08519_00-...": it names a file, so only letters and digits */
  const std::string title_id(reinterpret_cast<const char *>(content_id) + 7, 9);
  for (const char letter : title_id)
    if (!std::isalnum(static_cast<unsigned char>(letter)))
      return {};
  (void)mkdir(ONION_DIST_USER_DATA_ROOT "/ShellUI", 0777);
  (void)mkdir(kPackageIcons, 0777);
  const std::string icon = std::string(kPackageIcons) + "/" + title_id + ".png";
  const int fd = open(icon.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    return {};
  const bool written =
      write(fd, png.data(), png.size()) == static_cast<ssize_t>(png.size());
  close(fd);
  if (!written) {
    unlink(icon.c_str());
    return {};
  }
  return icon;
}

/* The package's name, whole characters only, and its saved icon; each stays
   empty if the package does not give it. Reads the few small entries only. */
void read_package(const std::string &path, std::string &title,
                  std::string &icon) {
  const int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0)
    return;
  uint8_t head[0x70] = {};
  uint64_t base = 0;
  const std::vector<uint8_t> table = package_table(fd, head, base);
  title = sfo_title(package_entry(fd, base, table, kPackageSfo, kMaxPackageMeta));
  if (title.empty()) {
    const std::vector<uint8_t> json =
        package_entry(fd, base, table, kPackageJson, kMaxPackageMeta);
    title = json_title(std::string(json.begin(), json.end()));
  }
  const std::vector<uint8_t> png =
      package_entry(fd, base, table, kPackagePng, kMaxPackageIcon);
  close(fd);
  if (title.size() > kMaxPackageTitle) {
    size_t end = kMaxPackageTitle;
    while (end > 0 && (static_cast<uint8_t>(title[end]) & 0xC0) == 0x80)
      --end;
    title.resize(end);
  }
  icon = store_package_icon(head + 0x40, png);
}

/* The system's result, 0 when it took the package. */
int install_package(const std::string &path) {
  const uint32_t module = app_inst_util_module();
  if (!module)
    return -1;
  const auto initialize = reinterpret_cast<int (*)()>(
      find_symbol(module, "sceAppInstUtilInitialize"));
  const auto install = reinterpret_cast<int (*)(
      MetaInfo *, SceAppInstallPkgInfo *, PlayGoInfo *)>(
      find_symbol(module, "sceAppInstUtilInstallByPackage"));
  if (!initialize || !install)
    return -1;
  /* ShellUI may have done this already: a real failure shows in the install */
  initialize();
  /* the installer is another process: it knows a drive by its /mnt path */
  const std::string system_path =
      path.rfind("/usb", 0) == 0 ? "/mnt" + path : path;
  const std::string name = path.substr(path.rfind('/') + 1);
  std::string title, icon;
  read_package(path, title, icon);
  static PlayGoInfo playgo; /* 10 KB, too much for ShellUI's stack */
  std::memset(&playgo, 0, sizeof(playgo));
  SceAppInstallPkgInfo package = {};
  MetaInfo meta = {};
  meta.uri = system_path.c_str();
  meta.ex_uri = "";
  meta.playgo_scenario_id = "";
  /* empty: the installer takes it from the package and fills `package` */
  meta.content_id = "";
  meta.content_name = title.empty() ? name.c_str() : title.c_str();
  meta.icon_url = icon.c_str();
  return install(&meta, &package, &playgo);
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
  if (s_scene_handle) { /* opened from the Control Center */
    entries.push_back({tr("home_menu.cheats"), kCheatsIcon});
    s_rows.push_back({Row::Cheats, 0});
  }
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
