/* AniketosHEN distribution identity.
 *
 * AniketosHEN is a derivative distribution of OnionHEN
 * (https://github.com/aydencharles/onionHEN, Copyright (C) OnionHEN /
 * LightningMods, GPL-3.0-or-later). This header is the single place that
 * names the distribution and the persistent locations it owns.
 *
 * What is deliberately NOT derived from the distribution name:
 *   - the onion_ / ONION_ / libonion_ code namespace (upstream provenance);
 *   - /system_tmp/onionhen and the onion_*.elf process names (runtime
 *     contract shared with upstream-built plugins; see <onion/system_tmp.h>);
 *   - the "OnionHEN?..." ShellUI URIs and /system_ex/vsh_asset/onionhen.png
 *     (byte-exact, firmware-specific HomeUI patches).
 */
#pragma once

/* Name shown to users, in logs, notifications and artifact names. */
#define ONION_DIST_NAME "AniketosHEN"
/* Directory component under /data, /user/data and on USB drives. */
#define ONION_DIST_DIR_NAME "AniketosHEN"

/* The upstream distribution this tree derives from and migrates from. */
#define ONION_DIST_LEGACY_NAME "OnionHEN"
#define ONION_DIST_LEGACY_DIR_NAME "OnionHEN"
#define ONION_DIST_UPSTREAM_URL "https://github.com/aydencharles/onionHEN"

/* Where crash notifications send bug reports. This must be the tracker of
 * whoever publishes the build - never upstream's, whose maintainers did not
 * ship it. Set it with -DANIKETOSHEN_ISSUES_URL=<url> (CMake) or the
 * ANIKETOSHEN_ISSUES_URL environment variable (scripts/build.sh). */
#ifndef ONION_DIST_ISSUES_URL
#define ONION_DIST_ISSUES_URL "the " ONION_DIST_NAME " issue tracker"
#endif

/* Persistent data roots. /data is the elevated view; /user/data is the
 * SceShellUI sandbox view of the same storage. */
#define ONION_DIST_DATA_ROOT "/data/" ONION_DIST_DIR_NAME
#define ONION_DIST_USER_DATA_ROOT "/user/data/" ONION_DIST_DIR_NAME

/* Roots written by OnionHEN. Read-only for this distribution: they are the
 * migration source and stay untouched as the user's recoverable backup. */
#define ONION_DIST_LEGACY_DATA_ROOT "/data/" ONION_DIST_LEGACY_DIR_NAME
#define ONION_DIST_LEGACY_USER_DATA_ROOT                                       \
  "/user/data/" ONION_DIST_LEGACY_DIR_NAME

#define ONION_DIST_CONFIG_NAME "config.ini"
#define ONION_DIST_CONFIG_PATH ONION_DIST_DATA_ROOT "/" ONION_DIST_CONFIG_NAME
#define ONION_DIST_USER_CONFIG_PATH                                            \
  ONION_DIST_USER_DATA_ROOT "/" ONION_DIST_CONFIG_NAME
#define ONION_DIST_LEGACY_CONFIG_PATH                                          \
  ONION_DIST_LEGACY_DATA_ROOT "/" ONION_DIST_CONFIG_NAME
#define ONION_DIST_LEGACY_USER_CONFIG_PATH                                     \
  ONION_DIST_LEGACY_USER_DATA_ROOT "/" ONION_DIST_CONFIG_NAME

/* Log files (not migrated; OnionHEN's own logs stay where they are). */
#define ONION_DIST_LOG_PATH ONION_DIST_DATA_ROOT "/" ONION_DIST_NAME ".log"
#define ONION_DIST_CRASH_LOG_PATH                                              \
  ONION_DIST_DATA_ROOT "/" ONION_DIST_NAME "_crash.log"
#define ONION_DIST_UTIL_LOG_PATH                                               \
  ONION_DIST_DATA_ROOT "/" ONION_DIST_NAME "_util_daemon.log"
#define ONION_DIST_UTIL_CRASH_LOG_PATH                                         \
  ONION_DIST_DATA_ROOT "/" ONION_DIST_NAME "_util_crash.log"
#define ONION_DIST_SHELLUI_LOG_PATH                                            \
  ONION_DIST_USER_DATA_ROOT "/" ONION_DIST_NAME "_shellui.log"

/* Home script injection markers: ShellUI writes them, util and the daemon
 * read or clear them. */
#define ONION_DIST_JS_PENDING_PATH ONION_DIST_USER_DATA_ROOT "/js_inject_pending"
#define ONION_DIST_JS_UNCONFIRMED_PATH                                         \
  ONION_DIST_USER_DATA_ROOT "/js_inject_unconfirmed"

/* One-time OnionHEN -> AniketosHEN data migration (see <onion/migrate.h>). */
#define ONION_DIST_MIGRATION_MARKER ".migrated_from_" ONION_DIST_LEGACY_NAME
#define ONION_DIST_MIGRATION_LOG "migration.log"
