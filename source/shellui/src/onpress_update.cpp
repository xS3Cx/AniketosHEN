#include "onpress.hpp"

#include "settings_page_refresh.hpp"
#include "shellui_state.hpp"
#include "update_pages.hpp"
#include "update_progress.hpp"

#include <onion/update_store.hpp>

#include <onion_cjson.hpp>

#include <string>

extern "C" int sceSystemServiceRequestReboot(void);

namespace {

std::string selected_channel() {
  return g_settings.update_channel == onion::kUpdateChannelBeta
             ? "beta"
             : "stable";
}

bool enabled(const std::string &value) {
  return value == "1" || value == "true";
}

void start_update_job(bool download) {
  std::string reply;
  IPC_Client &ipc = IPC_Client::getInstance(true);
  const std::string channel = selected_channel();
  const bool sent = download ? ipc.UpdateDownload(channel, reply)
                             : ipc.UpdateCheck(channel, reply);
  if (!sent) {
    notify("notify.update.failed");
    return;
  }
  onion_cjson::Root root(reply);
  const char *state = root ? onion_cjson::string_item(root.get(), "state", "")
                           : "";
  const int task_id =
      root ? onion_cjson::int_item(root.get(), "task_id", 0) : 0;
  if (state && (std::string(state) == "started" ||
                std::string(state) == "already_running")) {
    update_progress_show(static_cast<std::uint32_t>(task_id));
    return;
  }
  notify("notify.update.busy");
}

OnPressResult auto_check(OnPressContext &ctx) {
  g_settings.update_auto_check = enabled(ctx.value);
  ctx.reload_util = true;
  return OnPressResult::Handled;
}

OnPressResult beta_channel(OnPressContext &ctx) {
  g_settings.update_channel = enabled(ctx.value) ? onion::kUpdateChannelBeta
                                                 : onion::kUpdateChannelStable;
  ctx.reload_util = true;
  return OnPressResult::Handled;
}

OnPressResult check_now(OnPressContext &ctx) {
  ctx.dirty = false;
  start_update_job(false);
  return OnPressResult::Consumed;
}

OnPressResult download_update(OnPressContext &ctx) {
  ctx.dirty = false;
  start_update_job(true);
  return OnPressResult::Consumed;
}

OnPressResult reboot_to_apply(OnPressContext &ctx) {
  ctx.dirty = false;
  if (sceSystemServiceRequestReboot() != 0)
    notify("notify.update.reboot_failed");
  return OnPressResult::Consumed;
}

OnPressResult delete_update(OnPressContext &ctx) {
  ctx.dirty = false;
  if (onion::update::remove_downloaded_update())
    notify("notify.update.deleted");
  else
    notify("notify.update.delete_failed");
  onion::shellui::settings::refresh(ctx.instance);
  return OnPressResult::Consumed;
}

const OnPressExactEntry kExact[] = {
    {"id_update_auto_check", auto_check},
    {"id_update_beta", beta_channel},
    {"id_update_check", check_now},
    {"id_update_download", download_update},
    {"id_update_reboot", reboot_to_apply},
    {"id_update_delete", delete_update},
};

} // namespace

const OnPressExactEntry *onpress_update_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}

const OnPressPrefixEntry *onpress_update_prefix(size_t *count) {
  *count = 0;
  return nullptr;
}
