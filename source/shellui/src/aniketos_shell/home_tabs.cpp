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
#include <onion/settings.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <vector>

extern int g_libmono_handle;
extern onion::Settings g_settings;

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
/* The Home that is up has the Emulators tab (set by home_js). */
std::atomic<bool> s_emulators_tab{false};

struct TabPanel {
  uint32_t handle = 0;
  std::string name; /* "space-switcher-<space id>" */
  float stock_x = 0, stock_width = 0;
  float written_x = -1, written_width = -1;
};
std::vector<TabPanel> s_tab_panels;

MonoClass *s_label_class = nullptr; /* UI3.Label */

/* A stock tab's label the text hook dressed, kept until it is found in the
   tab bar or elsewhere (Search shows the same words), or given up on. */
struct TabLabel {
  uint32_t handle = 0;
  StockTab tab = StockTab::Games;
  int frames = 0; /* waited so far for it to be put in a tree */
};
std::vector<TabLabel> s_tab_labels;
constexpr size_t kMaxTabLabels = 16;
constexpr int kTabLabelFrames = 120;
/* Labels found in the tab bar and given the user's name. React sets the
   system's caption on them again now and then (as the focus moves), and
   the text hook then hands it the name straight away. */
std::vector<TabLabel> s_named_labels;
constexpr size_t kMaxNamedLabels = 8;
int s_panel_frames = 0; /* frames until the bar's labels are looked at */
/* The system's captions as last seen: what the rename prompt starts with
   while a tab has no name of the user's. */
std::string s_stock_names[2];

std::mutex s_request_mutex;
int s_rename_request = -1; /* the tab a prompt is asked for */
int s_renaming = -1;       /* the tab whose prompt is open (UI thread) */

/* TabName's answers, kept by Tick: the Options menu is built on another
   thread than the one the names change on. */
std::mutex s_names_mutex;
std::string s_tab_names[2];

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
     - the Games tab's list: the same titles are left out of it, while
       Home has the Emulators tab to show them.
   The bookmark tab's list is made like the Emulators one, with a content
   limit of its own: its filter becomes the bookmark's title ids. The Games
   and Media rows also get the titles copied or moved into them from a
   title's Options menu, and lose the ones moved away (HomeBookmark).
   Every other list is untouched. */
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
  std::string upper = where;
  for (char &c : upper)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  for (const char *clause : {"ORDER BY", "GROUP BY", "LIMIT"})
    if (upper.find(clause) != std::string::npos)
      return where;
  return where.substr(0, at) + "WHERE ((" + where.substr(at + 5) +
         ") OR (icon.visible = 1 AND icon.titleId IN (" + ids + ") ) ) ";
}

