#pragma once

#include "external_symbols.hpp"

/* The distribution's menu (Toolbox, unload...): a native pop-up opened from
 * its icon in Home's top bar and from its button in the Control Center. */
namespace HomeMenu {
/* True when the pop-up can be shown; otherwise the icon keeps opening the
 * Toolbox directly. */
bool Available();
/* Ask for the menu (any thread). It opens on the next UI frame, on `scene`
 * or, when that is null, on Home's scene. */
void Request(MonoObject *scene = nullptr);
/* UI thread, once a frame. */
void Tick();
}

/* Takes over the Control Center's "Game Base" button: our icon, our label,
 * and a press opens HomeMenu instead. Found by the button's test id at run
 * time; if the Control Center of some firmware has no such button, nothing
 * happens. */
namespace ControlCenterEntry {
void Install(MonoImage *react_pui);
void Tick();
}

/* The distribution's icon as a file PUI can load: written from the copy
 * embedded in this module into our own data folder (never into a system
 * directory). Empty when it could not be written. */
const char *AniketosIconPath();
/* The globe shown beside the Internet switch: white while the connection is
 * enabled, red and struck through while it is off. */
const char *InternetIconPath(bool enabled);
/* The Shell Menu's colour slider tracks: the hues fading to white, black to
   white, plain white (tinted to the colour) and white fading out. */
enum class ColorTrack { Hues, Gray, Solid, Fade };
const char *ColorTrackPath(ColorTrack track);
/* The black gradients laid over the Home video or picture (home_video): a
   vignette, and shading at the top and bottom. */
enum class HomeEffectImage { Vignette, Edges };
const char *HomeEffectImagePath(HomeEffectImage image);
