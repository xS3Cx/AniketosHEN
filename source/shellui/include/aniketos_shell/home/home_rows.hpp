#pragma once

/* What Home's rows of tiles hold. AppBrowseItemAccessor.BuildWhereClause is
 * hooked for the Games and Media rows and the lists of the tabs we add: this
 * side knows which installed titles are emulators (the words in their names,
 * and <data>/emulators.txt), which titles the bookmark holds or moved, and
 * which apps are hidden. The system's own app database is only read. */
namespace HomeRows {
/* The content limit by which the Emulators list's query is recognised. */
inline constexpr int kEmulatorsContentLimit = 7771;

void Install();
/* True when the Emulators tab can be given a tile row (the database query
 * hook is in place): the condition for adding the tab. */
bool Ready();
/* Whether the Home that is starting has the Emulators tab (set as the script
 * goes in): only then are the emulators kept out of Games. */
void SetEmulatorsTabShown(bool shown);
/* Whether an emulator is installed; the titles are read again while none
 * was, in case one has been installed since. */
bool FindEmulators();
}