MonoString *build_where_hook(MonoObject *accessor, MonoObject *binds) {
  MonoString *where = s_build_where_original
                          ? s_build_where_original(accessor, binds)
                          : nullptr;
  if (!where || !accessor)
    return where;
  const std::string space =
      enum_name(field_value(accessor, "filterTypeDispLocationSpace"));
  if (space != "Game" && space != "Media")
    return where;
  const std::string original = text_of(reinterpret_cast<MonoObject *>(where));
  const std::string switcher = enum_name(
      field_value(accessor, "filterTypeDispLocationExperienceSwitcher"));
  if (switcher != "Display") {
    if (space == "Media")
      log_list(ListKind::MediaOther, switcher + ": " + original);
    return where;
  }
  MonoObject *limit = field_value(accessor, "contentLimit");
  const int32_t content_limit =
      limit ? *static_cast<int32_t *>(mono_object_unbox(limit)) : 0;
  const bool emulators_list = content_limit == kEmulatorsContentLimit;
  using HomeBookmark::Target;
  if (space == "Media") {
    std::string changed =
        with_added(original, HomeBookmark::AddedSql(Target::Media));
    const std::string hidden = HomeBookmark::HiddenSql(Target::Media);
    if (!hidden.empty())
      changed += " AND (icon.titleId NOT IN (" + hidden + ") ) ";
    log_list(ListKind::Media, changed);
    return changed == original
               ? where
               : mono_string_new(mono_domain_get(), changed.c_str());
  }
  /* the bookmark tab's list (HomeBookmark), marked as the Emulators one */
  if (content_limit == HomeBookmark::kContentLimit)
    return mono_string_new(mono_domain_get(),
                           HomeBookmark::Where(original).c_str());
  /* No emulators installed: the Emulators list matches nothing. */
  const std::string ids = "(" + (s_filter_sql.empty() ? "''" : s_filter_sql) + ")";
  std::string changed;
  if (!emulators_list) {
    /* Without the emulators while they have a tab of their own (installed,
       switched on, the script in Home), unless copied or moved here, and
       without the titles moved away; with the ones copied or moved here. */
    const std::string added = HomeBookmark::AddedSql(Target::Games);
    std::string left_out;
    if (!s_filter_sql.empty() && s_emulators_tab)
      left_out += added.empty()
                      ? " AND (icon.titleId NOT IN " + ids + " ) "
                      : " AND (icon.titleId NOT IN " + ids +
                            " OR icon.titleId IN (" + added + ") ) ";
    const std::string hidden = HomeBookmark::HiddenSql(Target::Games);
    if (!hidden.empty())
      left_out += " AND (icon.titleId NOT IN (" + hidden + ") ) ";
    changed = with_added(original, added) + left_out;
  } else if (original.find('?') == std::string::npos) {
    /* Emulators that are not pinned to Home belong here too, so the
       condition on where a title is displayed is replaced, not added to. */
    changed = " WHERE (icon.visible = 1 )  AND (icon.titleId IN " + ids + " ) ";
  } else {
    changed = original + " AND (icon.titleId IN " + ids + " ) ";
  }
  log_list(emulators_list ? ListKind::Emulators : ListKind::Games, changed);
  return changed == original
             ? where
             : mono_string_new(mono_domain_get(), changed.c_str());
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
  for (TabPanel &tab : s_tab_panels)
    keep(tab.handle, nullptr);
  s_tab_panels.clear();
  MonoObject *child = get(switcher, "get_FirstChild");
  for (int guard = 0; child && guard < 8; ++guard) {
    TabPanel panel;
    panel.handle = mono_gchandle_new_weakref(child, 0);
    panel.name = text_of(get(child, "get_Name"));
    s_tab_panels.push_back(panel);
    child = get(child, "get_NextSibling");
  }
  LOG_INFO("[HOMETABS] tab bar found, tabs=%zu", s_tab_panels.size());
  return !s_tab_panels.empty();
}

int index_of(StockTab tab) { return tab == StockTab::Games ? 0 : 1; }

/* Home's stock tab a panel of the bar is, by the name Home gives it. */
bool stock_tab_of(const std::string &panel_name, StockTab *tab) {
  if (panel_name == "space-switcher-game")
    *tab = StockTab::Games;
  else if (panel_name == "space-switcher-media")
    *tab = StockTab::Media;
  else
    return false;
  return true;
}

/* A tab is a panel sized by React for its caption as React knows it, with
   the caption label inside; the focus frame is drawn around the panel.
   Home's script gives React the captions the tabs show (the glyph, the
   user's name: ScriptCaption), so React's layout is normally right and
   nothing is moved; React lays the bar out again on every page switch, and
   a correction here would show a frame late. Should a label still be wider
   than its panel (no script, a language the text hook dresses on its own),
   the panel is grown to it and the following ones are pushed right; a stock
   tab under a name of the user's (`captions`, by StockTab) is fitted to that
   name, narrower or not. Whatever is not our own last value is taken as
   React's new stock geometry. */
void fit_tab_panels(const std::string (&captions)[2]) {
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
    StockTab stock = StockTab::Games;
    const bool renamed = stock_tab_of(tab.name, &stock) &&
                         !captions[index_of(stock)].empty() &&
                         label_width > 8.0f;
    const float wanted_width =
        std::max(renamed ? 0.0f : tab.stock_width,
                 label_width + 2.0f * label_x);
    const float wanted_x = tab.stock_x + shift;
    if (std::fabs(x - wanted_x) > 0.5f)
      set_float(panel, "set_X", wanted_x);
    if (std::fabs(width - wanted_width) > 0.5f)
      set_float(panel, "set_Width", wanted_width);
    static int fitted_logs = 0;
    if (renamed && std::fabs(tab.written_width - wanted_width) > 0.5f &&
        fitted_logs < 8) {
      ++fitted_logs;
      LOG_INFO("[HOMETABS] %s fitted: stock x=%.0f w=%.0f, label x=%.0f "
               "w=%.0f, now x=%.0f w=%.0f",
               tab.name.c_str(), tab.stock_x, tab.stock_width, label_x,
               label_width, wanted_x, wanted_width);
    }
    tab.written_x = wanted_x;
    tab.written_width = wanted_width;
    shift += wanted_width - tab.stock_width;
  }
}

