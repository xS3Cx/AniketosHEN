#include "aniketos_shell/home_folders.hpp"

#include "aniketos_shell/home_menu.hpp"
#include "aniketos_shell/system_dialogs.hpp"
#include "hooked_funcs.hpp"
#include "onion_cjson.hpp"
#include "toolbox_i18n.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

extern onion::Settings g_settings;

namespace HomeFolders {
namespace {

using toolbox_i18n::tr;

constexpr const char *kFile = ONION_DIST_USER_DATA_ROOT "/home_folders.json";
/* What the script asks Home's registry module for. */
constexpr const char *kRegistryKey = "aniketos.home_folders";
/* A folder's tile stands in the row as a title of its own: "AKFLD" and the
   folder's number, nine characters like a real title id. */
constexpr const char *kTilePrefix = "AKFLD";
constexpr int kMaxFolderId = 9999;
/* Bytes of a name, cut at a character boundary. */
constexpr size_t kMaxNameBytes = 60;

struct Folder {
  int id = 0;
  std::string name;
  std::vector<std::string> titles;
};

std::mutex s_mutex; /* everything down to s_revision */
bool s_loaded = false;
std::vector<Folder> s_folders;
std::vector<std::string> s_favorites;
int s_next_id = 1;
/* Changes whenever the script has to draw the row again. */
unsigned s_revision = 1;

/* ---- the folders -------------------------------------------------------- */

bool valid_title(const std::string &id) {
  if (id.size() != 9)
    return false;
  for (const char c : id)
    if (!std::isalnum(static_cast<unsigned char>(c)))
      return false;
  return true;
}

Folder *find_locked(int id) {
  for (Folder &folder : s_folders)
    if (folder.id == id)
      return &folder;
  return nullptr;
}

Folder *holder_locked(const std::string &title) {
  for (Folder &folder : s_folders)
    if (std::find(folder.titles.begin(), folder.titles.end(), title) !=
        folder.titles.end())
      return &folder;
  return nullptr;
}

/* A name as typed: control characters out, spaces trimmed, cut short at a
   character boundary. */
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

void load_locked() {
  if (s_loaded)
    return;
  s_loaded = true;
  FILE *file = std::fopen(kFile, "rb");
  if (!file)
    return;
  std::string text(256 * 1024, '\0');
  text.resize(std::fread(text.data(), 1, text.size(), file));
  std::fclose(file);
  onion_cjson::Root root(text);
  const cJSON *folders =
      root ? cJSON_GetObjectItemCaseSensitive(root.get(), "folders") : nullptr;
  const cJSON *folder = nullptr;
  cJSON_ArrayForEach(folder, folders) {
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(folder, "id");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(folder, "name");
    if (!cJSON_IsNumber(id) || id->valueint <= 0 ||
        id->valueint > kMaxFolderId || find_locked(id->valueint) ||
        !cJSON_IsString(name))
      continue;
    Folder entry;
    entry.id = id->valueint;
    entry.name = clean_name(name->valuestring);
    const cJSON *title = nullptr;
    cJSON_ArrayForEach(title,
                       cJSON_GetObjectItemCaseSensitive(folder, "titles")) {
      /* a title is in one folder only */
      if (cJSON_IsString(title) && valid_title(title->valuestring) &&
          !holder_locked(title->valuestring) &&
          std::find(entry.titles.begin(), entry.titles.end(),
                    title->valuestring) == entry.titles.end())
        entry.titles.push_back(title->valuestring);
    }
    s_next_id = std::max(s_next_id, entry.id + 1);
    s_folders.push_back(std::move(entry));
  }
  const cJSON *favorites =
      root ? cJSON_GetObjectItemCaseSensitive(root.get(), "favorites")
           : nullptr;
  const cJSON *favorite = nullptr;
  cJSON_ArrayForEach(favorite, favorites) {
    if (cJSON_IsString(favorite) && valid_title(favorite->valuestring) &&
        std::find(s_favorites.begin(), s_favorites.end(),
                  favorite->valuestring) == s_favorites.end())
      s_favorites.push_back(favorite->valuestring);
  }
  LOG_INFO("[FOLDERS] loaded %zu folders, %zu favourites", s_folders.size(),
           s_favorites.size());
}

void save_locked() {
  cJSON *root = cJSON_CreateObject();
  cJSON *folders = cJSON_AddArrayToObject(root, "folders");
  for (const Folder &folder : s_folders) {
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "id", folder.id);
    cJSON_AddStringToObject(entry, "name", folder.name.c_str());
    cJSON *titles = cJSON_AddArrayToObject(entry, "titles");
    for (const std::string &title : folder.titles)
      cJSON_AddItemToArray(titles, cJSON_CreateString(title.c_str()));
    cJSON_AddItemToArray(folders, entry);
  }
  cJSON *favorites = cJSON_AddArrayToObject(root, "favorites");
  for (const std::string &title : s_favorites)
    cJSON_AddItemToArray(favorites, cJSON_CreateString(title.c_str()));
  const std::string text = onion_cjson::print_owned(root, true);
  const std::string temporary = std::string(kFile) + ".tmp";
  FILE *file = text.empty() ? nullptr : std::fopen(temporary.c_str(), "wb");
  bool written = false;
  if (file) {
    written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    written = std::fclose(file) == 0 && written;
  }
  if (!written || std::rename(temporary.c_str(), kFile) != 0) {
    unlink(temporary.c_str());
    LOG_WARN("[FOLDERS] could not save %s", kFile);
  }
}

/* Saved, and the script told to draw the row again. */
void changed_locked() {
  ++s_revision;
  save_locked();
}

/* A name not taken yet: "New Folder", "New Folder 2"... */
std::string free_name_locked() {
  const std::string base = tr("folders.default_name");
  for (int n = 1;; ++n) {
    const std::string name = n == 1 ? base : base + " " + std::to_string(n);
    bool taken = false;
    for (const Folder &folder : s_folders)
      taken = taken || folder.name == name;
    if (!taken)
      return name;
  }
}

/* "'ID','ID'" for an SQL list; the ids are letters and digits only. */
std::string sql_list(const std::vector<std::string> &titles) {
  std::string out;
  for (const std::string &title : titles) {
    if (!valid_title(title))
      continue;
    if (!out.empty())
      out += ",";
    out += "'" + title + "'";
  }
  return out.empty() ? "''" : out;
}

std::string tile_of(int id) {
  char tile[16];
  std::snprintf(tile, sizeof(tile), "%s%04d", kTilePrefix, id);
  return tile;
}

/* What the script reads: the folders and the picture of a folder's tile. */
std::string state_json_locked() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "rev", s_revision);
  cJSON_AddStringToObject(root, "icon", HomeFolderIconPath());
  cJSON *folders = cJSON_AddArrayToObject(root, "folders");
  for (const Folder &folder : s_folders) {
    if (!Enabled())
      break; /* the favourites alone: the revision is what counts */
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "id", folder.id);
    cJSON_AddStringToObject(entry, "tile", tile_of(folder.id).c_str());
    cJSON_AddStringToObject(entry, "name", folder.name.c_str());
    cJSON_AddNumberToObject(entry, "count",
                            static_cast<double>(folder.titles.size()));
    cJSON_AddItemToArray(folders, entry);
  }
  const std::string text = onion_cjson::print_owned(root);
  return text.empty() ? "{}" : text;
}

