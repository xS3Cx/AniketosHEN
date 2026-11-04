#include "aniketos_shell/home/home_rows.hpp"

#include "aniketos_shell/home/hidden_apps.hpp"
#include "aniketos_shell/home/home_tabs_store.hpp"
#include "aniketos_shell/home/title_info.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "hooked_funcs.hpp"

#include <onion/detour.h>
#include <onion/log.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace HomeRows {
namespace {

namespace TitleInfo = AniketosShell::TitleInfo;
using AniketosUI::Reflect::FieldValue;
using HomeTabsStore::StockRow;

using BuildWhere = MonoString *(*)(MonoObject *, MonoObject *);
BuildWhere s_build_where_original = nullptr;

std::string upper(std::string text) {
  for (char &c : text)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return text;
}

std::string enum_name(MonoObject *boxed) {
  MonoObject *exception = nullptr;
  MonoString *shown = boxed ? mono_object_to_string(boxed, &exception) : nullptr;
  return exception ? std::string() : Mono_to_String(shown);
}

enum class ListKind { Games, Media, MediaOther, CustomTab };

void log_list(ListKind kind, const std::string &where) {
  static std::mutex log_mutex;
  static std::string logged[4];
  static int logs[4] = {0, 0, 0, 0};
  static constexpr const char *kNames[] = {"games", "media", "other media",
                                           "tab"};
  const int at = static_cast<int>(kind);
  std::lock_guard<std::mutex> lock(log_mutex);
  if (logged[at] == where || logs[at] >= 8)
    return;
  logged[at] = where;
  ++logs[at];
  LOG_INFO("[HOMETABS] %s list: %s", kNames[at], where.substr(0, 400).c_str());
}

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

std::string without(const std::string &where, const std::string &ids) {
  return ids.empty() ? where
                     : append_condition(where, " AND (icon.titleId NOT IN (" +
                                                   ids + ") ) ");
}

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

MonoString *build_where_hook(MonoObject *accessor, MonoObject *binds) {
  MonoString *where = s_build_where_original
                          ? s_build_where_original(accessor, binds)
                          : nullptr;
  if (!where || !accessor)
    return where;
  MonoObject *limit = FieldValue(accessor, "contentLimit");
  const int32_t content_limit =
      limit ? *static_cast<int32_t *>(mono_object_unbox(limit)) : 0;
  if (content_limit >= HomeTabsStore::kContentLimitFirst &&
      content_limit <= HomeTabsStore::kContentLimitLast) {
    const std::string original = Mono_to_String(where);
    const std::string changed =
        without(only_titles(original, HomeTabsStore::TitlesSql(content_limit)),
                HiddenApps::SqlList());
    log_list(ListKind::CustomTab,
             std::to_string(content_limit) + ": " + changed);
    return changed_string(where, original, changed);
  }
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
  if (space == "Media") {
    const std::string changed = without(
        without(with_added(original,
                           HomeTabsStore::AddedSql(StockRow::Media)),
                HomeTabsStore::HiddenSql(StockRow::Media)),
        HiddenApps::SqlList());
    log_list(ListKind::Media, changed);
    return changed_string(where, original, changed);
  }
  const std::string changed = without(
      without(with_added(original, HomeTabsStore::AddedSql(StockRow::Games)),
              HomeTabsStore::HiddenSql(StockRow::Games)),
      HiddenApps::SqlList());
  log_list(ListKind::Games, changed);
  return changed_string(where, original, changed);
}

} // namespace

void Install() {
  if (s_build_where_original)
    return;
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
  if (!hooked)
    s_build_where_original = nullptr;
}

bool Ready() { return s_build_where_original != nullptr; }

} // namespace HomeRows
