#pragma once

#include <string>

/* Trophy tools: unlock and lock trophies with the system's own calls, the
   ones its developer trophy screens make (sceNpTrophy2System* for PS5
   titles, sceNpTrophySystem* for PS4 ones), found by name at run time.

   Three ways in:
   - "Unlock all trophies" in a game's options menu on Home (UnlockAllUri,
     opened through the boot URI hook), asked for confirmation first;
   - the Trophies app's options menus, where a script adds View, Unlock and
     Lock (see trophy_inject_js.inc); its choices arrive as console lines;
   - nothing else: every change goes through one worker thread, so the UI
     never waits on the trophy service. */
namespace TrophyTools {

/* The boot URI of "Unlock all trophies" in a game's options menu. */
constexpr const char kUnlockAllUri[] = "OnionHEN?TrophyUnlockAll";

/* Once a frame, on ShellUI's UI thread: confirmations and results. */
void Tick();

/* "Unlock all trophies" of the game whose options menu was opened last
   (`title_id` as in its URL: CUSA12345, PPSA01234). */
void RequestUnlockAll(const std::string &title_id);

/* A request from the Trophies app's script: "op|type|id|user|trophy". */
void HandleScriptMessage(const std::string &message);

/* The script for the Trophies app, and the settings it is given. */
const char *InjectedScript();
std::string ScriptConfig();

} // namespace TrophyTools
