/* Copyright (C) 2026 AniketosHEN — glyphs on Home's Games and Media tab labels
   and the focus frame around them. */

#include "aniketos_theme/tiles/space_switcher_labels.hpp"
#include "home_tiles_internal.hpp"

#include "aniketos_shell/home/home_bookmark.hpp"
#include "aniketos_shell/home/home_tabs.hpp"
#include "aniketos_theme/mono_text.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace AniketosTheme::SpaceSwitcherLabels {
namespace {

using namespace HomeTiles;

constexpr const char *kSharedFocusLayerId = "experience-switcher-focus-layer";
constexpr int kGamesTab = 0;
constexpr int kMediaTab = 1;

struct TabLabelLayout {
  uint32_t handle = 0;
  float stock_x = 0.0f;
  float stock_width = 0.0f;
};

struct TabFocusPadding {
  uint32_t handle = 0;
  int stock_left = 0;
  int stock_right = 0;
  int applied_left = -1;
  int applied_right = -1;
};

std::unordered_set<std::string> s_logged_focus_layers;
std::vector<uint32_t> s_root_handles;
std::vector<uint32_t> s_focus_handles;
std::vector<uint32_t> s_label_handles;
std::vector<TabLabelLayout> s_label_layouts;
std::vector<TabFocusPadding> s_focus_paddings;

MonoObject *parent_of(MonoObject *object) {
  if (!object || !mono_object_get_class || !mono_class_get_method_from_name ||
      !mono_runtime_invoke)
    return nullptr;
  MonoClass *klass = mono_object_get_class(object);
  if (!klass)
    return nullptr;
  // React/PUI can attach a node through its layout/base relationship while
  // the public Parent is still null.  Resolve semantically and accept the
  // first live relationship instead of binding to one firmware's storage.
  static constexpr const char *kParentGetters[] = {
      "get_Parent", "get_ParentBase", "get_LayoutParent",
      "get_NativeParent"};
  /* Every label text set in ShellUI walks its parents here: the getters are
     looked up once per class. */
  struct Getters {
    MonoClass *klass;
    MonoMethod *methods[std::size(kParentGetters)];
  };
  static Getters cache[32];
  static size_t next = 0;
  Getters *entry = std::find_if(std::begin(cache), std::end(cache),
                                [klass](const Getters &known) {
                                  return known.klass == klass;
                                });
  if (entry == std::end(cache)) {
    entry = &cache[next];
    next = (next + 1) % std::size(cache);
    entry->klass = klass;
    for (size_t i = 0; i < std::size(kParentGetters); ++i)
      entry->methods[i] =
          mono_class_get_method_from_name(klass, kParentGetters[i], 0);
  }
  for (MonoMethod *getter : entry->methods) {
    if (!getter)
      continue;
    MonoObject *exception = nullptr;
    MonoObject *parent =
        mono_runtime_invoke(getter, object, nullptr, &exception);
    if (!exception && parent)
      return parent;
  }
  return nullptr;
}

bool derives_from(MonoObject *object, MonoClass *base) {
  if (!object || !base || !mono_object_get_class || !mono_class_get_parent)
    return false;
  for (MonoClass *klass = mono_object_get_class(object); klass;
       klass = mono_class_get_parent(klass)) {
    if (klass == base)
      return true;
  }
  return false;
}

bool is_home_space_switcher(const std::string &test_id) {
  return test_id.find(kSharedFocusLayerId) != std::string::npos ||
         test_id.find("space-switcher-game") != std::string::npos ||
         test_id.find("space-switcher-media") != std::string::npos;
}

void remember_space_switcher_root(MonoObject *widget) {
  if (!widget)
    return;
  if (!find_live(s_focus_handles, widget))
    if (const uint32_t handle = weak_handle(widget))
      s_focus_handles.push_back(handle);
  MonoObject *root = parent_of(widget);
  if (!root || find_live(s_root_handles, root))
    return;
  const uint32_t handle = weak_handle(root);
  if (!handle)
    return;
  s_root_handles.push_back(handle);
  MonoClass *root_class = mono_object_get_class ? mono_object_get_class(root)
                                                 : nullptr;
  const char *class_name = root_class && mono_class_get_name
                               ? mono_class_get_name(root_class)
                               : "?";
  LOG_INFO("[HOME-SPACES:NATIVE] owner class='%s' root=%p count=%zu",
           class_name ? class_name : "?", static_cast<void *>(root),
           s_root_handles.size());
}

/* The label's stock tab, once bound, or -1. */
int bind_space_switcher_label(MonoObject *label, const std::string &text) {
  if (!label || !s_pui_widget_class || s_focus_handles.empty())
    return -1;
  if (const uint32_t *known = find_live(s_label_handles, label))
    return static_cast<int>(known - s_label_handles.data());
  // This fallback is called only after the ancestor resolves to the native
  // shared space-switcher focus layer. Do not use pixel dimensions here:
  // those vary by HomeUI layout/firmware. Its first two text children are the
  // stock Games and Media tabs, in that order.
  if (s_label_handles.size() >= 2)
    return -1;
  const uint32_t handle = weak_handle(label);
  if (!handle)
    return -1;
  s_label_handles.push_back(handle);
  const int index = static_cast<int>(s_label_handles.size()) - 1;
  LOG_INFO("[HOME-SPACES:NATIVE] bound top-tab index=%d text='%s'", index,
           text.c_str());
  return index;
}

std::string space_switcher_ancestor(MonoObject *object) {
  for (unsigned depth = 0; object && depth < 20; ++depth) {
    if (find_live(s_root_handles, object))
      return kSharedFocusLayerId;
    if (derives_from(object, s_pui_widget_class) && s_get_test_id) {
      /* every label set in ShellUI walks up through here: the id is only
         copied when it can be a switcher's (all three carry "switcher") */
      MonoString *managed_id = s_get_test_id(object);
      if (MonoText::ContainsAscii(managed_id, "switcher")) {
        const std::string test_id = Mono_to_String(managed_id);
        if (is_home_space_switcher(test_id))
          return test_id;
      }
    }
    MonoObject *parent = parent_of(object);
    if (!parent || parent == object)
      break;
    object = parent;
  }
  return {};
}

int localized_space_tab(const std::string &text) {
  // The HomeUI space labels are localized by the system, not by Aniketos.
  // Resolve exact visible labels; never infer a tab from its position,
  // dimensions, firmware version, or a guessed native address. These labels
  // are unique category names in the ShellUI header, unlike app titles.
  static constexpr const char *kGamesLabels[] = {
      "Games", "Gry", "Spiele", "Jeux", "Juegos", "Giochi", "Jogos",
      "Игры", "ゲーム", "遊戲", "游戏", "게임", "Oyunlar", "الألعاب"};
  static constexpr const char *kMediaLabels[] = {
      "Media", "Multimedia", "Média", "Médias", "Medien", "Multimédia",
      "Мультимедиа", "メディア", "媒體", "媒体", "미디어", "Medya",
      "الوسائط"};
  for (const char *label : kGamesLabels)
    if (text == label)
      return kGamesTab;
  for (const char *label : kMediaLabels)
    if (text == label)
      return kMediaTab;
  return -1;
}

/* The tab of a stock caption with our glyph in front, or -1. */
int dressed_space_tab(const std::string &text) {
  const bool games = text.starts_with(HomeTabs::kGamesGlyph);
  if (!games && !text.starts_with(HomeTabs::kMediaGlyph))
    return -1;
  size_t start = games ? sizeof(HomeTabs::kGamesGlyph) - 1
                       : sizeof(HomeTabs::kMediaGlyph) - 1;
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
    ++start;
  const int tab = games ? kGamesTab : kMediaTab;
  return localized_space_tab(text.substr(start)) == tab ? tab : -1;
}

void remember_space_tab_label_layout(MonoObject *label) {
  if (!label || !s_pui_widget_class)
    return;
  const float x = Get_Property<float>(s_pui_widget_class, label, "X");
  const float width = Get_Property<float>(s_pui_widget_class, label, "Width");
  if (TabLabelLayout *layout = find_live(s_label_layouts, label)) {
    // Called only while the label still contains the stock, undecorated
    // text. Refresh the baseline if HomeUI rebuilds or relocalizes it.
    layout->stock_x = x;
    layout->stock_width = width;
    return;
  }
  if (s_label_layouts.size() >= 4)
    return;
  if (const uint32_t handle = weak_handle(label))
    s_label_layouts.push_back({handle, x, width});
}

} // namespace

