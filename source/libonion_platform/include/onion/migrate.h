/* One-time import of an OnionHEN data directory into this distribution's.
 *
 * AniketosHEN keeps its persistent data under its own root
 * (<onion/distribution.h>). A console that ran OnionHEN has the user's
 * config.ini, payloads, plugins, cheats and SPRX files under /data/OnionHEN.
 * This module copies them across, once.
 *
 * Guarantees:
 *   - The OnionHEN root is only ever read. It is the user's backup and the
 *     way back to OnionHEN; nothing here deletes, renames or rewrites it.
 *   - Nothing that already exists under the target root is overwritten.
 *   - Every file arrives through a sibling temp file and rename(), so an
 *     interrupted run leaves complete files or none, and the next run resumes.
 *   - A completion marker is written last. With the marker present the whole
 *     operation is a single lstat().
 *   - Symbolic links and special files are never followed or copied.
 *   - One process at a time: an flock() on a file in the target root. A second
 *     caller gets ONION_MIGRATE_BUSY and touches nothing. (Two bootstrappers
 *     copying the same tree removed each other's temp files and failed with
 *     "cannot create" / "cannot store" on random cheat files.)
 *   - Bounded: at most ONION_MIGRATE_MAX_ATTEMPTS starts try it. An attempt is
 *     counted before the copy begins, so a start cut short by a crash or a
 *     kernel panic counts as well. After that the import is left alone for
 *     good (ONION_MIGRATE_GAVE_UP) and the OnionHEN folder stays as it is.
 *   - Bounded in size: a tree over ONION_MIGRATE_MAX_FILES files or
 *     ONION_MIGRATE_MAX_BYTES bytes is not copied at all, single files over
 *     ONION_MIGRATE_MAX_FILE_BYTES are left behind, and the copy only starts
 *     when the volume keeps at least the reserve free afterwards (allocation
 *     estimated per file, not just summed sizes).
 *   - The top-level games folder (game dumps, tens of GB) is never copied.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum onion_migrate_status {
  /* No OnionHEN directory: fresh install, nothing to do. */
  ONION_MIGRATE_NO_LEGACY = 0,
  /* The completion marker is present: migrated on an earlier boot. */
  ONION_MIGRATE_ALREADY_DONE,
  /* This call brought the data across and wrote the marker. */
  ONION_MIGRATE_COMPLETED,
  /* Could not finish (storage full, I/O error, ...). No marker is written, so
   * the next boot retries; files already copied are kept and skipped then. */
  ONION_MIGRATE_FAILED,
  /* Another process is importing this root right now. Nothing was touched. */
  ONION_MIGRATE_BUSY,
  /* Not imported and no longer retried: ONION_MIGRATE_MAX_ATTEMPTS starts
   * failed, or the tree is over the size limits. The report's final_attempt
   * is set only by the call that made that decision. */
  ONION_MIGRATE_GAVE_UP,
  /* Not attempted by this call (the other view of the same storage did not
   * finish, so repeating it at once could only fail the same way). */
  ONION_MIGRATE_SKIPPED,
} onion_migrate_status;

#define ONION_MIGRATE_MAX_ATTEMPTS 3u
#define ONION_MIGRATE_MAX_FILES 30000u
#define ONION_MIGRATE_MAX_BYTES (1024ull * 1024ull * 1024ull)
#define ONION_MIGRATE_MAX_FILE_BYTES (128ull * 1024ull * 1024ull)
/* Free space the volume must keep after the copy. */
#define ONION_MIGRATE_DEFAULT_RESERVE_BYTES (2048ull * 1024ull * 1024ull)

/* Bookkeeping files in the target root (never copied from the source). */
#define ONION_MIGRATE_LOCK_NAME ".migrate.lock"
#define ONION_MIGRATE_ATTEMPTS_NAME ".migration_attempts"

