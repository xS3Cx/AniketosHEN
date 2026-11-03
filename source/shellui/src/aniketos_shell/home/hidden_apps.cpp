#include "aniketos_shell/home/hidden_apps.hpp"

#include "aniketos_shell/home/title_info.hpp"
#include "aniketos_ui/small_file.hpp"
#include "hooked_funcs.hpp"
#include "onion_cjson.hpp"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

extern onion::Settings g_settings;

namespace HiddenApps {
namespace {

namespace TitleInfo = AniketosShell::TitleInfo;

constexpr const char *kFile = ONION_DIST_USER_DATA_ROOT "/hidden_apps.json";
constexpr int kMaxMutationAttempts = 4;

std::mutex s_mutex;
bool s_loaded = false;
std::vector<std::string> s_titles;
std::vector<Entry> s_entries;

bool has(const std::vector<std::string> &titles, const std::string &title_id) {
  return std::find(titles.begin(), titles.end(), title_id) != titles.end();
}

std::string display_name(const std::string &title_id) {
  const std::string name = TitleInfo::ReadName(title_id);
  return name.empty() ? title_id : name;
}

std::string name_locked(const std::string &title_id) {
  for (const Entry &entry : s_entries)
    if (entry.title_id == title_id)
      return entry.name;
  return title_id;
}

std::vector<std::string> read_titles() {
  std::vector<std::string> titles;
  std::string text;
  if (!AniketosUI::ReadSmallFile(kFile, 64 * 1024, text))
    return titles;
  onion_cjson::Root root(text);
  if (!root) {
    LOG_WARN("[HIDDEN] ignoring unreadable %s", kFile);
    return titles;
  }
  const cJSON *entry_json = nullptr;
  cJSON_ArrayForEach(entry_json, onion_cjson::item(root.get(), "hidden")) {
    if (cJSON_IsString(entry_json) && entry_json->valuestring &&
        TitleInfo::Usable(entry_json->valuestring) &&
        !has(titles, entry_json->valuestring))
      titles.push_back(entry_json->valuestring);
  }
  return titles;
}

void ensure_loaded() {
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_loaded)
      return;
  }
  std::vector<std::string> titles = read_titles();
  const size_t count = titles.size();
  std::vector<Entry> entries;
  entries.reserve(count);
  for (const std::string &title_id : titles)
    entries.push_back({title_id, display_name(title_id)});
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_loaded)
      return;
    s_titles = std::move(titles);
    s_entries = std::move(entries);
    s_loaded = true;
  }
  LOG_INFO("[HIDDEN] loaded %zu apps", count);
}

bool write_titles(const std::vector<std::string> &titles) {
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    LOG_ERROR("[HIDDEN] could not build %s", kFile);
    return false;
  }
  cJSON_AddNumberToObject(root, "version", 1);
  cJSON *hidden = cJSON_AddArrayToObject(root, "hidden");
  if (!hidden) {
    cJSON_Delete(root);
    LOG_ERROR("[HIDDEN] could not build %s", kFile);
    return false;
  }
  for (const std::string &title_id : titles)
    cJSON_AddItemToArray(hidden, cJSON_CreateString(title_id.c_str()));
  const std::string text = onion_cjson::print_owned(root, true);
  if (text.empty()) {
    LOG_ERROR("[HIDDEN] could not serialize %s", kFile);
    return false;
  }
  if (!write_file_atomic(kFile, text.data(), text.size())) {
    LOG_ERROR("[HIDDEN] could not save %s", kFile);
    return false;
  }
  chmod(kFile, 0644); /* write_file_atomic leaves 0777 */
  return true;
}

enum class Change { None, Lost, Made };

/* `edit` turns a copy of the list into the new one, under the lock, false when
   there is nothing to do. The file is written outside the lock, then `commit`
   updates the rest of the state, unless the list changed meanwhile: the whole
   thing starts over then. Failing to write leaves the old list and says None. */
