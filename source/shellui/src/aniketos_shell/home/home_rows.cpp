#include "aniketos_shell/home/home_rows.hpp"

#include "aniketos_shell/home/hidden_apps.hpp"
#include "aniketos_shell/home/home_bookmark.hpp"
#include "aniketos_shell/home/title_info.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "hooked_funcs.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace HomeRows {
namespace {

namespace TitleInfo = AniketosShell::TitleInfo;
using AniketosUI::Reflect::FieldValue;

/* Words that mark an installed title as an emulator. More titles can be
   listed by id, one per line, in <data>/emulators.txt ("!ID" leaves one out). */
constexpr const char *kEmulatorWords[] = {
    "emu",     "retroarch", "snes",   "nes9",   "pcsx",   "sx2",   "dolphin",
    "ppsspp",  "eden",      "yuzu",   "ryujinx", "citra", "mame",  "x360",
    "xenia",   "duckstation", "flycast", "mgba", "vita3k", "rpcs3",
    "scummvm", "dosbox",    "mednafen", "mupen", "desmume", "genesis",
};

using BuildWhere = MonoString *(*)(MonoObject *, MonoObject *);
BuildWhere s_build_where_original = nullptr;

/* Read by the query hook on any thread, rebuilt on the UI thread. */
std::mutex s_filter_mutex;
std::string s_filter_sql; /* "'ID','ID',..." of the emulators */
/* The Home that is up has the Emulators tab (set by script_injector). */
std::atomic<bool> s_emulators_tab{false};

std::string lower(std::string text) {
  for (char &c : text)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

std::string upper(std::string text) {
  for (char &c : text)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return text;
}

bool has(const std::vector<std::string> &list, const std::string &id) {
  return std::find(list.begin(), list.end(), id) != list.end();
}

std::vector<std::string> scan_emulators() {
  std::vector<std::string> emulators, listed, excluded;
  if (FILE *file = std::fopen(ONION_DIST_USER_DATA_ROOT "/emulators.txt", "r")) {
    char line[64];
    while (std::fgets(line, sizeof(line), file)) {
      std::string id(line);
      while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back())))
        id.pop_back();
      if (id.size() == 10 && id[0] == '!')
        excluded.push_back(id.substr(1));
      else if (id.size() == 9)
        listed.push_back(id);
    }
    std::fclose(file);
  }
  DIR *dir = opendir("/user/app");
  if (!dir)
    return emulators;
  while (dirent *entry = readdir(dir)) {
    const std::string id = entry->d_name;
    if (id.size() != 9 || id[0] == '.' || has(excluded, id))
      continue;
    const std::string haystack = lower(TitleInfo::ReadName(id));
    bool emulator = has(listed, id);
    for (const char *word : kEmulatorWords)
      emulator = emulator || haystack.find(word) != std::string::npos;
    if (emulator)
      emulators.push_back(id);
  }
  closedir(dir);
  LOG_INFO("[HOMETABS] emulators found=%zu", emulators.size());
  return emulators;
}

void build_filter() {
  std::string filter = TitleInfo::SqlList(scan_emulators());
  std::lock_guard<std::mutex> lock(s_filter_mutex);
  s_filter_sql = std::move(filter);
}

std::string filter_sql() {
  std::lock_guard<std::mutex> lock(s_filter_mutex);
  return s_filter_sql;
}

std::string enum_name(MonoObject *boxed) {
  MonoObject *exception = nullptr;
  MonoString *shown = boxed ? mono_object_to_string(boxed, &exception) : nullptr;
  return exception ? std::string() : Mono_to_String(shown);
}

enum class ListKind { Games, Emulators, Media, MediaOther };

/* Each list once, and again when it changes. */
void log_list(ListKind kind, const std::string &where) {
  static std::mutex log_mutex;
  static std::string logged[4];
  static int logs[4] = {0, 0, 0, 0};
  static constexpr const char *kNames[] = {"games", "emulators", "media",
                                           "other media"};
  const int at = static_cast<int>(kind);
  std::lock_guard<std::mutex> lock(log_mutex);
  if (logged[at] == where || logs[at] >= 8)
    return;
  logged[at] = where;
  ++logs[at];
  LOG_INFO("[HOMETABS] %s list: %s", kNames[at], where.substr(0, 400).c_str());
}

/* `where` letting the titles `ids` in as well, as Home shows a title; as it
   is when there are none or when it is not a WHERE alone. */
std::string with_added(const std::string &where, const std::string &ids) {
  const size_t at = where.find("WHERE");
  if (ids.empty() || at == std::string::npos)
    return where;
  const std::string upper_where = upper(where);
  for (const char *clause : {"ORDER BY", "GROUP BY", "LIMIT"})
    if (upper_where.find(clause) != std::string::npos)
      return where;
  return where.substr(0, at) + "WHERE ((" + where.substr(at + 5) +
         ") OR (icon.visible = 1 AND icon.titleId IN (" + ids + ") ) ) ";
}

std::string append_condition(const std::string &where,
                             const std::string &clause) {
  const std::string upper_where = upper(where);
  if (upper_where.find("WHERE") == std::string::npos)
    return where;
  size_t at = std::string::npos;
  for (const char *bound : {"ORDER BY", "GROUP BY", "LIMIT"}) {
    const size_t found = upper_where.find(bound);
    if (found != std::string::npos && (at == std::string::npos || found < at))
      at = found;
  }
  if (at == std::string::npos)
    return where + clause;
  return where.substr(0, at) + clause + where.substr(at);
}

