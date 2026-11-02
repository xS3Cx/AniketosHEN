/* Keyboard and mouse input inside the game.
 *
 * Only the process with the screen gets the USB mouse and keyboard, so the
 * agent reads them here: libSceMouse for the mouse, libSceIme's keyboard
 * (with our own sceImeUpdate pump) for the keys; libSceKeyboard never
 * delivers keys. A low-priority reader thread polls every 4 ms (50 ms while
 * nothing is open); the hook thread takes snapshots without locks and
 * without ever blocking the game. Once our keyboard is open, the game's own
 * sceImeUpdate gets every event we received, so its text fields still work.
 */
#pragma once

#include <stdarg.h>
#include <time.h>

#include "onion/kbm_engine.hpp" /* InputFrame */
#include "onion/kbm_types.hpp"

namespace onion::kbm::agent {

inline uint64_t NowUs() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<uint64_t>(t.tv_sec) * 1000000u +
         static_cast<uint64_t>(t.tv_nsec) / 1000u;
}

/* Finds libkernel and its klog call: before LogLine and InputInit. */
void LogInit();

/* One klog line: `prefix`, the text, a newline. */
void LogLine(const char *prefix, const char *fmt, va_list args);

/* Loads libSceUserService, libSceMouse and libSceIme (from the game's
   sandbox when the game has not loaded them) and resolves what the reader
   needs by name. false: neither the mouse nor the IME keyboard is usable. */
bool InputInit();

/* Starts the reader thread. `control` may be null; otherwise the reader
   bumps control->heartbeat every loop and keeps kStatusMouseOpen and
   kStatusKeyboardOpen up to date, leaving the other bits alone. It first
   waits up to 500 ms for an older agent to clear those two. */
bool InputStart(ControlPage *control);

/* For the hook thread (any game thread, several at once are fine): the
   inputs held now, plus every press since the last take (a click shorter
   than the game's pad period still counts), and the mouse motion, wheel
   (+ up) and tilt (+ right) notches accumulated since the last take; when
   the last take is over 100 ms old, those are dropped instead. Lock-free,
   never blocks. false: the reader is not running or has not looped for
   500 ms; `out` is then an empty frame (nothing held, no
   motion) with now_us set, so the engine can release everything. */
bool TakeFrame(InputFrame *out);

/* Drops the motion, notches and presses not taken yet: the hook calls it
   when the system has the pad (intercepted), so they do not land later. */
void InputDiscard();

/* Stops the reader and waits for it; it closes the mouse handle and the IME
   keyboard (if still ours) and points the game's IME imports back at the
   real functions, so a replacement agent can take them. Call from the agent's main
   thread, not from a hook. */
void InputStop();

} // namespace onion::kbm::agent
