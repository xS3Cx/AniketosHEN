/* Copyright (C) 2026 AniketosHEN contributors
 *
 * Aniketos ShellUI fan control. This is deliberately a capability-based
 * wrapper around the PS5 ICC fan device: if the device or ioctl interface is
 * unavailable on a firmware/model, no write is attempted.
 */
#pragma once

namespace AniketosFan {

/** Start the local adaptive fan worker once. */
bool Start();

/** Apply or restore the fan policy without going through an Aniketos daemon. */
bool SetEnabled(bool enabled, int target_celsius);

/** Change the local autoservo target when dynamic control is enabled. */
bool SetTarget(int target_celsius);

/** Return control to the system-owned autoservo profile. */
bool RestoreAutomatic();

} // namespace AniketosFan
