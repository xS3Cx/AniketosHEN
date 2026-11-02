/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_safety.h"

#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/migrate.h>
#include <onion/notify.h>
#include <onion/report.h>
#include <onion/version.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr size_t kNoticeMax = 3;
constexpr size_t kNoticeBytes = 320;
char s_notices[kNoticeMax][kNoticeBytes];
size_t s_notice_count = 0;

/* The logs a crashed or panicked start leaves behind. Each tail is small: the
 * last lines before the stop are what matter, and the report has a cap. */
struct LogTail {
  const char *label;
  const char *path;
  size_t bytes;
};
constexpr LogTail kTails[] = {
    {"daemon log", ONION_DIST_LOG_PATH, 2600},
    {"daemon crash log", ONION_DIST_CRASH_LOG_PATH, 1200},
    {"util log", ONION_DIST_UTIL_LOG_PATH, 1600},
    {"util crash log", ONION_DIST_UTIL_CRASH_LOG_PATH, 1000},
    {"shellui log",
     ONION_DIST_USER_DATA_ROOT "/" ONION_DIST_NAME "_shellui.log", 2600},
    {"migration log", ONION_DIST_DATA_ROOT "/" ONION_DIST_MIGRATION_LOG, 600},
};

const char *level_effect(onion_boot_level level) {
  switch (level) {
  case ONION_BOOT_SAFE:
    return "data import, auto-start payloads and a custom kstuff.elf skipped";
  case ONION_BOOT_MINIMAL:
    return "as safe, and kstuff not loaded";
  case ONION_BOOT_RECOVERY:
    return "as minimal, and the daemon not started (nothing injected into "
           "ShellUI)";
  case ONION_BOOT_NORMAL:
  default:
    return "everything enabled";
  }
}

} // namespace

bool bootstrap_safety_lock(void) {
  /* The descriptor is kept open on purpose: the lock lives as long as this
     process and is dropped by the kernel when it exits or crashes. */
  return onion_instance_lock_acquire(ONION_BOOT_LOCK_PATH) !=
         ONION_INSTANCE_LOCK_BUSY;
}

void bootstrap_report(const char *kind, const char *body) {
  (void)onion_report_write(ONION_REPORT_DIR, kind, ONIONHEN_VERSION, body);
}

onion_boot_level bootstrap_safety_begin(const char *firmware_text) {
  OnionBootGuard guard{};
  if (!onion_boot_guard_begin(ONION_BOOT_STATE_PATH, ONION_BOOT_SESSION_PATH,
                              &guard))
    LOG_WARN("boot state could not be recorded; starting at level %s",
             onion_boot_level_name(guard.level));
  LOG_INFO("boot level %s (previous %s, unfinished=%d, restarted=%d, "
           "ended by the user=%d)",
           onion_boot_level_name(guard.level),
           onion_boot_level_name(guard.previous),
           guard.previous_unfinished ? 1 : 0, guard.previous_rebooted ? 1 : 0,
           guard.previous_ended ? 1 : 0);

  if (guard.previous_unfinished && guard.previous_rebooted) {
    enum { kBodyBytes = ONION_REPORT_MAX_BYTES };
    char *body = static_cast<char *>(std::malloc(kBodyBytes));
    if (body) {
      std::snprintf(
          body, kBodyBytes,
          "The previous start (level %s) did not finish: the console "
          "restarted before the stack had been up for %d s (kernel panic, "
          "crash or power loss).\n"
          "This start: level %s (%s).\n"
          "firmware: %s\n"
          "The logs below may end before the failed start: it can stop before "
          "it writes anything.\n",
          onion_boot_level_name(guard.previous),
          ONION_BOOT_RUNNING_AFTER_SECONDS, onion_boot_level_name(guard.level),
          level_effect(guard.level),
          firmware_text && firmware_text[0] ? firmware_text : "unknown");
      for (const LogTail &tail : kTails)
        onion_report_append_tail(body, kBodyBytes, tail.label, tail.path,
                                 tail.bytes);
      bootstrap_report("boot-unfinished", body);
      std::free(body);
    }
  }

  if (guard.level != ONION_BOOT_NORMAL) {
    char text[kNoticeBytes];
    std::snprintf(text, sizeof(text),
                  "The last start did not finish (the console restarted). "
                  "Started in %s mode: %s. It returns to normal after starts "
                  "that finish.",
                  onion_boot_level_name(guard.level),
                  level_effect(guard.level));
    bootstrap_safety_notice(text);
  }
  return guard.level;
}

void bootstrap_safety_notice(const char *text) {
  if (!text || !text[0] || s_notice_count >= kNoticeMax)
    return;
  std::snprintf(s_notices[s_notice_count++], kNoticeBytes, "%s", text);
}

void bootstrap_safety_flush(bool icon_ready) {
  static const char *const kIds[kNoticeMax] = {"588193131", "588193132",
                                               "588193133"};
  for (size_t i = 0; i < s_notice_count; ++i) {
    LOG_WARN("notice: %s", s_notices[i]);
    /* Rich toast with the distribution's icon: a regular notification, not
       the "Debug:" kernel toast. Without the icon it would fall back to that,
       so the notice then stays in the log only. */
    if (icon_ready)
      onion_notify_rich("notify.brand", s_notices[i], ONION_NOTIFY_ICON_PATH,
                        "download", kIds[i]);
  }
  s_notice_count = 0;
}
