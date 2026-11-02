#pragma once

#include "external_symbols.hpp"

/* The distribution's menu (Toolbox, unload...): a native pop-up opened from
 * its icon in Home's top bar and from its button in the Control Center. */
namespace HenMenu {
/* True when the pop-up can be shown; otherwise the icon keeps opening the
 * Toolbox directly. */
bool Available();
/* Ask for the menu (any thread). It opens on the next UI frame, on `scene`
 * or, when that is null, on Home's scene. */
void Request(MonoObject *scene = nullptr);
/* UI thread, once a frame. */
void Tick();
}