/* ---- names of the user's for Games and Media --------------------------- */

std::string &name_setting(StockTab tab) {
  return tab == StockTab::Games ? g_settings.home_tab_games_name
                                : g_settings.home_tab_media_name;
}

/* What a stock tab is to show: the user's name for it, with its glyph (see
   Caption); empty while it keeps the system's caption. */
std::string caption_of(StockTab tab) {
  const std::string name = HomeBookmark::CleanName(name_setting(tab));
  if (name.empty())
    return {};
  return Caption(tab == StockTab::Games ? kGamesGlyph : kMediaGlyph, name);
}

bool is_label(MonoObject *object) {
  for (MonoClass *k = s_label_class ? mono_object_get_class(object) : nullptr;
       k; k = mono_class_get_parent(k))
    if (k == s_label_class)
      return true;
  return false;
}

/* Where a label stands: in the bar's Games or Media panel (Home names them
   "space-switcher-<space id>"), elsewhere, or in no tree yet. */
enum class Place { Unattached, Games, Media, Elsewhere };

Place place_of(MonoObject *label) {
  MonoObject *node = get(label, "get_Parent");
  for (int depth = 0; depth < 6; ++depth) {
    if (!node)
      return Place::Unattached;
    StockTab tab = StockTab::Games;
    const std::string name = text_of(get(node, "get_Name"));
    if (stock_tab_of(name, &tab))
      return tab == StockTab::Games ? Place::Games : Place::Media;
    /* another tab, or the bar itself */
    if (name.compare(0, 14, "space-switcher") == 0)
      return Place::Elsewhere;
    node = get(node, "get_Parent");
  }
  return Place::Elsewhere;
}

bool named_tab(MonoObject *label, StockTab *tab) {
  for (const TabLabel &entry : s_named_labels)
    if (target(entry.handle) == label) {
      *tab = entry.tab;
      return true;
    }
  return false;
}

void remember_named(MonoObject *label, StockTab tab) {
  StockTab known = tab;
  if (named_tab(label, &known))
    return;
  /* the labels of a Home that is gone first, then the oldest if still full */
  for (size_t i = 0; i < s_named_labels.size();) {
    MonoObject *other = target(s_named_labels[i].handle);
    if (other && !ShellClock::IsWidgetDisposed(other)) {
      ++i;
      continue;
    }
    mono_gchandle_free(s_named_labels[i].handle);
    s_named_labels.erase(s_named_labels.begin() + static_cast<long>(i));
  }
  if (s_named_labels.size() >= kMaxNamedLabels) {
    mono_gchandle_free(s_named_labels.front().handle);
    s_named_labels.erase(s_named_labels.begin());
  }
  TabLabel entry;
  entry.handle = mono_gchandle_new_weakref(label, 0);
  entry.tab = tab;
  if (entry.handle)
    s_named_labels.push_back(entry);
}

/* The label shows `caption`, sized to it and never cut short (the text hook
   lays the stock caption out the same way). Our text goes through that
   hook too, which leaves a caption with a glyph in front as it is. */
void show_caption(MonoObject *label, StockTab tab,
                  const std::string &caption) {
  if (caption.empty())
    return;
  remember_named(label, tab);
  if (text_of(get(label, "get_Text")) == caption)
    return;
  bool fit = true;
  set(label, "set_FitWidthToText", &fit);
  int32_t no_trimming = 0;
  set(label, "set_TextTrimming", &no_trimming);
  set(label, "set_Text", mono_string_new(mono_domain_get(), caption.c_str()));
  static int shown_logs = 0;
  if (shown_logs++ < 32)
    LOG_INFO("[HOMETABS] %s tab shows '%s' label=%p",
             tab == StockTab::Games ? "games" : "media",
             HomeBookmark::CleanName(name_setting(tab)).c_str(),
             static_cast<void *>(label));
}

/* The labels the text hook handed over: the user's name goes into the ones
   in the tab bar, the moment they are put in it. */
