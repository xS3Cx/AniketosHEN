#include "aniketos_shell/games/cheats_menu.hpp"

#include "aniketos_shell/core/host_scene.hpp"
#include "aniketos_shell/games/cheats_list.hpp"
#include "aniketos_shell/input/pad_reader.hpp"
#include "aniketos_shell/system_ui/native_menu.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "aniketos_ui/pui_primitives.hpp"
#include "hooked_funcs.hpp"
#include "toolbox_i18n.hpp"
#include "ps5/kernel.h"

#include <onion/log.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace AniketosShell::CheatsMenu {
namespace {

using AniketosUI::Color;
using AniketosUI::MonotonicMs;
using CheatsList::Cheat;
using CheatsList::Group;
using CheatsList::JobKind;
using CheatsList::JobResult;
using CheatsList::ToggleRequest;
namespace Pui = AniketosUI::Pui;
namespace Reflect = AniketosUI::Reflect;

/* The classic PS4 "HaxCry" mod menu, measured on its screenshots (a 720p
 * capture) and scaled by 1.5 for the 1920x1080 Game layer: a 243 px wide
 * menu, 31 px header, 24.4 px rows and a 25 px footer there. */
constexpr float kMarginX = 96.0f;
constexpr float kMarginY = 96.0f;
constexpr float kWidth = 365.0f;
constexpr float kTitleHeight = 46.0f;
constexpr float kSubtitleHeight = 22.0f;
constexpr float kRowHeight = 37.0f;
constexpr float kFooterHeight = 37.0f;
constexpr float kEdge = 2.0f;
constexpr float kTextLeft = 6.0f;
constexpr float kTextRight = 10.0f;
constexpr float kValueGap = 24.0f;
constexpr int kTitleFont = 36;
constexpr int kSubtitleFont = 18;
constexpr int kRowFont = 27;
constexpr int kFooterFont = 24;
constexpr int kMaxRows = 14;
constexpr int kTrimEllipsis = 3; /* TextTrimming.EllipsisCharacter */
constexpr int kTrimMarquee = 5;  /* TextTrimming.Marquee */

/* Header and footer of the main menu, olive green; a submenu's header is
   mint. The list panel is black at 81%, the selected row a white bar. */
constexpr Color kMainBar{121 / 255.0f, 157 / 255.0f, 81 / 255.0f, 1.0f};
constexpr Color kSubBar{135 / 255.0f, 222 / 255.0f, 205 / 255.0f, 1.0f};
constexpr Color kBody{0.0f, 0.016f, 0.035f, 0.81f};
constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color kBlack{0.0f, 0.0f, 0.0f, 1.0f};
constexpr Color kTitleOutline{0.0f, 0.0f, 0.0f, 0.85f};
constexpr Color kOff{0.86f, 0.0f, 0.0f, 1.0f};
constexpr Color kOn{0.10f, 0.78f, 0.10f, 1.0f};
constexpr Color kClear{0.0f, 0.0f, 0.0f, 0.0f};

constexpr const char *kRootName = "id_aniketos_cheats_menu";
constexpr const char *kTitleText = "Cheats Menu";
constexpr const char *kSubtitleText = "powered by AniketosHEN";

/* Keeping the controller from the game:
 * libScePad's scePadSetForceIntercepted (the call ShellUI's own
 * Sce.Vsh.ShellUI.PadInternal.NativeCall makes) marks the pad as taken by the
 * system, so the game reads no buttons while the menu is open. ShellUI keeps
 * reading it. Should our own reads come back intercepted too, the menu would
 * be stuck: give the controller back at once and do without. */
using SetForceInterceptedFn = int (*)(bool intercepted);
SetForceInterceptedFn s_set_force_intercepted = nullptr;
bool s_intercept_resolved = false;
bool s_intercept_unusable = false;
bool s_intercepting = false;
/* The menu has closed; the game gets the controller back once every button
   is up, or the one that closed it would reach the game as a new press. */
bool s_release_pending = false;
uint64_t s_release_since_ms = 0;

/* libScePad as ShellUI loaded it, looked up by name through the kernel's
   list of the process's modules (ShellUI may not call dl_get_list itself). */
void *find_pad_symbol(const char *name) {
  uint32_t handle = 0;
  if (kernel_dynlib_handle(getpid(), "libScePad.sprx", &handle) != 0 || !handle) {
    LOG_WARN("[CHEATSMENU] libScePad.sprx not found");
    return nullptr;
  }
  void *symbol =
      reinterpret_cast<void *>(kernel_dynlib_dlsym(getpid(), handle, name));
  LOG_INFO("[CHEATSMENU] libScePad handle=%u %s=%p", handle, name, symbol);
  return symbol;
}

void set_intercept(bool on) {
  if (!s_intercept_resolved) {
    s_intercept_resolved = true;
    s_set_force_intercepted = reinterpret_cast<SetForceInterceptedFn>(
        find_pad_symbol("scePadSetForceIntercepted"));
  }
  if (on && s_intercept_unusable)
    return;
  if (!s_set_force_intercepted || on == s_intercepting)
    return;
  const int result = s_set_force_intercepted(on);
  s_intercepting = on && result == 0;
  LOG_INFO("[CHEATSMENU] force intercepted=%d result=0x%x", on ? 1 : 0,
           static_cast<unsigned>(result));
}

/* Widgets:
 * UI2 only: the Game layer is a UI2 scene. Everything hangs off one panel
 * that is removed again when the menu closes. */
struct Widgets {
  MonoObject *root = nullptr;
  MonoObject *edges[4] = {};
  MonoObject *header = nullptr;
  MonoObject *title = nullptr;
  MonoObject *subtitle = nullptr;
  MonoObject *body = nullptr;
  MonoObject *highlight = nullptr;
  MonoObject *footer = nullptr;
  MonoObject *footer_label = nullptr;
  MonoObject *names[kMaxRows] = {};
  MonoObject *values[kMaxRows] = {};
};

Pui::Runtime s_pui;
Widgets s_widgets;
std::vector<uint32_t> s_handles;
std::vector<MonoObject *> s_objects;

bool retain(MonoObject *object, void *) {
  if (!object || !mono_gchandle_new)
    return false;
  const uint32_t handle = mono_gchandle_new(object, /*pinned=*/1);
  if (!handle)
    return false;
  s_handles.push_back(handle);
  s_objects.push_back(object);
  return true;
}

/* A UI2.UIFont is a struct: keep its box pinned, hand out the value. */
MonoObject *make_font(int size, int weight) {
  MonoClass *klass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "UIFont");
  MonoObject *boxed = klass ? New_Object(klass) : nullptr;
  if (!boxed || !mono_gchandle_new)
    return nullptr;
  const uint32_t handle = mono_gchandle_new(boxed, /*pinned=*/1);
  if (!handle)
    return nullptr;
  s_handles.push_back(handle);
  MonoObject *value = static_cast<MonoObject *>(mono_object_unbox(boxed));
  Invoke<void>(pui_img, klass, value, ".ctor", size, 0, weight);
  return value;
}

