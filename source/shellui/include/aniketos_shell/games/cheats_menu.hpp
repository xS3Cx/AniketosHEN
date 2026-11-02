#pragma once

#include "external_symbols.hpp"

#include <cstdint>

/* The in-game cheats menu ("Visual Cheats Menu" in the Toolbox): a mod-menu
 * style list of the running game's cheats, drawn in the Game layer over the
 * game and opened with L1+L3+R3. Toggling goes through the same util call as
 * the Toolbox cheats page, so both show the same state. */
namespace AniketosShell::CheatsMenu {

/* Once per UI frame while a game is the running BigApp. Takes the L1+L3+R3
 * request and the menu buttons from the pad reader; returns true while the
 * menu is open and owns the controller. */
bool Tick(bool toggle_requested, uint32_t buttons_down,
          uint32_t buttons_repeated);

/* The Control Center's AniketosHEN menu asked for the cheats: the running
 * game's cheats open as PUI's own card (switches) on `scene`. */
void OpenFromControlCenter(MonoObject *scene);

/* Take the menu down and give the controller back to the game (the game
 * ended, the HEN is going away). Safe to call when it is not open. */
void Close();

/* A game has the screen (LayerManager's focus layer is the game's). */
bool GameHasFocus();

} // namespace AniketosShell::CheatsMenu
