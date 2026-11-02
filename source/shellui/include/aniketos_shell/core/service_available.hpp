/* Copyright (C) 2026 AniketosHEN contributors
 *
 * Service boundary for the Aniketos ShellUI core.
 * Visual features are local. Actions owned by the crit/util daemons probe
 * IPC first so a daemon restart cannot turn a menu action into a bad call.
 */

#pragma once

#include "ipc.hpp"

namespace AniketosRuntime {

inline bool service_available(bool util, const char *feature) {
  IPC_Client &client = IPC_Client::getInstance(util);
  if (client.IsAvailable()) {
    return true;
  }
  LOG_DEBUG("Aniketos: %s service unavailable for %s",
            util ? "util" : "crit", feature ? feature : "feature");
  return false;
}

} // namespace AniketosRuntime