MonoObject *make_outline(Color color) {
  MonoClass *klass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2",
                                          "TextShadowSettings");
  MonoObject *shadow = klass ? New_Object(klass) : nullptr;
  if (!shadow)
    return nullptr;
  mono_runtime_object_init(shadow);
  Pui::SetColor(klass, shadow, "Color", color);
  /* BorderStyle with no offset: a stroke around each glyph. */
  Set_Property(klass, shadow, "HorizontalOffset", 0.0f);
  Set_Property(klass, shadow, "VerticalOffset", 0.0f);
  Set_Property(klass, shadow, "Style", 1);
  return retain(shadow, nullptr) ? shadow : nullptr;
}

MonoObject *panel(MonoObject *parent, const char *name) {
  return Pui::CreatePanel(s_pui, parent, name);
}

MonoObject *label(MonoObject *parent, const char *name, MonoObject *font,
                  int horizontal) {
  s_pui.font = font;
  MonoObject *object = Pui::CreateLabel(s_pui, parent, name, horizontal);
  if (object)
    Set_Property(s_pui.label, object, "TextTrimming", kTrimEllipsis);
  return object;
}

void label_color(MonoObject *object, Color color) {
  Pui::SetColor(s_pui.label, object, "TextColor", color);
}

void destroy_widgets() {
  if (s_widgets.root)
    Pui::Remove(s_pui, s_widgets.root);
  for (MonoObject *object : s_objects) {
    Pui::Disown(object);
    Pui::Forget(object);
  }
  if (mono_gchandle_free)
    for (uint32_t handle : s_handles)
      mono_gchandle_free(handle);
  s_handles.clear();
  s_objects.clear();
  s_widgets = {};
}

