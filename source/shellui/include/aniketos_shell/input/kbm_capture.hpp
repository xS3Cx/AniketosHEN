#pragma once

#include <onion/kbm_types.hpp>

#include <cstdint>

/* Keyboard & mouse for games: "press a key" in the Toolbox's mapping editor.
 *
 * ShellUI never sees keys as key events. Its own keyboard reader is PUI's
 * ImeKbController (the web view's): ScePsmUtil's internal calls open one
 * libSceIme keyboard client and queue its key events, pumped on each read.
 * We call those same internal calls, found by name, so ShellUI's text fields
 * and IME are never touched. libSceIme allows one keyboard client per
 * process: while a web view holds it, the keyboard is busy and capture is
 * mouse only; we never close a client that is not ours.
 *
 * The mouse comes from MouseControl, which hands its buttons and wheel to
 * us while a session runs (nothing on screen is clicked then).
 *
 * Everything here runs on ShellUI's UI thread.
 */
namespace KbmCapture {

using onion::kbm::Input;

enum class State {
  None,      /* still waiting */
  Captured,  /* input (+ modifiers) is the answer */
  Cancelled, /* Esc held a second, or the keyboard was taken away */
  TimedOut,
};

struct Result {
  State state;
  Input input;
  /* modifier keys (0xe0-0xe7) held with it, kInputNone when unused */
  Input modifiers[onion::kbm::kMaxModifiers];
};

/* Waits for one input. A key, mouse button or wheel notch answers at once
   with the modifier keys held; a modifier key, or Esc, answers when it is
   released with nothing pressed meanwhile (Shift alone, Esc alone). Returns
   whether the keyboard is listened to (false: mouse only). */
bool BeginCapture(uint32_t timeout_ms);
/* Call at least every couple of seconds while waiting: a session nobody
   polls is ended. Once the state is not None the keyboard and mouse are
   given back already. */
Result PollCapture();
void EndCapture();

/* Once a frame (OnRender): reads the keyboard, ends timed-out sessions. */
void Tick();

} // namespace KbmCapture
