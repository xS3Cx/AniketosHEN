#pragma once

#include "external_symbols.hpp"

#include <string>

/* Folders of games on Home's Games row.
 *
 * A folder is a name and a list of title ids, kept in
 * <data>/home_folders.json. Its parts:
 *  - a game's Options menu (hook_launch) gets "Add to Folder" and, for a
 *    game in a folder, "Remove from Folder"; the URIs they carry come back
 *    here through the boot hook (hook_boot). Adding opens a native pop-up
 *    menu of the folders, with "New Folder" (a native prompt for its name)
 *    and the management of the folders (rename, delete), which Options on
 *    a folder's tile and the Shell Menu open too;
 *  - the database query behind the row (home_tabs' query hook): titles in a
 *    folder are left out of Games;
 *  - the script in Home (home_js) puts a tile for each folder in the row. It
 *    reads the folders through Home's own registry module, whose Get is
 *    hooked here for one key of ours. Below a folder's tile Home shows the
 *    Game Library as its hub (Down, or the tile pressed), with a parameter
 *    naming the folder; the script in that library (library_inject_js.inc)
 *    marks its lists with a content limit of kLibraryContentLimit plus the
 *    folder's id, and the query hook keeps only the folder's titles in them.
 * Everything is found by name at run time. */
namespace HomeFolders {
/* Carried by the Options menu entries; handed back by the boot hook. */
inline constexpr char kAddUri[] = "OnionHEN?FolderAdd";
inline constexpr char kRemoveUri[] = "OnionHEN?FolderRemove";
/* A folder tile's link (with the folder's id after it). Home opens it when
 * the tile is pressed; the script has already acted on the press, so it
 * only has to be taken out of the way. */
inline constexpr char kTileUri[] = "OnionHEN?Folder=";
/* Content limits from here up mark the Game Library's lists of a folder's
 * hub: the folder's id is the amount above it. */
inline constexpr int kLibraryContentLimit = 7800;

/* Favorites: a tab of Home's next to Emulators (made by the script in Home
 * the same way), holding the games added to it from their Options menu.
 * They stay in the Games row too. The tab is there while it holds a game:
 * Home is reloaded when the first one is added or the last one removed. */
inline constexpr char kFavoriteAddUri[] = "OnionHEN?FavoriteAdd";
inline constexpr char kFavoriteRemoveUri[] = "OnionHEN?FavoriteRemove";
/* The content limit by which the Favorites tab's list is recognised. */
inline constexpr int kFavoritesContentLimit = 7772;
/* Glyph in front of the Favorites caption: U+EFBD (a heart). */
inline constexpr char kFavoritesGlyph[] = "\xEE\xBE\xBD";
bool FavoritesEnabled();
bool IsFavorite(const std::string &title_id);
/* Whether `title_id` may be a favourite (a game or app of the user's). */
bool FavoriteAllowed(const std::string &title_id);
/* The Favorites tab belongs in Home: switched on and holding a game. */
bool FavoritesShown();
/* The Favorites tab's WHERE clause, made from Home's for its list. */
std::string FavoritesWhere(const std::string &where);

/* Hooks Home's registry module, through which the script reads the
 * folders. Without it the script finds none and Home stays as it is. */
void Install();
bool Enabled();
/* Whether `title_id` may go in a folder: a game or app of the user's, not
 * one of the system's own tiles. */
bool Fileable(const std::string &title_id);
bool InFolder(const std::string &title_id);
/* A URI the boot hook was asked to open; true when it was one of ours.
 * `menu_title` is the title whose Options menu was open last. */
bool HandleUri(const std::string &uri, const std::string &menu_title);
/* The Games row's WHERE clause with the folders applied: the clause to use
 * instead of `where`, or `where` itself when nothing changes. */
std::string GamesRowWhere(const std::string &where);
/* A list of the Game Library made with `content_limit`: when that marks a
 * folder's hub, the clause keeping only the folder's titles; otherwise
 * `where` itself. */
std::string LibraryWhere(int content_limit, const std::string &where);
/* A console line of Home's script, after "ANIKETOS:folder|":
 * "options|<id>", Options pressed on a folder's tile. */
void HandleScriptMessage(const std::string &message);
/* Opens the list of the folders, each with Rename and Delete (from the
 * Shell Menu). Any thread; shown on the next UI frame. */
void RequestManage();
/* UI thread, once a frame: the pop-up menus and prompts. */
void Tick();
} // namespace HomeFolders
