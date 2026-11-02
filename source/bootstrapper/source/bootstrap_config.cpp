/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_config.h"

#include <onion/log_settings.hpp>
#include <onion/notify.h>
#include <onion/settings.hpp>

#include <cstdint>
#include <cstdio>

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

} // namespace

bool bootstrap_config_load(BootstrapConfig *config) {
  if (!config)
    return false;

  onion_notify_set_send(reinterpret_cast<onion_notify_send_fn>(
      sceKernelSendNotificationRequest));
  onion_notify_set_rich_send(reinterpret_cast<onion_notify_rich_send_fn>(
      sceNotificationSend));
  onion_notify_set_toast_user(bootstrap_foreground_user);

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
