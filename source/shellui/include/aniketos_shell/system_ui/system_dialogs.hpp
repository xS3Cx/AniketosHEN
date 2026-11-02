#pragma once

#include <string>
#include <vector>

#include "external_symbols.hpp"

/* ShellUI's own dialogs, opened with our text and buttons.
 *
 * Everything is found by name through Mono reflection at the time of the
 * call (classes, methods and fields of Sce.Vsh.UILib and Sce.PlayStation.PUI),
 * so nothing here depends on firmware-specific addresses. A firmware that
 * renames or drops one of them makes the call fail with a message instead of
 * misbehaving. Call from the UI thread only. */
namespace SystemDialogs {

/* The system error screen ("Something went wrong with this game or app"). */
struct ErrorOptions {
  std::string title;
  std::string message;
  std::string positive;     /* empty: the system's default button */
  std::string negative;     /* empty: no second button */
  std::string positive_uri; /* opened when the positive button is pressed */
  int error_code = 0;
  int show_code = -1; /* -1 system default, 0 hide, 1 show */
  bool popup = false; /* false: full screen */
};
bool ShowError(const ErrorOptions &options, std::string *failure = nullptr);

/* A plain PUI alert: up to four buttons and an optional check box. */
enum class AlertStyle { FullScreen, HalfScreen, Popup, TransparentPopup };
struct AlertOptions {
  AlertStyle style = AlertStyle::Popup;
  std::string title;
  std::string message;
  std::string positive;
  std::string negative;
  std::string neutral;
  std::string second_neutral;
  std::string check_box;
};
/* `scene` is the scene to open on; null means the one Home lives in. */
bool ShowAlert(const AlertOptions &options, std::string *failure = nullptr,
               MonoObject *scene = nullptr);

/* Which button closed the alert opened last: 0 positive, 1 negative,
 * 2 neutral, 3 second neutral, 4 dismissed. False while it is still open (or
 * none was opened). Reports each alert once. */
bool PollAlert(int *result);

/* A line of text from the user: PUI's prompt dialog (a message, a text field
 * that opens the system keyboard, OK and Cancel). */
struct PromptOptions {
  std::string title;
  std::string message;
  std::string text; /* what the field starts with */
  std::string positive;
  std::string negative;
};
bool ShowPrompt(const PromptOptions &options, std::string *failure = nullptr,
                MonoObject *scene = nullptr);
/* The prompt opened last has closed: true once, with whether it was
 * confirmed and what its field held. False while it is open. */
bool PollPrompt(bool *confirmed, std::string *text);

/* A native pop-up menu: a short list of entries, as the Control Center's
 * cards use. PollPopupMenu reports the chosen entry once; nothing is reported
 * when the menu is dismissed. Needs Install() to have hooked the selection
 * callback, which is what PopupMenuAvailable() says. */
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
/* Gives the menu on screen other entries, in place. */
bool UpdatePopupMenu(const std::vector<MenuEntry> &entries,
                     const std::string &header, int focus = 0,
                     std::string *failure = nullptr);
/* True once, when a stay-open menu has been backed out of by the user. */
bool PopupMenuDismissed();
void ClosePopupMenu();
/* Debug console: as if entry `index` of the popup had been chosen. */
void DebugPressPopupMenu(int index);
bool PollPopupMenu(int *index);
/* A row of a submenu of the menu shown last was chosen: its id, once. */
bool PollPopupSubmenu(std::string *id);
/* The current position of the switch at `index` of the menu shown last.
 * False when that entry is not a switch or the menu is gone. */
bool PopupMenuSwitchValue(int index, bool *value);
/* Changes the icon of an entry of the menu shown last. */
void SetPopupMenuEntryIcon(int index, const std::string &icon);

/* A native menu that stays open while it is used: PUI's PopupMenu filled
 * with its own row types (button, switch, slider). Focus, scrolling, sounds
 * and input are PUI's. One at a time. */
struct NativeMenuItem {
  enum class Kind { Button, Switch, Slider, Choice };
  Kind kind = Kind::Button;
  std::string title;
  std::string value;       /* Button: text at the right of the row */
  std::string description; /* a second line under the title */
  std::string icon;
  bool enabled = true;
  bool on = false;                                     /* Switch */
  float number = 0, minimum = 0, maximum = 1, step = 1; /* Slider */
  std::vector<std::string> choices; /* Choice: a drop-down list */
  int chosen = 0;
};
/* Where the card goes: hung to the left of a point `margin` px from the
 * right edge (the default), or to the right of one `margin` px from the left
 * edge; vertically centred either way. `min_width` is the card's width. */
struct NativeMenuPlacement {
  bool left = false;
  float margin = 96.0f;
  float min_width = 720.0f;
};
/* `focus` is the row that has the focus when it opens. */
bool OpenNativeMenu(const std::vector<NativeMenuItem> &items,
                    const std::string &header, int focus = 0,
                    std::string *failure = nullptr, MonoObject *scene = nullptr,
                    const NativeMenuPlacement &placement = {});
enum class NativeMenuEvent { None, Selected, Closed };
/* Selected: a row was pressed (a switch has already flipped). Closed: the
 * user dismissed the menu. Each is reported once. */
NativeMenuEvent PollNativeMenu(int *index);
bool NativeMenuSwitch(int index, bool *value);
bool NativeMenuSlider(int index, float *value);
bool NativeMenuChoice(int index, int *chosen);
/* Closes it without a Closed event (to open another in its place). */
void CloseNativeMenu();
bool NativeMenuOpen();
/* Debug console: as if row `index` had been pressed. */
void DebugPressNativeMenu(int index);
/* The scene that has the focus now: where a menu opened by the user should
 * appear, whatever is on screen. Null when it cannot be told. */
MonoObject *FocusedScene();

/* Closes whatever ShowError/ShowAlert opened last, if it is still open. */
void CloseLast();

} // namespace SystemDialogs
