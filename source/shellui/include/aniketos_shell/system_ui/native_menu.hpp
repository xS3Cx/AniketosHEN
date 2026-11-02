#pragma once

#include <string>
#include <vector>

#include "external_symbols.hpp"

/* A native menu that stays open while it is used: PUI's PopupMenu filled
 * with its own row types (button, switch, slider). Focus, scrolling, sounds
 * and input are PUI's. One at a time. Found by name and called as
 * system_dialogs.hpp says. */
namespace SystemDialogs {

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

} // namespace SystemDialogs
