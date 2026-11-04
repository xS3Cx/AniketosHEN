#pragma once

#include <algorithm>

#include <onion/settings.hpp>

extern onion::Settings g_settings;

namespace AniketosTheme {

/* The Shell Menu's master switch for the interface's look: background,
 * focus outlines and tiles, panels, clock, switches, Home video, picture
 * and particles, the avatar status dot. Off, each of them behaves as if its
 * own setting were off; the settings themselves are kept for when it is
 * turned back on. The HEN button and the Home tabs are not part of it. */
inline bool CustomizationOn() { return g_settings.shell_customization_enabled; }

/* How round the Home game tiles are, as a percentage of a full circle, or
 * -1 when they keep Sony's shape (the rounded-tiles switch off). */
inline int TileRoundingPercent() {
  if (!CustomizationOn() || !g_settings.shell_tiles_circular)
    return -1;
  return std::clamp(g_settings.shell_tiles_corner_radius, 0, 100);
}

} // namespace AniketosTheme