/* ---- Home's registry module -------------------------------------------- */

std::string text_of(MonoObject *managed) {
  if (!managed)
    return {};
  const char *utf =
      mono_string_to_utf8(reinterpret_cast<MonoString *>(managed));
  std::string text = utf ? utf : "";
  if (utf && mono_free)
    mono_free(const_cast<char *>(utf));
  return text;
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
   system registry. The one asking for our key gets the folders instead. */
void registry_get_hook(MonoObject *module, MonoObject *param,
                       MonoObject *promise) {
  if (param && promise && (Enabled() || FavoritesEnabled())) {
    MonoObject *exception = nullptr;
    MonoString *shown = mono_object_to_string(param, &exception);
    const std::string asked =
        exception ? std::string()
                  : text_of(reinterpret_cast<MonoObject *>(shown));
    if (asked.find(kRegistryKey) != std::string::npos) {
      std::string state;
      {
        std::lock_guard<std::mutex> lock(s_mutex);
        load_locked();
        state = state_json_locked();
      }
      if (resolve(promise, state))
        return;
      LOG_WARN("[FOLDERS] could not answer the script");
    }
  }
  if (s_registry_get_original)
    s_registry_get_original(module, param, promise);
}

/* ---- requests from the menus ------------------------------------------ */

struct Request {
  enum class Kind {
    None, Add, Remove, Folder, Manage, FavoriteAdd, FavoriteRemove
  } kind = Kind::None;
  std::string title;
  int folder = 0;
};
std::mutex s_request_mutex;
Request s_request;
/* What is on screen for the folders (UI thread only). */
enum class Step { Idle, Choose, FolderMenu, Name, Rename, ConfirmDelete };
struct Flow {
  Step step = Step::Idle;
  std::string title; /* the title being filed */
  int folder = 0;    /* the folder being renamed or deleted */
  std::vector<int> rows; /* the folder of each row of the chooser */
  bool manage = false;   /* the list of the folders: its rows are submenus */
};
Flow s_flow;

constexpr const char *kRenameId = "rename:";
constexpr const char *kDeleteId = "delete:";

void file_title(const std::string &title, int id) {
  std::string name;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    Folder *folder = find_locked(id);
    if (!folder)
      return;
    for (Folder &other : s_folders)
      other.titles.erase(
          std::remove(other.titles.begin(), other.titles.end(), title),
          other.titles.end());
    folder->titles.insert(folder->titles.begin(), title);
    name = folder->name;
    changed_locked();
  }
  LOG_INFO("[FOLDERS] %s filed in folder %d", title.c_str(), id);
  notify("%s", (std::string(tr("folders.added")) + " " + name).c_str());
}

