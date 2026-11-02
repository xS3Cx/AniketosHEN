#include "aniketos_shell/home/package_files.hpp"

#include "aniketos_ui/small_file.hpp"

#include <onion/distribution.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <ps5/kernel.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

namespace PackageFiles {
namespace {

constexpr size_t kMaxPackages = 40; /* in one place */

bool is_package(const char *name) {
  const size_t length = std::strlen(name);
  return length > 4 && name[0] != '.' &&
         strcasecmp(name + length - 4, ".pkg") == 0;
}

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

/* A drive's mount point is always there, and empty until something is mounted
   on it. */
void add_drive(std::vector<Place> &places, const std::string &folder,
               std::string shown) {
  if (!has_entries(folder))
    return;
  const std::string summary = drive_summary(folder);
  if (!summary.empty())
    shown += "  (" + summary + ")";
  places.push_back({folder, shown, 0, true, {}});
}

/* /data and /data/pkg, and every external drive that is plugged in. /data
   holds a great many folders of other tools, so only its own files are looked
   at; /data/pkg one folder down too; a drive, the files at its root. */
std::vector<Place> find_places() {
  std::vector<Place> places;
  places.push_back({"/user/data", "/data", 0, false, {}});
  places.push_back({"/user/data/pkg", "/data/pkg", 1, false, {}});
  for (int drive = 0; drive < 8; ++drive)
    add_drive(places, "/usb" + std::to_string(drive),
              "USB " + std::to_string(drive));
  for (int drive = 0; drive < 2; ++drive)
    add_drive(places, "/mnt/ext" + std::to_string(drive),
              "M.2 / HDD " + std::to_string(drive));
  return places;
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

} // namespace

Listing Find() {
  Listing listing;
  listing.places = find_places();
  for (Place &place : listing.places) {
    std::vector<std::string> found;
    collect_packages(place.folder, place.depth, found);
    for (const std::string &path : found) {
      place.packages.push_back(static_cast<int>(listing.packages.size()));
      listing.packages.push_back(path);
    }
  }
  return listing;
}

std::string Label(const std::string &place, const std::string &path) {
  return path.size() > place.size() + 1 ? path.substr(place.size() + 1) : path;
}

int Install(const std::string &path) {
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
  const std::string name = AniketosUI::FileName(path);
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

} // namespace PackageFiles
