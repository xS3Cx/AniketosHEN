#pragma once

#include "monodef.h"
#include <string>

namespace ShellClock {

struct DateOverlayState {
  bool visible = false;
  /* A date should be shown at all, whatever the clock's opacity is. */
  bool wanted = false;
  std::string text;
  float opacity = 0.0f;
  float time_width = 0.0f;
  float time_center_x = 0.0f;
  float time_bottom_y = 0.0f;
  float time_text_bottom_y = 0.0f;
  bool has_global_time_geometry = false;
  int font_size = 17;
  int date_spacing = -2;
  int r = 255;
  int g = 255;
  int b = 255;
};

// Install the native UI3 clock-label hooks by Mono type and method name.
void Install(MonoImage *pui);

// UI-thread refresh, called from the ShellUI render hook.
void Tick();
bool GetDateOverlayState(DateOverlayState *state);
/* How opaque the system clock is right now, read live so it can be mirrored
 * every frame (0 while Home is hidden behind Settings, the Control Center, a
 * game...). False when no system clock is known yet, in which case there is
 * nothing to follow. */
bool GetNativeClockOpacity(float *opacity);
/* Root widget of the scene the system clock is in, or null. Children of it
 * are faded and animated by PUI together with that scene. */
MonoObject *GetHomeRoot();
bool IsWidgetDisposed(MonoObject *widget);
/* Calls `visit` with the root widget of every scene in ShellUI's scene tree
 * (sub-scenes and modal scenes). UI thread. */
void VisitSceneRoots(void (*visit)(MonoObject *root, void *context),
                     void *context);
void RemoveStaleHomeChildren(MonoObject *root);
std::string DebugPropText(MonoObject *object, const char *name);
void RequestRefresh();
/* The custom clock is switched on (and customization with it). Home and the
 * Control Center then get the script that draws it in Sony's clock
 * (home_inject_js.inc); a clock the script draws is marked by its test id
 * and the label hooks leave it alone. */
bool CustomClockOn();
/* The clock's settings for that script, as JSON: on, seconds, hour format,
 * the AM/PM words, the date's parts and the menu language's month and
 * weekday names, colours and sizes. Plain ASCII (other text is written as
 * \u escapes): the script executor cuts a script with other text short. */
std::string ScriptConfig();
/** One-line description of the clock state, for the profiler log. */
void DebugSnapshot(char *out, size_t out_size);
void DebugDumpProperties(const char *tag);
void DebugDumpChain(const char *tag);
void DebugDumpPuiImage(const char *path);

} // namespace ShellClock
