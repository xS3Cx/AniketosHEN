#include "payload_center.hpp"

#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "payload_progress.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_navigation.hpp"

#include <onion/distribution.h>
#include <onion/net.h>
#include <onion/payload_catalog_policy.hpp>
#include <onion/payload_catalog.hpp>
#include <onion/payload_identity.h>
#include <onion/payload_sources_json.hpp>

#include <onion_cjson.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace onion::shellui::payload_center {
namespace {

constexpr const char *kSourcesUser =
    ONION_DIST_USER_DATA_ROOT "/payload_center/sources.json";
constexpr const char *kSourcesData =
    ONION_DIST_DATA_ROOT "/payload_center/sources.json";
constexpr const char *kCacheUser =
    ONION_DIST_USER_DATA_ROOT "/payload_center/cache";
constexpr const char *kCacheData = ONION_DIST_DATA_ROOT "/payload_center/cache";
constexpr const char *kPayloadUser =
    ONION_DIST_USER_DATA_ROOT "/payloads";
constexpr const char *kPayloadData = ONION_DIST_DATA_ROOT "/payloads";

struct Installed {
  std::string path;
  std::string identity;
  std::string match_key;
  std::string name;
  std::string version;
  std::string last_update;
  bool autostart = false;
};

std::vector<Entry> s_entries;
std::vector<std::string> s_categories;
std::map<std::string, Installed> s_installed;
onion::payload_catalog::SourcesFile s_sources;
long long s_cache_stamp = -1;
uint64_t s_prompt_token = 0;
bool s_prompt_pending = false;
enum class PromptKind { None, AddSource, InstallUrl, Search };
PromptKind s_prompt_kind = PromptKind::None;

std::string alias_data(std::string_view path) {
  if (path.starts_with(ONION_DIST_DATA_ROOT))
    return std::string(ONION_DIST_USER_DATA_ROOT) +
           std::string(path.substr(std::strlen(ONION_DIST_DATA_ROOT)));
  return std::string(path);
}

bool read_text(const std::string &path, std::string *out, std::size_t maximum) {
  const std::string candidates[] = {path, alias_data(path)};
  for (const std::string &candidate : candidates) {
    FILE *file = std::fopen(candidate.c_str(), "rb");
    if (!file)
      continue;
    struct stat st {};
    if (fstat(fileno(file), &st) != 0 || st.st_size < 0 ||
        static_cast<std::size_t>(st.st_size) > maximum) {
      std::fclose(file);
      return false;
    }
    out->assign(static_cast<std::size_t>(st.st_size), '\0');
    const std::size_t read_bytes =
        out->empty() ? 0 : std::fread(out->data(), 1, out->size(), file);
    std::fclose(file);
    if (read_bytes != out->size())
      return false;
    return true;
  }
  return false;
}

long long mtime_of(const std::string &path) {
  const std::string candidates[] = {path, alias_data(path)};
  for (const std::string &candidate : candidates) {
    struct stat st {};
    if (stat(candidate.c_str(), &st) == 0)
      return static_cast<long long>(st.st_mtime);
  }
  return 0;
}

std::string json_string(const cJSON *root, const char *key) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

void load_sources() {
  onion::payload_catalog::SourcesFile loaded;
  if (onion::payload_catalog::load_sources_file(kSourcesUser, &loaded) ||
      onion::payload_catalog::load_sources_file(kSourcesData, &loaded)) {
    s_sources = std::move(loaded);
    return;
  }
  s_sources = onion::payload_catalog::default_sources();
}

bool save_sources() {
  return onion::payload_catalog::save_sources_file(kSourcesUser, s_sources) ||
         onion::payload_catalog::save_sources_file(kSourcesData, s_sources);
}

std::string cache_catalog_path(const onion::payload_catalog::Source &source) {
  return std::string(ONION_DIST_DATA_ROOT) + "/payload_center/cache/catalog-" +
         onion::payload_catalog::source_hash8(source.url) + ".json";
}

void scan_payloads() {
  s_installed.clear();
  const std::string directories[] = {kPayloadUser, kPayloadData};
  for (const std::string &directory : directories) {
    DIR *dir = opendir(directory.c_str());
    if (!dir)
      continue;
    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
      const std::size_t length = std::strlen(entry->d_name);
      if (length < 5 || std::strcmp(entry->d_name + length - 4, ".elf") != 0)
        continue;
      const std::string path = directory + "/" + entry->d_name;
      char identity[ONION_PAYLOAD_IDENTITY_SIZE] = {};
      if (!onion_payload_identity_from_path(path.c_str(), identity,
                                            sizeof(identity)))
        continue;
      Installed installed;
      installed.path = path;
      installed.identity = identity;
      installed.autostart = access((path + ".auto_start").c_str(), F_OK) == 0;
      std::string raw;
      if (read_text(path + ".meta.json", &raw, 128 * 1024)) {
        cJSON *root = cJSON_Parse(raw.c_str());
        if (root) {
          installed.match_key = json_string(root, "match_key");
          installed.name = json_string(root, "name");
          installed.version = json_string(root, "item_version");
          if (installed.version.empty())
            installed.version = json_string(root, "version");
          installed.last_update = json_string(root, "last_update");
          cJSON_Delete(root);
        }
      }
      if (!installed.match_key.empty())
        s_installed[installed.match_key] = std::move(installed);
    }
    closedir(dir);
  }
}

