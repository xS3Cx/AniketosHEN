#pragma once

#include "external_symbols.hpp"

#include <string>

/* A bookmark on Home: a tab of its own after Emulators, named by the user,
 * holding the games added to it from their Options menu (they stay in the
 * Games row too). One for now.
 *
 * Its parts:
 *  - a game's Options menu (hook_launch) gets "Add to Bookmark" ("Add to
 *    <name>" once it exists) or "Remove from <name>"; the URIs they carry
 *    come back here through the boot hook (hook_boot). The first game added
 *    asks for the bookmark's name with a native prompt;
 *  - the script in Home (home_js) adds the tab the way it adds Emulators,
 *    and the query hook of home_tabs gives its list the bookmark's titles.
 *    The tab is there while the bookmark holds a game: Home is reloaded when
 *    it is made, renamed or emptied. The script reads a revision through
 *    Home's registry module, whose Get is hooked here for one key of ours,
 *    and has the rows fetched again when it changes.
 * Kept in <data>/home_bookmark.json. Everything is found by name at run
 * time; the system's own app database is only read. */
namespace HomeBookmark {
/* Carried by the Options menu entries; handed back by the boot hook. */
inline constexpr char kAddUri[] = "OnionHEN?BookmarkAdd";
inline constexpr char kRemoveUri[] = "OnionHEN?BookmarkRemove";
/* The content limit by which the tab's list is recognised (the Emulators
 * tab's is the one before). */
inline constexpr int kContentLimit = 7772;
/* Glyph in front of the tab's caption: U+EFAA (a star). */
inline constexpr char kGlyph[] = "\xEE\xBE\xAA";

/* Hooks Home's registry module, through which the script follows the
 * bookmark. Without it the bookmark is not offered. */
void Install();
bool Enabled();
/* Whether `title_id` may go in the bookmark: a game or app of the user's,
 * not one of the system's own tiles. */
bool Allowed(const std::string &title_id);
bool Contains(const std::string &title_id);
/* The bookmark's name; empty while there is none. */
std::string Name();
/* The tab belongs in Home: switched on and holding a game. */
bool Shown();
/* The tab's WHERE clause, made from Home's for its list. */
std::string Where(const std::string &where);
/* A URI the boot hook was asked to open; true when it was one of ours.
 * `menu_title` is the title whose Options menu was open last. */
bool HandleUri(const std::string &uri, const std::string &menu_title);
/* Asks for a new name (from the Shell Menu). Any thread. */
void RequestRename();
/* UI thread, once a frame: the prompt. */
void Tick();
} // namespace HomeBookmark