/* `where` leaving the titles `ids` out. */
std::string without(const std::string &where, const std::string &ids) {
  return ids.empty() ? where
                     : append_condition(where, " AND (icon.titleId NOT IN (" +
                                                   ids + ") ) ");
}

/* The list of one of our tabs. Titles that are not pinned to Home belong in it
   too, so the condition on where a title is displayed is replaced, not added
   to. No titles match no rows. */
std::string only_titles(const std::string &where, const std::string &ids) {
  const std::string in = "(" + (ids.empty() ? "''" : ids) + ")";
  if (where.find('?') == std::string::npos)
    return " WHERE (icon.visible = 1 )  AND (icon.titleId IN " + in + " ) ";
  return where + " AND (icon.titleId IN " + in + " ) ";
}

MonoString *changed_string(MonoString *where, const std::string &original,
                           const std::string &changed) {
  return changed == original
             ? where
             : mono_string_new(mono_domain_get(), changed.c_str());
}

/* AppBrowseItemAccessor.BuildWhereClause: the WHERE of every list of apps
   ShellUI shows. Two of them concern us, both made by Home for the "Game"
   space of its experience switcher:
     - the Emulators tab's list, which the script in Home marks with a
       content limit of its own (the database has no such space): its filter
       becomes the emulators' title ids;
     - the Games tab's list: the same titles are left out of it, while
       Home has the Emulators tab to show them.
   The bookmark tab's list is made like the Emulators one, with a content
   limit of its own: its filter becomes the bookmark's title ids. The Games
   and Media rows also get the titles copied or moved into them from a
   title's Options menu, and lose the ones moved away (HomeBookmark). Hidden
   apps leave all of them (HiddenApps).
   Every other list is untouched. */
MonoString *build_where_hook(MonoObject *accessor, MonoObject *binds) {
  MonoString *where = s_build_where_original
                          ? s_build_where_original(accessor, binds)
                          : nullptr;
  if (!where || !accessor)
    return where;
  const std::string space =
      enum_name(FieldValue(accessor, "filterTypeDispLocationSpace"));
  if (space != "Game" && space != "Media")
    return where;
  const std::string original = Mono_to_String(where);
  const std::string switcher =
      enum_name(FieldValue(accessor, "filterTypeDispLocationExperienceSwitcher"));
  if (switcher != "Display") {
    if (space == "Media")
      log_list(ListKind::MediaOther, switcher + ": " + original);
    return where;
  }
  MonoObject *limit = FieldValue(accessor, "contentLimit");
  const int32_t content_limit =
      limit ? *static_cast<int32_t *>(mono_object_unbox(limit)) : 0;
  using HomeBookmark::Target;
  if (space == "Media") {
    const std::string changed = without(
        without(with_added(original, HomeBookmark::AddedSql(Target::Media)),
                HomeBookmark::HiddenSql(Target::Media)),
        HiddenApps::SqlList());
    log_list(ListKind::Media, changed);
    return changed_string(where, original, changed);
  }
  /* the bookmark tab's list (HomeBookmark), marked as the Emulators one */
  if (content_limit == HomeBookmark::kContentLimit)
    return changed_string(
        where, original,
        without(only_titles(original, HomeBookmark::TitlesSql()),
                HiddenApps::SqlList()));
  const bool emulators_list = content_limit == kEmulatorsContentLimit;
  const std::string emulators = filter_sql();
  std::string changed;
  if (emulators_list) {
    changed = only_titles(original, emulators);
  } else {
    /* Without the emulators while they have a tab of their own (installed,
       switched on, the script in Home), unless copied or moved here, and
       without the titles moved away; with the ones copied or moved here. */
    const std::string added = HomeBookmark::AddedSql(Target::Games);
    std::string left_out;
    if (!emulators.empty() && s_emulators_tab)
      left_out = " AND (icon.titleId NOT IN (" + emulators + ")" +
                 (added.empty() ? std::string()
                                : " OR icon.titleId IN (" + added + ")") +
                 " ) ";
    changed = without(append_condition(with_added(original, added), left_out),
                      HomeBookmark::HiddenSql(Target::Games));
  }
  changed = without(changed, HiddenApps::SqlList());
  log_list(emulators_list ? ListKind::Emulators : ListKind::Games, changed);
  return changed_string(where, original, changed);
}

} // namespace

void Install() {
  MonoImage *db = getDLLimage("Sce.Vsh.Accessor.Db.dll");
  if (!db)
    return;
  MonoClass *accessor =
      mono_class_from_name(db, "Sce.Vsh.Accessor.Db", "AppBrowseItemAccessor");
  const uint64_t address =
      accessor ? Get_Address_of_Method(db, accessor, "BuildWhereClause", 1) : 0;
  const bool hooked =
      address &&
      InstallDetour(address, reinterpret_cast<void *>(&build_where_hook),
                    reinterpret_cast<void **>(&s_build_where_original));
  LOG_INFO("[HOMETABS] query hook target=0x%lx installed=%d",
           static_cast<unsigned long>(address), hooked ? 1 : 0);
  if (!hooked) {
    s_build_where_original = nullptr;
    return;
  }
  /* Ready before Home first asks for its lists. Without the query hook there
     is nothing to fill an Emulators tab with, so none is offered. */
  build_filter();
}

bool Ready() { return s_build_where_original != nullptr; }

void SetEmulatorsTabShown(bool shown) {
  s_emulators_tab = shown;
  LOG_INFO("[HOMETABS] Home starts %s the Emulators tab",
           shown ? "with" : "without");
}

bool FindEmulators() {
  if (filter_sql().empty())
    build_filter();
  return !filter_sql().empty();
}

} // namespace HomeRows
