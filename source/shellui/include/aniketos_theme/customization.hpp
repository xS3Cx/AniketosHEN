#pragma once

#include <onion/settings.hpp>

extern onion::Settings g_settings;

namespace AniketosTheme {

/* The Shell Menu's master switch for the interface's look: background,
 * focus outlines and tiles, panels, clock, switches, Home video, picture
 * and particles, the avatar status dot. Off, each of them behaves as if its
 * own setting were off; the settings themselves are kept for when it is
 * turned back on. The HEN button and the Emulators tab are not part of it. */
inline bool CustomizationOn() { return g_settings.shell_customization_enabled; }

} // namespace AniketosTheme
