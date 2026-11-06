/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Launch user Payloads whose adjacent .auto_start marker is present. */
void bootstrap_payload_autostart(bool enabled);

#ifdef __cplusplus
}
#endif
