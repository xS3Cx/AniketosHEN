/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Bootstrapper side of <onion/boot_safety.h> and <onion/report.h>: the
 * single-instance lock, the boot level for this start, local error reports
 * and the few user-facing notices that go with them.
 */
#pragma once

#include <onion/boot_safety.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Take the instance lock. False when another bootstrapper is running (the
 * user has been told); the caller must then stop without touching anything. */
bool bootstrap_safety_lock(void);

/* True when the built-in kstuff has no support for @firmware_version, after
 * logging that and telling the user. */
bool bootstrap_kstuff_too_new(uint32_t firmware_version);

/* Decide this start's level from the previous start, and report a previous
 * start that was cut short by a restart (with the tails of the logs it left,
 * read before this start's processes rotate them). */
onion_boot_level bootstrap_safety_begin(const char *firmware_text);

/* Save a local error report (sent later by util when allowed). */
void bootstrap_report(const char *kind, const char *body);

/* Queue a plain-text notice for the user. Shown by bootstrap_safety_flush()
 * once the notification icon is on disk, as a regular (not debug) toast. */
void bootstrap_safety_notice(const char *text);
void bootstrap_safety_flush(bool icon_ready);

#ifdef __cplusplus
}
#endif