template <typename Edit, typename Commit>
Change change_list(Edit edit, Commit commit) {
  for (int attempt = 0; attempt < kMaxMutationAttempts; ++attempt) {
    std::vector<std::string> before, after;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      before = after = s_titles;
      if (!edit(after))
        return Change::None;
    }
    if (!write_titles(after))
      return Change::None;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_titles != before)
      continue;
    s_titles = after;
    commit();
    return Change::Made;
  }
  return Change::Lost;
}

}

bool Allowed(const std::string &title_id) { return TitleInfo::Usable(title_id); }

bool FilterEnabled() { return g_settings.home_hide_apps; }

std::string SqlList() {
  if (!FilterEnabled())
    return {};
  ensure_loaded();
  std::lock_guard<std::mutex> lock(s_mutex);
  return TitleInfo::SqlList(s_titles);
}

void Hide(const std::string &title_id) {
  if (!TitleInfo::Usable(title_id)) {
    LOG_WARN("[HIDDEN] invalid title '%s'", title_id.c_str());
    return;
  }
  ensure_loaded();
  const std::string name = display_name(title_id);
  size_t total = 0;
  const Change change = change_list(
      [&](std::vector<std::string> &titles) {
        if (has(titles, title_id))
          return false;
        titles.push_back(title_id);
        return true;
      },
      [&] {
        s_entries.push_back({title_id, name});
        total = s_titles.size();
      });
  if (change == Change::Lost)
    LOG_ERROR("[HIDDEN] %s was not recorded", title_id.c_str());
  if (change != Change::Made)
    return;
  LOG_INFO("[HIDDEN] hid %s, %zu total", title_id.c_str(), total);
  shellui_request_home_reload();
  notify("%s", TitleInfo::WithName("hideapp.hidden", name).c_str());
}

void Show(const std::string &title_id) {
  if (!TitleInfo::Usable(title_id)) {
    LOG_WARN("[HIDDEN] invalid title '%s'", title_id.c_str());
    return;
  }
  ensure_loaded();
  std::string name;
  size_t total = 0;
  const Change change = change_list(
      [&](std::vector<std::string> &titles) {
        const auto at = std::find(titles.begin(), titles.end(), title_id);
        if (at == titles.end())
          return false;
        titles.erase(at);
        name = name_locked(title_id);
        return true;
      },
      [&] {
        s_entries.erase(std::remove_if(s_entries.begin(), s_entries.end(),
                                       [&title_id](const Entry &entry) {
                                         return entry.title_id == title_id;
                                       }),
                        s_entries.end());
        total = s_titles.size();
      });
  if (change == Change::Lost)
    LOG_ERROR("[HIDDEN] %s was not restored", title_id.c_str());
  if (change != Change::Made)
    return;
  LOG_INFO("[HIDDEN] restored %s, %zu left", title_id.c_str(), total);
  shellui_request_home_reload();
  notify("%s", TitleInfo::WithName("hideapp.shown", name).c_str());
}

void ShowAll() {
  ensure_loaded();
  size_t total = 0;
  std::string names;
  const Change change = change_list(
      [&](std::vector<std::string> &titles) {
        if (titles.empty())
          return false;
        total = titles.size();
        titles.clear();
        names.clear();
        for (const Entry &entry : s_entries) {
          if (!names.empty())
            names += ", ";
          names += entry.name;
        }
        return true;
      },
      [] { s_entries.clear(); });
  if (change == Change::Lost)
    LOG_ERROR("[HIDDEN] the list was not cleared");
  if (change != Change::Made)
    return;
  LOG_INFO("[HIDDEN] restored all %zu apps", total);
  shellui_request_home_reload();
  notify("%s", TitleInfo::WithName("hideapp.shown", names).c_str());
}

std::vector<Entry> List() {
  ensure_loaded();
  std::lock_guard<std::mutex> lock(s_mutex);
  return s_entries;
}

}