void open_chooser(const std::string &title) {
  std::vector<SystemDialogs::MenuEntry> entries;
  s_flow = Flow{};
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    const Folder *holder = holder_locked(title);
    SystemDialogs::MenuEntry manage;
    manage.title = tr("folders.manage");
    for (const Folder &folder : s_folders) {
      SystemDialogs::MenuEntry row;
      row.title = folder.name;
      row.icon = HomeFolderIconPath();
      row.enabled = &folder != holder;
      entries.push_back(row);
      s_flow.rows.push_back(folder.id);

      SystemDialogs::MenuEntry item;
      item.title = folder.name;
      item.icon = HomeFolderIconPath();
      SystemDialogs::MenuEntry rename;
      rename.title = tr("folders.rename");
      rename.id = kRenameId + std::to_string(folder.id);
      SystemDialogs::MenuEntry remove;
      remove.title = tr("folders.delete");
      remove.id = kDeleteId + std::to_string(folder.id);
      item.children = {rename, remove};
      manage.children.push_back(item);
    }
    SystemDialogs::MenuEntry create;
    create.title = tr("folders.new");
    entries.push_back(create);
    if (!manage.children.empty())
      entries.push_back(manage);
  }
  std::string failure;
  if (!SystemDialogs::ShowPopupMenu(entries, tr("folders.choose_title"),
                                    &failure, nullptr, true)) {
    LOG_WARN("[FOLDERS] chooser: %s", failure.c_str());
    s_flow = Flow{};
    return;
  }
  s_flow.step = Step::Choose;
  s_flow.title = title;
}

void open_name_prompt(Step step, const std::string &title, int folder,
                      const std::string &current) {
  SystemDialogs::PromptOptions options;
  options.title = step == Step::Rename ? tr("folders.rename_title")
                                       : tr("folders.new_title");
  options.message = tr("folders.name_message");
  options.text = current;
  options.positive = step == Step::Rename ? tr("folders.save")
                                          : tr("folders.create");
  options.negative = tr("folders.cancel");
  std::string failure;
  if (!SystemDialogs::ShowPrompt(options, &failure)) {
    LOG_WARN("[FOLDERS] prompt: %s", failure.c_str());
    s_flow = Flow{};
    return;
  }
  s_flow = Flow{};
  s_flow.step = step;
  s_flow.title = title;
  s_flow.folder = folder;
}

