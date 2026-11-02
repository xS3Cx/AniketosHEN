/* Copyright (C) 2026 AniketosHEN — single source of controller telemetry.
 *
 * Toolbox and the VSH overlay both read from here. Neither performs its own
 * discovery, and neither sees a firmware-specific detail: the backend resolves
 * the pad interface, validates what it gets back, and publishes a normalized
 * snapshot behind a lock the render path only touches to copy.
 */
#pragma once

#include "aniketos_shell/controller/controller_types.hpp"

namespace AniketosHEN::Controller {

/* Starts the sampling worker. Idempotent; safe to call from injection. */
bool Start();

/* Stops the worker and clears published state. Blocks until the worker has
 * actually left, so nothing can call back into unloaded code afterwards. */
void Stop();

/* Cheap copy of the latest normalized state. Safe from the render path. */
ControllerInfo GetSnapshot();

/* True once the backend resolved and validated the pad interface. */
bool IsReady();

} // namespace AniketosHEN::Controller
