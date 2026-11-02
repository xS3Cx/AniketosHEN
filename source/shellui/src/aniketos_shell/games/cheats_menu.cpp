#include "aniketos_shell/games/cheats_menu.hpp"

#include "aniketos_shell/input/pad_reader.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_ui/pui_primitives.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "onion_cjson.hpp"
#include "toolbox_i18n.hpp"
#include "ps5/kernel.h"

#include <onion/log.h>
#include <onion/proc_query.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>
#include <time.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

extern MonoObject *Game;
extern int g_libmono_handle;
extern onion::Settings g_settings;
bool resolve_root_dimensions(MonoObject *root, float *screen_w,
                             float *screen_h);

namespace AniketosShell::CheatsMenu {
namespace {

using AniketosUI::Color;
namespace Pui = AniketosUI::Pui;

/* ---- Look -----------------------------------------------------------------
 * The classic PS4 "HaxCry" mod menu, measured on its screenshots (a 720p
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

/* ---- Cheat list, as the util payload's cheat service reports it ---------- */
struct Cheat {
  std::string key;
  std::string name;
  bool enabled = false;
};

struct Group {
  std::string label;
  std::vector<Cheat> cheats;
};

struct CheatList {
  std::string session_id;
  std::string game;
  std::string authors;
  std::vector<Group> groups;
};

std::string join_authors(const cJSON *object) {
  std::string joined;
  std::unordered_set<std::string> seen;
  const cJSON *authors = onion_cjson::item(object, "authors");
  if (!cJSON_IsArray(authors))
    return joined;
  const cJSON *author = nullptr;
  cJSON_ArrayForEach(author, authors) {
    const char *value = onion_cjson::string_value(author);
    if (!value || !*value || !seen.insert(value).second)
      continue;
    if (!joined.empty())
      joined += ", ";
    joined += value;
  }
  return joined;
}

void parse_cheats(const cJSON *array, Group &group) {
  if (!cJSON_IsArray(array))
    return;
  const cJSON *entry = nullptr;
  cJSON_ArrayForEach(entry, array) {
    Cheat cheat;
    cheat.key = onion_cjson::string_item(entry, "key", "");
    cheat.name = onion_cjson::string_item(entry, "name", "");
    cheat.enabled = onion_cjson::bool_item(entry, "enabled");
    if (!cheat.key.empty())
      group.cheats.push_back(std::move(cheat));
  }
}

bool parse_list(const std::string &json, CheatList &out) {
  onion_cjson::Root root(json);
  if (!root || !onion_cjson::bool_item(root.get(), "canToggle"))
    return false;
  out.session_id = onion_cjson::string_item(root.get(), "sessionId", "");
  out.game = onion_cjson::string_item(root.get(), "name", "");
  out.authors = join_authors(root.get());
  const cJSON *groups = onion_cjson::item(root.get(), "groups");
  if (cJSON_IsArray(groups) && cJSON_GetArraySize(groups) > 0) {
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, groups) {
      Group group;
      const std::string format = onion_cjson::string_item(entry, "format", "");
      const std::string authors = join_authors(entry);
      group.label = authors.empty() ? format : authors;
      if (!authors.empty() && !format.empty())
        group.label += " (" + format + ")";
      parse_cheats(onion_cjson::item(entry, "cheats"), group);
      if (!group.cheats.empty())
        out.groups.push_back(std::move(group));
    }
  } else {
    Group group;
    group.label = out.game;
    parse_cheats(onion_cjson::item(root.get(), "cheats"), group);
    if (!group.cheats.empty())
      out.groups.push_back(std::move(group));
  }
  return !out.session_id.empty() && !out.groups.empty();
}

/* ---- Util calls off the UI thread ------------------------------------------
 * Both calls are synchronous IPC round trips (the first list of a game also
 * reads and decrypts its cheat files), so they run on a short-lived thread and
 * the UI thread picks the result up on a later frame. One job at a time. */
enum class JobKind { Fetch, Toggle };

struct JobResult {
  JobKind kind = JobKind::Fetch;
  bool ok = false;
  CheatList list;
  std::string reply;
  size_t group = 0;
  size_t index = 0;
  bool enabled = false;
};

struct ToggleRequest {
  std::string session_id;
  std::string key;
  bool enabled = false;
  size_t group = 0;
  size_t index = 0;
};

std::atomic<bool> s_job_busy{false};
std::atomic<bool> s_job_done{false};
std::mutex s_job_mutex;
ToggleRequest s_toggle_request; /* written before the toggle thread starts */
JobResult s_job_result;         /* under s_job_mutex */

std::string read_and_remove(const std::string &path) {
  std::string text;
  FILE *file = std::fopen(path.c_str(), "rb");
  if (file) {
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
      text.append(buffer, got);
    std::fclose(file);
  }
  unlink(path.c_str());
  return text;
}

void publish(JobResult &&result) {
  {
    std::lock_guard<std::mutex> lock(s_job_mutex);
    s_job_result = std::move(result);
  }
  s_job_done.store(true, std::memory_order_release);
  s_job_busy.store(false, std::memory_order_release);
}

void *fetch_job(void *) {
  JobResult result;
  result.kind = JobKind::Fetch;
  std::string title_id;
  int app_id = -1;
  onion_bigapp_process_t process{};
  if (Get_Running_App_TID(title_id, app_id)) {
    const int found = onion_resolve_running_bigapp(&process);
    const bool have_process = (found == 0 || found == -2) && process.pid > 0 &&
                              process.session_generation != 0 &&
                              process.process_name[0] != '\0';
    std::string path;
    if (have_process &&
        IPC_Client::getInstance(true).GetGameCheats(
            title_id, path, "runtime", process.pid, process.appid,
            process.process_name, process.session_generation,
            /*quiet=*/true) &&
        !path.empty())
      result.ok = parse_list(read_and_remove(path), result.list);
  }
  size_t count = 0;
  for (const Group &group : result.list.groups)
    count += group.cheats.size();
  LOG_INFO("[CHEATSMENU] list title=%s pid=%d ok=%d groups=%zu cheats=%zu",
           title_id.c_str(), static_cast<int>(process.pid), result.ok ? 1 : 0,
           result.list.groups.size(), count);
  publish(std::move(result));
  return nullptr;
}

void *toggle_job(void *) {
  const ToggleRequest request = s_toggle_request;
  JobResult result;
  result.kind = JobKind::Toggle;
  result.group = request.group;
  result.index = request.index;
  result.enabled = request.enabled;
  result.ok = IPC_Client::getInstance(true).ToggleGameCheat(
      request.session_id, request.key, request.enabled, result.reply);
  LOG_INFO("[CHEATSMENU] toggle enabled=%d ok=%d reply=%s",
           request.enabled ? 1 : 0, result.ok ? 1 : 0, result.reply.c_str());
  publish(std::move(result));
  return nullptr;
}

bool start_job(void *(*job)(void *)) {
  if (s_job_busy.exchange(true, std::memory_order_acq_rel))
    return false;
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, job, nullptr) != 0) {
    s_job_busy.store(false, std::memory_order_release);
    LOG_ERROR("[CHEATSMENU] failed to start a worker thread");
    return false;
  }
  pthread_detach(thread);
  return true;
}

