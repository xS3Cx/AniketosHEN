#pragma once

#include "external_symbols.hpp"

#include <string>

/* A bookmark on Home: a tab of its own after Emulators, named by the user,
 * holding the games added to it from their Options menu. One for now. The
 * same menu puts a game or app in Home's own Games or Media row too.
 *
 * Its parts:
 *  - a game's Options menu (hook_launch) gets one entry with AniketosHEN's
 *    star in front; its URI comes back here through the boot hook (hook_boot)
 *    and opens a native pop-up menu, as the HEN button's does: "Copy to",
 *    "Move to" and "Remove from", each opening the tabs under the names
 *    they have now (Games, Media, the bookmark). The first game copied or
 *    moved to the bookmark asks for its name with a native prompt. A copy
 *    shows in both places; a moved title leaves the stock row it was in
 *    while it shows somewhere else in Home. Nothing is written to the
 *    system's database: the query hook of home_rows adds the titles to, or
 *    leaves them out of, Home's rows;
 *  - the script in Home (script_injector) adds the tab the way it adds
 *    Emulators, and the query hook of home_rows gives its list the bookmark's
 *    titles.
 *    The tab is there while the bookmark holds a game: Home is reloaded when
 *    it is made, renamed or emptied. The script reads a revision through
 *    Home's registry module, whose Get is hooked here for one key of ours,
 *    and has the rows fetched again when it changes.
 * Kept in <data>/home_bookmark.json. Everything is found by name at run
 * time; the system's own app database is only read. */
namespace HomeBookmark {
/* Carried by the Options menu entry; handed back by the boot hook. */
inline constexpr char kMenuUri[] = "OnionHEN?BookmarkMenu";
/* The content limit by which the tab's list is recognised (the Emulators
 * tab's is the one before). */
inline constexpr int kContentLimit = 7772;
/* AniketosHEN's star (U+EFAA), in front of the Copy / move entry in a game's
   Options menu. The tab itself shows the user's name as typed. */
inline constexpr char kGlyph[] = "\xEE\xBE\xAA";

/* Where a title can be copied or moved. */
enum class Target { Games, Media, Bookmark };
inline constexpr Target kTargets[] = {Target::Games, Target::Media,
                                      Target::Bookmark};

/* Hooks Home's registry module, through which the script follows the
 * bookmark. Without it the bookmark is not offered. */
void Install();
bool Enabled();
/* Whether `title_id` may go in the bookmark: a game or app of the user's,
 * not one of the system's own tiles. */
bool Allowed(const std::string &title_id);
/* The bookmark's name; empty while there is none. */
std::string Name();
/* A name as typed, made fit for a tab's caption: control characters out,
 * spaces trimmed, cut short at a character boundary. */
std::string CleanName(const std::string &typed);
/* The tab belongs in Home: switched on and holding a game. */
bool Shown();
/* The tab's titles as "'ID','ID'" (any thread); empty for none. */
std::string TitlesSql();
/* The Home that is starting (set as the script goes in): whether the script
 * is in it, without which the rows stay as the system has them, and whether
 * it has the tab, without which a title moved there stays in its row. */
void SetHome(bool script_in, bool tab_shown);
/* For the Games or Media row (any thread), "'ID','ID'" lists: the titles
 * added to it, and the ones moved away that show elsewhere in this Home. */
std::string AddedSql(Target row);
std::string HiddenSql(Target row);
/* A URI the boot hook was asked to open; true when it was one of ours.
 * `menu_title` is the title whose Options menu was open last. */
bool HandleUri(const std::string &uri, const std::string &menu_title);
/* Asks for a new name (from the Shell Menu). Any thread. */
void RequestRename();
/* UI thread, once a frame: the prompt. */
void Tick();
} // namespace HomeBookmark
