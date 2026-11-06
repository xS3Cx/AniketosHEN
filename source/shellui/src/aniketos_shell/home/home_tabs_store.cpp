#include "aniketos_shell/home/home_tabs_store.hpp"

#include "aniketos_shell/home/home_tabs.hpp"
#include "aniketos_shell/home/title_info.hpp"
#include "aniketos_shell/home/web_tiles.hpp"
#include "aniketos_shell/input/osk_glyphs.hpp"
#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_theme/clock/shell_clock.hpp"
#include "aniketos_ui/small_file.hpp"
#include "hooked_funcs.hpp"
#include "onion_cjson.hpp"
#include "toolbox_i18n.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace HomeTabsStore {
namespace {

namespace TitleInfo = AniketosShell::TitleInfo;
using toolbox_i18n::tr;

constexpr const char *kFile = ONION_DIST_USER_DATA_ROOT "/home_tabs.json";
constexpr const char *kOldFile =
    ONION_DIST_USER_DATA_ROOT "/home_bookmark.json";
constexpr const char *kRegistryKey = "aniketos.home_tabs";
constexpr const char *kClockKey = "aniketos.clock";
constexpr size_t kMaxNameBytes = 40;

struct Tab {
  std::string id;
  std::string name;
  std::string icon;
  bool visible = true;
  std::vector<std::string> titles;
};

struct Target {
  bool stock = false;
  StockRow row = StockRow::Games;
  int slot = -1;
};

std::mutex s_mutex;
bool s_loaded = false;
std::vector<Tab> s_tabs;
std::vector<std::string> s_games;
std::vector<std::string> s_media;
std::vector<std::string> s_moved;
unsigned s_revision = 1;
std::atomic<bool> s_script_in{false};
std::atomic<bool> s_ui_changed{false};

bool has(const std::vector<std::string> &list, const std::string &title) {
  return std::find(list.begin(), list.end(), title) != list.end();
}

bool erase(std::vector<std::string> &list, const std::string &title) {
  const auto at = std::find(list.begin(), list.end(), title);
  if (at == list.end())
    return false;
  list.erase(at);
  return true;
}

int slot_of(const std::string &id) {
  if (id.size() != 2 || id[0] != 'c')
    return -1;
  const int slot = id[1] - '1';
  if (slot < 0 || slot >= kMaxCustomTabs)
    return -1;
  return slot;
}

std::string id_of_slot(int slot) {
  if (slot < 0 || slot >= kMaxCustomTabs)
    return {};
  return std::string("c") + static_cast<char>('1' + slot);
}

int limit_of_slot(int slot) {
  return kContentLimitFirst + slot;
}

int slot_of_limit(int limit) {
  if (limit < kContentLimitFirst || limit > kContentLimitLast)
    return -1;
  return limit - kContentLimitFirst;
}

std::string trim_spaces(const std::string &text) {
  size_t first = 0;
  while (first < text.size() &&
         (text[first] == ' ' || text[first] == '\t' || text[first] == '\n' ||
          text[first] == '\r'))
    ++first;
  size_t last = text.size();
  while (last > first &&
         (text[last - 1] == ' ' || text[last - 1] == '\t' ||
          text[last - 1] == '\n' || text[last - 1] == '\r'))
    --last;
  return text.substr(first, last - first);
}

std::string clean_name(const std::string &typed) {
  std::string name;
  for (const char c : typed)
    if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f)
      name += c;
  const size_t first = name.find_first_not_of(' ');
  if (first == std::string::npos)
    return {};
  name = name.substr(first, name.find_last_not_of(' ') - first + 1);
  if (name.size() > kMaxNameBytes) {
    size_t cut = kMaxNameBytes;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xc0) == 0x80)
      --cut;
    name.resize(cut);
  }
  return name;
}

std::string default_tab_name(int slot) {
  return toolbox_i18n::format("home_tabs.default_tab_name", slot + 1);
}

bool decode_codepoint(const std::string &text, uint32_t *code) {
  if (text.empty())
    return false;
  const auto lead = static_cast<unsigned char>(text[0]);
  size_t length = 0;
  uint32_t value = 0;
  if (lead < 0x80) {
    length = 1;
    value = lead;
  } else if ((lead & 0xe0) == 0xc0) {
    length = 2;
    value = lead & 0x1f;
  } else if ((lead & 0xf0) == 0xe0) {
    length = 3;
    value = lead & 0x0f;
  } else if ((lead & 0xf8) == 0xf0) {
    length = 4;
    value = lead & 0x07;
  } else {
    return false;
  }
  if (text.size() != length)
    return false;
  for (size_t i = 1; i < length; ++i) {
    const auto next = static_cast<unsigned char>(text[i]);
    if ((next & 0xc0) != 0x80)
      return false;
    value = (value << 6) | (next & 0x3f);
  }
  if ((length == 2 && value < 0x80) || (length == 3 && value < 0x800) ||
      (length == 4 && value < 0x10000))
    return false;
  if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
    return false;
  *code = value;
  return true;
}

