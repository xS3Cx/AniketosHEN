/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BootstrapConfig {
  uint32_t firmware_version;
  char firmware_text[32]; /* e.g. "11.40", for reports */
  bool kstuff_autoload;
} BootstrapConfig;

/** Configure notifications/logging and resolve the startup settings. */
bool bootstrap_config_load(BootstrapConfig *config);

/**
 * Import the data a previous OnionHEN installation left under /data/OnionHEN
 * (once; later boots return immediately). Never fatal: on failure the
 * OnionHEN directory is untouched, settings are still read from it, and the
 * next boot retries. Must run before the services that create files under
 * the AniketosHEN data root are launched.
 */
void bootstrap_migrate_legacy_data(void);

#ifdef __cplusplus
}
#endif
