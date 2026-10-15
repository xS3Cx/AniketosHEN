#pragma once

#include "external_symbols.hpp"

#include <cstdint>

namespace AniketosShell {

bool Initialize(MonoImage *app_system, MonoImage *pui);
bool ResolveSystemHost();
bool AttachMenu();
void DetachMenu();
void Tick();
void HandleInput(uint32_t buttons_down);
void RequestToggle();
bool IsShellAlive();
bool IsAttached();
bool ShouldSuppressShellInput();
MonoObject *HostRoot();

} // namespace AniketosShell
