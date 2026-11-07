#pragma once

#include <string>

/* "Plugins" in a game's options menu on Home: the .sprx/.prx plugins of the
   plugins folder, each with a switch that makes it load into that game (the
   SPRX catalog's title IDs, changed through the daemon). A plugin set to
   every game shows its switch greyed out: that is changed in the Toolbox. */
namespace SprxGameMenu {

/* The boot URI of the row in a game's options menu. */
constexpr const char kMenuUri[] = "OnionHEN?SprxPlugins";

/* The menu for this title (CUSA12345, PPSA01234), opened on the next Tick. */
void Request(const std::string &title_id);

/* Once a frame, on ShellUI's UI thread. */
void Tick();

} // namespace SprxGameMenu