std::string utf8_of(uint32_t code) {
  std::string out;
  if (code < 0x80) {
    out += static_cast<char>(code);
  } else if (code < 0x800) {
    out += static_cast<char>(0xc0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 0x3f));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xe0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
    out += static_cast<char>(0x80 | (code & 0x3f));
  } else {
    out += static_cast<char>(0xf0 | (code >> 18));
    out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
    out += static_cast<char>(0x80 | (code & 0x3f));
  }
  return out;
}

bool tab_holds_locked(const std::string &title) {
  for (const Tab &tab : s_tabs)
    if (has(tab.titles, title))
      return true;
  return false;
}

bool visible_holds_locked(const std::string &title) {
  for (const Tab &tab : s_tabs)
    if (tab.visible && has(tab.titles, title))
      return true;
  return false;
}

Tab *find_tab_locked(int slot) {
  for (Tab &tab : s_tabs)
    if (slot_of(tab.id) == slot)
      return &tab;
  return nullptr;
}

const Tab *find_tab_const_locked(int slot) {
  for (const Tab &tab : s_tabs)
    if (slot_of(tab.id) == slot)
      return &tab;
  return nullptr;
}

void read_list(const cJSON *root, const char *key,
               std::vector<std::string> &into) {
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, key)) {
    if (cJSON_IsString(item) && TitleInfo::Valid(item->valuestring) &&
        !has(into, item->valuestring))
      into.push_back(item->valuestring);
  }
}

void write_list(cJSON *root, const char *key,
                const std::vector<std::string> &list) {
  cJSON *array = cJSON_AddArrayToObject(root, key);
  for (const std::string &title : list)
    cJSON_AddItemToArray(array, cJSON_CreateString(title.c_str()));
}

void write_tab(cJSON *array, const Tab &tab) {
  cJSON *object = cJSON_CreateObject();
  cJSON_AddStringToObject(object, "id", tab.id.c_str());
  cJSON_AddStringToObject(object, "name", tab.name.c_str());
  cJSON_AddStringToObject(object, "icon", tab.icon.c_str());
  cJSON_AddBoolToObject(object, "visible", tab.visible);
  write_list(object, "titles", tab.titles);
  cJSON_AddItemToArray(array, object);
}

std::string text_locked() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "version", 2);
  cJSON_AddNumberToObject(root, "revision", s_revision);
  cJSON *tabs = cJSON_AddArrayToObject(root, "tabs");
  for (const Tab &tab : s_tabs)
    write_tab(tabs, tab);
  write_list(root, "games", s_games);
  write_list(root, "media", s_media);
  write_list(root, "moved", s_moved);
  return onion_cjson::print_owned(root, true);
}

void write_now(const std::string &text) {
  if (text.empty() || !write_file_atomic(kFile, text.data(), text.size())) {
    LOG_WARN("[HOMETABS] could not save %s", kFile);
    return;
  }
  chmod(kFile, 0644);
}

void save(const std::string &text) {
  write_now(text);
}

std::string changed_locked() {
  ++s_revision;
  s_ui_changed = true;
  return text_locked();
}

void clean_moved_locked() {
  s_moved.erase(std::remove_if(s_moved.begin(), s_moved.end(),
                               [](const std::string &title) {
                                 return !tab_holds_locked(title) &&
                                        !has(s_games, title) &&
                                        !has(s_media, title);
                               }),
                s_moved.end());
}

