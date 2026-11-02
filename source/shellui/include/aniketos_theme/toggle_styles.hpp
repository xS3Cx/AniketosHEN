#pragma once

struct MonoImage;
struct MonoObject;

/* Other looks for the system's on/off switch, drawn with the system's own
 * widgets: the switch is PUI's CheckBox in its "switch" style, which is two
 * panels (a 64x32 frame and a 26x26 thumb the system slides itself). A style
 * here recolours those two as the system colours them, and may add panels of
 * its own under the thumb, which it moves and tints frame by frame. Nothing
 * is an image, and everything is found by name at run time.
 *
 * Chosen by the Shell Menu setting shell_toggle_style (0: the system's own).
 * A switch takes the style when the system next colours it, which it does
 * when the switch is made and whenever focus moves over it. */
namespace AniketosTheme::ToggleStyles {

void Install(MonoImage *pui);

/* From the Panel background hook (panel_tint.cpp): `rgba` is the colour the
 * system is about to give `panel`, changed in place when the panel is part
 * of a switch and a style is chosen; true then. */
bool OnPanelBackground(MonoObject *panel, float *rgba);

/* UI thread, once a frame: moves the added parts of the switches on screen. */
void Tick();

/* Number of styles, the system's own included, and their names. */
int Count();
const char *Name(int style);

} // namespace AniketosTheme::ToggleStyles
