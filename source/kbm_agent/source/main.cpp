/* Keyboard & mouse agent: the entry point inside the game.
 *
 * Input reader (kbm_input) → engine (onion::kbm::Engine) → the game's pad
 * reads (kbm_hooks). main never returns: a payload's main returning ends the
 * process, which here is the game.
 */
#include <stdarg.h>
#include <unistd.h>

#include "kbm_hooks.hpp"
#include "kbm_input.hpp"
#include "onion/kbm_engine.hpp"

using namespace onion::kbm;

namespace {

Engine s_engine; /* driven by the hooks only, one game thread at a time */

__attribute__((format(printf, 1, 2))) void Log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  agent::LogLine("[KBM-AGENT] ", fmt, ap);
  va_end(ap);
}

void Tick(const ProfileBlob *profile, bool profile_changed, PadState *out) {
  InputFrame frame;
  if (profile_changed) {
    s_engine.SetProfile(profile);
    agent::TakeFrame(&frame); /* drop motion piled up while we were idle */
  }
  agent::TakeFrame(&frame); /* false gives an empty frame: all released */
  s_engine.Tick(frame, out);
}

[[noreturn]] void Park() {
  for (;;)
    sleep(3600);
}

} // namespace

int main() {
  agent::LogInit();
  Log("agent in pid %d", getpid());

  if (!agent::InputInit()) {
    Log("no mouse or keyboard API; the game is left alone");
    Park();
  }
  /* the daemon injects once libScePad is in; retry a little in case it is
     not yet, then leave the game alone */
  int tries = 0;
  while (!hooks::Init(Tick, Log)) {
    if (++tries >= 30) {
      Log("hooks not installed after %d tries; the game is left alone", tries);
      Park();
    }
    sleep(1);
  }
  ControlPage *control = hooks::Control();
  if (!agent::InputStart(control)) {
    hooks::Unhook();
    Park();
  }
  __atomic_or_fetch(&control->agent_status, kStatusRunning, __ATOMIC_RELEASE);

  /* the reader beats the control page's heartbeat; this loop picks up new
     profiles and modules loaded later, and steps aside for a newer agent */
  while (!hooks::Superseded()) {
    hooks::Service();
    usleep(200000); /* a saved profile reaches the game within this */
  }
  Log("replaced by a newer agent; stopping");
  agent::InputStop(); /* frees the mouse and keyboard for it */
  Park();
}
