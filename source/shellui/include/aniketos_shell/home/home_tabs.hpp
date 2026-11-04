#pragma once

#include "external_symbols.hpp"

#include <string>

/* Home's tab bar ("Games", "Media" and the user's custom tabs).
 *
 * Games and Media can show names and icons of the user's
 * (home_tab_games_name, home_tab_media_name, home_tab_games_icon,
 * home_tab_media_icon): React keeps Sony's caption, and the label the text
 * hook dresses is given the user's caption once it turns out to be in the tab
 * bar. The custom tabs are added by the script script_injector.cpp puts into
 * Home's runtime; their captions come from the same settings and from
 * /data/AniketosHEN/home_tabs.json. Everything is found by name at run time. */
namespace HomeTabs {
/* Home's own tabs, the ones that can be renamed. */
enum class StockTab { Games, Media };

bool IsHomeTabLabel(MonoObject *label);

/* Whether the tabs carry their icons (home_tab_icons). */
bool IconsShown();
/* The icon a stock tab shows: the user's setting, or the default. */
std::string StockIcon(StockTab tab);
/* A tab's caption: `name` with `icon` in front while the icons are shown and
 * the icon is not empty, the name alone otherwise. */
std::string Caption(const std::string &icon, const std::string &name);
/* For Home's script, which gives React a stock tab's caption so that React
 * lays the bar out for the text the tab really shows: `name` is the user's
 * caption with its icon (empty while the tab keeps the system's caption) and
 * `icon` what goes in front of the system's caption (empty without icons). */
void ScriptCaption(StockTab tab, std::string *name, std::string *icon);

void Install(MonoImage *pui);
/* UI thread, once a frame. */
void Tick();
/* What the text hook is to show in a label it is giving the icon of a stock
 * tab (UI thread), `stock` being the system's caption it was handed: the
 * icon and the user's name for a label known to be in the tab bar, or empty
 * for the icon and `stock` (a new label then gets the name as soon as it
 * turns out to be in the bar). */
std::string TabLabelText(MonoObject *label, StockTab tab,
                         const std::string &stock);
/* What a stock tab is called now (any thread): the user's name, else the
 * system's caption as last seen, else ours for it. */
std::string TabName(StockTab tab);
/* Asks for a new name for a stock tab (from the Shell Menu). Any thread. */
void RequestRename(StockTab tab);
/* Asks for a new icon for a stock tab (from the Shell Menu). Any thread. */
void RequestIcon(StockTab tab);
/* Games and Media back to the system's captions and every tab's icon back to
 * its default (from the Shell Menu; UI thread). */
void RestoreDefaults();
bool ConsumeUiChange();
}
