#pragma once

#include "external_symbols.hpp"

/* Home's tab bar ("Games", "Media", and ours: "Emulators").
 *
 * The Emulators tab is a real space in Home's own React code, added by the
 * script home_js.cpp puts into Home's runtime. This side supplies what the
 * script cannot: which installed titles are emulators, and the database
 * filter that gives the new space its tile row (and takes the same titles
 * out of Games). It also keeps each tab's focus frame around the whole
 * caption, glyph included. Everything is found by name at run time. */
namespace HomeTabs {
/* Glyph in front of the Emulators caption: U+EFB1 (system symbol 01). */
inline constexpr char kEmulatorsGlyph[] = "\xEE\xBE\xB1";
/* The content limit by which the Emulators list's query is recognised. */
inline constexpr int kEmulatorsContentLimit = 7771;

void Install(MonoImage *pui);
/* UI thread, once a frame. */
void Tick();
/* True when the Emulators tab can be given a tile row (the database query
 * hook is in place): the condition for adding the tab. */
bool Ready();
/* True when at least one installed emulator was found. With none, entering
 * the tab shows a notice and returns to Games. */
bool HasEmulators();
}
