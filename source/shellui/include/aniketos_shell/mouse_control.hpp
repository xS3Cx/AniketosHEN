#pragma once

#include "hooked_funcs.hpp"

/* A USB mouse driving the PS5's own interface, the way a computer's does.
 *
 * The mouse is read with the system's mouse library (libSceMouse, found by
 * name). Its pointer is the one the system's web browser draws, in the
 * SystemOverlay layer above every screen. What the pointer rests on is
 * selected through PUI's own hit test (Scene.FindHitWidget) and focus; a
 * click selects what is under the pointer and, once the screen has given it
 * the focus, presses the confirm button, so everything opens exactly as it
 * would with the controller.
 *
 *   left button    select and confirm
 *   right button   back
 *   middle button  options
 *   wheel / tilt   up, down / left, right
 *   side buttons   back / confirm
 *
 * Off while a game has the screen, and with the Shell Menu's switch
 * (mouse_control_enabled).
 */
namespace MouseControl {

/* Once a frame, on ShellUI's UI thread: read the mouse, move the pointer,
   select what is under it. */
void Tick();

/* GamePad.GetData: adds the mouse's presses to what ShellUI reads. */
void FilterPad(GamePadData &data);

/* While set, the mouse's buttons, notches and motion go to `sink` instead of
   the screen (the Toolbox's key capture); nullptr gives them back. */
using CaptureSink = void (*)(uint32_t buttons, uint32_t went_down, int wheel,
                             int tilt, int dx, int dy);
void SetCaptureSink(CaptureSink sink);

} // namespace MouseControl