void load_locked() {
  if (s_loaded)
    return;
  s_loaded = true;
  if (access(kOldFile, F_OK) == 0) {
    unlink(kOldFile);
    LOG_INFO("[HOMETABS] removed %s", kOldFile);
  }
  if (access(kFile, F_OK) != 0) {
    write_now(text_locked());
    LOG_INFO("[HOMETABS] created %s", kFile);
    return;
  }
  std::string text;
  if (!AniketosUI::ReadSmallFile(kFile, 64 * 1024, text)) {
    LOG_WARN("[HOMETABS] %s cannot be read; starting empty", kFile);
    return;
  }
  onion_cjson::Root root(text);
  if (!root) {
    LOG_WARN("[HOMETABS] %s is not valid JSON; starting empty", kFile);
    return;
  }
  if (onion_cjson::int_item(root.get(), "version", 0) != 2) {
    LOG_WARN("[HOMETABS] %s is not version 2; starting empty", kFile);
    return;
  }
  s_revision = static_cast<unsigned>(
      std::max(1, onion_cjson::int_item(root.get(), "revision", 1)));
  const cJSON *tabs = onion_cjson::item(root.get(), "tabs");
  if (tabs && !cJSON_IsArray(tabs))
    LOG_WARN("[HOMETABS] %s: tabs is not an array; ignored", kFile);
  if (tabs && cJSON_IsArray(tabs)) {
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, tabs) {
      if (static_cast<int>(s_tabs.size()) >= kMaxCustomTabs)
        break;
      if (!cJSON_IsObject(entry))
        continue;
      const std::string id = onion_cjson::string_item(entry, "id", "");
      const int slot = slot_of(id);
      if (slot < 0 || find_tab_const_locked(slot)) {
        LOG_WARN("[HOMETABS] ignored a tab with id '%s'", id.c_str());
        continue;
      }
      Tab tab;
      tab.id = id;
      tab.name = clean_name(onion_cjson::string_item(entry, "name", ""));
      if (tab.name.empty())
        tab.name = default_tab_name(slot);
      const char *raw_icon = onion_cjson::string_item(entry, "icon", "");
      tab.icon = SanitizeIcon(raw_icon, kDefaultCustomIcon);
      if (raw_icon[0] != '\0' && tab.icon != raw_icon)
        LOG_WARN("[HOMETABS] tab %d had an invalid icon; default used",
                 slot + 1);
      tab.visible = onion_cjson::bool_item(entry, "visible", true);
      read_list(entry, "titles", tab.titles);
      s_tabs.push_back(tab);
    }
  }
  read_list(root.get(), "games", s_games);
  read_list(root.get(), "media", s_media);
  read_list(root.get(), "moved", s_moved);
  clean_moved_locked();
  LOG_INFO("[HOMETABS] loaded %zu tabs, %zu in Games, %zu in Media, %zu moved",
           s_tabs.size(), s_games.size(), s_media.size(), s_moved.size());
}

std::string target_name(Target target) {
  if (target.stock)
    return HomeTabs::TabName(target.row == StockRow::Games
                                 ? HomeTabs::StockTab::Games
                                 : HomeTabs::StockTab::Media);
  std::lock_guard<std::mutex> lock(s_mutex);
  const Tab *tab = find_tab_const_locked(target.slot);
  return tab ? tab->name : std::string();
}

void reload_home() { shellui_request_home_reload(); }

bool resolve(MonoObject *promise, const std::string &value) {
  MonoMethod *method = nullptr;
  for (MonoClass *k = mono_object_get_class(promise); k && !method;
       k = mono_class_get_parent(k)) {
    method = mono_class_get_method_from_name(k, "Resolve", 1);
    void *iter = nullptr;
    while (!method) {
      MonoMethod *candidate = mono_class_get_methods(k, &iter);
      if (!candidate)
        break;
      const char *name = mono_method_get_name(candidate);
      const size_t length = name ? std::strlen(name) : 0;
      MonoMethodSignature *signature = mono_method_signature(candidate);
      if (length > 8 && std::strcmp(name + length - 8, ".Resolve") == 0 &&
          signature && mono_signature_get_param_count(signature) == 1)
        method = candidate;
    }
  }
  if (!method)
    return false;
  void *args[1] = {mono_string_new(mono_domain_get(), value.c_str())};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, promise, args, &exception);
  return exception == nullptr;
}

using RegistryGet = void (*)(MonoObject *, MonoObject *, MonoObject *);
RegistryGet s_registry_get_original = nullptr;