typedef enum onion_migrate_config_status {
  /* OnionHEN left no config.ini. */
  ONION_MIGRATE_CONFIG_NONE = 0,
  /* The target already has a config.ini; it was left exactly as it is. */
  ONION_MIGRATE_CONFIG_KEPT_EXISTING,
  /* OnionHEN's config.ini was converted, validated and installed. */
  ONION_MIGRATE_CONFIG_MIGRATED,
  /* OnionHEN's config.ini is not usable (malformed, oversized, not a regular
   * file). It was not installed; defaults apply. The original is untouched. */
  ONION_MIGRATE_CONFIG_REJECTED,
  /* Reading or writing failed. Counts as ONION_MIGRATE_FAILED. */
  ONION_MIGRATE_CONFIG_FAILED,
} onion_migrate_config_status;

/* Returns true when the converted config at @path loads as a valid config for
 * this build. Called on the temp file before it is committed. */
typedef bool (*onion_migrate_validate_fn)(const char *path, void *user);

typedef struct OnionMigrateOptions {
  const char *legacy_root; /* e.g. /data/OnionHEN */
  const char *target_root; /* e.g. /data/AniketosHEN */
  /* Optional. Without it the converted config only has to be free of NUL
   * bytes, which is all this C module can judge by itself. */
  onion_migrate_validate_fn validate_config;
  void *validate_user;
  /* Free space to keep after the copy; 0 means
   * ONION_MIGRATE_DEFAULT_RESERVE_BYTES. */
  unsigned long long reserve_bytes;
} OnionMigrateOptions;

typedef struct OnionMigrateReport {
  onion_migrate_status status;
  onion_migrate_config_status config;
  unsigned files_copied;        /* regular files brought across */
  unsigned files_present;       /* already under the target; left alone */
  unsigned entries_skipped;     /* logs, caches, temp files, links, specials */
  unsigned files_too_large;     /* over ONION_MIGRATE_MAX_FILE_BYTES; left */
  unsigned directories_created;
  unsigned long long bytes_copied;
  unsigned files_to_copy;             /* measured before copying */
  unsigned long long bytes_to_copy;   /* measured before copying */
  unsigned long long bytes_available; /* free on the target volume */
  unsigned attempt;   /* 1-based number of this attempt, 0 if none was made */
  bool final_attempt; /* this call stopped further attempts */
  int error;        /* errno of the first failure, 0 if none */
  char detail[192]; /* what failed, as a path relative to the root; no data */
} OnionMigrateReport;

/* Migrate one root. @report may be NULL. */
onion_migrate_status onion_migrate_legacy_root(const OnionMigrateOptions *options,
                                               OnionMigrateReport *report);

/* Migrate both views of the data directory (/data and /user/data). On a
 * console they are the same storage, so the second pass finds the marker and
 * returns at once; where they are distinct, each is migrated on its own.
 * When the first pass does not complete, the second is not attempted
 * (ONION_MIGRATE_SKIPPED): on a console it would repeat the same work on the
 * same files and count a second attempt for the same start.
 * @reports may be NULL; index 0 is /data, index 1 is /user/data.
 * Returns the more severe of the two statuses. */
onion_migrate_status
onion_migrate_legacy_data(onion_migrate_validate_fn validate_config,
                          void *validate_user, OnionMigrateReport reports[2]);

/* True when onion_migrate_legacy_root() would start copying for these roots:
 * the OnionHEN directory exists, no completion marker, attempts left. A cheap
 * check (three lstat()s and a small read) for telling the user to wait. */
bool onion_migrate_pending(const char *legacy_root, const char *target_root);

/* Convert OnionHEN config.ini text to this distribution's spelling. Only the
 * [game_menu] show_onionhen_options key (and the generated comment line
 * describing it) is renamed; every other byte, including unknown sections,
 * unknown keys, comments and line endings, is passed through. Returns a
 * malloc()ed buffer in *@out (caller frees) or false on allocation failure. */
bool onion_migrate_config_text(const char *in, size_t in_len, char **out,
                               size_t *out_len);

const char *onion_migrate_status_name(onion_migrate_status status);
const char *onion_migrate_config_status_name(onion_migrate_config_status status);

#ifdef __cplusplus
}
#endif
