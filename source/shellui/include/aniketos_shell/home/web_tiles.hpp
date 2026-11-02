#pragma once

#include <string>

/* The web browser's additions: full screen and websites kept as tiles among
 * Home's games.
 *
 * Both live in JavaScript (web_browser_inject_js.inc in the browser's card,
 * home_inject_js.inc on Home). This side keeps the list of sites in the user's
 * data folder and has the util process fetch each site's icon. Home's rows
 * are made up in Home's script: nothing here touches the app database or any
 * system file, so a tile is gone while the HEN is not running. */
namespace WebTiles {

/* What Home's script asks its registry module for. */
inline constexpr const char *kRegistryKey = "aniketos.web_tiles";

/* A script defining __aniketosWeb: the texts and the sites on Home. */
std::string ScriptConfig();
const char *InjectedScript();

/* {"rev":N,"tiles":[...]}, valid JSON and valid plain-ASCII JavaScript. */
std::string HomeState();

/* A line of the browser's script (any thread): the JSON after "ANIKETOS:web|". */
void HandleScriptMessage(const std::string &message);

void Tick();

} // namespace WebTiles
