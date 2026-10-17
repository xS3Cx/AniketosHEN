/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_config.h"

#include "bootstrap_assets.h"
#include "bootstrap_safety.h"

#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/log_settings.hpp>
#include <onion/migrate.h>
#include <onion/notify.h>
#include <onion/settings.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

struct OrbisKernelSwVersion {
  uint64_t pad0;
  char version_str[0x1C];
  uint32_t version;
  uint64_t pad1;
};

struct OrbisNotificationRequest;

extern "C" {
int sceKernelSendNotificationRequest(int32_t device,
                                     OrbisNotificationRequest *request,
                                     size_t size, int32_t blocking);
int sceNotificationSend(int user_id, bool is_logged, const char *payload);
int sceSystemServiceParamGetInt(int param_id, int *value);
int sceKernelGetProsperoSystemSwVersion(OrbisKernelSwVersion *version);
int sceKernelIsGenuineDevKit();
}

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

bool bootstrap_config_load(BootstrapConfig *config) {
  if (!config)
    return false;

  onion_notify_set_send(reinterpret_cast<onion_notify_send_fn>(
      sceKernelSendNotificationRequest));
  onion_notify_set_rich_send(reinterpret_cast<onion_notify_rich_send_fn>(
      sceNotificationSend));

  onion::Settings settings{};
  (void)onion::settings_load(&settings);
  (void)onion::apply_log_settings(settings);

  int system_language = 1;
  if (settings.ui_lang == onion::kUiLanguageSystem)
    (void)sceSystemServiceParamGetInt(1, &system_language);
  onion_notify_apply_ui_language(settings.ui_lang, system_language);

  OrbisKernelSwVersion version{};
  if (sceKernelGetProsperoSystemSwVersion(&version) != 0) {
    LOG_ERROR("Failed to resolve the system software version");
    return false;
  }

  if (version.version < 0x3000000 && !sceKernelIsGenuineDevKit()) {
    LOG_DEBUG("FW %s is below 3.00 and no HV path is available",
              version.version_str);
  }

  config->firmware_version = version.version;
  /* "11.40" from the number, not version_str: the report scrubber reads a
     dotted run like "11.40.00.04.00" as an IP address and blanks it. */
  std::snprintf(config->firmware_text, sizeof(config->firmware_text),
                "%x.%02x", (version.version >> 24) & 0xffu,
                (version.version >> 16) & 0xffu);
  config->kstuff_autoload = settings.kstuff_autoload;
  return true;
}