/* ---- Who has the screen ---------------------------------------------------
 * LayerManager.s_curFocusLayer (Sce.Vsh.ShellUI.AppSystem), read by name: the
 * menu belongs to the game and goes away when Home, the Control Center or a
 * system app comes up. */
bool s_focus_resolved = false;
MonoVTable *s_focus_vtable = nullptr;
MonoClassField *s_focus_field = nullptr;
int32_t s_focus_game = -1;

MonoClass *nested_class(MonoClass *outer, const char *name) {
  static MonoClass *(*nested_types)(MonoClass *, void **) = nullptr;
  if (!nested_types)
    nested_types = reinterpret_cast<decltype(nested_types)>(
        kernel_dynlib_dlsym(-1, g_libmono_handle,
                            "mono_class_get_nested_types"));
  if (!outer || !nested_types)
    return nullptr;
  void *iter = nullptr;
  while (MonoClass *inner = nested_types(outer, &iter))
    if (!std::strcmp(mono_class_get_name(inner), name))
      return inner;
  return nullptr;
}

bool game_has_focus() {
  if (!s_focus_resolved) {
    s_focus_resolved = true;
    MonoImage *app_system = getDLLimage("Sce.Vsh.ShellUI.AppSystem.dll");
    MonoClass *manager =
        app_system ? mono_class_from_name(app_system,
                                          "Sce.Vsh.ShellUI.AppSystem",
                                          "LayerManager")
                   : nullptr;
    MonoClass *layers = nested_class(manager, "FocusLayer");
    MonoClassField *game =
        layers ? mono_class_get_field_from_name(layers, "Game") : nullptr;
    s_focus_field =
        manager ? mono_class_get_field_from_name(manager, "s_curFocusLayer")
                : nullptr;
    if (game && s_focus_field) {
      mono_field_static_get_value(mono_class_vtable(mono_domain_get(), layers),
                                  game, &s_focus_game);
      s_focus_vtable = mono_class_vtable(mono_domain_get(), manager);
    }
    LOG_INFO("[CHEATSMENU] focus layer field=%d game=%d",
             s_focus_vtable ? 1 : 0, s_focus_game);
  }
  /* Without the field the running BigApp is all there is to go by. */
  if (!s_focus_vtable)
    return true;
  int32_t value = -1;
  mono_field_static_get_value(s_focus_vtable, s_focus_field, &value);
  return value == s_focus_game;
}

