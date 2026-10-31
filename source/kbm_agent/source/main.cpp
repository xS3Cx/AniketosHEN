/* Keyboard & mouse agent: the entry point inside the game.
 *
 * Input reader (kbm_input) → engine (onion::kbm::Engine) → the game's pad
 * reads (kbm_hooks). main never returns: a payload's main returning ends the
 * process, which here is the game.
 */
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "kbm_hooks.hpp"
#include "kbm_input.hpp"
#include "onion/kbm_engine.hpp"

using namespace onion::kbm;

namespace {

int (*s_out)(int, const char *);
Engine s_engine; /* driven by the hooks only, one game thread at a time */

__attribute__((format(printf, 1, 2))) void Log(const char *fmt, ...) {
  if (!s_out)
    return;
  char line[256];
  int n = snprintf(line, sizeof(line), "[KBM-AGENT] ");
  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
  va_end(ap);
  if (n > static_cast<int>(sizeof(line)) - 2)
    n = sizeof(line) - 2;
  line[n] = '\n';
  line[n + 1] = 0;
  s_out(0, line);
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
  uint32_t kernel = 0;
  if (kernel_dynlib_handle(getpid(), "libkernel.sprx", &kernel) != 0)
    kernel_dynlib_handle(getpid(), "libkernel_sys.sprx", &kernel);
  const intptr_t out = kernel_dynlib_dlsym(getpid(), kernel, "sceKernelDebugOutText");
  if (out > 0)
    s_out = reinterpret_cast<decltype(s_out)>(out);
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
      agent::InputStop();
      Park();
    }
    sleep(1);
  }
  ControlPage *control = hooks::Control();
  if (!agent::InputStart(control)) {
    hooks::Unhook();
    Park();
  }
  __atomic_or_fetch(&control->agent_status, 1u, __ATOMIC_RELEASE);

  /* the reader beats the control page's heartbeat; this loop picks up new
     profiles and modules loaded later, and steps aside for a newer agent */
  while (!hooks::Superseded()) {
    hooks::Service();
    usleep(50000);
  }
  Log("replaced by a newer agent; stopping");
  agent::InputStop(); /* frees the mouse and keyboard for it */
  Park();
}
