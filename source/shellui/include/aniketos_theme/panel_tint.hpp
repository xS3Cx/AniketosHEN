#pragma once

#include <string>

struct MonoImage;
struct MonoObject;

namespace AniketosTheme::SharedUI {

// Installs the native UI3 Panel background setter hook used by React and PUI.
void Install(MonoImage *pui);

// Colors the shared basemat ancestors selected by a semantic focus-layer ID.
void ApplyFocusHierarchy(MonoObject *focus_layer, const std::string &test_id);

// Queues an already-resolved modal ReactPanel for a deferred render-frame tint.
void QueueModalPanel(MonoObject *panel, const std::string &anchor_test_id);

// Advances animated tint modes and restores stock colors when disabled.
void Tick();

// Releases pinned HomeUI panels before the React application is reloaded.
void ClearTrackedPanels();

} // namespace AniketosTheme::SharedUI
