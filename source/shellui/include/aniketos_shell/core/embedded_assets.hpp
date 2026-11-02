#pragma once

/* Images embedded in this module (embedded_assets.s.in), as files PUI can
 * load: written into our own data folder, never into a system directory.
 * Each path is empty when the file could not be written. */

/* The distribution's icon, in the logo colour when one is chosen. */
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
