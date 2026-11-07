#pragma once

#include <string>

/* x5Guard: AniketosHEN's guard. Each shield stops one kind of action that
   could cost the user the jailbreak (the PlayStation Network Blocker stops
   every PSN sign-in, connection and system update attempt). Whatever a
   shield stops is reported here: logged, counted and shown as the guard's
   top banner. Runtime only: nothing is written outside /data/AniketosHEN. */
namespace X5Guard {

enum class Shield { PsnBlocker };

/* The shield's switch (Toolbox > x5Guard). */
bool Enabled(Shield shield);

/* A blocked action. `event` is a catalog code ("signin.utility",
   "update.dialog", ...: see guard.cpp), `detail` the technical name of what
   was stopped (a method, a link, a use case id). Any thread. */
void Report(Shield shield, const char *event, const std::string &detail);

/* Since the HEN started, for the Toolbox page. */
unsigned BlockedCount();
std::string LastBlocked(); /* the technical label of the last one, or empty */

} // namespace X5Guard
