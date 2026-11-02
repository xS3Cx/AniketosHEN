/* Copyright (C) 2026 OnionHEN / LightningMods */

#include <onion/distribution.h>
#include "bootstrap_assets.h"

#include <onion/fs.h>
#include <onion/notify.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

extern uint8_t sicon_start[];
extern const unsigned int sicon_size;

#define ONION_ICON(name)                                                       \
  extern uint8_t name##_start[];                                               \
  extern const unsigned int name##_size;
#include "icon_manifest.inc"
#undef ONION_ICON

typedef struct EmbeddedIcon {
  const char *name;
  const uint8_t *data;
  const unsigned int *size;
} EmbeddedIcon;

static const EmbeddedIcon kEmbeddedIcons[] = {
#define ONION_ICON(name) {#name, name##_start, &name##_size},
#include "icon_manifest.inc"
#undef ONION_ICON
};

bool bootstrap_assets_write_notify_icon(void) {
  mkdir("/user/data", 0777);
  mkdir(ONION_DIST_USER_DATA_ROOT, 0777);
  if (bootstrap_logo_recoloured() && if_exists(ONION_NOTIFY_ICON_PATH))
    return true;
  return write_file_atomic_if_changed(ONION_NOTIFY_ICON_PATH, sicon_start,
                                      sicon_size);
}

bool bootstrap_assets_write(void) {
  mkdir(ONION_DIST_DATA_ROOT, 0777);
  mkdir(ONION_DIST_DATA_ROOT "/assets", 0777);
  const bool startup_icon_ready = bootstrap_assets_write_notify_icon();

  /* Unchanged files are left alone: these used to be rewritten (temp file,
     fsync, rename) on every start. */
  for (size_t i = 0; i < sizeof(kEmbeddedIcons) / sizeof(kEmbeddedIcons[0]);
       ++i) {
    char path[256];
    snprintf(path, sizeof(path), ONION_DIST_DATA_ROOT "/assets/%s.png",
             kEmbeddedIcons[i].name);
    (void)write_file_atomic_if_changed(path, kEmbeddedIcons[i].data,
                                       *kEmbeddedIcons[i].size);
  }

  /* The only file written to a system partition. /system_ex is mounted
     read-write with async metadata, so it is written once per new icon,
     not on every start. */
  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_file_atomic_if_changed("/system_ex/vsh_asset/onionhen.png",
                                     sicon_start, sicon_size);
  return startup_icon_ready;
}