void registry_get_hook(MonoObject *module, MonoObject *param,
                       MonoObject *promise) {
  if (param && promise) {
    MonoObject *exception = nullptr;
    MonoString *shown = mono_object_to_string(param, &exception);
    const std::string asked = exception ? std::string() : Mono_to_String(shown);
    if (asked.find(kClockKey) != std::string::npos &&
        resolve(promise, ShellClock::ScriptConfig()))
      return;
    if (asked.find(WebTiles::kRegistryKey) != std::string::npos &&
        resolve(promise, WebTiles::HomeState()))
      return;
    if (asked.find(kRegistryKey) != std::string::npos) {
      unsigned revision = 0;
      {
        std::lock_guard<std::mutex> lock(s_mutex);
        load_locked();
        revision = s_revision;
      }
      if (resolve(promise, "{\"rev\":" + std::to_string(revision) + "}"))
        return;
      LOG_WARN("[HOMETABS] could not answer the script");
    }
  }
  if (s_registry_get_original)
    s_registry_get_original(module, param, promise);
}

enum class RequestKind { None, Menu, Create, Rename, Icon, Delete };

struct Request {
  RequestKind kind = RequestKind::None;
  int slot = -1;
  std::string title;
};

std::mutex s_request_mutex;
Request s_request;

enum class Step { Idle, Create, Rename, Icon };

Step s_step = Step::Idle;
uint64_t s_prompt_token = 0;
int s_step_slot = -1;
std::string s_pending_title;
bool s_pending_moved = false;

bool s_delete_open = false;
int s_delete_slot = -1;

bool s_menu_up = false;
std::string s_menu_title;

void open_prompt(Step step, int slot, const std::string &text) {
  SystemDialogs::PromptOptions options;
  if (step == Step::Create) {
    options.title = tr("home_tabs.new_tab_title");
    options.message = tr("home_tabs.tab_name_message");
    options.positive = tr("home_tabs.create");
  } else if (step == Step::Rename) {
    options.title = tr("home_tabs.rename_tab_title");
    options.message = tr("home_tabs.tab_name_message");
    options.positive = tr("home_tabs.save");
  } else {
    options.title = tr("home_tabs.icon_title");
    options.message = tr("home_tabs.icon_message");
    options.positive = tr("home_tabs.save");
  }
  options.text = text;
  options.negative = tr("home_tabs.cancel");
  std::string failure;
  if (!SystemDialogs::ShowPrompt(options, &failure, nullptr, &s_prompt_token)) {
    LOG_WARN("[HOMETABS] prompt: %s", failure.c_str());
    s_step = Step::Idle;
    s_step_slot = -1;
    s_pending_title.clear();
    s_pending_moved = false;
    return;
  }
  s_step = step;
  s_step_slot = slot;
}

void create_from_prompt(int slot, const std::string &typed) {
  if (slot < 0 || slot >= kMaxCustomTabs) {
    notify("%s", tr("home_tabs.tab_full"));
    return;
  }
  const std::string title = s_pending_title;
  const bool moved = s_pending_moved;
  s_pending_title.clear();
  s_pending_moved = false;
  std::string name = clean_name(typed);
  if (name.empty())
    name = default_tab_name(slot);
  std::string text;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (find_tab_const_locked(slot)) {
      notify("%s", tr("home_tabs.tab_full"));
      return;
    }
    Tab tab;
    tab.id = id_of_slot(slot);
    tab.name = name;
    tab.icon = kDefaultCustomIcon;
    tab.visible = true;
    s_tabs.push_back(tab);
    if (TitleInfo::Valid(title)) {
      Tab *created = find_tab_locked(slot);
      if (created) {
        if (moved) {
          erase(s_games, title);
          erase(s_media, title);
          for (Tab &other : s_tabs)
            if (other.id != created->id)
              erase(other.titles, title);
          if (!has(s_moved, title))
            s_moved.push_back(title);
        }
        created->titles.insert(created->titles.begin(), title);
      }
    }
    text = changed_locked();
  }
  save(text);
  LOG_INFO("[HOMETABS] created tab %d '%s'", slot + 1, name.c_str());
  if (TitleInfo::Valid(title)) {
    notify("%s", TitleInfo::WithName(
                     moved ? "home_tabs.moved" : "home_tabs.copied", name)
                     .c_str());
  }
  reload_home();
}