bool create_widgets(MonoObject *host) {
  if (!Pui::Resolve(s_pui, pui_img, retain, nullptr))
    return false;
  Pui::RemoveNamed(s_pui, host, kRootName); /* left over from a crash */
  MonoObject *title_font = make_font(kTitleFont, 700);
  MonoObject *subtitle_font = make_font(kSubtitleFont, 400);
  MonoObject *row_font = make_font(kRowFont, 400);
  MonoObject *footer_font = make_font(kFooterFont, 400);
  MonoObject *outline = make_outline(kTitleOutline);
  if (!title_font || !subtitle_font || !row_font || !footer_font)
    return false;

  Widgets &w = s_widgets;
  w.root = panel(host, kRootName);
  if (!w.root)
    return false;
  w.body = panel(w.root, "id_cheats_menu_body");
  w.header = panel(w.root, "id_cheats_menu_header");
  w.footer = panel(w.root, "id_cheats_menu_footer");
  w.highlight = panel(w.root, "id_cheats_menu_highlight");
  for (int i = 0; i < 4; ++i)
    w.edges[i] = panel(w.root, "id_cheats_menu_edge");
  w.title = label(w.root, "id_cheats_menu_title", title_font, 1);
  w.subtitle = label(w.root, "id_cheats_menu_subtitle", subtitle_font, 1);
  w.footer_label = label(w.root, "id_cheats_menu_footer_text", footer_font, 1);
  for (int i = 0; i < kMaxRows; ++i) {
    w.names[i] = label(w.root, "id_cheats_menu_row", row_font, 0);
    w.values[i] = label(w.root, "id_cheats_menu_value", row_font, 2);
  }
  if (!w.body || !w.header || !w.footer || !w.highlight || !w.title ||
      !w.subtitle || !w.footer_label)
    return false;
  for (int i = 0; i < kMaxRows; ++i)
    if (!w.names[i] || !w.values[i])
      return false;
  if (outline) {
    Set_Property(s_pui.label, w.title, "EnableThemedShadowColor", false);
    Set_Property_Invoke(s_pui.label, w.title, "TextShadow", outline);
  }
  Pui::PanelColor(s_pui, w.body, kBody);
  Pui::PanelColor(s_pui, w.highlight, kWhite);
  label_color(w.title, kWhite);
  label_color(w.subtitle, kWhite);
  label_color(w.footer_label, kWhite);
  return true;
}

/* UTF-8 aware width estimate of a label's text; PUI's own measuring is
   asynchronous and lags the text it is given. */
float text_width(const std::string &text, float font_size) {
  float em = 0.0f;
  const auto *cursor = reinterpret_cast<const unsigned char *>(text.c_str());
  while (*cursor) {
    uint32_t c = *cursor++;
    int more = (c & 0xe0u) == 0xc0u ? 1 : (c & 0xf0u) == 0xe0u ? 2
                                        : (c & 0xf8u) == 0xf0u ? 3 : 0;
    for (; more > 0 && (*cursor & 0xc0u) == 0x80u; --more)
      c = (c << 6u) | (*cursor++ & 0x3fu);
    if (c >= 0x1100u && c != 0x2605u && (c < 0x2000u || c > 0x2bffu))
      em += 1.0f;
    else if (c == ' ')
      em += 0.28f;
    else if (c < 0x80u && std::strchr("ilIj.,:;'!|", static_cast<int>(c)))
      em += 0.27f;
    else if (c < 0x80u && std::strchr("MW@mw", static_cast<int>(c)))
      em += 0.84f;
    else if (c >= 'A' && c <= 'Z')
      em += 0.66f;
    else
      em += 0.55f;
  }
  return em * font_size;
}

