/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True while the user's recoloured logo exists: the notification icon on
 *  disk is then that picture, and a new start leaves it. */
bool bootstrap_logo_recoloured(void);

/** Write only the notification icon (under /user/data, not a system
 *  partition), so notices can be shown before the rest of the start. */
bool bootstrap_assets_write_notify_icon(void);

/** Materialize embedded assets and report whether the startup icon is ready. */
bool bootstrap_assets_write(void);

#ifdef __cplusplus
}
#endif