void finish_prompt(bool confirmed, const std::string &typed) {
  const Step step = s_step;
  const int slot = s_step_slot;
  s_step = Step::Idle;
  s_step_slot = -1;
  if (!confirmed) {
    s_pending_title.clear();
    s_pending_moved = false;
    return;
  }
  if (step == Step::Create) {
    create_from_prompt(slot, typed);
    return;
  }
  std::string text;
  if (step == Step::Rename) {
    std::string name = clean_name(typed);
    if (name.empty())
      name = default_tab_name(slot);
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      load_locked();
      Tab *tab = find_tab_locked(slot);
      if (!tab || tab->name == name)
        return;
      tab->name = name;
      text = changed_locked();
    }
    save(text);
    LOG_INFO("[HOMETABS] renamed tab %d", slot + 1);
    reload_home();
    return;
  }
  if (step == Step::Icon) {
    std::string glyph;
    const IconText result = ParseIconText(typed, &glyph);
    if (result == IconText::Invalid) {
      notify("%s", tr("home_tabs.icon_invalid"));
      return;
    }
    if (result == IconText::Empty)
      glyph = kDefaultCustomIcon;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      load_locked();
      Tab *tab = find_tab_locked(slot);
      if (!tab || tab->icon == glyph)
        return;
      tab->icon = glyph;
      text = changed_locked();
    }
    save(text);
    LOG_INFO("[HOMETABS] tab %d icon changed", slot + 1);
    reload_home();
  }
}

void destroy_tab(int slot) {
  std::vector<std::string> titles;
  std::string text;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    const auto at = std::find_if(s_tabs.begin(), s_tabs.end(),
                                 [slot](const Tab &tab) {
                                   return slot_of(tab.id) == slot;
                                 });
    if (at == s_tabs.end())
      return;
    titles = at->titles;
    s_tabs.erase(at);
    for (const std::string &title : titles)
      if (!tab_holds_locked(title) && !has(s_games, title) &&
          !has(s_media, title))
        erase(s_moved, title);
    text = changed_locked();
  }
  save(text);
  LOG_INFO("[HOMETABS] deleted tab %d", slot + 1);
  reload_home();
}

void place(const std::string &title, Target target, bool moved) {
  std::string text;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (target.stock) {
      std::vector<std::string> &list =
          target.row == StockRow::Games ? s_games : s_media;
      if (moved) {
        std::vector<std::string> &other =
            target.row == StockRow::Games ? s_media : s_games;
        erase(other, title);
        for (Tab &tab : s_tabs)
          erase(tab.titles, title);
        if (!has(s_moved, title))
          s_moved.push_back(title);
      }
      if (!has(list, title))
        list.push_back(title);
    } else {
      Tab *tab = find_tab_locked(target.slot);
      if (!tab)
        return;
      if (moved) {
        erase(s_games, title);
        erase(s_media, title);
        for (Tab &other : s_tabs)
          if (other.id != tab->id)
            erase(other.titles, title);
        if (!has(s_moved, title))
          s_moved.push_back(title);
      }
      if (!has(tab->titles, title))
        tab->titles.insert(tab->titles.begin(), title);
    }
    text = changed_locked();
  }
  save(text);
  const std::string name = target_name(target);
  LOG_INFO("[HOMETABS] %s %s to %s", title.c_str(),
           moved ? "moved" : "copied", name.c_str());
  notify("%s", TitleInfo::WithName(
                   moved ? "home_tabs.moved" : "home_tabs.copied", name)
                   .c_str());
}

void remove_title(const std::string &title, Target target) {
  bool removed = false;
  std::string text;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (target.stock) {
      removed = erase(target.row == StockRow::Games ? s_games : s_media, title);
    } else if (Tab *tab = find_tab_locked(target.slot)) {
      removed = erase(tab->titles, title);
    }
    if (!removed)
      return;
    if (!tab_holds_locked(title) && !has(s_games, title) &&
        !has(s_media, title))
      erase(s_moved, title);
    text = changed_locked();
  }
  save(text);
  const std::string name = target_name(target);
  LOG_INFO("[HOMETABS] %s removed from %s", title.c_str(), name.c_str());
  notify("%s", TitleInfo::WithName("home_tabs.removed", name).c_str());
}

std::string key_of(Target target) {
  if (target.stock)
    return target.row == StockRow::Games ? "games" : "media";
  return "tab:" + id_of_slot(target.slot);
}

Target target_of_key(const std::string &key) {
  Target target;
  if (key == "games") {
    target.stock = true;
    target.row = StockRow::Games;
  } else if (key == "media") {
    target.stock = true;
    target.row = StockRow::Media;
  } else if (key.compare(0, 4, "tab:") == 0) {
    target.slot = slot_of(key.substr(4));
  }
  return target;
}

