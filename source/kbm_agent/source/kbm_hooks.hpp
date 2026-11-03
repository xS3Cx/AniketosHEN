/* KB+M agent: the game's pad reads, hooked.
 *
 * Every scePadRead* import slot of the game's own modules is pointed at a
 * hook that calls the real function and lays the engine's PadState over
 * what it returned. State shared with the daemon and with an older agent
 * lives in the control page (mapped near kControlHint, found by its magic).
 */
#pragma once

#include <stdint.h>

#include "onion/kbm_types.hpp"

namespace onion::kbm::hooks {

/* Which scePadRead* a slot imports (ControlPage.hook_kinds). */
enum Kind : uint32_t {
  kReadState = 0,    /* (handle, ScePadData*) -> 0, 0x78 per sample */
  kRead = 1,         /* (handle, ScePadData*, num) -> count */
  kReadStateExt = 2, /* 0x80 per sample */
  kReadExt = 3,
  kKindCount = 4,
};

/* Produces the controller state for one pad read. Called from game threads,
   never two at once, only while the reader is alive, the profile enabled and
   the system does not have the pad. `profile` stays valid until the next
   call that passes profile_changed. `out` comes neutral (sticks 128,
   active 0). No heap, no locks, no blocking. */
using TickFn = void (*)(const ProfileBlob *profile, bool profile_changed,
                        PadState *out);
using LogFn = void (*)(const char *fmt, ...);

/* Resolves libScePad's reads, maps the control page or takes it over from an
   older agent (its slots are repointed here), then scans the game's modules.
   False: no libScePad or no control page; nothing was changed. */
bool Init(TickFn tick, LogFn log);

/* Call from the reader loop: copies a new profile out of the control page and
   re-scans for modules loaded later (every few seconds). */
void Service();

/* A newer agent took over: stop reading input; do not call Unhook. */
bool Superseded();

/* Points every slot back at the real function (not when superseded). */
void Unhook();

ControlPage *Control();

/* The import slots of the game's own modules (not system .sprx) holding
   `fn`, made writable; at most `max`. For the IME forwarding in kbm_input. */
int FindImportSlots(const void *fn, uint64_t **out, int max);

/* Puts `to` in a slot that still holds `from`. A slot whose page has gone (its
   module unloaded) is left alone. */
void SwapSlot(uint64_t *slot, uint64_t from, uint64_t to);

} // namespace onion::kbm::hooks