bool load_catalog(const onion::payload_catalog::Source &source,
                  onion::payload_catalog::Catalog *catalog) {
  std::string body;
  if (!read_text(cache_catalog_path(source), &body,
                 onion::payload_catalog::kMaxCatalogBytes))
    return false;
  const auto parsed = onion::payload_catalog::parse_catalog(body);
  if (!parsed.ok)
    return false;
  *catalog = parsed.catalog;
  return true;
}

std::string selection_for(const onion::payload_catalog::Source &source,
                          const onion::payload_catalog::Item &item) {
  return onion::payload_catalog::source_hash8(source.url) + "|" + item.id;
}

void rebuild() {
  s_entries.clear();
  s_categories.clear();
  for (const auto &source : s_sources.sources) {
    onion::payload_catalog::Catalog catalog;
    if (!load_catalog(source, &catalog))
      continue;
    for (const auto &item : catalog.items) {
      Entry entry;
      entry.item = item;
      entry.source_id = onion::payload_catalog::source_hash8(source.url);
      entry.source_name = source.name;
      entry.source_url = source.url;
      entry.selection = selection_for(source, item);
      const std::string match_key =
          onion::payload_catalog::item_match_key(item, source.url);
      const auto found = s_installed.find(match_key);
      if (found != s_installed.end()) {
        entry.installed = true;
        entry.autostart = found->second.autostart;
        entry.installed_path = found->second.path;
        entry.identity = found->second.identity;
        entry.installed_version = found->second.version;
        entry.update_available =
            onion::payload_catalog::compare_versions(
                item.version, item.last_update, found->second.version,
                found->second.last_update) > 0;
      }
      if (!item.category.empty() &&
          std::find(s_categories.begin(), s_categories.end(), item.category) ==
              s_categories.end())
        s_categories.push_back(item.category);
      s_entries.push_back(std::move(entry));
    }
  }
  g_ui.pldc_items.clear();
  for (const auto &entry : s_entries)
    g_ui.pldc_items.push_back(entry.item);
}

std::string state_from_reply(const std::string &reply, int *task_id) {
  onion_cjson::Root root(reply);
  if (!root)
    return {};
  if (task_id)
    *task_id = onion_cjson::int_item(root.get(), "task_id", 0);
  return onion_cjson::string_item(root.get(), "state", "");
}

bool query_matches(const Entry &entry) {
  if (g_ui.pldc_query.empty())
    return true;
  const std::string query = onion::payload_catalog::ascii_lower(g_ui.pldc_query);
  const std::string haystack = onion::payload_catalog::ascii_lower(
      entry.item.name + " " + entry.item.description + " " +
      entry.item.category);
  return haystack.find(query) != std::string::npos;
}

}

bool entry_matches_query(const Entry &entry) { return query_matches(entry); }

void reload(bool force) {
  long long stamp = mtime_of(kSourcesUser);
  stamp += mtime_of(ONION_DIST_DATA_ROOT "/payloads");
  for (const auto &source : s_sources.sources)
    stamp += mtime_of(cache_catalog_path(source));
  if (!force && stamp == s_cache_stamp && !s_entries.empty())
    return;
  s_cache_stamp = stamp;
  load_sources();
  scan_payloads();
  rebuild();
}

