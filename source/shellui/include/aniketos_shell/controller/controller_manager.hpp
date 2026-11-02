/* Copyright (C) 2026 AniketosHEN — single source of controller telemetry.
 *
 * The backend resolves the pad interface, validates what it gets back, and
 * publishes a normalized snapshot behind a lock readers only touch to copy.
 */
#pragma once

#include "aniketos_shell/controller/controller_types.hpp"

namespace AniketosHEN::Controller {

/* Starts the sampling worker. Idempotent; safe to call from injection. */
bool Start();

/* Cheap copy of the latest normalized state. Safe from the render path. */
ControllerInfo GetSnapshot();

} // namespace AniketosHEN::Controller
