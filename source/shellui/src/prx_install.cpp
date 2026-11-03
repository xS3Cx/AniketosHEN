/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */

#include "hooked_funcs.hpp"

#include <onion/platform.h>
#include <string>

void ReloadApp(MonoString *str) {
  std::string tid = Mono_to_String(str);
  LOG_DEBUG("Reloading %s scenes", tid.c_str());
  notify("notify.app.reload_scenes", tid.c_str());
  Orig_ReloadApp(str);
}
