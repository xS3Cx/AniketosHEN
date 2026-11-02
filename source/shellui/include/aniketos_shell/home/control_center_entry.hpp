#pragma once

#include "external_symbols.hpp"

/* Takes over the Control Center's "Game Base" button: our icon, our label,
 * and a press opens HenMenu instead. Found by the button's test id at run
 * time; if the Control Center of some firmware has no such button, nothing
 * happens. */
namespace ControlCenterEntry {
void Install(MonoImage *react_pui);
void Tick();
}