void name_tab_labels(const std::string (&captions)[2]) {
  for (size_t i = 0; i < s_tab_labels.size();) {
    TabLabel &entry = s_tab_labels[i];
    MonoObject *label = target(entry.handle);
    const Place place = label && !ShellClock::IsWidgetDisposed(label)
                            ? place_of(label)
                            : Place::Elsewhere;
    if (place == Place::Unattached && ++entry.frames < kTabLabelFrames) {
      ++i;
      continue;
    }
    if (place == Place::Games || place == Place::Media) {
      const StockTab tab =
          place == Place::Games ? StockTab::Games : StockTab::Media;
      show_caption(label, tab, captions[index_of(tab)]);
    } else if (label) {
      static int left_alone = 0;
      if (left_alone++ < 16)
        LOG_INFO("[HOMETABS] %s label=%p is not in the tab bar (%s), left "
                 "as it is",
                 entry.tab == StockTab::Games ? "games" : "media",
                 static_cast<void *>(label),
                 place == Place::Unattached ? "in no tree" : "elsewhere");
    }
    mono_gchandle_free(entry.handle);
    s_tab_labels.erase(s_tab_labels.begin() + static_cast<long>(i));
  }
}

/* The same for the bar's own labels, twice a second, should one have been
   missed or set back: a language the text hook has no word for. */
void name_tab_panels(const std::string (&captions)[2]) {
  if (--s_panel_frames > 0)
    return;
  s_panel_frames = 30;
  for (const TabPanel &tab : s_tab_panels) {
    StockTab stock = StockTab::Games;
    if (!stock_tab_of(tab.name, &stock) || captions[index_of(stock)].empty())
      continue;
    MonoObject *panel = target(tab.handle);
    MonoObject *label = panel ? get(panel, "get_FirstChild") : nullptr;
    if (label && is_label(label))
      show_caption(label, stock, captions[index_of(stock)]);
  }
}

void open_rename_prompt(StockTab tab) {
  const std::string name = HomeBookmark::CleanName(name_setting(tab));
  SystemDialogs::PromptOptions options;
  options.title = toolbox_i18n::tr(tab == StockTab::Games
                                       ? "home_tabs.rename_games_title"
                                       : "home_tabs.rename_media_title");
  options.message = toolbox_i18n::tr("home_tabs.name_message");
  /* The user's name, to be changed; none for the system's caption, which
     would only have to be deleted (and the text hook would put the tab's
     glyph in front of it in the field). */
  options.text = name;
  options.positive = toolbox_i18n::tr("home_tabs.save");
  options.negative = toolbox_i18n::tr("home_tabs.cancel");
  std::string failure;
  if (!SystemDialogs::ShowPrompt(options, &failure)) {
    LOG_WARN("[HOMETABS] prompt: %s", failure.c_str());
    return;
  }
  s_renaming = index_of(tab);
}

void renamed(bool confirmed, const std::string &typed) {
  const StockTab tab = s_renaming == 0 ? StockTab::Games : StockTab::Media;
  s_renaming = -1;
  if (!confirmed)
    return;
  std::string name = HomeBookmark::CleanName(typed);
  /* The system's own caption is not kept as a name: without one the tab
     follows the console's language. */
  if (name == s_stock_names[index_of(tab)])
    name.clear();
  std::string &setting = name_setting(tab);
  if (name == HomeBookmark::CleanName(setting))
    return;
  setting = name;
  if (!SaveSettings())
    LOG_WARN("[HOMETABS] the tab's name was not saved");
  LOG_INFO("[HOMETABS] %s tab %s", tab == StockTab::Games ? "games" : "media",
           name.empty() ? "back to the system's name" : "renamed");
  /* the name goes in as Home builds its tab bar */
  shellui_request_home_reload();
}

void remember_tab_names() {
  for (const StockTab tab : {StockTab::Games, StockTab::Media}) {
    std::string name = HomeBookmark::CleanName(name_setting(tab));
    if (name.empty())
      name = s_stock_names[index_of(tab)];
    std::lock_guard<std::mutex> lock(s_names_mutex);
    if (s_tab_names[index_of(tab)] != name)
      s_tab_names[index_of(tab)] = name;
  }
}

/* The rename prompt asked for from the Shell Menu, and its answer. A prompt
   can go without one (closed by the system, as when a game starts over
   Home): asking again replaces it. */
