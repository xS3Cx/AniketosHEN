#include "aniketos_shell/system_ui/native_menu.hpp"

#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "system_dialogs_internal.hpp"

#include <onion/log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace SystemDialogs {
namespace {

using RowKind = NativeMenuItem::Kind;

constexpr float kNotLabelled = -1e30f;

/* What each row holds, kept here: the row widgets are made from the configs
   by PUI and made again when the list scrolls, and they do not start from
   the value they were given. */
struct NativeRow {
  uint32_t config = 0; /* strong */
  RowKind kind = RowKind::Button;
  float number = 0;
  int chosen = 0;
  bool seen = false; /* its widget was there at the last look */
  /* The widget shows what the row holds. Until it does, what it reports is
     its own starting value, not anything the user did, and is not taken. */
  bool synced = false;
  std::string title; /* slider rows: the number is written after it */
  float step = 1;
  float labelled = kNotLabelled; /* the number its title shows now */
};

uint32_t s_native_menu = 0; /* strong */
/* A widget of ours at the right edge of the scene, which the menu hangs off:
   PUI places a pop-up menu relative to the widget it belongs to. */
uint32_t s_native_anchor = 0;
std::vector<NativeRow> s_native_rows;
/* where the menu shown last asked to go */
NativeMenuPlacement s_native_placement;
int s_native_selected = -1;
bool s_native_was_open = false;

void drop_native_anchor() {
  if (MonoObject *anchor = Reflect::Target(s_native_anchor)) {
    Reflect::Call(anchor, "RemoveFromParent", 0);
    Reflect::Call(anchor, "Dispose", 0);
  }
  Reflect::Release(s_native_anchor);
}

void free_native_menu() {
  for (NativeRow &row : s_native_rows)
    Reflect::Release(row.config);
  s_native_rows.clear();
  Reflect::Release(s_native_menu);
  drop_native_anchor();
  s_native_selected = -1;
  s_native_was_open = false;
}

const NativeRow *row_of_kind(int index, RowKind kind) {
  if (index < 0 || static_cast<size_t>(index) >= s_native_rows.size())
    return nullptr;
  const NativeRow &row = s_native_rows[static_cast<size_t>(index)];
  return row.kind == kind ? &row : nullptr;
}

bool native_menu_is_open() {
  return menu_is_open(Reflect::Target(s_native_menu));
}

/* The menu, hung off a new anchor: a point near the right (or left) edge of
   the scene, half way down. Null when that cannot be done. */
MonoObject *anchored_menu(MonoObject *scene,
                          const NativeMenuPlacement &placement) {
  MonoObject *root = property_object(scene, "get_RootWidget");
  MonoObject *anchor = root ? create(ui3_class("Widget")) : nullptr;
  MonoClass *menu_class = ui3_class("PopupMenu");
  MonoMethod *with_target =
      menu_class ? find_method_taking(menu_class, ".ctor", 1,
                                      "Sce.PlayStation.PUI.UI3.Widget")
                 : nullptr;
  if (!anchor || !with_target)
    return nullptr;
  float width = 1920.0f, height = 1080.0f, one = 1.0f;
  Reflect::FloatProperty(root, "get_Width", &width);
  Reflect::FloatProperty(root, "get_Height", &height);
  float x = placement.left ? placement.margin : width - placement.margin;
  float y = height * 0.5f;
  set_property(anchor, "set_X", &x);
  set_property(anchor, "set_Y", &y);
  set_property(anchor, "set_Width", &one);
  set_property(anchor, "set_Height", &one);
  void *arguments[1] = {anchor};
  if (!Reflect::Call(root, "AppendChild", 1, arguments))
    return nullptr;
  Reflect::Hold(s_native_anchor, anchor, /*strong=*/true);
  MonoObject *menu = mono_object_new(mono_domain_get(), menu_class);
  MonoObject *exception = nullptr;
  if (menu)
    mono_runtime_invoke(with_target, menu, arguments, &exception);
  if (menu && !exception)
    return menu;
  drop_native_anchor();
  return nullptr;
}

float row_step(const NativeMenuItem &item) {
  return item.step > 0 ? item.step : 1.0f;
}

MonoObject *switch_config(const NativeMenuItem &item) {
  MonoObject *config = create(ui3_class("MenuListSwitchConfig"));
  bool value = item.on;
  set_property(config, "set_Value", &value);
  return config;
}

MonoObject *slider_config(const NativeMenuItem &item) {
  float minimum = item.minimum, maximum = item.maximum;
  float step = row_step(item), value = item.number;
  /* marks for a few steps, a plain bar for many */
  MonoObject *config = create(ui3_class((maximum - minimum) / step > 16.0f
                                            ? "MenuListContinuousSliderConfig"
                                            : "MenuListDiscreteSliderConfig"));
  set_property(config, "set_MinValue", &minimum);
  set_property(config, "set_MaxValue", &maximum);
  set_property(config, "set_Step", &step);
  set_property(config, "set_Value", &value);
  return config;
}

MonoObject *choice_config(const NativeMenuItem &item) {
  MonoObject *config = create(ui3_class("MenuListDropdownConfig"));
  MonoMethod *from_text = from_string_operator("MenuListSelectableConfig");
  MonoObject *choices = from_text ? new_list_for(config, "get_Items") : nullptr;
  if (!choices)
    return nullptr;
  for (const std::string &name : item.choices) {
    MonoObject *thrown = nullptr;
    MonoObject *choice = config_from_text(from_text, name, &thrown);
    void *arguments[1] = {choice};
    if (choice && !thrown)
      Reflect::Call(choices, "Add", 1, arguments);
  }
  set_property(config, "set_Items", choices);
  int32_t chosen = item.chosen;
  set_property(config, "set_SelectedIndex", &chosen);
  return config;
}

MonoObject *row_config(const NativeMenuItem &item, MonoMethod *from_text,
                       MonoObject **exception) {
  switch (item.kind) {
  case RowKind::Switch:
    return switch_config(item);
  case RowKind::Slider:
    return slider_config(item);
  case RowKind::Choice:
    return choice_config(item);
  case RowKind::Button:
    break;
  }
  MonoObject *config = config_from_text(from_text, item.title, exception);
  if (config && !*exception)
    set_text_property(config, "set_ButtonTitle", item.value);
  return config;
}

/* Beside the anchor (to its left, or to its right), centred on it. */
void place_beside_anchor(MonoObject *menu, bool left) {
  int32_t relative = left ? 3 : 1; /* RelativePosition.Right/Left */
  int32_t middle = 2;              /* VerticalAlignment.Center */
  set_property(menu, "set_RelativePosition", &relative);
  set_property(menu, "set_VerticalAlignment", &middle);
  /* at a screen edge it is pushed back on screen, never flipped to the
     anchor's other side */
  int32_t fit = 1; /* CollisionBehavior.Fit */
  set_property(menu, "set_HorizontalCollisionBehavior", &fit);
  set_property(menu, "set_VerticalCollisionBehavior", &fit);
}

/* On failure the caller frees what was made so far. */
bool show_native_menu(const std::vector<NativeMenuItem> &items,
                      const std::string &header, int focus, MonoObject *scene,
                      Failure &result) {
  const NativeMenuPlacement &placement = s_native_placement;
  MonoObject *menu = anchored_menu(scene, placement);
  const bool anchored = menu != nullptr;
  if (!menu)
    menu = create(ui3_class("PopupMenu"));
  MonoMethod *from_text = from_string_operator("MenuListButtonConfig");
  if (!menu || !from_text)
    return result.fail("popup menu classes not found");
  MonoObject *list = new_list_for(menu, "get_ContextItems");
  if (!list)
    return result.fail("cannot create the menu's row list");
  s_native_menu = mono_gchandle_new(menu, 0);

  bool first = true;
  for (const NativeMenuItem &item : items) {
    MonoObject *exception = nullptr;
    MonoObject *config = row_config(item, from_text, &exception);
    if (exception || !config)
      return result.fail("menu row: " + exception_text(exception));
    NativeRow row;
    row.config = mono_gchandle_new(config, 0);
    row.kind = item.kind;
    row.number = item.number;
    row.chosen = item.chosen;
    row.title = item.title;
    row.step = row_step(item);
    s_native_rows.push_back(row);
    if (item.kind != RowKind::Button)
      set_text_property(config, "set_Title", item.title);
    set_text_property(config, "set_Description", item.description);
    if (first)
      set_text_property(config, "set_Header", header);
    first = false;
    decorate_row(config, item.icon, item.enabled);
    if (!checked_call(list, "Add", config, result))
      return false;
  }
  /* It is ours to close: a press must not dismiss it. */
  bool dismiss = false;
  set_property(menu, "set_DismissOnSelect", &dismiss);
  if (anchored)
    place_beside_anchor(menu, placement.left);
  LOG_INFO("[SYSDIALOG] native menu anchored=%d left=%d rows=%zu",
           anchored ? 1 : 0, placement.left ? 1 : 0, items.size());
  float width = placement.min_width; /* one width for every page */
  set_property(menu, "set_MinWidth", &width);
  if (!checked_call(menu, "set_ContextItems", list, result))
    return false;
  if (focus > 0 && focus < static_cast<int>(items.size())) {
    int32_t index = focus;
    set_property(menu, "set_SelectedIndex", &index);
  }
  return checked_call(menu, "Open", scene, result);
}

MonoObject *named_descendant(MonoObject *widget, const char *name, int depth) {
  if (!widget || depth > 4)
    return nullptr;
  MonoObject *child = property_object(widget, "get_FirstChild");
  for (int guard = 0; child && guard < 16; ++guard) {
    MonoObject *text = property_object(child, "get_Name");
    if (text && Mono_to_String(reinterpret_cast<MonoString *>(text)) == name)
      return child;
    if (MonoObject *found = named_descendant(child, name, depth + 1))
      return found;
    child = property_object(child, "get_NextSibling");
  }
  return nullptr;
}

/* The card goes to the right of the screen, vertically centred: PUI puts a
   menu that hangs off no widget wherever it likes. */
void place_card(MonoObject *list) {
  MonoObject *frame = property_object(list, "get_Parent");
  MonoObject *root = property_object(frame, "get_Parent");
  float frame_width = 0, frame_height = 0, root_width = 0, root_height = 0;
  const bool measured =
      frame && root && Reflect::FloatProperty(frame, "get_Width", &frame_width) &&
      Reflect::FloatProperty(frame, "get_Height", &frame_height) &&
      Reflect::FloatProperty(root, "get_Width", &root_width) &&
      Reflect::FloatProperty(root, "get_Height", &root_height) &&
      frame_width > 8 && root_width > frame_width;
  if (!measured)
    return;
  float x = std::round(s_native_placement.left
                           ? s_native_placement.margin
                           : root_width - frame_width -
                                 s_native_placement.margin);
  float y = std::round(std::max(48.0f, (root_height - frame_height) * 0.5f));
  float now_x = 0, now_y = 0;
  Reflect::FloatProperty(frame, "get_X", &now_x);
  Reflect::FloatProperty(frame, "get_Y", &now_y);
  static int logged = 0;
  if (logged < 6 &&
      (std::fabs(now_x - x) > 0.5f || std::fabs(now_y - y) > 0.5f)) {
    ++logged;
    LOG_INFO("[SYSDIALOG] menu card at %.0f,%.0f (%.0fx%.0f), wanted %.0f,%.0f",
             now_x, now_y, frame_width, frame_height, x, y);
  }
  /* Only a menu PUI placed by itself is moved; one hung off our anchor is
     kept beside it by PUI, through every layout change (scrolling, a page
     of another height). */
  if (s_native_anchor)
    return;
  if (std::fabs(now_x - x) > 0.5f)
    set_property(frame, "set_X", &x);
  if (std::fabs(now_y - y) > 0.5f)
    set_property(frame, "set_Y", &y);
}

void sync_slider_row(MonoObject *row, NativeRow &state) {
  bool press_first = false;
  set_property(row, "set_ClickToEdit", &press_first);
  float value = state.number;
  if (Reflect::FloatProperty(row, "get_Value", &value)) {
    if (!state.seen) {
      state.synced = false;
      state.labelled = kNotLabelled; /* a new widget: label it again */
    }
    if (!state.synced) {
      if (std::fabs(value - state.number) <= 0.0001f)
        state.synced = true;
      else
        set_property(row, "set_Value", &state.number);
    } else {
      state.number = value; /* the user moved it */
    }
  }
  /* the number, after the title: a slider row has no place for it */
  if (state.labelled == state.number)
    return;
  state.labelled = state.number;
  char number[32];
  if (state.step >= 1.0f)
    std::snprintf(number, sizeof(number), "%d",
                  static_cast<int>(std::lround(state.number)));
  else
    std::snprintf(number, sizeof(number), "%.2f",
                  static_cast<double>(state.number));
  set_property(named_descendant(row, "title", 3), "set_Text",
               managed_string(state.title + ":  " + number));
}

/* The row's Value is its selection; SelectedIndex cannot be set on the
   widget. */
void sync_choice_row(MonoObject *row, NativeRow &state) {
  int32_t chosen = 0;
  if (!property_value(row, "get_Value", &chosen))
    return;
  if (!state.seen)
    state.synced = false;
  if (!state.synced) {
    if (chosen == state.chosen) {
      state.synced = true;
    } else {
      int32_t wanted = state.chosen;
      set_property(row, "set_Value", &wanted);
    }
  } else {
    state.chosen = chosen; /* the user picked another */
  }
}

/* Slider rows are adjusted by Left and Right as soon as they have the focus
   (PUI's default wants a press first), and slider and drop-down rows show
   what they hold: a row's widget is given its value when it appears, and read
   when it was already there, which is the user having changed it. */
void sync_rows(MonoObject *list) {
  std::vector<bool> seen(s_native_rows.size(), false);
  MonoObject *container = named_descendant(list, "ItemContainer", 0);
  MonoObject *shim = property_object(container, "get_FirstChild");
  for (int guard = 0; shim && guard < 64; ++guard) {
    float y = 0, height = 0;
    MonoObject *row = property_object(shim, "get_FirstChild");
    if (row && Reflect::FloatProperty(shim, "get_Y", &y) &&
        Reflect::FloatProperty(shim, "get_Height", &height) && height > 8) {
      const size_t index = static_cast<size_t>(std::lround(y / height));
      if (index < s_native_rows.size()) {
        NativeRow &state = s_native_rows[index];
        if (state.kind == RowKind::Slider) {
          sync_slider_row(row, state);
          seen[index] = true;
        } else if (state.kind == RowKind::Choice) {
          sync_choice_row(row, state);
          seen[index] = true;
        }
      }
    }
    shim = property_object(shim, "get_NextSibling");
  }
  for (size_t index = 0; index < s_native_rows.size(); ++index)
    s_native_rows[index].seen = seen[index];
}

/* Once a frame while the menu is up. */
void maintain_native_menu() {
  MonoObject *list =
      property_object(Reflect::Target(s_native_menu), "get_ListPanel");
  place_card(list);
  sync_rows(list);
}

} // namespace

