#include "aniketos_shell/home/home_bookmark.hpp"

#include "aniketos_shell/home/home_tabs.hpp"
#include "aniketos_shell/home/title_info.hpp"
#include "aniketos_shell/home/web_tiles.hpp"
#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_ui/small_file.hpp"
#include "hooked_funcs.hpp"
#include "onion_cjson.hpp"
#include "aniketos_theme/clock/shell_clock.hpp"
#include "toolbox_i18n.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern onion::Settings g_settings;

namespace HomeBookmark {
namespace {

namespace TitleInfo = AniketosShell::TitleInfo;
using toolbox_i18n::tr;

constexpr const char *kFile = ONION_DIST_USER_DATA_ROOT "/home_bookmark.json";
/* What the script asks Home's registry module for. */
constexpr const char *kRegistryKey = "aniketos.home_bookmark";
/* ... and for the custom clock's settings (ShellClock::ScriptConfig). */
constexpr const char *kClockKey = "aniketos.clock";
/* Bytes of a name, cut at a character boundary: a tab's caption. */
constexpr size_t kMaxNameBytes = 40;

std::mutex s_mutex; /* everything down to s_revision */
bool s_loaded = false;
std::string s_name; /* empty while there is no bookmark */
std::vector<std::string> s_titles; /* the bookmark's */
/* Titles put in Home's Games and Media rows by us. */
std::vector<std::string> s_games;
std::vector<std::string> s_media;
/* Titles moved rather than copied: out of the stock rows they are not in
   by us, while they show somewhere else in Home (hidden_locked). */
std::vector<std::string> s_moved;
/* Changes whenever the script has to fetch the rows again. */
unsigned s_revision = 1;
/* The Home that is up has the script, and the tab (set by script_injector). */
std::atomic<bool> s_script_in{false};
std::atomic<bool> s_tab_shown{false};

bool has(const std::vector<std::string> &list, const std::string &title) {
  return std::find(list.begin(), list.end(), title) != list.end();
}

/* True when it was there. */
bool erase(std::vector<std::string> &list, const std::string &title) {
  const auto at = std::find(list.begin(), list.end(), title);
  if (at == list.end())
    return false;
  list.erase(at);
  return true;
}

std::vector<std::string> &list_locked(Target target) {
  switch (target) {
  case Target::Games: return s_games;
  case Target::Media: return s_media;
  case Target::Bookmark: break;
  }
  return s_titles;
}

/* A moved title stays out of a stock row that we did not put it in, but
   only while it shows somewhere else in this Home, so that it never goes
   missing: in the other row by us, or in the bookmark while the tab is in
   Home. */
bool hidden_locked(Target row, const std::string &title) {
  if (!has(s_moved, title) || has(list_locked(row), title))
    return false;
  const Target other = row == Target::Games ? Target::Media : Target::Games;
  return has(list_locked(other), title) ||
         (s_tab_shown && has(s_titles, title));
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

void load_locked() {
  if (s_loaded)
    return;
  s_loaded = true;
  std::string text;
  if (!AniketosUI::ReadSmallFile(kFile, 64 * 1024, text))
    return;
  onion_cjson::Root root(text);
  if (!root)
    return;
  const char *name = onion_cjson::string_item(root.get(), "name", "");
  read_list(root.get(), "titles", s_titles);
  read_list(root.get(), "games", s_games);
  read_list(root.get(), "media", s_media);
  read_list(root.get(), "moved", s_moved);
  s_name = s_titles.empty() ? std::string() : CleanName(name);
  if (s_name.empty())
    s_titles.clear();
  /* moved from where it was to one of ours */
  s_moved.erase(std::remove_if(s_moved.begin(), s_moved.end(),
                               [](const std::string &title) {
                                 return !has(s_titles, title) &&
                                        !has(s_games, title) &&
                                        !has(s_media, title);
                               }),
                s_moved.end());
  LOG_INFO("[BOOKMARK] loaded %zu titles, %zu in Games, %zu in Media, "
           "%zu moved",
           s_titles.size(), s_games.size(), s_media.size(), s_moved.size());
}

void save_locked() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", s_name.c_str());
  write_list(root, "titles", s_titles);
  write_list(root, "games", s_games);
  write_list(root, "media", s_media);
  write_list(root, "moved", s_moved);
  const std::string text = onion_cjson::print_owned(root, true);
  if (text.empty() || !write_file_atomic(kFile, text.data(), text.size()))
    LOG_WARN("[BOOKMARK] could not save %s", kFile);
}

/* Saved, and the script told to fetch the rows again. */
void changed_locked() {
  ++s_revision;
  save_locked();
}

/* "games", "media", "bookmark": a target in a menu row's id and the log. */
const char *key_of(Target target) {
  switch (target) {
  case Target::Games: return "games";
  case Target::Media: return "media";
  case Target::Bookmark: break;
  }
  return "bookmark";
}

/* As the tab shows it now (any thread): the user's name for Games or Media,
   else the system's, or the bookmark's name, empty while there is none. */
std::string target_name(Target target) {
  switch (target) {
  case Target::Games: return HomeTabs::TabName(HomeTabs::StockTab::Games);
  case Target::Media: return HomeTabs::TabName(HomeTabs::StockTab::Media);
  case Target::Bookmark: break;
  }
  return Name();
}

/* The tab's caption comes in with Home's start: Home is started again when
   the tab comes, goes or is renamed. */
void reload_home() {
  if (g_settings.home_bookmark)
    shellui_request_home_reload();
}

/* IPromise.Resolve(object) of the promise the module was handed. */
bool resolve(MonoObject *promise, const std::string &value) {
  MonoMethod *method = nullptr;
  for (MonoClass *k = mono_object_get_class(promise); k && !method;
       k = mono_class_get_parent(k)) {
    method = mono_class_get_method_from_name(k, "Resolve", 1);
    void *iter = nullptr;
    while (!method) {
      /* an explicit implementation is named after its interface */
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

/* RegistryModule.Get(JObject param, IPromise promise): Home's reads of the
   system registry. The one asking for our key gets the revision instead. */
void registry_get_hook(MonoObject *module, MonoObject *param,
                       MonoObject *promise) {
  if (param && promise) {
    MonoObject *exception = nullptr;
    MonoString *shown = mono_object_to_string(param, &exception);
    const std::string asked = exception ? std::string() : Mono_to_String(shown);
    /* Home's script asks for the custom clock's settings now and then, so a
       change in the Shell Menu shows without reloading Home */
    if (asked.find(kClockKey) != std::string::npos &&
        resolve(promise, ShellClock::ScriptConfig()))
      return;
    /* ... and for the websites kept on it */
    if (asked.find(WebTiles::kRegistryKey) != std::string::npos &&
        resolve(promise, WebTiles::HomeState()))
      return;
    if (Enabled() && asked.find(kRegistryKey) != std::string::npos) {
      unsigned revision = 0;
      {
        std::lock_guard<std::mutex> lock(s_mutex);
        revision = s_revision;
      }
      if (resolve(promise, "{\"rev\":" + std::to_string(revision) + "}"))
        return;
      LOG_WARN("[BOOKMARK] could not answer the script");
    }
  }
  if (s_registry_get_original)
    s_registry_get_original(module, param, promise);
}

struct Request {
  enum class Kind { None, Menu, Rename } kind = Kind::None;
  std::string title;
};
std::mutex s_request_mutex;
Request s_request;

/* The prompt on screen (UI thread only). */
enum class Step { Idle, Name, Rename };
Step s_step = Step::Idle;
std::string s_pending; /* the game the new bookmark is made for */
bool s_pending_moved = false; /* ...and whether it is moved there */

void open_prompt(Step step, const std::string &text) {
  SystemDialogs::PromptOptions options;
  options.title = step == Step::Rename ? tr("bookmark.rename_title")
                                       : tr("bookmark.new_title");
  options.message = tr("bookmark.name_message");
  options.text = text;
  options.positive =
      step == Step::Rename ? tr("bookmark.save") : tr("bookmark.create");
  options.negative = tr("bookmark.cancel");
  std::string failure;
  if (!SystemDialogs::ShowPrompt(options, &failure)) {
    LOG_WARN("[BOOKMARK] prompt: %s", failure.c_str());
    s_step = Step::Idle;
    return;
  }
  s_step = step;
}

/* A title in no list of ours any more is not moved either. */
void unmove_if_free_locked(const std::string &title) {
  if (!has(s_titles, title) && !has(s_games, title) && !has(s_media, title))
    erase(s_moved, title);
}

/* A copy shows where it was and in `target`; a moved title shows in
   `target` alone, as far as our lists go (hidden_locked does the rest). */
void place(const std::string &title, Target target, bool moved) {
  bool emptied = false;
  bool made = false;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (target == Target::Bookmark && s_name.empty()) {
      /* no bookmark yet: it is made, with its name, around this game */
      s_pending = title;
      s_pending_moved = moved;
      made = true;
    } else {
      if (moved) {
        for (const Target other : kTargets)
          if (other != target)
            erase(list_locked(other), title);
        if (!has(s_moved, title))
          s_moved.push_back(title);
        /* the bookmark's last game moved elsewhere takes the tab with it */
        if (s_titles.empty() && !s_name.empty()) {
          s_name.clear();
          emptied = true;
        }
      }
      std::vector<std::string> &list = list_locked(target);
      if (!has(list, title)) {
        if (target == Target::Bookmark)
          list.insert(list.begin(), title);
        else
          list.push_back(title);
      }
      changed_locked();
    }
  }
  if (made) {
    open_prompt(Step::Name, tr("bookmark.default_name"));
    return;
  }
  const std::string name = target_name(target);
  LOG_INFO("[BOOKMARK] %s %s to %s", title.c_str(),
           moved ? "moved" : "copied", key_of(target));
  notify("%s", TitleInfo::WithName(moved ? "bookmark.moved" : "bookmark.copied",
                                   name)
                   .c_str());
  if (emptied)
    reload_home();
}

void remove_title(const std::string &title, Target target) {
  const std::string name = target_name(target);
  bool emptied = false;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (!erase(list_locked(target), title))
      return;
    /* a moved title in no list of ours goes back to its own row */
    unmove_if_free_locked(title);
    /* the last game takes the bookmark with it */
    if (target == Target::Bookmark && s_titles.empty()) {
      s_name.clear();
      emptied = true;
    }
    changed_locked();
  }
  LOG_INFO("[BOOKMARK] %s removed from %s%s", title.c_str(), key_of(target),
           emptied ? ", bookmark gone" : "");
  notify("%s", TitleInfo::WithName("bookmark.removed", name).c_str());
  if (emptied)
    reload_home();
}

void finish_prompt(bool confirmed, const std::string &typed) {
  const Step step = s_step;
  s_step = Step::Idle;
  const std::string title = s_pending;
  const bool moved = s_pending_moved;
  s_pending.clear();
  s_pending_moved = false;
  if (!confirmed)
    return;
  std::string name = CleanName(typed);
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (step == Step::Rename) {
      if (name.empty() || s_name.empty() || name == s_name)
        return;
      s_name = name;
      changed_locked();
    } else {
      if (!TitleInfo::Valid(title))
        return;
      if (name.empty())
        name = tr("bookmark.default_name");
      s_name = name;
      s_titles = {title};
      if (moved) {
        erase(s_games, title);
        erase(s_media, title);
        if (!has(s_moved, title))
          s_moved.push_back(title);
      }
      changed_locked();
    }
  }
  LOG_INFO("[BOOKMARK] %s",
           step == Step::Rename ? "renamed" : moved ? "made, moved" : "made");
  if (step != Step::Rename)
    notify("%s",
           TitleInfo::WithName(moved ? "bookmark.moved" : "bookmark.copied", name)
               .c_str());
  reload_home();
}

bool s_menu_up = false; /* UI thread, like the rest of the menu */
std::string s_menu_title;

constexpr const char *kActionKeys[] = {"copy", "move", "remove"};
constexpr const char *kActionLabels[] = {"bookmark.menu_copy_tab",
                                         "bookmark.menu_move_tab",
                                         "bookmark.menu_remove_tab"};

/* Copy to, Move to and Remove from, each opening the tabs it can go to:
   copied or moved to where it is in no list of ours (the system's own row
   for it may be one of them: which one is not known here), removed from
   where it is. A row with nowhere to go is left out. */
std::vector<SystemDialogs::MenuEntry> menu_rows(const std::string &title) {
  bool added[3] = {false, false, false};
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    for (const Target target : kTargets)
      added[static_cast<int>(target)] = has(list_locked(target), title);
  }
  std::vector<SystemDialogs::MenuEntry> rows;
  for (int action = 0; action < 3; ++action) {
    SystemDialogs::MenuEntry row{tr(kActionLabels[action]), ""};
    for (const Target target : kTargets) {
      const bool in = added[static_cast<int>(target)];
      if (action == 2 ? !in : in)
        continue;
      std::string name = target_name(target);
      if (name.empty()) /* no bookmark yet: this makes it */
        name = tr("bookmark.new_title");
      SystemDialogs::MenuEntry tab{name, ""};
      tab.id = std::string(kActionKeys[action]) + ":" + key_of(target);
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
              SystemDialogs::ShowPopupMenu(rows, tr("bookmark.menu_tabs"),
                                           &failure, nullptr,
                                           /*stay_open=*/true, 0);
  if (!s_menu_up) {
    LOG_WARN("[BOOKMARK] menu: %s", failure.c_str());
    return;
  }
  s_menu_title = title;
  LOG_INFO("[BOOKMARK] menu for %s", title.c_str());
}

/* A tab chosen in one of the submenus, by its row's id ("copy:games"). */
void run_menu_choice(const std::string &id) {
  const size_t colon = id.find(':');
  if (colon == std::string::npos)
    return;
  const std::string action = id.substr(0, colon);
  const std::string key = id.substr(colon + 1);
  for (const Target target : kTargets) {
    if (key != key_of(target))
      continue;
    LOG_INFO("[BOOKMARK] menu: %s %s for %s", action.c_str(), key.c_str(),
             s_menu_title.c_str());
    if (action == "remove")
      remove_title(s_menu_title, target);
    else if (action == "copy" || action == "move")
      place(s_menu_title, target, action == "move");
    return;
  }
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
  (void)SystemDialogs::PollPopupMenu(&row); /* a submenu row: PUI opens it */
  if (SystemDialogs::PopupMenuDismissed())
    s_menu_up = false;
}

} // namespace

void Install() {
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
  LOG_INFO("[BOOKMARK] registry hook target=0x%lx installed=%d",
           static_cast<unsigned long>(address), hooked ? 1 : 0);
  if (!hooked)
    s_registry_get_original = nullptr;
}

bool Enabled() {
  return g_settings.home_bookmark && s_registry_get_original != nullptr;
}

bool Allowed(const std::string &title_id) {
  return Enabled() && TitleInfo::Usable(title_id);
}

std::string Name() {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return s_name;
}

std::string CleanName(const std::string &typed) {
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

bool Shown() {
  if (!Enabled())
    return false;
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return !s_name.empty() && !s_titles.empty();
}

std::string TitlesSql() {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return TitleInfo::SqlList(s_titles);
}

void SetHome(bool script_in, bool tab_shown) {
  s_script_in = script_in;
  s_tab_shown = tab_shown;
}

std::string AddedSql(Target row) {
  if (row == Target::Bookmark || !s_script_in || !Enabled())
    return {};
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return TitleInfo::SqlList(list_locked(row));
}

std::string HiddenSql(Target row) {
  if (row == Target::Bookmark || !s_script_in || !Enabled())
    return {};
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  std::vector<std::string> hidden;
  for (const std::string &title : s_moved)
    if (hidden_locked(row, title))
      hidden.push_back(title);
  return TitleInfo::SqlList(hidden);
}

bool HandleUri(const std::string &uri, const std::string &menu_title) {
  if (uri.compare(0, std::strlen(kMenuUri), kMenuUri) != 0)
    return false;
  if (!TitleInfo::Valid(menu_title)) {
    LOG_WARN("[BOOKMARK] no title for '%s' ('%s')", uri.c_str(),
             menu_title.c_str());
    return true;
  }
  Request request;
  request.kind = Request::Kind::Menu;
  request.title = menu_title;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
  return true;
}

void RequestRename() {
  Request request;
  request.kind = Request::Kind::Rename;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
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
    if (SystemDialogs::PollPrompt(&confirmed, &typed)) {
      finish_prompt(confirmed, typed);
    } else if (request.kind != Request::Kind::None) {
      /* A prompt can go without an answer (closed by the system, as when a
         game starts over Home): a new request replaces it. */
      LOG_INFO("[BOOKMARK] the last prompt never answered; dropped");
      s_step = Step::Idle;
      s_pending.clear();
      s_pending_moved = false;
    } else {
      return;
    }
  }
  if (s_menu_up)
    menu_tick();
  switch (request.kind) {
  case Request::Kind::Menu:
    open_menu(request.title);
    break;
  case Request::Kind::Rename: {
    const std::string name = Name();
    if (name.empty())
      notify("%s", tr("bookmark.none"));
    else
      open_prompt(Step::Rename, name);
    break;
  }
  case Request::Kind::None:
    break;
  }
}

} // namespace HomeBookmark
