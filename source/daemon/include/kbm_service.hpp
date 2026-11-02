/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Keyboard & mouse for games, daemon side: injects the KB+M agent into a
 * started game and keeps the compiled profile in the agent's control page.
 * All the slow work runs on the service's own thread, never on the shared
 * lifecycle worker or an IPC thread.
 */
#pragma once

#include <onion/app_lifecycle.hpp>

#include <mutex>

namespace onion::daemon {

/** libNineS injection is not thread safe (global ptrace state, swaps the
 *  daemon's authid): hold this around every Inject_Toolbox call. */
std::mutex &inject_mutex();

} // namespace onion::daemon

namespace onion::daemon::kbm {

/** Lifecycle subscriber (BigAppStarted / BigAppExited): queues the work, and
 *  blocks a title that ended right after the agent went in. */
onion::lifecycle::ISubscriber &subscriber();

/** "kbm reload" from ShellUI: recompile the running game's profile and write
 *  it again; injects first when KB+M was switched on while the game ran. */
void request_reload();

} // namespace onion::daemon::kbm