void note_native_selection(MonoObject *menu, int index, int section) {
  if (menu && section == 0 && Reflect::Target(s_native_menu) == menu)
    s_native_selected = index;
}

void CloseNativeMenu() {
  MonoObject *menu = Reflect::Target(s_native_menu);
  if (menu_is_open(menu))
    Reflect::Call(menu, "Close", 0);
  free_native_menu();
}

bool OpenNativeMenu(const std::vector<NativeMenuItem> &items,
                    const std::string &header, int focus, std::string *failure,
                    MonoObject *scene, const NativeMenuPlacement &placement) {
  Failure result{failure};
  if (!PopupMenuAvailable())
    return result.fail("popup menu selection hook not installed");
  if (!scene)
    scene = FocusedScene();
  if (!scene)
    scene = host_scene();
  if (!scene)
    return result.fail("no scene to open the menu on");
  CloseNativeMenu();
  s_native_placement = placement;
  if (!show_native_menu(items, header, focus, scene, result)) {
    free_native_menu();
    return false;
  }
  s_native_selected = -1;
  s_native_was_open = false;
  return true;
}

NativeMenuEvent PollNativeMenu(int *index) {
  if (!s_native_menu)
    return NativeMenuEvent::None;
  if (native_menu_is_open())
    maintain_native_menu();
  if (s_native_selected >= 0) {
    if (index)
      *index = s_native_selected;
    s_native_selected = -1;
    return NativeMenuEvent::Selected;
  }
  /* Open() takes a frame or two to show it; closed counts once it has been
     seen open. */
  const bool open = native_menu_is_open();
  if (open) {
    s_native_was_open = true;
  } else if (s_native_was_open) {
    free_native_menu();
    return NativeMenuEvent::Closed;
  }
  return NativeMenuEvent::None;
}

bool NativeMenuSwitch(int index, bool *value) {
  const NativeRow *row = row_of_kind(index, RowKind::Switch);
  return row && value &&
         property_value(Reflect::Target(row->config), "get_Value", value);
}

bool NativeMenuSlider(int index, float *value) {
  const NativeRow *row = row_of_kind(index, RowKind::Slider);
  if (!row || !value)
    return false;
  *value = row->number;
  return true;
}

bool NativeMenuChoice(int index, int *chosen) {
  const NativeRow *row = row_of_kind(index, RowKind::Choice);
  if (!row || !chosen)
    return false;
  *chosen = row->chosen;
  return true;
}

} // namespace SystemDialogs