std::vector<SystemDialogs::MenuEntry> menu_rows(const std::string &title) {
  std::vector<std::pair<Target, std::string>> targets;
  std::vector<bool> present;
  bool free_slot = false;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    Target games;
    games.stock = true;
    games.row = StockRow::Games;
    targets.emplace_back(games, HomeTabs::TabName(HomeTabs::StockTab::Games));
    present.push_back(has(s_games, title));
    Target media;
    media.stock = true;
    media.row = StockRow::Media;
    targets.emplace_back(media, HomeTabs::TabName(HomeTabs::StockTab::Media));
    present.push_back(has(s_media, title));
    for (const Tab &tab : s_tabs) {
      if (!tab.visible)
        continue;
      Target target;
      target.slot = slot_of(tab.id);
      targets.emplace_back(target, tab.name);
      present.push_back(has(tab.titles, title));
    }
    free_slot = static_cast<int>(s_tabs.size()) < kMaxCustomTabs;
  }
  std::vector<SystemDialogs::MenuEntry> rows;
  for (int action = 0; action < 3; ++action) {
    SystemDialogs::MenuEntry row{
        tr(action == 0 ? "home_tabs.menu_copy_tab"
                       : action == 1 ? "home_tabs.menu_move_tab"
                                     : "home_tabs.menu_remove_tab"),
        ""};
    for (size_t i = 0; i < targets.size(); ++i) {
      if (action == 2 ? !present[i] : present[i])
        continue;
      SystemDialogs::MenuEntry tab{targets[i].second, ""};
      tab.id = std::string(action == 0 ? "copy" : action == 1 ? "move"
                                                              : "remove") +
               ":" + key_of(targets[i].first);
      row.children.push_back(tab);
    }
    if (action < 2 && free_slot) {
      SystemDialogs::MenuEntry tab{tr("home_tabs.new_tab_menu"), ""};
      tab.id = std::string(action == 0 ? "copy" : "move") + ":new";
      row.children.push_back(tab);
    }
    if (row.children.empty())
      continue;
    row.children.front().header = row.title;
    rows.push_back(row);
  }
  return rows;
}

void open_menu(const std::string &title) {
  const std::vector<SystemDialogs::MenuEntry> rows = menu_rows(title);
  std::string failure;
  SystemDialogs::ClosePopupMenu();
  s_menu_up = !rows.empty() &&
              SystemDialogs::ShowPopupMenu(rows, tr("home_tabs.menu_tabs"),
                                           &failure, nullptr, true, 0);
  if (!s_menu_up) {
    LOG_WARN("[HOMETABS] menu: %s", failure.c_str());
    return;
  }
  s_menu_title = title;
  LOG_INFO("[HOMETABS] menu for %s", title.c_str());
}

void run_menu_choice(const std::string &id) {
  const size_t colon = id.find(':');
  if (colon == std::string::npos)
    return;
  const std::string action = id.substr(0, colon);
  const std::string key = id.substr(colon + 1);
  if (key == "new") {
    if (action == "remove")
      return;
    int slot = FirstFreeSlot();
    if (slot < 0) {
      notify("%s", tr("home_tabs.tab_full"));
      return;
    }
    s_pending_title = s_menu_title;
    s_pending_moved = action == "move";
    open_prompt(Step::Create, slot, default_tab_name(slot));
    return;
  }
  const Target target = target_of_key(key);
  if (!target.stock && target.slot < 0)
    return;
  LOG_INFO("[HOMETABS] menu: %s %s for %s", action.c_str(), key.c_str(),
           s_menu_title.c_str());
  if (action == "remove")
    remove_title(s_menu_title, target);
  else if (action == "copy" || action == "move")
    place(s_menu_title, target, action == "move");
}

void menu_tick() {
  std::string id;
  if (SystemDialogs::PollPopupSubmenu(&id)) {
    SystemDialogs::ClosePopupMenu();
    s_menu_up = false;
    run_menu_choice(id);
    return;
  }
  int row = -1;
  (void)SystemDialogs::PollPopupMenu(&row);
  if (SystemDialogs::PopupMenuDismissed())
    s_menu_up = false;
}

}

IconText ParseIconText(const std::string &text, std::string *glyph) {
  const std::string trimmed = trim_spaces(text);
  if (trimmed.empty())
    return IconText::Empty;
  uint32_t code = 0;
  if (!decode_codepoint(trimmed, &code))
    return IconText::Invalid;
  if (code < 0xe000 || code > 0xf8ff || !OskGlyphs::HasGlyph(code))
    return IconText::Invalid;
  if (glyph)
    *glyph = utf8_of(code);
  return IconText::Valid;
}

std::string SanitizeIcon(const std::string &icon, const char *fallback) {
  std::string glyph;
  return ParseIconText(icon, &glyph) == IconText::Valid
             ? glyph
             : std::string(fallback ? fallback : kDefaultCustomIcon);
}

