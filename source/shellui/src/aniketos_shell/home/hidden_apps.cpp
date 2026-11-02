#include "aniketos_shell/home/hidden_apps.hpp"

#include "hooked_funcs.hpp"
#include "onion_cjson.hpp"
#include "toolbox_i18n.hpp"

#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

extern onion::Settings g_settings;

namespace HomeHidden {
namespace {

constexpr const char *kFile = ONION_DIST_USER_DATA_ROOT "/hidden_apps.json";
constexpr int kMaxMutationAttempts = 4;

std::mutex s_mutex;
bool s_loaded = false;
std::vector<std::string> s_titles;
std::vector<Entry> s_entries;

bool usable_title(const std::string &title_id) {
  if (title_id.size() != 9)
    return false;
  for (const char c : title_id)
    if (!std::isalnum(static_cast<unsigned char>(c)))
      return false;
  return title_id.compare(0, 4, "NPXS") != 0;
}

bool has(const std::vector<std::string> &titles, const std::string &title_id) {
  return std::find(titles.begin(), titles.end(), title_id) != titles.end();
}

std::string read_title_name(const std::string &title_id) {
  const std::string path = "/user/app/" + title_id + "/sce_sys/param.json";
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return {};
  std::string json(64 * 1024, '\0');
  json.resize(std::fread(json.data(), 1, json.size(), file));
  std::fclose(file);
  size_t at = json.find("\"titleName\"");
  if (at == std::string::npos)
    return {};
  at = json.find(':', at);
  at = at == std::string::npos ? at : json.find('"', at);
  if (at == std::string::npos)
    return {};
  const size_t end = json.find('"', at + 1);
  return end == std::string::npos ? std::string()
                                  : json.substr(at + 1, end - at - 1);
}

std::string display_name(const std::string &title_id) {
  const std::string name = read_title_name(title_id);
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
  FILE *file = std::fopen(kFile, "rb");
  if (!file)
    return titles;
  std::string text(64 * 1024, '\0');
  text.resize(std::fread(text.data(), 1, text.size(), file));
  std::fclose(file);
  onion_cjson::Root root(text);
  if (!root) {
    LOG_WARN("[HIDDEN] ignoring unreadable %s", kFile);
    return titles;
  }
  const cJSON *entry_json = nullptr;
  cJSON_ArrayForEach(entry_json, onion_cjson::item(root.get(), "hidden")) {
    if (cJSON_IsString(entry_json) && entry_json->valuestring &&
        usable_title(entry_json->valuestring) &&
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
  const std::string temporary = std::string(kFile) + ".tmp";
  FILE *file = std::fopen(temporary.c_str(), "wb");
  bool written = false;
  if (file) {
    written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    written = std::fclose(file) == 0 && written;
  }
  if (!written || std::rename(temporary.c_str(), kFile) != 0) {
    unlink(temporary.c_str());
    LOG_ERROR("[HIDDEN] could not save %s", kFile);
    return false;
  }
  return true;
}

std::string with_name(const char *key, const std::string &name) {
  const std::string text = toolbox_i18n::tr(key);
  const size_t at = text.find("%s");
  return at == std::string::npos
             ? text
             : text.substr(0, at) + name + text.substr(at + 2);
}

}

bool Allowed(const std::string &title_id) { return usable_title(title_id); }

bool FilterEnabled() { return g_settings.home_hide_apps; }

std::string SqlList() {
  if (!FilterEnabled())
    return {};
  ensure_loaded();
  std::lock_guard<std::mutex> lock(s_mutex);
  std::string out;
  for (const std::string &title_id : s_titles) {
    if (!usable_title(title_id))
      continue;
    if (!out.empty())
      out += ",";
    out += "'" + title_id + "'";
  }
  return out;
}

bool IsHidden(const std::string &title_id) {
  ensure_loaded();
  std::lock_guard<std::mutex> lock(s_mutex);
  return has(s_titles, title_id);
}

void Hide(const std::string &title_id) {
  if (!usable_title(title_id)) {
    LOG_WARN("[HIDDEN] invalid title '%s'", title_id.c_str());
    return;
  }
  ensure_loaded();
  const std::string name = display_name(title_id);
  size_t total = 0;
  bool changed = false;
  for (int attempt = 0; attempt < kMaxMutationAttempts && !changed; ++attempt) {
    std::vector<std::string> before;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (has(s_titles, title_id))
        return;
      before = s_titles;
    }
    std::vector<std::string> after = before;
    after.push_back(title_id);
    if (!write_titles(after))
      return;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (s_titles != before)
        continue;
      s_titles = after;
      s_entries.push_back({title_id, name});
      total = s_titles.size();
      changed = true;
    }
  }
  if (!changed) {
    LOG_ERROR("[HIDDEN] %s was not recorded", title_id.c_str());
    return;
  }
  LOG_INFO("[HIDDEN] hid %s, %zu total", title_id.c_str(), total);
  shellui_request_home_reload();
  notify("%s", with_name("hideapp.hidden", name).c_str());
}

void Show(const std::string &title_id) {
  if (!usable_title(title_id)) {
    LOG_WARN("[HIDDEN] invalid title '%s'", title_id.c_str());
    return;
  }
  ensure_loaded();
  std::string name;
  size_t total = 0;
  bool changed = false;
  for (int attempt = 0; attempt < kMaxMutationAttempts && !changed; ++attempt) {
    std::vector<std::string> before;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      const auto at = std::find(s_titles.begin(), s_titles.end(), title_id);
      if (at == s_titles.end())
        return;
      name = name_locked(title_id);
      before = s_titles;
    }
    std::vector<std::string> after = before;
    const auto at = std::find(after.begin(), after.end(), title_id);
    if (at != after.end())
      after.erase(at);
    if (!write_titles(after))
      return;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (s_titles != before)
        continue;
      s_titles = after;
      s_entries.erase(std::remove_if(s_entries.begin(), s_entries.end(),
                                     [&title_id](const Entry &entry) {
                                       return entry.title_id == title_id;
                                     }),
                      s_entries.end());
      total = s_titles.size();
      changed = true;
    }
  }
  if (!changed) {
    LOG_ERROR("[HIDDEN] %s was not restored", title_id.c_str());
    return;
  }
  LOG_INFO("[HIDDEN] restored %s, %zu left", title_id.c_str(), total);
  shellui_request_home_reload();
  notify("%s", with_name("hideapp.shown", name).c_str());
}

void ShowAll() {
  ensure_loaded();
  size_t total = 0;
  std::string names;
  bool changed = false;
  for (int attempt = 0; attempt < kMaxMutationAttempts && !changed; ++attempt) {
    std::vector<std::string> before;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (s_titles.empty())
        return;
      before = s_titles;
      names.clear();
      for (const Entry &entry : s_entries) {
        if (!names.empty())
          names += ", ";
        names += entry.name;
      }
    }
    const std::vector<std::string> after;
    if (!write_titles(after))
      return;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (s_titles != before)
        continue;
      s_titles.clear();
      s_entries.clear();
      total = before.size();
      changed = true;
    }
  }
  if (!changed) {
    LOG_ERROR("[HIDDEN] the list was not cleared");
    return;
  }
  LOG_INFO("[HIDDEN] restored all %zu apps", total);
  shellui_request_home_reload();
  notify("%s", with_name("hideapp.shown", names).c_str());
}

std::vector<Entry> List() {
  ensure_loaded();
  std::lock_guard<std::mutex> lock(s_mutex);
  return s_entries;
}

}