enum class State { Closed, Loading, Open, Native };
/* What a list being fetched is for: the drawn menu, or PUI's own card (over
   the game, or on the Control Center's scene). */
enum class Target { Overlay, NativeGame, NativeScene };

State s_state = State::Closed;
Target s_target = Target::Overlay;
CheatsList::List s_list;
/* With more than one cheat file the first page lists the files and each
   opens as a submenu; with one it lists the cheats straight away. */
int s_group = -1;            /* -1: the file list */
int s_selected = 0;
int s_top = 0;               /* first visible row */
int s_return_selected = 0;   /* selection on the file list */
bool s_toggle_pending = false;
bool s_dirty = true;
int s_trimming[kMaxRows] = {}; /* TextTrimming set on each name label */
/* The scene that had the focus when the menu opened. Another one taking it
   (the Control Center, a system dialog) hides the menu and gives the
   controller to it until it is back. */
uintptr_t s_open_focus = 0;
bool s_hidden = false;
int s_focus_frames = 0;
/* PUI's card: its rows, as (group, cheat) or -1 for the footer row, and the
   scene it was opened on (weak; none over the game). */
std::vector<std::pair<int, int>> s_native_rows;
uint32_t s_native_scene = 0;
std::vector<ToggleRequest> s_toggle_queue;
/* What each switch of the card was last set to, by PUI or by us. */
std::vector<bool> s_native_wanted;

bool on_file_list() { return s_group < 0; }

int row_count() {
  if (on_file_list())
    return static_cast<int>(s_list.groups.size());
  return static_cast<int>(s_list.groups[s_group].cheats.size());
}

std::string footer_text() {
  if (!s_list.authors.empty())
    return "Cheats by " + s_list.authors;
  return s_list.game;
}

/* A name too long for its row is cut with an ellipsis; on the selected row
   it scrolls instead (the label's own marquee). */
void name_trimming(int row, bool scroll) {
  MonoObject *name = s_widgets.names[row];
  const int wanted = scroll ? kTrimMarquee : kTrimEllipsis;
  if (s_trimming[row] == wanted)
    return;
  s_trimming[row] = wanted;
  Set_Property(s_pui.label, name, "TextTrimming", wanted);
  if (scroll)
    Invoke<void>(pui_img, s_pui.label, name, "ResetMarquee");
}

MonoObject *game_root() {
  return Game ? Get_Property<MonoObject *>(pui_img, "Sce.PlayStation.PUI.UI2",
                                           "Scene", Game, "RootWidget")
              : nullptr;
}

