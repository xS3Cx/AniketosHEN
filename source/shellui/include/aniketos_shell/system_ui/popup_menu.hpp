#pragma once

#include <string>
#include <vector>

#include "external_symbols.hpp"

/* A native pop-up menu: a short list of entries, as the Control Center's
 * cards use. PollPopupMenu reports the chosen entry once; nothing is reported
 * when the menu is dismissed. Needs Install() to have hooked the selection
 * callback, which is what PopupMenuAvailable() says. Found by name and called
 * as system_dialogs.hpp says. */
namespace SystemDialogs {

void Install(MonoImage *pui);
bool PopupMenuAvailable();
struct MenuEntry {
  std::string title;
  /* Shown before the title: a system icon ("cxml://ui3/iconid_...") or the
     path of an image file. Empty for none. */
  std::string icon;
  /* A native switch instead of a plain entry: the row shows an on/off
     toggle, which the user flips in place. */
  bool is_switch = false;
  bool value = false;
  /* A row that opens a menu of its own beside this one (PUI's submenu row):
     these are its rows, to any depth. Choosing one of them is reported by
     PollPopupSubmenu with the row's id. */
  std::vector<MenuEntry> children;
  std::string id;
  /* Header over this row (rows of a submenu; the menu's first row takes the
     `header` argument). */
  std::string header;
  bool enabled = true;
};
/* stay_open: choosing an entry does not close it; the caller closes it
 * (ClosePopupMenu) or the user backs out of it (PopupMenuDismissed).
 * focus: the entry that has the focus when it opens. */
bool ShowPopupMenu(const std::vector<MenuEntry> &entries,
                   const std::string &header, std::string *failure = nullptr,
                   MonoObject *scene = nullptr, bool stay_open = false,
                   int focus = 0);
/* Counts the menus shown: tells an owner whether its menu is still the one
 * on screen (the menu is one at a time). */
uint32_t PopupMenuSerial();
/* A menu of ours (this one or the HEN menu) is on screen. */
bool PopupMenuOpen();
/* True once, when a stay-open menu has been backed out of by the user. */
bool PopupMenuDismissed();
void ClosePopupMenu();
bool PollPopupMenu(int *index);
/* A row of a submenu of the menu shown last was chosen: its id, once. */
bool PollPopupSubmenu(std::string *id);
/* The current position of the switch at `index` of the menu shown last.
 * False when that entry is not a switch or the menu is gone. */
bool PopupMenuSwitchValue(int index, bool *value);
/* Changes the icon of an entry of the menu shown last. */
void SetPopupMenuEntryIcon(int index, const std::string &icon);

} // namespace SystemDialogs