/* ---- Keeping the controller from the game ---------------------------------
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

/* ---- Widgets ---------------------------------------------------------------
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
MonoObject *s_fonts[4] = {}; /* title, subtitle, row, footer (unboxed) */
MonoObject *s_outline = nullptr;

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
    Set_Property(s_pui.label, object, "TextTrimming", 3); /* EllipsisCharacter */
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
  std::fill(std::begin(s_fonts), std::end(s_fonts), nullptr);
  s_outline = nullptr;
}

bool create_widgets(MonoObject *host) {
  if (!Pui::Resolve(s_pui, pui_img, retain, nullptr))
    return false;
  Pui::RemoveNamed(s_pui, host, kRootName); /* left over from a crash */
  s_fonts[0] = make_font(kTitleFont, 700);
  s_fonts[1] = make_font(kSubtitleFont, 400);
  s_fonts[2] = make_font(kRowFont, 400);
  s_fonts[3] = make_font(kFooterFont, 400);
  s_outline = make_outline(kTitleOutline);
  for (MonoObject *font : s_fonts)
    if (!font)
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
  w.title = label(w.root, "id_cheats_menu_title", s_fonts[0], 1);
  w.subtitle = label(w.root, "id_cheats_menu_subtitle", s_fonts[1], 1);
  w.footer_label = label(w.root, "id_cheats_menu_footer_text", s_fonts[3], 1);
  for (int i = 0; i < kMaxRows; ++i) {
    w.names[i] = label(w.root, "id_cheats_menu_row", s_fonts[2], 0);
    w.values[i] = label(w.root, "id_cheats_menu_value", s_fonts[2], 2);
  }
  if (!w.body || !w.header || !w.footer || !w.highlight || !w.title ||
      !w.subtitle || !w.footer_label)
    return false;
  for (int i = 0; i < kMaxRows; ++i)
    if (!w.names[i] || !w.values[i])
      return false;
  if (s_outline) {
    Set_Property(s_pui.label, w.title, "EnableThemedShadowColor", false);
    Set_Property_Invoke(s_pui.label, w.title, "TextShadow", s_outline);
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

/* ---- Menu state ------------------------------------------------------------ */
enum class State { Closed, Loading, Open, Native };
/* What a list being fetched is for: the drawn menu, or PUI's own card (over
   the game, or on the Control Center's scene). */
enum class Target { Overlay, NativeGame, NativeScene };

State s_state = State::Closed;
Target s_target = Target::Overlay;
CheatList s_list;
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

constexpr int kTrimEllipsis = 3; /* TextTrimming.EllipsisCharacter */
constexpr int kTrimMarquee = 5;  /* TextTrimming.Marquee */

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

void layout() {
  Widgets &w = s_widgets;
  MonoObject *host = w.root;
  if (!host)
    return;
  MonoObject *game_root = Get_Property<MonoObject *>(
      pui_img, "Sce.PlayStation.PUI.UI2", "Scene", Game, "RootWidget");
  float screen_w = 1920.0f, screen_h = 1080.0f;
  if (!resolve_root_dimensions(game_root, &screen_w, &screen_h)) {
    screen_w = 1920.0f;
    screen_h = 1080.0f;
  }

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
  const float width = kWidth;
  const float value_width = text_width("Off", kRowFont) + kTextRight;
  const float header_h = kTitleHeight + (main ? kSubtitleHeight : 0.0f);
  const float footer_h = main ? kFooterHeight : 0.0f;
  const float body_h = visible * kRowHeight;
  const float height = header_h + body_h + footer_h;
  const float x = std::max(0.0f, screen_w - kMarginX - width);
  const float y = std::min(kMarginY, std::max(0.0f, screen_h - height));
  Pui::PanelGeometry(s_pui, w.root, x, y, width, height);

  const Color bar = main ? kMainBar : kSubBar;
  Pui::PanelGeometry(s_pui, w.header, 0, 0, width, header_h);
  Pui::PanelColor(s_pui, w.header, bar);
  Pui::LabelGeometry(s_pui, w.title, kTextLeft, main ? 2.0f : 0.0f,
                     width - 2 * kTextLeft, kTitleHeight);
  Pui::LabelText(s_pui, w.title, title);
  if (main) {
    Pui::LabelGeometry(s_pui, w.subtitle, 0, kTitleHeight - 6.0f, width,
                       kSubtitleHeight);
    Pui::LabelText(s_pui, w.subtitle, kSubtitleText);
  } else {
    Pui::LabelText(s_pui, w.subtitle, "");
  }
  Pui::PanelGeometry(s_pui, w.body, 0, header_h, width, body_h);
  Pui::PanelGeometry(s_pui, w.footer, 0, header_h + body_h, width, footer_h);
  Pui::PanelColor(s_pui, w.footer, main ? kMainBar : kClear);
  Pui::LabelGeometry(s_pui, w.footer_label, kTextLeft, header_h + body_h,
                     width - 2 * kTextLeft, footer_h);
  Pui::LabelText(s_pui, w.footer_label, main ? footer_text() : "");

  const Color edge = bar;
  Pui::PanelGeometry(s_pui, w.edges[0], 0, 0, width, kEdge);
  Pui::PanelGeometry(s_pui, w.edges[1], 0, height - kEdge, width, kEdge);
  Pui::PanelGeometry(s_pui, w.edges[2], 0, 0, kEdge, height);
  Pui::PanelGeometry(s_pui, w.edges[3], width - kEdge, 0, kEdge, height);
  for (MonoObject *object : w.edges)
    Pui::PanelColor(s_pui, object, edge);

  const int selected_row = s_selected - s_top;
  Pui::PanelGeometry(s_pui, w.highlight, 0,
                     header_h + selected_row * kRowHeight, width, kRowHeight);
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
        on_file_list() ? width - kTextLeft - kTextRight
                       : width - kTextLeft - kValueGap - value_width;
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
    Pui::LabelGeometry(s_pui, value, width - kTextRight - value_width, row_y,
                       value_width, kRowHeight);
    Pui::LabelText(s_pui, value, cheat.enabled ? "On" : "Off");
    label_color(value, cheat.enabled ? kOn : kOff);
  }
}

uintptr_t focused_scene() {
  return reinterpret_cast<uintptr_t>(SystemDialogs::FocusedScene());
}

void open_menu(CheatList &&list) {
  MonoObject *host = Game ? Get_Property<MonoObject *>(
                                pui_img, "Sce.PlayStation.PUI.UI2", "Scene",
                                Game, "RootWidget")
                          : nullptr;
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

uint64_t now_millis() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000u +
         static_cast<uint64_t>(now.tv_nsec) / 1000000u;
}

void release_controller_when_idle(bool now) {
  if (!s_intercepting) {
    s_release_pending = false;
    return;
  }
  if (!s_release_pending) {
    s_release_pending = true;
    s_release_since_ms = now_millis();
  }
  /* Two seconds at most: a button held on is the player's business. */
  if (now || Input::HeldButtons() == 0 ||
      now_millis() - s_release_since_ms >= 2000) {
    set_intercept(false);
    s_release_pending = false;
  }
}

void forget_native_scene() {
  if (s_native_scene)
    mono_gchandle_free(s_native_scene);
  s_native_scene = 0;
}

void close_menu(bool release_now = false) {
  if (s_state == State::Open || s_state == State::Native)
    LOG_INFO("[CHEATSMENU] close");
  if (s_state == State::Native)
    SystemDialogs::CloseNativeMenu();
  release_controller_when_idle(release_now);
  destroy_widgets();
  forget_native_scene();
  s_state = State::Closed;
  s_list = {};
  s_native_rows.clear();
  s_toggle_pending = false;
}

/* ---- PUI's own card ----------------------------------------------------------
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

  MonoObject *scene =
      s_native_scene ? mono_gchandle_get_target(s_native_scene) : nullptr;
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

void start_native(CheatList &&list) {
  s_list = std::move(list);
  if (!open_native(0)) {
    CheatList drawn = std::move(s_list);
    s_list = {};
    forget_native_scene();
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
  if (s_toggle_queue.empty() || s_job_busy.load(std::memory_order_acquire))
    return;
  s_toggle_request = s_toggle_queue.front();
  if (start_job(toggle_job)) {
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
  s_toggle_request = {s_list.session_id, cheat.key, !cheat.enabled, group,
                      index};
  s_toggle_pending = start_job(toggle_job);
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
  if (!s_job_done.exchange(false, std::memory_order_acq_rel))
    return;
  JobResult result;
  {
    std::lock_guard<std::mutex> lock(s_job_mutex);
    result = std::move(s_job_result);
  }
  if (result.kind == JobKind::Fetch) {
    if (s_state != State::Loading)
      return;
    s_state = State::Closed;
    if (!result.ok) {
      if (s_target == Target::NativeScene)
        notify("%s", toolbox_i18n::tr("home_menu.cheats.none"));
      forget_native_scene();
      return;
    }
    /* The drawn menu only while it is still wanted: the game in front, the
       switch on. */
    if (s_target == Target::NativeScene)
      start_native(std::move(result.list));
    else if (!g_settings.cheats_visual_menu || !game_has_focus())
      return;
    else if (s_target == Target::NativeGame)
      start_native(std::move(result.list));
    else
      open_menu(std::move(result.list));
    return;
  }
  s_toggle_pending = false;
  if ((s_state != State::Open && s_state != State::Native) ||
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
    open_native(focus);
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
        game_has_focus() && start_job(fetch_job)) {
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
        (!g_settings.cheats_visual_menu || !game_has_focus())) {
      close_menu(/*release_now=*/true);
      return false;
    }
    tick_native();
    return false; /* PUI reads the controller itself */
  }
  if (s_state != State::Open)
    return false;
  if (!g_settings.cheats_visual_menu || !game_has_focus()) {
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
  if (s_state != State::Closed || !start_job(fetch_job)) {
    LOG_INFO("[CHEATSMENU] busy; Control Center request dropped");
    return;
  }
  forget_native_scene();
  if (scene)
    s_native_scene = mono_gchandle_new_weakref(scene, 0);
  s_target = Target::NativeScene;
  s_state = State::Loading;
}

void Close() {
  s_toggle_queue.clear();
  if (s_state == State::Loading)
    s_state = State::Closed;
  if (s_state == State::Open || s_state == State::Native || s_release_pending)
    close_menu(/*release_now=*/true);
}

bool IsOpen() { return s_state == State::Open || s_state == State::Native; }

bool GameHasFocus() { return game_has_focus(); }

} // namespace AniketosShell::CheatsMenu