void layout() {
  Widgets &w = s_widgets;
  if (!w.root)
    return;
  float screen_w = 1920.0f, screen_h = 1080.0f;
  resolve_root_dimensions(game_root(), &screen_w, &screen_h);

  const bool main = on_file_list() || s_list.groups.size() == 1;
  const int count = row_count();
  const int visible = std::min(count, kMaxRows);
  if (s_selected < s_top)
    s_top = s_selected;
  if (s_selected >= s_top + visible)
    s_top = s_selected - visible + 1;
  s_top = std::clamp(s_top, 0, std::max(0, count - visible));

  const std::string title =
      main ? kTitleText : s_list.groups[s_group].label;
  const float value_width = text_width("Off", kRowFont) + kTextRight;
  const float header_h = kTitleHeight + (main ? kSubtitleHeight : 0.0f);
  const float footer_h = main ? kFooterHeight : 0.0f;
  const float body_h = visible * kRowHeight;
  const float height = header_h + body_h + footer_h;
  const float x = std::max(0.0f, screen_w - kMarginX - kWidth);
  const float y = std::min(kMarginY, std::max(0.0f, screen_h - height));
  Pui::PanelGeometry(s_pui, w.root, x, y, kWidth, height);

  const Color bar = main ? kMainBar : kSubBar;
  Pui::PanelGeometry(s_pui, w.header, 0, 0, kWidth, header_h);
  Pui::PanelColor(s_pui, w.header, bar);
  Pui::LabelGeometry(s_pui, w.title, kTextLeft, main ? 2.0f : 0.0f,
                     kWidth - 2 * kTextLeft, kTitleHeight);
  Pui::LabelText(s_pui, w.title, title);
  if (main) {
    Pui::LabelGeometry(s_pui, w.subtitle, 0, kTitleHeight - 6.0f, kWidth,
                       kSubtitleHeight);
    Pui::LabelText(s_pui, w.subtitle, kSubtitleText);
  } else {
    Pui::LabelText(s_pui, w.subtitle, "");
  }
  Pui::PanelGeometry(s_pui, w.body, 0, header_h, kWidth, body_h);
  Pui::PanelGeometry(s_pui, w.footer, 0, header_h + body_h, kWidth, footer_h);
  Pui::PanelColor(s_pui, w.footer, main ? kMainBar : kClear);
  Pui::LabelGeometry(s_pui, w.footer_label, kTextLeft, header_h + body_h,
                     kWidth - 2 * kTextLeft, footer_h);
  Pui::LabelText(s_pui, w.footer_label, main ? footer_text() : "");

  Pui::PanelGeometry(s_pui, w.edges[0], 0, 0, kWidth, kEdge);
  Pui::PanelGeometry(s_pui, w.edges[1], 0, height - kEdge, kWidth, kEdge);
  Pui::PanelGeometry(s_pui, w.edges[2], 0, 0, kEdge, height);
  Pui::PanelGeometry(s_pui, w.edges[3], kWidth - kEdge, 0, kEdge, height);
  for (MonoObject *object : w.edges)
    Pui::PanelColor(s_pui, object, bar);

  const int selected_row = s_selected - s_top;
  Pui::PanelGeometry(s_pui, w.highlight, 0,
                     header_h + selected_row * kRowHeight, kWidth, kRowHeight);
  for (int row = 0; row < kMaxRows; ++row) {
    MonoObject *name = w.names[row];
    MonoObject *value = w.values[row];
    if (row >= visible) {
      Pui::LabelText(s_pui, name, "");
      Pui::LabelText(s_pui, value, "");
      Pui::LabelGeometry(s_pui, name, 0, 0, 0, 0);
      Pui::LabelGeometry(s_pui, value, 0, 0, 0, 0);
      name_trimming(row, false);
      continue;
    }
    const int index = s_top + row;
    const float row_y = header_h + row * kRowHeight;
    const bool selected = index == s_selected;
    label_color(name, selected ? kBlack : kWhite);
    const float name_width =
        on_file_list() ? kWidth - kTextLeft - kTextRight
                       : kWidth - kTextLeft - kValueGap - value_width;
    const std::string &text = on_file_list()
                                  ? s_list.groups[index].label
                                  : s_list.groups[s_group].cheats[index].name;
    Pui::LabelGeometry(s_pui, name, kTextLeft, row_y, name_width, kRowHeight);
    Pui::LabelText(s_pui, name, text);
    name_trimming(row, selected && text_width(text, kRowFont) > name_width);
    if (on_file_list()) {
      Pui::LabelText(s_pui, value, "");
      continue;
    }
    const Cheat &cheat = s_list.groups[s_group].cheats[index];
    Pui::LabelGeometry(s_pui, value, kWidth - kTextRight - value_width, row_y,
                       value_width, kRowHeight);
    Pui::LabelText(s_pui, value, cheat.enabled ? "On" : "Off");
    label_color(value, cheat.enabled ? kOn : kOff);
  }
}

uintptr_t focused_scene() {
  return reinterpret_cast<uintptr_t>(SystemDialogs::FocusedScene());
}

void open_menu(CheatsList::List &&list) {
  MonoObject *host = game_root();
  if (!host) {
    LOG_WARN("[CHEATSMENU] no Game layer root");
    return;
  }
  s_list = std::move(list);
  s_group = s_list.groups.size() == 1 ? 0 : -1;
  s_selected = 0;
  s_top = 0;
  s_return_selected = 0;
  s_toggle_pending = false;
  std::fill(std::begin(s_trimming), std::end(s_trimming), kTrimEllipsis);
  if (!create_widgets(host)) {
    LOG_ERROR("[CHEATSMENU] could not build the menu");
    destroy_widgets();
    return;
  }
  s_state = State::Open;
  s_dirty = true;
  s_hidden = false;
  s_open_focus = focused_scene();
  s_release_pending = false;
  set_intercept(true);
  LOG_INFO("[CHEATSMENU] open game=%s groups=%zu focus=%p",
           s_list.game.c_str(), s_list.groups.size(),
           reinterpret_cast<void *>(s_open_focus));
}

