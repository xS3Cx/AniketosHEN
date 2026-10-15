#pragma once

#include <string>

/* The shape of ShellUI's picture: ultrawide formats (and rotation).
 *
 * The console always sends a 16:9 picture. On a 21:9 monitor that is either
 * shown with bars at the sides or stretched, and stretched it is distorted.
 * An ultrawide format here lays the interface out for the wider screen and
 * squeezes it into the 16:9 picture, so that a monitor set to stretch the
 * picture across the panel ("Full" / "Wide" aspect) shows it undistorted
 * and edge to edge.
 *
 * How: PUI draws everything through one camera (UIValues.Camera: position,
 * target, up vector, frustum). Widening its frustum squeezes the picture;
 * the layout's screen size is widened to match, React apps are resized and
 * told their new window size through React Native's own DeviceInfo module,
 * and the scissor rectangles PUI clips with are scaled the same way. Games
 * draw their own picture and are not affected. Everything is found by name
 * at run time; no system file is changed.
 *
 * The format comes from the Shell Menu setting screen_format. Rotation by
 * quarter turns uses the same machinery and is kept for experiments (debug
 * console only). */
namespace ScreenRotation {

struct Format {
  const char *name; /* shown in the menu as is */
  int width, height; /* the aspect ratio */
};
int FormatCount();
const Format &FormatAt(int index);

/* Hooks the scissor calls. Once, at start. */
void Install();
/* UI thread, once a frame: applies the setting, sizes scenes made since,
 * and puts the camera back if PUI placed it again. */
void Tick();
/* The layout's screen now, in layout units (1920x1080 when nothing is
 * changed). */
float LayoutWidth();
float LayoutHeight();

/* Experiments (debug console): quarter turns, optionally with a portrait
 * layout portrait_width units across. Overrides the setting until the
 * setting changes. */
bool Set(int quarter_turns, bool portrait_layout, int portrait_width = 1080);
void LogScissors(int count);
/* Use this format whatever the setting says (-1: follow the setting). */
void ForceFormat(int format);
void FlipPresent(bool flip);
/* Home's React native modules whose class name contains `filter`. */
std::string DebugModules(const char *filter);
std::string Status();
}