void RememberFocusLayer(MonoObject *widget, const std::string &test_id) {
  if (!is_home_space_switcher(test_id))
    return;
  if (s_logged_focus_layers.insert(test_id).second)
    LOG_INFO("[HOME-SPACES:NATIVE] focus-layer test_id='%s' widget=%p",
             test_id.c_str(), static_cast<void *>(widget));
  remember_space_switcher_root(widget);
}

void RememberFocusPadding(MonoObject *settings, const std::string &test_id) {
  if (test_id != kSharedFocusLayerId || !settings || !s_focus_settings_class ||
      !mono_class_get_property_from_name ||
      !mono_class_get_property_from_name(s_focus_settings_class,
                                         "FocusImagePaddingLeft") ||
      !mono_class_get_property_from_name(s_focus_settings_class,
                                         "FocusImagePaddingRight") ||
      find_live(s_focus_paddings, settings))
    return;
  const uint32_t handle = weak_handle(settings);
  if (!handle)
    return;
  const int left = Get_Property<int>(s_focus_settings_class, settings,
                                     "FocusImagePaddingLeft");
  const int right = Get_Property<int>(s_focus_settings_class, settings,
                                      "FocusImagePaddingRight");
  s_focus_paddings.push_back({handle, left, right});
  LOG_INFO("[HOME-SPACES:FOCUS] padding baseline settings=%p left=%d right=%d",
           static_cast<void *>(settings), left, right);
}

