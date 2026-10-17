/* Start-up safety: one bootstrapper at a time, and a record of starts that
 * never finished so the next start can leave the risky steps out.
 *
 * Instance lock
 *   flock() on a file under /system_tmp (memory-backed, gone at reboot). The
 *   kernel drops the lock when its holder exits, crashes or is killed, so a
 *   stale lock never outlives its process. Two bootstrappers running side by
 *   side (an autoloader plus a manual send, or a send repeated while the first
 *   one was still busy) raced each other through the data import and loaded
 *   kstuff more than once; with the lock the second one stops at once.
 *
 * Boot level
 *   A small state file on /data records the level the last start ran at and
 *   whether that start got as far as "running" (marked by util once the stack
 *   has been up for a while). A start that never got there and ended with
 *   the console restarting (the session marker in /system_tmp is gone) points
 *   to a kernel panic, a crash or a power loss inside the start: the next
 *   start runs one level more cautious. Every start that reaches "running"
 *   steps one level back down, so a one-off does not stick.
 *
 *   A reload in the same session (no restart in between) keeps the level: it
 *   is the normal way to restart the stack and says nothing about a crash.
 *   So does a start the user ended before "running" by putting the console
 *   in rest mode, turning it off or restarting it (marked "ended" by the
 *   daemon when the system leaves its working state).
 */
#pragma once

#include <onion/distribution.h>
#include <onion/system_tmp.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_BOOT_LOCK_PATH ONION_SYSTEM_TMP_ROOT "/bootstrap.lock"
/* Loopback port bound instead when flock() turns out not to lock (checked at
 * every start: a second open of the lock file must be refused). */
#define ONION_BOOT_LOCK_FALLBACK_PORT 9047
#define ONION_BOOT_SESSION_PATH ONION_SYSTEM_TMP_ROOT "/boot_session"
#define ONION_BOOT_STATE_PATH ONION_DIST_DATA_ROOT "/.boot_state"

/* Seconds util waits after its start before calling the start "running". */
#define ONION_BOOT_RUNNING_AFTER_SECONDS 90

typedef enum onion_boot_level {
  ONION_BOOT_NORMAL = 0,
  /* No data import, no user payload autostart, built-in kstuff only. */
  ONION_BOOT_SAFE = 1,
  /* Also no kstuff. */
  ONION_BOOT_MINIMAL = 2,
  /* Also no daemon: nothing is injected into ShellUI. Util alone runs, to
   * send the error report and to mark the start as finished. */
  ONION_BOOT_RECOVERY = 3,
} onion_boot_level;

typedef struct OnionBootGuard {
  onion_boot_level level;    /* level for this start */
  onion_boot_level previous; /* level the previous start ran at */
  bool previous_unfinished;  /* the previous start never reached "running" */
  bool previous_rebooted;    /* ... and the console restarted since */
  bool previous_ended;       /* the previous start was ended on purpose */
} OnionBootGuard;

enum {
  ONION_INSTANCE_LOCK_BUSY = -1,        /* another process holds it */
  ONION_INSTANCE_LOCK_UNAVAILABLE = -2, /* locking failed: fail open */
};

/* Take the exclusive lock at @path for the life of this process (falling
 * back to the loopback port lock when flock() does not lock). Returns a lock
 * descriptor (>= 0), ONION_INSTANCE_LOCK_BUSY or
 * ONION_INSTANCE_LOCK_UNAVAILABLE. Never blocks. */
int onion_instance_lock_acquire(const char *path);

/* True when another process holds the lock at @path right now. A probe for
 * callers that only want to know (the unpacker): it creates nothing and does
 * not keep the lock. */
bool onion_instance_lock_busy(const char *path);

/* Read the previous start from @state_path, decide this start's level, and
 * record this start as in progress. @session_path is created to mark the
 * current console session. Returns false only when the new state could not be
 * written; @out is filled either way. */
bool onion_boot_guard_begin(const char *state_path, const char *session_path,
                            OnionBootGuard *out);

/* Record that the start recorded in @state_path reached a stable point. */
bool onion_boot_guard_mark_running(const char *state_path);

/* Record that the start in progress in @state_path was ended on purpose
 * (rest mode, power-off, restart) before it reached "running", so the next
 * start keeps its level. A start that already reached "running" stays so. */
bool onion_boot_guard_mark_ended(const char *state_path);

const char *onion_boot_level_name(onion_boot_level level);

#ifdef __cplusplus
}
#endif
