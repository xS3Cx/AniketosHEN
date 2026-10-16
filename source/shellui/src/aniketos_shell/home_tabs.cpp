#include "aniketos_shell/home_tabs.hpp"
#include "aniketos_shell/home_bookmark.hpp"

#include "hooked_funcs.hpp"
#include "aniketos_shell/system_dialogs.hpp"
#include "toolbox_i18n.hpp"
#include "ps5/kernel.h"
#include "shell_clock.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <vector>

extern int g_libmono_handle;

namespace HomeTabs {
namespace {


/* Words that mark an installed title as an emulator. More titles can be
   listed by id, one per line, in <data>/emulators.txt ("!ID" leaves one out). */
constexpr const char *kEmulatorWords[] = {
    "emu",     "retroarch", "snes",   "nes9",   "pcsx",   "sx2",   "dolphin",
    "ppsspp",  "eden",      "yuzu",   "ryujinx", "citra", "mame",  "x360",
    "xenia",   "duckstation", "flycast", "mgba", "vita3k", "rpcs3",
    "scummvm", "dosbox",    "mednafen", "mupen", "desmume", "genesis",
};

struct Emulator {
  std::string title_id;
  std::string name;
};

MonoImage *s_pui = nullptr;
using BuildWhere = MonoString *(*)(MonoObject *, MonoObject *);
BuildWhere s_build_where_original = nullptr;

uint32_t s_switcher = 0; /* React's 'space-switcher' focus layer */
std::vector<Emulator> s_emulators;
std::string s_filter_sql; /* "'ID','ID',..." of the emulators */
int s_search_countdown = 0;

struct TabPanel {
  uint32_t handle = 0;
  float stock_x = 0, stock_width = 0;
  float written_x = -1, written_width = -1;
};
std::vector<TabPanel> s_tab_panels;

/* ---- small reflection helpers ------------------------------------------ */

MonoObject *target(uint32_t handle) {
  return handle ? mono_gchandle_get_target(handle) : nullptr;
}

void keep(uint32_t &handle, MonoObject *object, bool strong = false) {
  if (handle)
    mono_gchandle_free(handle);
  handle = !object ? 0
           : strong ? mono_gchandle_new(object, 0)
                    : mono_gchandle_new_weakref(object, 0);
}

MonoMethod *method_of(MonoObject *object, const char *name, int parameters) {
  for (MonoClass *k = mono_object_get_class(object); k;
       k = mono_class_get_parent(k))
    if (MonoMethod *method =
            mono_class_get_method_from_name(k, name, parameters))
      return method;
  return nullptr;
}

MonoObject *get(MonoObject *object, const char *getter) {
  MonoMethod *method = object ? method_of(object, getter, 0) : nullptr;
  if (!method)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(method, object, nullptr, &exception);
  return exception ? nullptr : value;
}

void set(MonoObject *object, const char *setter, void *value) {
  MonoMethod *method = object ? method_of(object, setter, 1) : nullptr;
  if (!method)
    return;
  void *args[1] = {value};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, object, args, &exception);
}


float get_float(MonoObject *object, const char *getter) {
  MonoObject *boxed = get(object, getter);
  return boxed ? *static_cast<float *>(mono_object_unbox(boxed)) : 0.0f;
}


void set_float(MonoObject *object, const char *setter, float value) {
  set(object, setter, &value);
}




std::string text_of(MonoObject *managed) {
  if (!managed)
    return {};
  const char *utf = mono_string_to_utf8(reinterpret_cast<MonoString *>(managed));
  std::string text = utf ? utf : "";
  if (utf && mono_free)
    mono_free(const_cast<char *>(utf));
  return text;
}




/* ---- installed emulators ----------------------------------------------- */

std::string lower(std::string text) {
  for (char &c : text)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
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

void scan_emulators() {
  s_emulators.clear();
  std::vector<std::string> listed, excluded;
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
    return;
  while (dirent *entry = readdir(dir)) {
    const std::string id = entry->d_name;
    if (id.size() != 9 || id[0] == '.')
      continue;
    if (std::find(excluded.begin(), excluded.end(), id) != excluded.end())
      continue;
    const std::string name = read_title_name(id);
    bool emulator = std::find(listed.begin(), listed.end(), id) != listed.end();
    const std::string haystack = lower(name);
    for (const char *word : kEmulatorWords)
      emulator = emulator || haystack.find(word) != std::string::npos;
    if (emulator)
      s_emulators.push_back({id, name.empty() ? id : name});
  }
  closedir(dir);
  std::sort(s_emulators.begin(), s_emulators.end(),
            [](const Emulator &a, const Emulator &b) {
              return lower(a.name) < lower(b.name);
            });
  LOG_INFO("[HOMETABS] emulators found=%zu", s_emulators.size());
}

void build_filter() {
  scan_emulators();
  s_filter_sql.clear();
  for (const Emulator &emulator : s_emulators) {
    /* Title ids go into SQL as literals: letters and digits only. */
    bool clean = emulator.title_id.size() == 9;
    for (const char c : emulator.title_id)
      clean = clean && std::isalnum(static_cast<unsigned char>(c));
    if (!clean)
      continue;
    if (!s_filter_sql.empty())
      s_filter_sql += ",";
    s_filter_sql += "'" + emulator.title_id + "'";
  }
}

/* ---- the queries behind the tile rows ---------------------------------- */

MonoObject *field_value(MonoObject *object, const char *name) {
  static MonoObject *(*value_object)(MonoDomain *, MonoClassField *,
                                     MonoObject *) = nullptr;
  if (!value_object)
    value_object = reinterpret_cast<decltype(value_object)>(
        kernel_dynlib_dlsym(-1, g_libmono_handle,
                            "mono_field_get_value_object"));
  for (MonoClass *k = mono_object_get_class(object); k && value_object;
       k = mono_class_get_parent(k))
    if (MonoClassField *field = mono_class_get_field_from_name(k, name))
      return value_object(mono_domain_get(), field, object);
  return nullptr;
}

std::string enum_name(MonoObject *boxed) {
  MonoObject *exception = nullptr;
  MonoString *shown = boxed ? mono_object_to_string(boxed, &exception) : nullptr;
  return shown && !exception ? text_of(reinterpret_cast<MonoObject *>(shown))
                             : std::string();
}

/* AppBrowseItemAccessor.BuildWhereClause: the WHERE of every list of apps
   ShellUI shows. Two of them concern us, both made by Home for the "Game"
   space of its experience switcher:
     - the Emulators tab's list, which the script in Home marks with a
       content limit of its own (the database has no such space): its filter
       becomes the emulators' title ids;
     - the Games tab's list: the same titles are left out of it.
   The bookmark tab's list is made like the Emulators one, with a content
   limit of its own: its filter becomes the bookmark's title ids.
   Every other list is untouched. */
MonoString *build_where_hook(MonoObject *accessor, MonoObject *binds) {
  MonoString *where = s_build_where_original
                          ? s_build_where_original(accessor, binds)
                          : nullptr;
  if (!where || !accessor)
    return where;
  if (enum_name(field_value(accessor, "filterTypeDispLocationSpace")) !=
          "Game" ||
      enum_name(field_value(
          accessor, "filterTypeDispLocationExperienceSwitcher")) != "Display")
    return where;
  MonoObject *limit = field_value(accessor, "contentLimit");
  const int32_t content_limit =
      limit ? *static_cast<int32_t *>(mono_object_unbox(limit)) : 0;
  const bool emulators_list = content_limit == kEmulatorsContentLimit;
  const std::string original = text_of(reinterpret_cast<MonoObject *>(where));
  /* the bookmark tab's list (HomeBookmark), marked as the Emulators one */
  if (content_limit == HomeBookmark::kContentLimit)
    return mono_string_new(mono_domain_get(),
                           HomeBookmark::Where(original).c_str());
  /* No emulators installed: the Emulators list matches nothing. */
  const std::string ids = "(" + (s_filter_sql.empty() ? "''" : s_filter_sql) + ")";
  std::string changed;
  if (!emulators_list) {
    /* No emulators installed: Games is left as it is. */
    if (s_filter_sql.empty())
      return where;
    changed = original + " AND (icon.titleId NOT IN " + ids + " ) ";
  } else if (original.find('?') == std::string::npos) {
    /* Emulators that are not pinned to Home belong here too, so the
       condition on where a title is displayed is replaced, not added to. */
    changed = " WHERE (icon.visible = 1 )  AND (icon.titleId IN " + ids + " ) ";
  } else {
    changed = original + " AND (icon.titleId IN " + ids + " ) ";
  }
  static bool logged[2] = {false, false};
  if (!logged[emulators_list ? 1 : 0]) {
    logged[emulators_list ? 1 : 0] = true;
    LOG_INFO("[HOMETABS] %s list: %s", emulators_list ? "emulators" : "games",
             changed.substr(0, 300).c_str());
  }
  return mono_string_new(mono_domain_get(), changed.c_str());
}

/* ---- the tab bar ------------------------------------------------------- */

MonoObject *find_switcher(MonoObject *widget, int depth, int &budget) {
  if (!widget || depth > 10 || budget-- <= 0)
    return nullptr;
  if (text_of(get(widget, "get_Name")) == "space-switcher")
    return widget;
  MonoObject *child = get(widget, "get_FirstChild");
  for (int guard = 0; child && guard < 32; ++guard) {
    if (MonoObject *found = find_switcher(child, depth + 1, budget))
      return found;
    child = get(child, "get_NextSibling");
  }
  return nullptr;
}

bool resolve() {
  MonoObject *root = ShellClock::GetHomeRoot();
  if (!root)
    return false;
  int budget = 300;
  MonoObject *switcher = find_switcher(root, 0, budget);
  if (!switcher)
    return false; /* the clock's scene is not Home (Control Center...) */
  keep(s_switcher, switcher);
  s_tab_panels.clear();
  MonoObject *child = get(switcher, "get_FirstChild");
  for (int guard = 0; child && guard < 8; ++guard) {
    TabPanel panel;
    panel.handle = mono_gchandle_new_weakref(child, 0);
    s_tab_panels.push_back(panel);
    child = get(child, "get_NextSibling");
  }
  LOG_INFO("[HOMETABS] tab bar found, tabs=%zu", s_tab_panels.size());
  return !s_tab_panels.empty();
}

/* A tab is a panel sized by React for its caption as React knows it, with
   the caption label inside; the focus frame is drawn around the panel. The
   glyph our text hook puts in front makes the label wider, so each panel is
   grown to its label and the following ones are pushed right. React may lay
   the bar out again at any time: whatever is not our own last value is taken
   as its new stock geometry. */
void fit_tab_panels() {
  float shift = 0.0f;
  for (TabPanel &tab : s_tab_panels) {
    MonoObject *panel = target(tab.handle);
    if (!panel)
      continue;
    const float x = get_float(panel, "get_X");
    const float width = get_float(panel, "get_Width");
    if (std::fabs(x - tab.written_x) > 0.5f)
      tab.stock_x = x;
    if (std::fabs(width - tab.written_width) > 0.5f)
      tab.stock_width = width;
    MonoObject *label = get(panel, "get_FirstChild");
    const float label_x = label ? get_float(label, "get_X") : 0.0f;
    float label_width = label ? get_float(label, "get_Width") : 0.0f;
    if (label)
      if (MonoObject *measured = get(label, "GetTextWidth")) {
        const float text_width =
            *static_cast<float *>(mono_object_unbox(measured));
        if (text_width > 8.0f)
          label_width = text_width;
      }
    const float wanted_width =
        std::max(tab.stock_width, label_width + 2.0f * label_x);
    const float wanted_x = tab.stock_x + shift;
    if (std::fabs(x - wanted_x) > 0.5f)
      set_float(panel, "set_X", wanted_x);
    if (std::fabs(width - wanted_width) > 0.5f)
      set_float(panel, "set_Width", wanted_width);
    tab.written_x = wanted_x;
    tab.written_width = wanted_width;
    shift += wanted_width - tab.stock_width;
  }
}

} // namespace

bool HasEmulators() { return !s_filter_sql.empty(); }

bool Ready() { return s_build_where_original != nullptr; }

void Install(MonoImage *pui) {
  if (!pui || s_pui)
    return;
  s_pui = pui;
  if (MonoImage *db = getDLLimage("Sce.Vsh.Accessor.Db.dll")) {
    MonoClass *accessor = mono_class_from_name(db, "Sce.Vsh.Accessor.Db",
                                               "AppBrowseItemAccessor");
    const uint64_t address =
        accessor ? Get_Address_of_Method(db, accessor, "BuildWhereClause", 1)
                 : 0;
    const bool hooked =
        address &&
        InstallDetour(address, reinterpret_cast<void *>(&build_where_hook),
                      reinterpret_cast<void **>(&s_build_where_original));
    LOG_INFO("[HOMETABS] query hook target=0x%lx installed=%d",
             static_cast<unsigned long>(address), hooked ? 1 : 0);
    if (!hooked)
      s_build_where_original = nullptr;
  }
  /* Ready before Home first asks for its lists. Without the query hook
     there is nothing to fill an Emulators tab with, so none is offered. */
  if (s_build_where_original)
    build_filter();
}

void Tick() {
  if (!s_pui)
    return;
  MonoObject *switcher = target(s_switcher);
  if (!switcher || ShellClock::IsWidgetDisposed(switcher)) {
    if (--s_search_countdown > 0)
      return;
    s_search_countdown = 90;
    if (!resolve())
      return;
  }
  fit_tab_panels();

  /* Nothing to show in the Emulators tab: say so, once per arrival on the
     tab. The list is read again first, in case an emulator has been
     installed since the last look. */
  MonoObject *emulators_tab = nullptr;
  for (const TabPanel &tab : s_tab_panels) {
    MonoObject *panel = target(tab.handle);
    if (panel && text_of(get(panel, "get_Name")) == "space-switcher-emulators")
      emulators_tab = panel;
  }
  static bool was_on_emulators = false;
  MonoObject *focused = emulators_tab ? get(emulators_tab, "get_Focused")
                                      : nullptr;
  const bool on_emulators =
      focused && *static_cast<bool *>(mono_object_unbox(focused));
  if (on_emulators && !was_on_emulators) {
    if (s_filter_sql.empty())
      build_filter();
    if (s_filter_sql.empty()) {
      /* A notification, not a dialog: the tab is on the way to the icons on
         the right of the bar, and a dialog that took the focus back to Games
         made them unreachable. Not more often than every half minute. */
      static time_t last_notice = 0;
      const time_t now = time(nullptr);
      if (now - last_notice >= 30) {
        last_notice = now;
        notify("%s. %s", toolbox_i18n::tr("home_tabs.no_emulators.title"),
               toolbox_i18n::tr("home_tabs.no_emulators.body"));
        LOG_INFO("[HOMETABS] no emulators installed: notification sent");
      }
    }
  }
  was_on_emulators = on_emulators;
}

} // namespace HomeTabs