std::string CleanName(const std::string &typed) { return clean_name(typed); }

bool IsTabName(const std::string &text) {
  if (text.empty())
    return false;
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  for (const Tab &tab : s_tabs)
    if (tab.name == text)
      return true;
  return false;
}

void Install() {
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
  }
  if (s_registry_get_original)
    return;
  MonoImage *image = getDLLimage("ReactNative.Modules.Vsh.dll");
  MonoClass *klass =
      image ? mono_class_from_name(image, "ReactNative.Modules.Vsh",
                                   "RegistryModule")
            : nullptr;
  const uint64_t address =
      klass ? Get_Address_of_Method(image, klass, "Get", 2) : 0;
  const bool hooked =
      address &&
      InstallDetour(address, reinterpret_cast<void *>(&registry_get_hook),
                    reinterpret_cast<void **>(&s_registry_get_original));
  LOG_INFO("[HOMETABS] registry hook target=0x%lx installed=%d",
           static_cast<unsigned long>(address), hooked ? 1 : 0);
  if (!hooked)
    s_registry_get_original = nullptr;
}

bool Enabled() { return s_registry_get_original != nullptr; }

bool Allowed(const std::string &title_id) {
  return Enabled() && TitleInfo::Usable(title_id);
}

std::vector<TabView> Tabs(bool visible_only) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  std::vector<TabView> out;
  for (const Tab &tab : s_tabs) {
    if (visible_only && !tab.visible)
      continue;
    const int slot = slot_of(tab.id);
    if (slot < 0)
      continue;
    TabView view;
    view.slot = slot;
    view.limit = limit_of_slot(slot);
    view.space = "aniketos-" + tab.id;
    view.id = tab.id;
    view.name = tab.name;
    view.icon = tab.icon;
    view.visible = tab.visible;
    out.push_back(view);
  }
  return out;
}

std::string TitlesSql(int limit) {
  const int slot = slot_of_limit(limit);
  if (slot < 0)
    return {};
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  const Tab *tab = find_tab_const_locked(slot);
  return tab ? TitleInfo::SqlList(tab->titles) : std::string();
}

bool SlotUsed(int slot) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return find_tab_const_locked(slot) != nullptr;
}

std::string SlotName(int slot) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  const Tab *tab = find_tab_const_locked(slot);
  return tab ? tab->name : std::string();
}

std::string SlotIcon(int slot) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  const Tab *tab = find_tab_const_locked(slot);
  return tab ? tab->icon : std::string();
}

int SlotPosition(int slot) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  int position = 3;
  for (const Tab &tab : s_tabs) {
    if (slot_of(tab.id) == slot)
      return position;
    ++position;
  }
  return 0;
}

bool SlotVisible(int slot) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  const Tab *tab = find_tab_const_locked(slot);
  return tab && tab->visible;
}

bool RestoreIcons() {
  bool changed = false;
  std::string text;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    for (Tab &tab : s_tabs) {
      if (tab.icon != kDefaultCustomIcon) {
        tab.icon = kDefaultCustomIcon;
        changed = true;
      }
    }
    if (changed)
      text = changed_locked();
  }
  if (!changed)
    return false;
  save(text);
  return true;
}

int FirstFreeSlot() {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  for (int slot = 0; slot < kMaxCustomTabs; ++slot)
    if (!find_tab_const_locked(slot))
      return slot;
  return -1;
}

std::string AddedSql(StockRow row) {
  if (!s_script_in)
    return {};
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return TitleInfo::SqlList(row == StockRow::Games ? s_games : s_media);
}

std::string HiddenSql(StockRow row) {
  if (!s_script_in)
    return {};
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  const std::vector<std::string> &list =
      row == StockRow::Games ? s_games : s_media;
  const std::vector<std::string> &other =
      row == StockRow::Games ? s_media : s_games;
  std::vector<std::string> hidden;
  for (const std::string &title : s_moved)
    if (!has(list, title) &&
        (has(other, title) || visible_holds_locked(title)))
      hidden.push_back(title);
  return TitleInfo::SqlList(hidden);
}

void SetHome(bool script_in) { s_script_in = script_in; }

bool ConsumeUiChange() { return s_ui_changed.exchange(false); }

