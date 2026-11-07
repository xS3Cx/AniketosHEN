#pragma once

#include <string>

/* "Plugins" in a game's options menu on Home: the game's Load on startup
   switch and one switch per plugin of its folders (<title>/ and all/), the
   same config.ini the Toolbox's game page writes. catalog.ini / plugins.ini
   entries for the game are listed greyed out. */
namespace SprxGameMenu {

/* The boot URI of the row in a game's options menu. */
constexpr const char kMenuUri[] = "OnionHEN?SprxPlugins";

/* The menu for this title (CUSA12345, PPSA01234), opened on the next Tick. */
void Request(const std::string &title_id);

/* Once a frame, on ShellUI's UI thread. */
void Tick();

} // namespace SprxGameMenu
