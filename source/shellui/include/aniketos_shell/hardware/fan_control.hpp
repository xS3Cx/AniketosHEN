/* Copyright (C) 2026 AniketosHEN contributors
 *
 * Shell Menu fan options, forwarded to the daemon over IPC. False when the
 * daemon is not reachable.
 */
#pragma once

namespace AniketosFan {

/** Turn the daemon's fan threshold on or off. */
bool SetEnabled(bool enabled, int target_celsius);

/** Turn the threshold on at this temperature. */
bool SetTarget(int target_celsius);

} // namespace AniketosFan