bool HandleUri(const std::string &uri, const std::string &menu_title) {
  if (uri.compare(0, std::strlen(kMenuUri), kMenuUri) != 0)
    return false;
  if (!TitleInfo::Valid(menu_title)) {
    LOG_WARN("[HOMETABS] no title for '%s' ('%s')", uri.c_str(),
             menu_title.c_str());
    return true;
  }
  Request request;
  request.kind = RequestKind::Menu;
  request.title = menu_title;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
  return true;
}

void RequestCreate(int slot) {
  Request request;
  request.kind = RequestKind::Create;
  request.slot = slot;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
}

void RequestRename(int slot) {
  Request request;
  request.kind = RequestKind::Rename;
  request.slot = slot;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
}

void RequestIcon(int slot) {
  Request request;
  request.kind = RequestKind::Icon;
  request.slot = slot;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
}

void RequestDelete(int slot) {
  Request request;
  request.kind = RequestKind::Delete;
  request.slot = slot;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
}

void ToggleVisible(int slot) {
  bool shown = false;
  std::string text;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    Tab *tab = find_tab_locked(slot);
    if (!tab)
      return;
    tab->visible = !tab->visible;
    shown = tab->visible;
    text = changed_locked();
  }
  save(text);
  LOG_INFO("[HOMETABS] tab %d %s", slot + 1, shown ? "shown" : "hidden");
  reload_home();
}

void MoveSlot(int slot, int direction) {
  bool moved = false;
  std::string text;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    const auto at = std::find_if(s_tabs.begin(), s_tabs.end(),
                                 [slot](const Tab &tab) {
                                   return slot_of(tab.id) == slot;
                                 });
    if (at == s_tabs.end())
      return;
    const long index = static_cast<long>(at - s_tabs.begin());
    const long next = index + (direction < 0 ? -1 : 1);
    if (next < 0 || next >= static_cast<long>(s_tabs.size()))
      return;
    std::swap(s_tabs[static_cast<size_t>(index)],
              s_tabs[static_cast<size_t>(next)]);
    moved = true;
    text = changed_locked();
  }
  if (!moved)
    return;
  save(text);
  LOG_INFO("[HOMETABS] tab %d moved %s", slot + 1,
           direction < 0 ? "left" : "right");
  reload_home();
}

void Tick() {
  Request request;
  {
    std::lock_guard<std::mutex> lock(s_request_mutex);
    std::swap(request, s_request);
  }
  if (s_step != Step::Idle) {
    bool confirmed = false;
    std::string typed;
    if (SystemDialogs::PollPrompt(s_prompt_token, &confirmed, &typed)) {
      finish_prompt(confirmed, typed);
    } else if (request.kind != RequestKind::None) {
      LOG_INFO("[HOMETABS] the last prompt never answered; dropped");
      s_step = Step::Idle;
      s_step_slot = -1;
      s_pending_title.clear();
      s_pending_moved = false;
    } else {
      return;
    }
  }
  if (s_delete_open) {
    int result = -1;
    if (SystemDialogs::PollAlert(&result)) {
      s_delete_open = false;
      const int slot = s_delete_slot;
      s_delete_slot = -1;
      if (result == 0)
        destroy_tab(slot);
    }
    return;
  }
  if (s_menu_up)
    menu_tick();
  switch (request.kind) {
  case RequestKind::Menu:
    open_menu(request.title);
    break;
  case RequestKind::Create:
    if (SlotUsed(request.slot)) {
      notify("%s", tr("home_tabs.tab_full"));
      break;
    }
    if (FirstFreeSlot() < 0) {
      notify("%s", tr("home_tabs.tab_full"));
      break;
    }
    open_prompt(Step::Create, request.slot, default_tab_name(request.slot));
    break;
  case RequestKind::Rename:
    open_prompt(Step::Rename, request.slot, SlotName(request.slot));
    break;
  case RequestKind::Icon:
    open_prompt(Step::Icon, request.slot, SlotIcon(request.slot));
    break;
  case RequestKind::Delete: {
    const std::string name = SlotName(request.slot);
    if (name.empty())
      break;
    SystemDialogs::AlertOptions options;
    options.title = tr("home_tabs.delete_title");
    options.message =
        toolbox_i18n::format("home_tabs.delete_message", name.c_str());
    options.positive = tr("home_tabs.delete_yes");
    options.negative = tr("home_tabs.delete_no");
    std::string failure;
    if (!SystemDialogs::ShowAlert(options, &failure)) {
      LOG_WARN("[HOMETABS] delete alert: %s", failure.c_str());
      break;
    }
    s_delete_open = true;
    s_delete_slot = request.slot;
    break;
  }
  case RequestKind::None:
    break;
  }
}

}