void confirm_delete(int id) {
  std::string name;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    const Folder *folder = find_locked(id);
    if (!folder)
      return;
    name = folder->name;
  }
  SystemDialogs::AlertOptions options;
  options.title = tr("folders.delete_title");
  options.message = name + "\n\n" + tr("folders.delete_message");
  options.positive = tr("folders.delete");
  options.negative = tr("folders.cancel");
  std::string failure;
  if (!SystemDialogs::ShowAlert(options, &failure)) {
    LOG_WARN("[FOLDERS] delete: %s", failure.c_str());
    return;
  }
  s_flow = Flow{};
  s_flow.step = Step::ConfirmDelete;
  s_flow.folder = id;
}

/* The rows that rename and delete `folder`, ids as chosen_submenu reads. */
std::vector<SystemDialogs::MenuEntry> folder_rows(const Folder &folder) {
  SystemDialogs::MenuEntry rename;
  rename.title = tr("folders.rename");
  rename.id = kRenameId + std::to_string(folder.id);
  SystemDialogs::MenuEntry remove;
  remove.title = tr("folders.delete");
  remove.id = kDeleteId + std::to_string(folder.id);
  return {rename, remove};
}

/* Options on a folder's tile: Rename, Delete. */
void open_folder_menu(int id) {
  std::vector<SystemDialogs::MenuEntry> entries;
  std::string header;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    const Folder *folder = find_locked(id);
    if (!folder)
      return;
    header = folder->name;
    entries = folder_rows(*folder);
  }
  s_flow = Flow{};
  std::string failure;
  if (!SystemDialogs::ShowPopupMenu(entries, header, &failure, nullptr, true)) {
    LOG_WARN("[FOLDERS] folder menu: %s", failure.c_str());
    return;
  }
  s_flow.step = Step::FolderMenu;
  s_flow.folder = id;
}

/* Every folder, each opening its Rename and Delete. */
void open_manage_menu() {
  std::vector<SystemDialogs::MenuEntry> entries;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    for (const Folder &folder : s_folders) {
      SystemDialogs::MenuEntry row;
      row.title = folder.name;
      row.icon = HomeFolderIconPath();
      row.children = folder_rows(folder);
      entries.push_back(row);
    }
  }
  if (entries.empty()) {
    notify("%s", tr("folders.none"));
    return;
  }
  s_flow = Flow{};
  std::string failure;
  if (!SystemDialogs::ShowPopupMenu(entries, tr("folders.manage"), &failure,
                                    nullptr, true)) {
    LOG_WARN("[FOLDERS] manage menu: %s", failure.c_str());
    return;
  }
  /* its rows are submenus: what is chosen comes as a submenu id */
  s_flow.step = Step::Choose;
  s_flow.manage = true;
}

void chosen_row(int index) {
  /* "Manage Folders": PUI opens its rows beside the menu */
  if (s_flow.manage || index > static_cast<int>(s_flow.rows.size()))
    return;
  SystemDialogs::ClosePopupMenu();
  const std::string title = s_flow.title;
  if (index >= 0 && index < static_cast<int>(s_flow.rows.size())) {
    const int id = s_flow.rows[static_cast<size_t>(index)];
    s_flow = Flow{};
    file_title(title, id);
    return;
  }
  if (index == static_cast<int>(s_flow.rows.size())) {
    std::string name;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      name = free_name_locked();
    }
    open_name_prompt(Step::Name, title, 0, name);
    return;
  }
  s_flow = Flow{};
}

void chosen_submenu_id(const std::string &id);

void chosen_folder_row(int index) {
  const int folder = s_flow.folder;
  SystemDialogs::ClosePopupMenu();
  s_flow = Flow{};
  chosen_submenu_id(index == 0 ? kRenameId + std::to_string(folder)
                               : kDeleteId + std::to_string(folder));
}

void chosen_submenu(const std::string &id) {
  SystemDialogs::ClosePopupMenu();
  s_flow = Flow{};
  chosen_submenu_id(id);
}

void chosen_submenu_id(const std::string &id) {
  const auto number = [&id](const char *prefix) {
    return id.compare(0, std::strlen(prefix), prefix) == 0
               ? std::atoi(id.c_str() + std::strlen(prefix))
               : 0;
  };
  if (const int folder = number(kRenameId)) {
    std::string name;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (const Folder *found = find_locked(folder))
        name = found->name;
    }
    open_name_prompt(Step::Rename, {}, folder, name);
  } else if (const int folder = number(kDeleteId)) {
    confirm_delete(folder);
  }
}

