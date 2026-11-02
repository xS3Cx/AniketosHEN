#pragma once

#include <string>

struct MonoImage;
struct MonoObject;

namespace AniketosTheme::PanelTint {

// Installs the native UI3 Panel background setter hook used by React and PUI.
void Install(MonoImage *pui);

// Colors the shared basemat ancestors selected by a semantic focus-layer ID.
void ApplyFocusHierarchy(MonoObject *focus_layer, const std::string &test_id);

// Advances animated tint modes and restores stock colors when disabled.
void Tick();

} // namespace AniketosTheme::PanelTint
