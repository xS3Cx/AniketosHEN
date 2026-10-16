#include "aniketos_shell/content_info.hpp"

#include "aniketos_shell/home_js.hpp"
#include "toolbox_i18n.hpp"

#include <onion/log.h>

#include <cctype>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace ContentInfo {
namespace {

/* The star in front of every label: the rows are AniketosHEN's, not the
   system's. Added here so no translation can lose it. */
constexpr const char kStar[] = "\xE2\x98\x85 ";

bool valid_title(const std::string &id) {
  if (id.size() != 9)
    return false;
  for (const char c : id)
    if (!std::isalnum(static_cast<unsigned char>(c)))
      return false;
  return true;
}

/* Games run from a dump keep a mount.lnk in their /user/app folder that
   names the folder they are mounted from; the database only knows the
   /user/app path. A JavaScript object of title id -> that folder. */
std::string mount_links() {
  std::string out;
  int count = 0;
  DIR *dir = opendir("/user/app");
  if (!dir)
    return "{}";
  while (dirent *entry = readdir(dir)) {
    const std::string id = entry->d_name;
    if (!valid_title(id))
      continue;
    const std::string path = "/user/app/" + id + "/mount.lnk";
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
      continue;
    char text[512];
    const ssize_t length = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (length <= 0)
      continue;
    std::string target(text, static_cast<size_t>(length));
    while (!target.empty() &&
           (target.back() == '\0' ||
            std::isspace(static_cast<unsigned char>(target.back()))))
      target.pop_back();
    if (target.empty() || target[0] != '/')
      continue;
    if (!out.empty())
      out += ", ";
    out += HomeJs::JsString(id) + ": " + HomeJs::JsString(target);
    ++count;
  }
  closedir(dir);
  LOG_INFO("[CONTENTINFO] mount links=%d", count);
  return "{" + out + "}";
}

} // namespace

const char *InjectedScript() {
  static const char script[] =
#include "content_info_inject_js.inc"
      ;
  return script;
}

std::string ScriptConfig() {
  const auto label = [](const char *key) {
    return HomeJs::JsString(std::string(kStar) + toolbox_i18n::tr(key));
  };
  const auto text = [](const char *key) {
    return HomeJs::JsString(toolbox_i18n::tr(key));
  };
  return "globalThis.__aniketosInfo = {labels: {"
         "titleId: " + label("content_info.title_id") +
         ", contentId: " + label("content_info.content_id") +
         ", region: " + label("content_info.region") +
         ", platform: " + label("content_info.platform") +
         ", masterVersion: " + label("content_info.master_version") +
         ", systemVersion: " + label("content_info.system_version") +
         ", sdkVersion: " + label("content_info.sdk_version") +
         ", path: " + label("content_info.path") +
         ", mountedFrom: " + label("content_info.mounted_from") +
         ", metadataPath: " + label("content_info.metadata_path") +
         ", installed: " + label("content_info.installed") +
         ", lastUsed: " + label("content_info.last_used") +
         "}, regions: {"
         "U: " + text("content_info.region.america") +
         ", E: " + text("content_info.region.europe") +
         ", J: " + text("content_info.region.japan") +
         ", H: " + text("content_info.region.asia") +
         "}, links: " + mount_links() + "};";
}

} // namespace ContentInfo