void release_controller_when_idle(bool now) {
  if (!s_intercepting) {
    s_release_pending = false;
    return;
  }
  if (!s_release_pending) {
    s_release_pending = true;
    s_release_since_ms = MonotonicMs();
  }
  /* Two seconds at most: a button held on is the player's business. */
  if (now || Input::HeldButtons() == 0 ||
      MonotonicMs() - s_release_since_ms >= 2000) {
    set_intercept(false);
    s_release_pending = false;
  }
}

void close_menu(bool release_now = false) {
  if (s_state == State::Open || s_state == State::Native)
    LOG_INFO("[CHEATSMENU] close");
  if (s_state == State::Native)
    SystemDialogs::CloseNativeMenu();
  release_controller_when_idle(release_now);
  destroy_widgets();
  Reflect::Release(s_native_scene);
  s_state = State::Closed;
  s_list = {};
  s_native_rows.clear();
  s_toggle_pending = false;
}

/* PUI's own card:
 * The same pop-up menu the Sweety Shell Menu is drawn with: one switch per
 * cheat under the "Cheats Menu" header, and a last row, greyed out, with who
 * made the cheats. Its focus, scrolling, sounds and input are PUI's. */
bool open_native(int focus) {
  std::vector<SystemDialogs::NativeMenuItem> items;
  s_native_rows.clear();
  s_native_wanted.clear();
  for (size_t g = 0; g < s_list.groups.size(); ++g) {
    const Group &group = s_list.groups[g];
    for (size_t c = 0; c < group.cheats.size(); ++c) {
      SystemDialogs::NativeMenuItem item;
      item.kind = SystemDialogs::NativeMenuItem::Kind::Switch;
      item.title = group.cheats[c].name;
      item.on = group.cheats[c].enabled;
      if (s_list.groups.size() > 1)
        item.description = group.label;
      items.push_back(item);
      s_native_rows.push_back({static_cast<int>(g), static_cast<int>(c)});
      s_native_wanted.push_back(item.on);
    }
  }
  SystemDialogs::NativeMenuItem footer;
  footer.title = footer_text();
  footer.description = kSubtitleText;
  footer.enabled = false;
  items.push_back(footer);
  s_native_rows.push_back({-1, -1});
  s_native_wanted.push_back(false);

  MonoObject *scene = Reflect::Target(s_native_scene);
  SystemDialogs::NativeMenuPlacement placement;
  placement.margin = kMarginX;
  placement.min_width = 652.0f;
  std::string failure;
  const bool opened = SystemDialogs::OpenNativeMenu(
      items, kTitleText, focus, &failure, scene, placement);
  LOG_INFO("[CHEATSMENU] native card opened=%d rows=%zu scene=%p focused=%p %s",
           opened ? 1 : 0, items.size(), static_cast<void *>(scene),
           reinterpret_cast<void *>(focused_scene()), failure.c_str());
  return opened;
}

void start_native(CheatsList::List &&list) {
  s_list = std::move(list);
  if (!open_native(0)) {
    CheatsList::List drawn = std::move(s_list);
    s_list = {};
    Reflect::Release(s_native_scene);
    if (s_target == Target::NativeGame) {
      LOG_WARN("[CHEATSMENU] no native card over the game; drawing the menu");
      open_menu(std::move(drawn));
    }
    return;
  }
  s_state = State::Native;
  s_toggle_pending = false;
  if (s_target == Target::NativeGame) {
    s_release_pending = false;
    set_intercept(true);
  }
}

void queue_toggle(int group, int index, bool enabled) {
  const Cheat &cheat = s_list.groups[group].cheats[index];
  s_toggle_queue.push_back({s_list.session_id, cheat.key, enabled,
                            static_cast<size_t>(group),
                            static_cast<size_t>(index)});
}