void named(bool confirmed, const std::string &typed) {
  const Flow flow = s_flow;
  s_flow = Flow{};
  if (!confirmed)
    return;
  std::string name;
  int id = 0;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    name = clean_name(typed);
    if (flow.step == Step::Rename) {
      Folder *folder = find_locked(flow.folder);
      if (!folder || name.empty())
        return;
      folder->name = name;
      changed_locked();
      LOG_INFO("[FOLDERS] folder %d renamed", folder->id);
      return;
    }
    if (name.empty())
      name = free_name_locked();
    if (s_next_id > kMaxFolderId)
      return;
    Folder folder;
    folder.id = id = s_next_id++;
    folder.name = name;
    s_folders.push_back(folder);
    changed_locked();
  }
  LOG_INFO("[FOLDERS] folder %d created", id);
  if (valid_title(flow.title))
    file_title(flow.title, id);
}

void deleted(int button) {
  const int id = s_flow.folder;
  s_flow = Flow{};
  if (button != 0)
    return;
  std::string name;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    const auto at = std::find_if(s_folders.begin(), s_folders.end(),
                                 [id](const Folder &f) { return f.id == id; });
    if (at == s_folders.end())
      return;
    name = at->name;
    s_folders.erase(at);
    changed_locked();
  }
  LOG_INFO("[FOLDERS] folder %d deleted", id);
  notify("%s", (std::string(tr("folders.deleted")) + " " + name).c_str());
}

void remove_title(const std::string &title) {
  std::string name;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    Folder *folder = holder_locked(title);
    if (!folder)
      return;
    folder->titles.erase(
        std::remove(folder->titles.begin(), folder->titles.end(), title),
        folder->titles.end());
    name = folder->name;
    changed_locked();
  }
  LOG_INFO("[FOLDERS] %s taken out of its folder", title.c_str());
  notify("%s", (std::string(tr("folders.removed")) + " " + name).c_str());
}

/* A game in or out of Favorites. The tab comes and goes with its first
   and last game: Home is reloaded to take it in or leave it out. */
void set_favorite(const std::string &title, bool favorite) {
  bool reload = false;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    const auto at = std::find(s_favorites.begin(), s_favorites.end(), title);
    if (favorite == (at != s_favorites.end()))
      return;
    const bool was_empty = s_favorites.empty();
    if (favorite)
      s_favorites.insert(s_favorites.begin(), title);
    else
      s_favorites.erase(at);
    reload = was_empty != s_favorites.empty();
    changed_locked();
  }
  LOG_INFO("[FOLDERS] %s %s favourites", title.c_str(),
           favorite ? "added to" : "taken out of");
  notify("%s", tr(favorite ? "favorites.added" : "favorites.removed"));
  if (reload && g_settings.home_favorites)
    shellui_request_home_reload();
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
  LOG_INFO("[FOLDERS] registry hook target=0x%lx installed=%d",
           static_cast<unsigned long>(address), hooked ? 1 : 0);
  if (!hooked)
    s_registry_get_original = nullptr;
}

bool Enabled() {
  return g_settings.home_folders && s_registry_get_original != nullptr;
}

bool Fileable(const std::string &title_id) {
  return Enabled() && valid_title(title_id) &&
         title_id.compare(0, 4, "NPXS") != 0;
}

bool FavoritesEnabled() {
  return g_settings.home_favorites && s_registry_get_original != nullptr;
}

bool FavoriteAllowed(const std::string &title_id) {
  return FavoritesEnabled() && valid_title(title_id) &&
         title_id.compare(0, 4, "NPXS") != 0;
}

bool IsFavorite(const std::string &title_id) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return std::find(s_favorites.begin(), s_favorites.end(), title_id) !=
         s_favorites.end();
}

bool FavoritesShown() {
  if (!FavoritesEnabled())
    return false;
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return !s_favorites.empty();
}

std::string FavoritesWhere(const std::string &where) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  const std::string ids = "(" + sql_list(s_favorites) + ")";
  /* every favourite, kept on Home or not, as the Emulators tab does */
  if (where.find('?') == std::string::npos)
    return " WHERE (icon.visible = 1 )  AND (icon.titleId IN " + ids + " ) ";
  return where + " AND (icon.titleId IN " + ids + " ) ";
}

