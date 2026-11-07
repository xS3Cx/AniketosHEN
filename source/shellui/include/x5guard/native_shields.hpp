#pragma once

namespace X5Guard {

/* The PlayStation Network Blocker's managed detours: the sign-in and system
   update actions ShellUI starts from its own (non React Native) screens.
   Each is found by assembly, class, method name and argument count; one that
   is missing stays off with a warning. Called once, after ShellUI is up. */
void InstallNativeShields();

} // namespace X5Guard