MonoString *Decorate(MonoObject *label, MonoString *text) {
  if (!label || !text)
    return text;
  const std::string original = Mono_to_String(text);
  /* Already carrying a glyph: ours, or the Emulators caption that stands in
     on the Games tab while that tab is showing. */
  if (original.empty() || original.starts_with(HomeTabs::kGamesGlyph) ||
      original.starts_with(HomeTabs::kMediaGlyph) ||
      original.starts_with(HomeTabs::kEmulatorsGlyph))
    return text;

  const std::string test_id = space_switcher_ancestor(label);
  int tab = -1;
  if (test_id.find("space-switcher-game") != std::string::npos) {
    tab = kGamesTab;
  } else if (test_id.find("space-switcher-media") != std::string::npos) {
    tab = kMediaTab;
  } else if (test_id == kSharedFocusLayerId) {
    // Some HomeUI builds expose only the shared switcher focus-layer ID.
    // Bind its two child labels at runtime; stock order is Games then Media.
    tab = bind_space_switcher_label(label, original);
  }
  if (tab < 0) {
    tab = localized_space_tab(original);
    /* the bookmark's caption is the user's name as typed, which may be a
       stock tab's word ("Games", "Gry", "Media"): it is not that tab. The
       words first: every label set in ShellUI comes here, and the Home check
       walks the label's parents. */
    if (tab < 0 || original == HomeBookmark::Name() ||
        !HomeTabs::IsHomeTabLabel(label))
      return text;
    LOG_INFO("[HOME-SPACES:NATIVE] semantic text-label tab=%s text='%s' "
             "label=%p",
             tab == kGamesTab ? "games" : "media", original.c_str(),
             static_cast<void *>(label));
  }

  remember_space_tab_label_layout(label);
  /* A name of the user's for the tab, in a label found in Home's tab bar
     (home_tabs; Search shows the same words): a new label gets it as soon
     as it is found there, a known one at once. */
  const bool games = tab == kGamesTab;
  const std::string named = HomeTabs::TabLabelText(
      label, games ? HomeTabs::StockTab::Games : HomeTabs::StockTab::Media,
      original);
  /* tab icons switched off: the system's caption as it is */
  if (named.empty() && !HomeTabs::IconsShown())
    return text;
  const char *symbol = games ? HomeTabs::kGamesGlyph : HomeTabs::kMediaGlyph;
  const std::string caption =
      named.empty() ? HomeTabs::Caption(symbol, original) : named;
  MonoString *replacement = Root_Domain && mono_string_new
                                ? mono_string_new(Root_Domain, caption.c_str())
                                : nullptr;
  if (!replacement)
    return text;
  LOG_INFO("[HOME-SPACES:NATIVE] decorated match='%s' symbol=U+%04X "
           "text='%s' label=%p",
           test_id.c_str(), games ? 0xefceu : 0xf871u, original.c_str(),
           static_cast<void *>(label));
  return replacement;
}

bool IsTabLabel(const std::string &text) {
  /* without the icons there is nothing to fit or put back on the label */
  return HomeTabs::IconsShown() && localized_space_tab(text) >= 0;
}

bool IsMediaTabLabel(const std::string &text) {
  return localized_space_tab(text) == kMediaTab ||
         dressed_space_tab(text) == kMediaTab;
}

void UpdateFocusBounds(MonoObject *label, const std::string &text) {
  if (!label || !s_pui_widget_class || !s_focus_settings_class ||
      s_label_layouts.empty() || s_focus_paddings.empty() ||
      dressed_space_tab(text) < 0 || !find_live(s_label_layouts, label))
    return;

  int extra_left = 0;
  int extra_right = 0;
  visit_live(s_label_layouts, [&](const TabLabelLayout &layout,
                                  MonoObject *target) {
    const float x = Get_Property<float>(s_pui_widget_class, target, "X");
    const float width =
        Get_Property<float>(s_pui_widget_class, target, "Width");
    const float stock_right = layout.stock_x + layout.stock_width;
    const float left_growth = std::clamp(layout.stock_x - x, 0.0f, 512.0f);
    const float right_growth =
        std::clamp(x + width - stock_right, 0.0f, 512.0f);
    extra_left = std::max(extra_left, static_cast<int>(std::ceil(left_growth)));
    extra_right =
        std::max(extra_right, static_cast<int>(std::ceil(right_growth)));
    return false;
  });

  visit_live(s_focus_paddings, [&](TabFocusPadding &padding,
                                   MonoObject *settings) {
    const int left = padding.stock_left + extra_left;
    const int right = padding.stock_right + extra_right;
    if (left != padding.applied_left || right != padding.applied_right) {
      Set_Property(s_focus_settings_class, settings, "FocusImagePaddingLeft",
                   left);
      Set_Property(s_focus_settings_class, settings, "FocusImagePaddingRight",
                   right);
      padding.applied_left = left;
      padding.applied_right = right;
      LOG_INFO("[HOME-SPACES:FOCUS] symbol bounds label=%p settings=%p "
               "growth-left=%d growth-right=%d padding-left=%d "
               "padding-right=%d",
               static_cast<void *>(label), static_cast<void *>(settings),
               extra_left, extra_right, left, right);
    }
    return false;
  });
}

} // namespace AniketosTheme::SpaceSwitcherLabels
