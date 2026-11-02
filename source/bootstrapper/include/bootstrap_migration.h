/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

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