void run_toggle_queue() {
  if (s_toggle_queue.empty())
    return;
  if (CheatsList::StartToggle(s_toggle_queue.front())) {
    s_toggle_queue.erase(s_toggle_queue.begin());
    s_toggle_pending = true;
  }
}

void tick_native() {
  int index = -1;
  const SystemDialogs::NativeMenuEvent event =
      SystemDialogs::PollNativeMenu(&index);
  if (event == SystemDialogs::NativeMenuEvent::Closed) {
    LOG_INFO("[CHEATSMENU] native card closed");
    s_state = State::Open; /* close_menu() logs and releases as for the menu */
    close_menu();
    return;
  }
  /* A switch flips in place without a selection event: follow the values. */
  for (size_t row = 0; row < s_native_rows.size(); ++row) {
    bool on = false;
    if (s_native_rows[row].first < 0 ||
        !SystemDialogs::NativeMenuSwitch(static_cast<int>(row), &on) ||
        on == s_native_wanted[row])
      continue;
    s_native_wanted[row] = on;
    queue_toggle(s_native_rows[row].first, s_native_rows[row].second, on);
  }
  run_toggle_queue();
}

void request_toggle(size_t group, size_t index) {
  if (s_toggle_pending)
    return;
  const Cheat &cheat = s_list.groups[group].cheats[index];
  s_toggle_pending = CheatsList::StartToggle(
      {s_list.session_id, cheat.key, !cheat.enabled, group, index});
}

void handle_input(uint32_t down, uint32_t repeated) {
  const int count = row_count();
  if (down & (Up | Down)) {
    const bool wrap = !(repeated & (Up | Down));
    int next = s_selected + ((down & Down) ? 1 : -1);
    if (next < 0)
      next = wrap ? count - 1 : 0;
    if (next >= count)
      next = wrap ? 0 : count - 1;
    if (next != s_selected) {
      s_selected = next;
      s_dirty = true;
    }
  }
  if (down & Cross) {
    if (on_file_list()) {
      s_return_selected = s_selected;
      s_group = s_selected;
      s_selected = 0;
      s_top = 0;
      s_dirty = true;
    } else {
      request_toggle(static_cast<size_t>(s_group),
                     static_cast<size_t>(s_selected));
    }
  } else if (down & Circle) {
    if (!on_file_list() && s_list.groups.size() > 1) {
      s_group = -1;
      s_selected = s_return_selected;
      s_top = 0;
      s_dirty = true;
    } else {
      close_menu();
    }
  }
}

/* Hidden while another scene has the focus, shown again when it is back. */
void follow_focus() {
  if (++s_focus_frames < 4)
    return;
  s_focus_frames = 0;
  const bool hidden = focused_scene() != s_open_focus;
  if (hidden == s_hidden)
    return;
  s_hidden = hidden;
  LOG_INFO("[CHEATSMENU] %s (focused scene %p)", hidden ? "hidden" : "shown",
           reinterpret_cast<void *>(focused_scene()));
  Set_Property(s_pui.widget, s_widgets.root, "Opacity", hidden ? 0.0f : 1.0f);
}

void take_job_result() {
  JobResult result;
  if (!CheatsList::TakeResult(result))
    return;
  if (result.kind == JobKind::Fetch) {
    if (s_state != State::Loading)
      return;
    s_state = State::Closed;
    if (!result.ok) {
      if (s_target == Target::NativeScene)
        notify("%s", toolbox_i18n::tr("home_menu.cheats.none"));
      Reflect::Release(s_native_scene);
      return;
    }
    /* The drawn menu only while it is still wanted: the game in front, the
       switch on. */
    if (s_target == Target::NativeScene)
      start_native(std::move(result.list));
    else if (!g_settings.cheats_visual_menu || !GameHasFocus())
      return;
    else if (s_target == Target::NativeGame)
      start_native(std::move(result.list));
    else
      open_menu(std::move(result.list));
    return;
  }
  s_toggle_pending = false;
  if ((s_state != State::Open && s_state != State::Native) ||
      result.session_id != s_list.session_id ||
      result.group >= s_list.groups.size() ||
      result.index >= s_list.groups[result.group].cheats.size())
    return;
  Cheat &cheat = s_list.groups[result.group].cheats[result.index];
  if (result.ok) {
    cheat.enabled = result.enabled;
    s_dirty = true;
    return;
  }
  if (!result.reply.empty())
    notify("notify.cheats.engine_status", result.reply.c_str());
  else
    notify("notify.cheats.activate_failed", cheat.name.c_str());
  /* PUI's switch has flipped already: show the card again as it is. */
  if (s_state == State::Native) {
    int focus = 0;
    for (size_t row = 0; row < s_native_rows.size(); ++row)
      if (s_native_rows[row].first == static_cast<int>(result.group) &&
          s_native_rows[row].second == static_cast<int>(result.index))
        focus = static_cast<int>(row);
    if (!open_native(focus))
      close_menu(/*release_now=*/true);
  }
}

} // namespace