void rename_tick() {
  int request = -1;
  {
    std::lock_guard<std::mutex> lock(s_request_mutex);
    std::swap(request, s_rename_request);
  }
  if (request >= 0) {
    if (s_renaming >= 0)
      LOG_INFO("[HOMETABS] the last prompt never answered; asking again");
    s_renaming = -1;
    open_rename_prompt(request == 0 ? StockTab::Games : StockTab::Media);
    return;
  }
  if (s_renaming >= 0) {
    bool confirmed = false;
    std::string typed;
    if (SystemDialogs::PollPrompt(&confirmed, &typed))
      renamed(confirmed, typed);
  }
}

} // namespace

bool HasEmulators() { return !s_filter_sql.empty(); }

bool IconsShown() { return g_settings.home_tab_icons; }

std::string Caption(const char *glyph, const std::string &name) {
  /* a glyph of the user's leads: U+E000-U+F8FF, EE 80 80 to EF A3 BF */
  const unsigned char lead = name.size() >= 3 ? name[0] : 0;
  const unsigned char second = name.size() >= 3 ? name[1] : 0;
  const bool own = lead == 0xee || (lead == 0xef && second <= 0xa3);
  if (!IconsShown() || own)
    return name;
  return std::string(glyph) + " " + name;
}

bool Ready() { return s_build_where_original != nullptr; }

void Install(MonoImage *pui) {
  if (!pui || s_pui)
    return;
  s_pui = pui;
  s_label_class = mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Label");
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

void SetEmulatorsTabShown(bool shown) {
  s_emulators_tab = shown;
  LOG_INFO("[HOMETABS] Home starts %s the Emulators tab",
           shown ? "with" : "without");
}

std::string TabLabelText(MonoObject *label, StockTab tab,
                         const std::string &stock) {
  if (!label)
    return {};
  s_stock_names[index_of(tab)] = stock;
  StockTab named = tab;
  if (named_tab(label, &named))
    return caption_of(named);
  /* not known yet: watched until it is put in a tree */
  if (caption_of(tab).empty() || s_tab_labels.size() >= kMaxTabLabels)
    return {};
  for (const TabLabel &entry : s_tab_labels)
    if (target(entry.handle) == label)
      return {};
  TabLabel entry;
  entry.handle = mono_gchandle_new_weakref(label, 0);
  entry.tab = tab;
  if (entry.handle)
    s_tab_labels.push_back(entry);
  return {};
}

void ScriptCaption(StockTab tab, std::string *name, std::string *glyph) {
  *name = caption_of(tab);
  *glyph = IconsShown() ? (tab == StockTab::Games ? kGamesGlyph : kMediaGlyph)
                        : "";
}

std::string TabName(StockTab tab) {
  {
    std::lock_guard<std::mutex> lock(s_names_mutex);
    if (!s_tab_names[index_of(tab)].empty())
      return s_tab_names[index_of(tab)];
  }
  return toolbox_i18n::tr(tab == StockTab::Games ? "home_tabs.games"
                                                 : "home_tabs.media");
}

void RequestRename(StockTab tab) {
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_rename_request = index_of(tab);
}

void RestoreNames() {
  const bool named = !g_settings.home_tab_games_name.empty() ||
                     !g_settings.home_tab_media_name.empty();
  notify("%s", toolbox_i18n::tr("home_tabs.names_restored"));
  if (!named)
    return;
  g_settings.home_tab_games_name.clear();
  g_settings.home_tab_media_name.clear();
  if (!SaveSettings())
    LOG_WARN("[HOMETABS] the system's names were not saved");
  LOG_INFO("[HOMETABS] games and media tabs back to the system's names");
  shellui_request_home_reload();
}

void Tick() {
  if (!s_pui)
    return;
  rename_tick();
  remember_tab_names();
  const std::string captions[2] = {caption_of(StockTab::Games),
                                   caption_of(StockTab::Media)};
  name_tab_labels(captions);
  MonoObject *switcher = target(s_switcher);
  if (!switcher || ShellClock::IsWidgetDisposed(switcher)) {
    if (--s_search_countdown > 0)
      return;
    s_search_countdown = 90;
    if (!resolve())
      return;
  }
  name_tab_panels(captions);
  fit_tab_panels(captions);

  /* Nothing to show in the Emulators tab: say so, once per arrival on the
     tab. The list is read again first, in case an emulator has been
     installed since the last look. */
  MonoObject *emulators_tab = nullptr;
  for (const TabPanel &tab : s_tab_panels) {
    MonoObject *panel = target(tab.handle);
    if (panel && tab.name == "space-switcher-emulators")
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
