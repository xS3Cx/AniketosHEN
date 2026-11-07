#include "aniketos_shell/system_ui/popup_menu.hpp"

#include "system_dialogs_internal.hpp"

#include <onion/detour.h>
#include <onion/log.h>

#include <utility>

namespace SystemDialogs {
namespace {

uint32_t s_menu_handle = 0;
std::vector<uint32_t> s_menu_entries; /* the entry configs, kept alive */
int s_menu_selected = -1;
/* The rows of the submenus of the menu shown last (configs, strong), with
   their ids, and the id of the one chosen last. */
std::vector<std::pair<uint32_t, std::string>> s_submenu_rows;
std::string s_submenu_selected;
/* The popup stays open when an entry is chosen (ShowPopupMenu's stay_open),
   and has been seen open since it was shown. */
bool s_menu_stays = false;
bool s_menu_was_open = false;
uint32_t s_menu_serial = 0; /* ShowPopupMenu calls so far */

using NotifyItemSelected = void (*)(MonoObject *, MonoObject *, int, int);
NotifyItemSelected s_notify_item_selected_original = nullptr;

/* A row chosen in some menu other than ours: one of our submenus' rows? */
void note_submenu_choice(MonoObject *menu, int index, int section) {
  int32_t section_index = section, row_index = index;
  void *args[2] = {&section_index, &row_index};
  MonoObject *config = nullptr;
  if (!Reflect::Call(menu, "GetMenuListItemConfig", 2, args, &config) ||
      !config)
    return;
  for (const auto &row : s_submenu_rows)
    if (Reflect::Target(row.first) == config) {
      s_submenu_selected = row.second;
      LOG_INFO("[SYSDIALOG] submenu row chosen id='%s'", row.second.c_str());
      return;
    }
}

/* PopupMenuBase.NotifyItemSelected(MenuSelectedEventArgs, int index, int
   section): PUI calls it when an entry is chosen. Managed events need a
   managed delegate, which native code cannot make here, so the choice is
   picked up on its way through instead. */
void notify_item_selected_hook(MonoObject *menu, MonoObject *args, int index,
                               int section) {
  MonoObject *popup = Reflect::Target(s_menu_handle);
  if (menu && popup == menu)
    s_menu_selected = index;
  note_native_selection(menu, index, section);
  /* a menu PUI opened from one of our submenu rows */
  if (menu && s_menu_handle && !s_submenu_rows.empty() && popup != menu)
    note_submenu_choice(menu, index, section);
  if (s_notify_item_selected_original)
    s_notify_item_selected_original(menu, args, index, section);
}

/* The config of one row (and of its submenu's rows, to any depth). */
MonoObject *make_row(const MenuEntry &entry, MonoMethod *from_text, bool top,
                     Failure &result) {
  MonoObject *exception = nullptr;
  MonoObject *config = nullptr;
  if (!entry.children.empty()) {
    config = create(ui3_class("MenuListSubmenuConfig"));
    MonoObject *rows = new_list_for(config, "get_Items");
    if (!rows) {
      result.fail("cannot create a submenu");
      return nullptr;
    }
    set_text_property(config, "set_Title", entry.title);
    for (const MenuEntry &child : entry.children) {
      MonoObject *row = make_row(child, from_text, false, result);
      if (!row || !checked_call(rows, "Add", row, result))
        return nullptr;
    }
    set_property(config, "set_Items", rows);
  } else if (entry.is_switch) {
    config = create(ui3_class("MenuListSwitchConfig"));
    if (config) {
      set_text_property(config, "set_Title", entry.title);
      bool value = entry.value;
      set_property(config, "set_Value", &value);
    }
  } else {
    config = config_from_text(from_text, entry.title, &exception);
  }
  if (exception || !config) {
    result.fail("popup menu entry: " + exception_text(exception));
    return nullptr;
  }
  const uint32_t handle = mono_gchandle_new(config, 0);
  if (top)
    s_menu_entries.push_back(handle);
  else
    s_submenu_rows.push_back({handle, entry.id});
  set_text_property(config, "set_Header", entry.header);
  decorate_row(config, entry.icon, entry.enabled);
  return config;
}

/* Gives `menu` these entries: on a menu that is on screen its rows change
   where they are. */
bool fill_popup(MonoObject *menu, const std::vector<MenuEntry> &entries,
                const std::string &header, Failure &result) {
  MonoMethod *from_text = from_string_operator("MenuListButtonConfig");
  if (!menu || !from_text)
    return result.fail("popup menu classes not found");

  /* The entry list is a List<IMenuListItemConfig>: make one of whatever type
     the ContextItems property is declared with. */
  MonoObject *list = new_list_for(menu, "get_ContextItems");
  if (!list)
    return result.fail("cannot create the popup menu entry list");

  for (uint32_t &handle : s_menu_entries)
    Reflect::Release(handle);
  s_menu_entries.clear();
  for (auto &row : s_submenu_rows)
    Reflect::Release(row.first);
  s_submenu_rows.clear();
  s_submenu_selected.clear();
  bool first = true;
  for (const MenuEntry &entry : entries) {
    MonoObject *config = make_row(entry, from_text, true, result);
    if (!config)
      return false;
    if (first)
      set_text_property(config, "set_Header", header);
    first = false;
    if (!checked_call(list, "Add", config, result))
      return false;
  }
  return checked_call(menu, "set_ContextItems", list, result);
}

void focus_popup_row(MonoObject *menu, int focus) {
  if (focus <= 0)
    return;
  int32_t index = focus;
  set_property(menu, "set_SelectedIndex", &index);
}

MonoObject *entry_config(int index) {
  return index >= 0 && static_cast<size_t>(index) < s_menu_entries.size()
             ? Reflect::Target(s_menu_entries[index])
             : nullptr;
}

} // namespace

void Install(MonoImage *pui) {
  if (!pui || s_notify_item_selected_original)
    return;
  MonoClass *menu_base = mono_class_from_name(pui, kUi3, "PopupMenuBase");
  const uint64_t address =
      menu_base ? Get_Address_of_Method(pui, menu_base, "NotifyItemSelected", 3)
                : 0;
  const bool installed =
      address &&
      InstallDetour(address,
                    reinterpret_cast<void *>(&notify_item_selected_hook),
                    reinterpret_cast<void **>(&s_notify_item_selected_original));
  LOG_INFO("[SYSDIALOG] popup menu selection hook target=0x%lx installed=%d",
           static_cast<unsigned long>(address), installed ? 1 : 0);
  if (!installed)
    s_notify_item_selected_original = nullptr;
}

bool PopupMenuAvailable() { return s_notify_item_selected_original != nullptr; }

bool ShowPopupMenu(const std::vector<MenuEntry> &entries,
                   const std::string &header, std::string *failure,
                   MonoObject *scene, bool stay_open, int focus) {
  Failure result{failure};
  if (!PopupMenuAvailable())
    return result.fail("popup menu selection hook not installed");
  if (!scene)
    scene = host_scene();
  if (!scene)
    return result.fail("no Home scene to open the menu on");
  MonoObject *menu = create(ui3_class("PopupMenu"));
  if (!fill_popup(menu, entries, header, result))
    return false;
  if (stay_open) {
    bool dismiss = false;
    set_property(menu, "set_DismissOnSelect", &dismiss);
  }
  focus_popup_row(menu, focus);
  if (!checked_call(menu, "Open", scene, result))
    return false;
  Reflect::Hold(s_menu_handle, menu, /*strong=*/true);
  s_menu_selected = -1;
  s_menu_stays = stay_open;
  s_menu_was_open = false;
  ++s_menu_serial;
  LOG_INFO("[SYSDIALOG] popup menu opened entries=%zu stay=%d", entries.size(),
           stay_open ? 1 : 0);
  return true;
}

uint32_t PopupMenuSerial() { return s_menu_serial; }

bool PopupMenuOpen() { return menu_is_open(Reflect::Target(s_menu_handle)); }

bool PopupMenuDismissed() {
  if (!s_menu_handle || !s_menu_stays)
    return false;
  MonoObject *menu = Reflect::Target(s_menu_handle);
  if (menu_is_open(menu)) {
    s_menu_was_open = true;
    return false;
  }
  if (menu && !s_menu_was_open)
    return false; /* still on its way in */
  Reflect::Release(s_menu_handle);
  s_menu_stays = false;
  return true;
}

void ClosePopupMenu() {
  MonoObject *menu = Reflect::Target(s_menu_handle);
  if (menu_is_open(menu))
    Reflect::Call(menu, "Close", 0);
  Reflect::Release(s_menu_handle);
  s_menu_stays = false;
}

bool PopupMenuSwitchValue(int index, bool *value) {
  return value && Reflect::Target(s_menu_handle) &&
         property_value(entry_config(index), "get_Value", value);
}

void SetPopupMenuSwitch(int index, bool value) {
  if (MonoObject *config = entry_config(index))
    set_property(config, "set_Value", &value);
}

void SetPopupMenuEntryIcon(int index, const std::string &icon) {
  if (MonoObject *config = entry_config(index))
    set_property(config, "set_IconUri", managed_string(icon));
}

bool PollPopupMenu(int *index) {
  if (s_menu_selected < 0 || !index)
    return false;
  *index = s_menu_selected;
  s_menu_selected = -1;
  if (!s_menu_stays) /* otherwise still on screen: ours to close */
    Reflect::Release(s_menu_handle);
  return true;
}

bool PollPopupSubmenu(std::string *id) {
  if (s_submenu_selected.empty() || !id)
    return false;
  *id = s_submenu_selected;
  s_submenu_selected.clear();
  return true;
}

} // namespace SystemDialogs