bool Tick(bool toggle_requested, uint32_t buttons_down,
          uint32_t buttons_repeated) {
  take_job_result();
  /* Switches flipped on the card just before it closed still go through. */
  run_toggle_queue();
  if (s_release_pending && s_state != State::Open && s_state != State::Native)
    release_controller_when_idle(false);

  if (toggle_requested) {
    if (s_state == State::Open || s_state == State::Native) {
      close_menu();
      return false;
    }
    if (s_state == State::Closed && g_settings.cheats_visual_menu &&
        GameHasFocus() && CheatsList::StartFetch()) {
      s_state = State::Loading;
      s_target = g_settings.cheats_visual_render == 1 ? Target::NativeGame
                                                      : Target::Overlay;
    } else {
      LOG_INFO("[CHEATSMENU] combo ignored enabled=%d state=%d",
               g_settings.cheats_visual_menu ? 1 : 0,
               static_cast<int>(s_state));
    }
  }

  if (s_state == State::Native) {
    if (s_target != Target::NativeScene &&
        (!g_settings.cheats_visual_menu || !GameHasFocus())) {
      close_menu(/*release_now=*/true);
      return false;
    }
    tick_native();
    return false; /* PUI reads the controller itself */
  }
  if (s_state != State::Open)
    return false;
  if (!g_settings.cheats_visual_menu || !GameHasFocus()) {
    close_menu(/*release_now=*/true);
    return false;
  }
  if (s_intercepting && Input::IsIntercepted()) {
    LOG_ERROR("[CHEATSMENU] ShellUI's own pad reads are intercepted; "
              "giving the controller back to the game");
    set_intercept(false);
    s_intercept_unusable = true;
  }

  follow_focus();
  if (s_hidden)
    return false;
  if (buttons_down)
    handle_input(buttons_down, buttons_repeated);
  if (s_state != State::Open)
    return false;
  if (s_dirty) {
    s_dirty = false;
    layout();
  }
  return true;
}

void OpenFromControlCenter(MonoObject *scene) {
  std::string title_id;
  int app_id = -1;
  if (!Get_Running_App_TID(title_id, app_id)) {
    notify("%s", toolbox_i18n::tr("home_menu.cheats.no_game"));
    return;
  }
  if (s_state == State::Open || s_state == State::Native)
    close_menu(/*release_now=*/true);
  if (s_state != State::Closed || !CheatsList::StartFetch()) {
    LOG_INFO("[CHEATSMENU] busy; Control Center request dropped");
    return;
  }
  Reflect::Hold(s_native_scene, scene);
  s_target = Target::NativeScene;
  s_state = State::Loading;
}

void Close() {
  s_toggle_queue.clear();
  if (s_state == State::Loading) {
    s_state = State::Closed;
    Reflect::Release(s_native_scene);
  }
  if (s_state == State::Open || s_state == State::Native || s_release_pending)
    close_menu(/*release_now=*/true);
}

/* LayerManager's focus layer: the menu belongs to the game and goes away when
   Home, the Control Center or a system app comes up. */
bool GameHasFocus() {
  const FocusLayer layer = CurrentFocusLayer();
  /* Without the field the running BigApp is all there is to go by. */
  return layer == FocusLayer::Game || layer == FocusLayer::Unknown;
}

} // namespace AniketosShell::CheatsMenu
