#pragma once

#include "external_symbols.hpp"

#include <string>

/* Home's tab bar ("Games", "Media", and ours: "Emulators").
 *
 * The Emulators tab is a real space in Home's own React code, added by the
 * script home_js.cpp puts into Home's runtime. This side supplies what the
 * script cannot: which installed titles are emulators, and the database
 * filter that gives the new space its tile row (and takes the same titles
 * out of Games). It also keeps each tab's focus frame around the whole
 * caption, glyph included.
 *
 * Games and Media can show names of the user's (home_tab_games_name,
 * home_tab_media_name): React keeps Sony's caption, and the label the text
 * hook dresses with the tab's glyph is given the user's name once it turns
 * out to be in the tab bar. The Emulators tab can be left out
 * (home_tab_emulators); its titles then stay in Games. Everything is found
 * by name at run time. */
namespace HomeTabs {
/* Glyphs in front of the captions: U+EFCE (Games), U+F871 (Media) and
 * U+EFB1 (Emulators, system symbol 01). */
inline constexpr char kGamesGlyph[] = "\xEE\xBF\x8E";
inline constexpr char kMediaGlyph[] = "\xEF\xA1\xB1";
inline constexpr char kEmulatorsGlyph[] = "\xEE\xBE\xB1";
/* The content limit by which the Emulators list's query is recognised. */
inline constexpr int kEmulatorsContentLimit = 7771;

/* Home's own tabs, the ones that can be renamed. */
enum class StockTab { Games, Media };

void Install(MonoImage *pui);
/* UI thread, once a frame. */
void Tick();
/* True when the Emulators tab can be given a tile row (the database query
 * hook is in place): the condition for adding the tab. */
bool Ready();
/* True when at least one installed emulator was found. With none, entering
 * the tab shows a notice and returns to Games. */
bool HasEmulators();
/* Whether the Home that is starting has the Emulators tab (set as the
 * script goes in): only then are the emulators kept out of Games. */
void SetEmulatorsTabShown(bool shown);
/* What the text hook is to show in a label it is giving the glyph of a
 * stock tab (UI thread), `stock` being the system's caption it was handed:
 * the glyph and the user's name for a label known to be in the tab bar, or
 * empty for the glyph and `stock` (a new label then gets the name as soon
 * as it turns out to be in the bar). */
std::string TabLabelText(MonoObject *label, StockTab tab,
                         const std::string &stock);
/* What a stock tab is called now (any thread): the user's name, else the
 * system's caption as last seen, else ours for it. */
std::string TabName(StockTab tab);
/* Asks for a new name for a stock tab (from the Shell Menu). Any thread. */
void RequestRename(StockTab tab);
/* Games and Media back to the system's captions (from the Shell Menu; UI
 * thread). */
void RestoreNames();
}