void Tick() {
  static long long last_check = 0;
  const long long now = static_cast<long long>(std::time(nullptr));
  if (now - last_check >= 2) {
    last_check = now;
    reload(false);
  }
  if (!s_prompt_pending)
    return;
  bool confirmed = false;
  std::string typed;
  if (!SystemDialogs::PollPrompt(s_prompt_token, &confirmed, &typed))
    return;
  s_prompt_pending = false;
  const PromptKind kind = s_prompt_kind;
  s_prompt_kind = PromptKind::None;
  if (!confirmed)
    return;
  if (kind == PromptKind::AddSource) {
    std::string error;
    if (!onion::payload_catalog::add_source(&s_sources, typed, typed, &error)) {
      notify("notify.payload_center.bad_source");
      return;
    }
    if (!save_sources()) {
      notify("notify.payload_center.save_failed");
      return;
    }
    reload(true);
    notify("notify.payload_center.source_added");
  } else if (kind == PromptKind::InstallUrl) {
    if (!start_install_url(onion::payload_catalog::trim(typed), true))
      notify("notify.payload_center.install_failed");
  } else if (kind == PromptKind::Search) {
    g_ui.pldc_query = onion::payload_catalog::trim(typed);
    toolbox_push_resource("pldc_list.xml");
  }
}

const std::vector<Entry> &entries() { return s_entries; }

std::vector<const Entry *> visible_entries() {
  std::vector<const Entry *> out;
  for (const auto &entry : s_entries) {
    if (!g_ui.pldc_category.empty() &&
        entry.item.category != g_ui.pldc_category)
      continue;
    if (!entry_matches_query(entry))
      continue;
    out.push_back(&entry);
    if (out.size() >= 200)
      break;
  }
  return out;
}

bool select_visible_index(std::size_t index) {
  const auto visible = visible_entries();
  if (index >= visible.size())
    return false;
  g_ui.active_pldc_item = visible[index]->selection;
  return true;
}

std::vector<std::string> categories() { return s_categories; }

const std::vector<onion::payload_catalog::Source> &sources() {
  return s_sources.sources;
}

const Entry *find_entry(std::string_view selection) {
  for (const auto &entry : s_entries)
    if (entry.selection == selection)
      return &entry;
  return nullptr;
}

std::string installed_identity(std::string_view selection) {
  const Entry *entry = find_entry(selection);
  return entry && entry->installed ? entry->identity : std::string{};
}

bool start_install(const Entry &entry, bool replace) {
  std::string reply;
  if (!IPC_Client::getInstance(true).InstallPayload(
          entry.source_id, entry.item.id, replace, reply)) {
    notify("notify.payload_center.install_failed");
    return false;
  }
  int task_id = 0;
  const std::string state = state_from_reply(reply, &task_id);
  if (state == "started" || state == "already_running") {
    show_progress(static_cast<uint32_t>(task_id));
    return true;
  }
  notify("notify.payload_center.busy");
  return false;
}

bool start_update_for_path(const std::string &payload_path) {
  reload(false);
  std::string raw;
  if (!read_text(payload_path + ".meta.json", &raw, 128 * 1024))
    return false;
  cJSON *root = cJSON_Parse(raw.c_str());
  if (!root)
    return false;
  const std::string item_id = json_string(root, "id");
  const std::string source_url = json_string(root, "source_url");
  cJSON_Delete(root);
  if (item_id.empty())
    return false;
  for (const auto &entry : s_entries) {
    if (entry.item.id == item_id && entry.source_url == source_url)
      return start_install(entry, true);
  }
  return false;
}

bool remove_installed(const std::string &identity, std::string name) {
  std::string reply;
  if (!IPC_Client::getInstance(true).RemovePayload(identity, reply))
    return false;
  if (name.empty())
    name = identity;
  notify("notify.payload_center.removed", name.c_str());
  reload(true);
  return true;
}

bool start_install_url(const std::string &url, bool replace) {  std::string reply;
  if (!IPC_Client::getInstance(true).InstallPayloadFromUrl(url, replace,
                                                           reply)) {
    notify("notify.payload_center.install_failed");
    return false;
  }
  int task_id = 0;
  const std::string state = state_from_reply(reply, &task_id);
  if (state == "started" || state == "already_running") {
    show_progress(static_cast<uint32_t>(task_id));
    return true;
  }
  notify("notify.payload_center.busy");
  return false;
}

bool start_refresh() {
  std::string reply;
  if (!IPC_Client::getInstance(true).RefreshPayloadCatalog("", reply)) {
    notify("notify.payload_center.refresh_failed");
    return false;
  }
  int task_id = 0;
  const std::string state = state_from_reply(reply, &task_id);
  if (state == "started" || state == "already_running") {
    show_progress(static_cast<uint32_t>(task_id));
    return true;
  }
  notify("notify.payload_center.busy");
  return false;
}

