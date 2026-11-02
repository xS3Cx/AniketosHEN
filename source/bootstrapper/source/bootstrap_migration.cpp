/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_migration.h"

#include "bootstrap_assets.h"
#include "bootstrap_safety.h"

#include <onion/distribution.h>
#include <onion/log_settings.hpp>
#include <onion/migrate.h>
#include <onion/notify.h>
#include <onion/settings.hpp>

#include <cstdio>
#include <cstring>

namespace {

bool config_loads(const char *path, void *) {
  onion::Settings parsed{};
  return onion::settings_load_file(path, &parsed);
}

} // namespace

void bootstrap_migrate_legacy_data(void) {
  /* A first import can copy thousands of cheat files. Say so up front: a
     start that looked stuck is what got the payload sent again. */
  if (onion_migrate_pending(ONION_DIST_LEGACY_DATA_ROOT,
                            ONION_DIST_DATA_ROOT) &&
      bootstrap_assets_write_notify_icon())
    onion_notify_rich("notify.brand",
                      "Copying your " ONION_DIST_LEGACY_NAME " files to "
                      ONION_DIST_NAME ". This can take a few minutes; "
                      "please wait.",
                      ONION_NOTIFY_ICON_PATH, "download", "588193130");

  OnionMigrateReport reports[2] = {};
  const onion_migrate_status status =
      onion_migrate_legacy_data(config_loads, nullptr, reports);
  const OnionMigrateReport &report =
      reports[0].status == status ? reports[0] : reports[1];

  switch (status) {
  case ONION_MIGRATE_NO_LEGACY:
  case ONION_MIGRATE_ALREADY_DONE:
  case ONION_MIGRATE_BUSY:
  case ONION_MIGRATE_SKIPPED:
    return;
  case ONION_MIGRATE_COMPLETED: {
    /* Told on Home by the ShellUI pop-up (migration_notice.cpp); no toast. */
    onion::Settings settings{};
    if (onion::settings_load(&settings))
      (void)onion::apply_log_settings(settings); /* config may be new */
    return;
  }
  case ONION_MIGRATE_GAVE_UP:
    if (!report.final_attempt)
      return; /* decided on an earlier start; reported and told then */
    break;
  case ONION_MIGRATE_FAILED:
  default:
    break;
  }

  /* A failure is logged and reported, never toasted: the user cannot act on
     a temp-file error, and the next start retries quietly. Only the start
     that gives up says so, once, in plain words. */
  char body[768];
  std::snprintf(body, sizeof(body),
                "Import of " ONION_DIST_LEGACY_DATA_ROOT " %s (attempt %u of "
                "%u).\n"
                "detail: %s\nerrno: %d (%s)\n"
                "measured: %u files, %llu bytes; free: %llu bytes\n"
                "copied: %u files, %llu bytes; present: %u; skipped: %u; "
                "too large: %u\n",
                status == ONION_MIGRATE_GAVE_UP ? "given up" : "failed",
                report.attempt, ONION_MIGRATE_MAX_ATTEMPTS, report.detail,
                report.error, report.error ? std::strerror(report.error) : "-",
                report.files_to_copy, report.bytes_to_copy,
                report.bytes_available, report.files_copied,
                report.bytes_copied, report.files_present,
                report.entries_skipped, report.files_too_large);
  bootstrap_report(status == ONION_MIGRATE_GAVE_UP ? "migration-gave-up"
                                                   : "migration-failed",
                   body);
  if (status == ONION_MIGRATE_GAVE_UP)
    bootstrap_safety_notice(
        "Your " ONION_DIST_LEGACY_NAME " files could not be copied "
        "automatically and will not be tried again. Nothing was changed: "
        "they are still in " ONION_DIST_LEGACY_DATA_ROOT ".");
}
