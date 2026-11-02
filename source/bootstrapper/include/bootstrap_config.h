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

/** The user at the console, or -1: where toast-only notifications go. */
int32_t bootstrap_foreground_user(void);

#ifdef __cplusplus
}
#endif