void show_progress(uint32_t task_id) {
  ::payload_progress_show(task_id);
  if (!::payload_progress_open_page())
    notify("notify.payload_center.navigation_failed");
}

void request_add_source() {
  SystemDialogs::PromptOptions options;
  options.title = toolbox_i18n::tr("payload_center.add_source");
  options.message = toolbox_i18n::tr("payload_center.add_source.message");
  options.positive = toolbox_i18n::tr("pkg.msg.ok");
  options.negative = toolbox_i18n::tr("pkg.msg.cancel");
  std::string failure;
  s_prompt_token = 0;
  if (!SystemDialogs::ShowPrompt(options, &failure, nullptr, &s_prompt_token)) {
    notify("notify.payload_center.prompt_failed");
    return;
  }
  s_prompt_kind = PromptKind::AddSource;
  s_prompt_pending = true;
}

void request_install_url() {
  SystemDialogs::PromptOptions options;
  options.title = toolbox_i18n::tr("payload_center.add_url");
  options.message = toolbox_i18n::tr("payload_center.add_url.message");
  options.text = "https://";
  options.positive = toolbox_i18n::tr("pkg.msg.ok");
  options.negative = toolbox_i18n::tr("pkg.msg.cancel");
  std::string failure;
  s_prompt_token = 0;
  if (!SystemDialogs::ShowPrompt(options, &failure, nullptr, &s_prompt_token)) {
    notify("notify.payload_center.prompt_failed");
    return;
  }
  s_prompt_kind = PromptKind::InstallUrl;
  s_prompt_pending = true;
}

void request_search() {
  SystemDialogs::PromptOptions options;
  options.title = toolbox_i18n::tr("payload_center.search");
  options.message = toolbox_i18n::tr("payload_center.search.message");
  options.text = g_ui.pldc_query;
  options.positive = toolbox_i18n::tr("pkg.msg.ok");
  options.negative = toolbox_i18n::tr("pkg.msg.cancel");
  std::string failure;
  s_prompt_token = 0;
  if (!SystemDialogs::ShowPrompt(options, &failure, nullptr, &s_prompt_token)) {
    notify("notify.payload_center.prompt_failed");
    return;
  }
  s_prompt_kind = PromptKind::Search;
  s_prompt_pending = true;
}

bool remove_source(std::string_view source_id) {
  for (const auto &source : s_sources.sources) {
    if (source.builtin)
      continue;
    if (source.url == source_id ||
        onion::payload_catalog::source_hash8(source.url) == source_id) {
      if (!onion::payload_catalog::remove_source(&s_sources, source.url))
        return false;
      if (!save_sources())
        return false;
      reload(true);
      return true;
    }
  }
  return false;
}

void clear_cache() {
  for (const char *directory : {kCacheUser, kCacheData}) {
    DIR *dir = opendir(directory);
    if (!dir)
      continue;
    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
      if (std::strncmp(entry->d_name, "catalog-", 8) != 0 &&
          std::strncmp(entry->d_name, "icons-", 6) != 0)
        continue;
      const std::string path = std::string(directory) + "/" + entry->d_name;
      unlink(path.c_str());
    }
    closedir(dir);
  }
  reload(true);
}

std::string ftp_hint() {
  char address[64] = {};
  if (onion_net_get_ip_address(address, sizeof(address)) != 0 || !address[0])
    return {};
  return std::string("ftp://") + address + ":2121 -> " +
         ONION_DIST_DATA_ROOT "/payloads";
}

std::string last_refresh_text() {
  long long newest = 0;
  for (const auto &source : s_sources.sources) {
    const std::string path = std::string(ONION_DIST_DATA_ROOT) +
                             "/payload_center/cache/catalog-" +
                             onion::payload_catalog::source_hash8(source.url) +
                             ".status.json";
    newest = std::max(newest, mtime_of(path));
  }
  if (newest <= 0)
    return toolbox_i18n::tr("payload_center.never");
  struct tm value {};
  const time_t stamp = static_cast<time_t>(newest);
  localtime_r(&stamp, &value);
  char text[32] = {};
  std::snprintf(text, sizeof(text), "%04d-%02d-%02d", value.tm_year + 1900,
                value.tm_mon + 1, value.tm_mday);
  return text;
}

}