bool InFolder(const std::string &title_id) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return holder_locked(title_id) != nullptr;
}

bool HandleUri(const std::string &uri, const std::string &menu_title) {
  if (uri.compare(0, std::strlen(kTileUri), kTileUri) == 0)
    return true; /* the script opened the folder already */
  Request request;
  if (uri == kAddUri)
    request.kind = Request::Kind::Add;
  else if (uri == kRemoveUri)
    request.kind = Request::Kind::Remove;
  else if (uri == kFavoriteAddUri)
    request.kind = Request::Kind::FavoriteAdd;
  else if (uri == kFavoriteRemoveUri)
    request.kind = Request::Kind::FavoriteRemove;
  else
    return false;
  request.title = menu_title;
  if (!valid_title(menu_title)) {
    LOG_WARN("[FOLDERS] no title for '%s' ('%s')", uri.c_str(),
             menu_title.c_str());
    return true;
  }
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
  return true;
}

std::string GamesRowWhere(const std::string &where) {
  if (!Enabled())
    return where;
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  std::vector<std::string> filed;
  for (const Folder &folder : s_folders)
    filed.insert(filed.end(), folder.titles.begin(), folder.titles.end());
  if (filed.empty())
    return where;
  return where + " AND (icon.titleId NOT IN (" + sql_list(filed) + ") ) ";
}

std::string LibraryWhere(int content_limit, const std::string &where) {
  const int id = content_limit - kLibraryContentLimit;
  if (!Enabled() || id <= 0 || id > kMaxFolderId)
    return where;
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  const Folder *folder = find_locked(id);
  /* a folder deleted meanwhile: its hub shows nothing */
  return where + " AND (icon.titleId IN (" +
         sql_list(folder ? folder->titles : std::vector<std::string>()) +
         ") ) ";
}

void HandleScriptMessage(const std::string &message) {
  /* options|<id>: Options pressed on a folder's tile */
  if (message.compare(0, 8, "options|") != 0)
    return;
  const int id = std::atoi(message.c_str() + 8);
  if (id <= 0 || id > kMaxFolderId)
    return;
  Request request;
  request.kind = Request::Kind::Folder;
  request.folder = id;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
}

void RequestManage() {
  Request request;
  request.kind = Request::Kind::Manage;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
}

void Tick() {
  if (s_flow.step == Step::Idle) {
    Request request;
    {
      std::lock_guard<std::mutex> lock(s_request_mutex);
      std::swap(request, s_request);
    }
    if (request.kind == Request::Kind::Add)
      open_chooser(request.title);
    else if (request.kind == Request::Kind::Remove)
      remove_title(request.title);
    else if (request.kind == Request::Kind::Folder)
      open_folder_menu(request.folder);
    else if (request.kind == Request::Kind::Manage)
      open_manage_menu();
    else if (request.kind == Request::Kind::FavoriteAdd ||
             request.kind == Request::Kind::FavoriteRemove)
      set_favorite(request.title,
                   request.kind == Request::Kind::FavoriteAdd);
    return;
  }
  switch (s_flow.step) {
  case Step::Choose: {
    int index = -1;
    std::string id;
    if (SystemDialogs::PollPopupSubmenu(&id))
      chosen_submenu(id);
    else if (SystemDialogs::PollPopupMenu(&index))
      chosen_row(index);
    else if (SystemDialogs::PopupMenuDismissed())
      s_flow = Flow{};
    break;
  }
  case Step::FolderMenu: {
    int index = -1;
    if (SystemDialogs::PollPopupMenu(&index))
      chosen_folder_row(index);
    else if (SystemDialogs::PopupMenuDismissed())
      s_flow = Flow{};
    break;
  }
  case Step::Name:
  case Step::Rename: {
    bool confirmed = false;
    std::string typed;
    if (SystemDialogs::PollPrompt(&confirmed, &typed))
      named(confirmed, typed);
    break;
  }
  case Step::ConfirmDelete: {
    int button = -1;
    if (SystemDialogs::PollAlert(&button))
      deleted(button);
    break;
  }
  case Step::Idle:
    break;
  }
}

} // namespace HomeFolders
