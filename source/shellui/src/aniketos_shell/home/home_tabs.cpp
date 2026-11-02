#include "aniketos_shell/home/home_tabs.hpp"

#include "aniketos_shell/home/home_bookmark.hpp"
#include "aniketos_shell/home/home_rows.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "hooked_funcs.hpp"
#include "toolbox_i18n.hpp"

#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

extern onion::Settings g_settings;

namespace HomeTabs {
namespace {

namespace Reflect = AniketosUI::Reflect;

bool s_installed = false;
uint32_t s_switcher = 0; /* React's 'space-switcher' focus layer */
int s_search_countdown = 0;

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

MonoObject *get(MonoObject *object, const char *getter) {
  MonoObject *value = nullptr;
  return Reflect::Call(object, getter, 0, nullptr, &value) ? value : nullptr;
}

void set(MonoObject *object, const char *setter, void *value) {
  void *args[1] = {value};
  Reflect::Call(object, setter, 1, args);
}

void set_float(MonoObject *object, const char *setter, float value) {
  set(object, setter, &value);
}

std::string text_property(MonoObject *object, const char *getter) {
  return Mono_to_String(reinterpret_cast<MonoString *>(get(object, getter)));
}

MonoObject *find_switcher(MonoObject *widget, int depth, int &budget) {
  if (!widget || depth > 10 || budget-- <= 0)
    return nullptr;
  if (text_property(widget, "get_Name") == "space-switcher")
    return widget;
  MonoObject *child = get(widget, "get_FirstChild");
  for (int guard = 0; child && guard < 32; ++guard) {
    if (MonoObject *found = find_switcher(child, depth + 1, budget))
      return found;
    child = get(child, "get_NextSibling");
  }
  return nullptr;
}

bool find_tab_bar() {
  MonoObject *root = AniketosUI::HomeScene::Root();
  if (!root)
    return false;
  int budget = 300;
  MonoObject *switcher = find_switcher(root, 0, budget);
  if (!switcher)
    return false; /* the clock's scene is not Home (Control Center...) */
  Reflect::Hold(s_switcher, switcher);
  for (TabPanel &tab : s_tab_panels)
    Reflect::Release(tab.handle);
  s_tab_panels.clear();
  MonoObject *child = get(switcher, "get_FirstChild");
  for (int guard = 0; child && guard < 8; ++guard) {
    TabPanel panel;
    Reflect::Hold(panel.handle, child);
    panel.name = text_property(child, "get_Name");
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
    MonoObject *panel = Reflect::Target(tab.handle);
    if (!panel)
      continue;
    const float x = Reflect::FloatProperty(panel, "get_X");
    const float width = Reflect::FloatProperty(panel, "get_Width");
    if (std::fabs(x - tab.written_x) > 0.5f)
      tab.stock_x = x;
    if (std::fabs(width - tab.written_width) > 0.5f)
      tab.stock_width = width;
    MonoObject *label = get(panel, "get_FirstChild");
    const float label_x = Reflect::FloatProperty(label, "get_X");
    float label_width = Reflect::FloatProperty(label, "get_Width");
    const float text_width = Reflect::FloatProperty(label, "GetTextWidth");
    if (text_width > 8.0f)
      label_width = text_width;
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
    const std::string name = text_property(node, "get_Name");
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
    if (Reflect::Target(entry.handle) == label) {
      *tab = entry.tab;
      return true;
    }
  return false;
}

void watch_label(std::vector<TabLabel> &labels, MonoObject *label,
                 StockTab tab) {
  TabLabel entry;
  Reflect::Hold(entry.handle, label);
  entry.tab = tab;
  if (entry.handle)
    labels.push_back(entry);
}

void remember_named(MonoObject *label, StockTab tab) {
  StockTab known = tab;
  if (named_tab(label, &known))
    return;
  /* the labels of a Home that is gone first, then the oldest if still full */
  for (size_t i = 0; i < s_named_labels.size();) {
    MonoObject *other = Reflect::Target(s_named_labels[i].handle);
    if (other && !AniketosUI::HomeScene::IsDisposed(other)) {
      ++i;
      continue;
    }
    Reflect::Release(s_named_labels[i].handle);
    s_named_labels.erase(s_named_labels.begin() + static_cast<long>(i));
  }
  if (s_named_labels.size() >= kMaxNamedLabels) {
    Reflect::Release(s_named_labels.front().handle);
    s_named_labels.erase(s_named_labels.begin());
  }
  watch_label(s_named_labels, label, tab);
}

/* The label shows `caption`, sized to it and never cut short (the text hook
   lays the stock caption out the same way). Our text goes through that
   hook too, which leaves a caption with a glyph in front as it is. */
void show_caption(MonoObject *label, StockTab tab,
                  const std::string &caption) {
  if (caption.empty())
    return;
  remember_named(label, tab);
  if (text_property(label, "get_Text") == caption)
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
    MonoObject *label = Reflect::Target(entry.handle);
    const Place place = label && !AniketosUI::HomeScene::IsDisposed(label)
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
    Reflect::Release(entry.handle);
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
    MonoObject *panel = Reflect::Target(tab.handle);
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

void finish_rename(bool confirmed, const std::string &typed) {
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
      finish_rename(confirmed, typed);
  }
}

/* Nothing to show in the Emulators tab: say so, once per arrival on the tab. */
void emulators_tab_tick() {
  MonoObject *emulators_tab = nullptr;
  for (const TabPanel &tab : s_tab_panels) {
    MonoObject *panel = Reflect::Target(tab.handle);
    if (panel && tab.name == "space-switcher-emulators")
      emulators_tab = panel;
  }
  static bool was_on_emulators = false;
  MonoObject *focused = get(emulators_tab, "get_Focused");
  const bool on_emulators =
      focused && *static_cast<bool *>(mono_object_unbox(focused));
  if (on_emulators && !was_on_emulators && !HomeRows::FindEmulators()) {
    /* A notification, not a dialog: the tab is on the way to the icons on the
       right of the bar, and a dialog that took the focus back to Games made
       them unreachable. Not more often than every half minute. */
    static time_t last_notice = 0;
    const time_t now = time(nullptr);
    if (now - last_notice >= 30) {
      last_notice = now;
      notify("%s. %s", toolbox_i18n::tr("home_tabs.no_emulators.title"),
             toolbox_i18n::tr("home_tabs.no_emulators.body"));
      LOG_INFO("[HOMETABS] no emulators installed: notification sent");
    }
  }
  was_on_emulators = on_emulators;
}

bool is_under(MonoObject *node, MonoObject *ancestor) {
  for (; node; node = get(node, "get_Parent"))
    if (node == ancestor)
      return true;
  return false;
}

} // namespace

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

void Install(MonoImage *pui) {
  if (!pui || s_installed)
    return;
  s_installed = true;
  s_label_class = mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Label");
  HomeRows::Install();
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
    if (Reflect::Target(entry.handle) == label)
      return {};
  watch_label(s_tab_labels, label, tab);
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

bool IsHomeTabLabel(MonoObject *label) {
  if (!label)
    return false;
  MonoObject *home = AniketosUI::HomeScene::Root();
  if (!home || AniketosUI::HomeScene::IsDisposed(home))
    return false;
  if (!is_under(label, home))
    return false;
  MonoObject *switcher = Reflect::Target(s_switcher);
  if (switcher && !AniketosUI::HomeScene::IsDisposed(switcher)) {
    if (is_under(label, switcher))
      return true;
    for (const TabPanel &tab : s_tab_panels) {
      MonoObject *panel = Reflect::Target(tab.handle);
      if (panel && get(panel, "get_FirstChild") == label)
        return true;
    }
    return false;
  }
  return true;
}

void Tick() {
  if (!s_installed)
    return;
  rename_tick();
  remember_tab_names();
  const std::string captions[2] = {caption_of(StockTab::Games),
                                   caption_of(StockTab::Media)};
  name_tab_labels(captions);
  MonoObject *switcher = Reflect::Target(s_switcher);
  if (!switcher || AniketosUI::HomeScene::IsDisposed(switcher)) {
    if (--s_search_countdown > 0)
      return;
    s_search_countdown = 90;
    if (!find_tab_bar())
      return;
  }
  name_tab_panels(captions);
  fit_tab_panels(captions);
  emulators_tab_tick();
}

} // namespace HomeTabs
