#pragma once

#include "external_symbols.hpp"

struct GamePadData;

namespace AniketosShell {

bool Initialize(MonoImage *app_system, MonoImage *pui);
void Tick();
bool ShouldSuppressShellInput();
/* The open Shell Menu owns the controller: empties ShellUI's pad state so the
 * same press cannot also navigate Home underneath it, and returns true. */
bool FilterPad(GamePadData &data);
MonoObject *HostRoot();

} // namespace AniketosShell
