/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_assets.h"

#include <onion/notify.h>

#include <dirent.h>
#include <stddef.h>

bool bootstrap_logo_recoloured(void) {
  DIR *folder = opendir(ONION_LOGO_DIR);
  if (!folder)
    return false;
  bool found = false;
  const struct dirent *entry;
  while (!found && (entry = readdir(folder)) != NULL)
    found = entry->d_name[0] != '.';
  closedir(folder);
  return found;
}
