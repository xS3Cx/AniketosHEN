/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress misc (kstuff, account, credits) */
#include <onion/distribution.h>
#include "onpress.hpp"
#include "account_activator.h"
#include "progress_dialog.hpp"
#include "onion_cjson.hpp"

#include <cstring>
#include <unistd.h>

OnPressResult onpress_kstuff_autoload(OnPressContext &ctx) {
  const bool enabled = value_as_int(ctx);
  if (enabled == g_settings.kstuff_autoload)
    return OnPressResult::EarlyReturn;

  g_settings.kstuff_autoload = enabled;
  if (enabled) {
    unlink(ONION_DIST_USER_DATA_ROOT "/no_kstuff");
    unlink(ONION_DIST_DATA_ROOT "/no_kstuff");
    notify("notify.kstuff.next_boot_on");
  } else {
    touch_file(ONION_DIST_USER_DATA_ROOT "/no_kstuff");
    touch_file(ONION_DIST_DATA_ROOT "/no_kstuff");
    notify("notify.kstuff.next_boot_off");
  }
  return OnPressResult::Handled;
}

static OnPressResult id_download_cheats(OnPressContext &ctx) {
  ctx.dirty = false;
  std::string reply;
  if (!IPC_Client::getInstance(true).DownloadCheats(nullptr, nullptr, reply)) {
    notify("notify.cheats.sync.error", "ipc");
    return OnPressResult::Consumed;
  }
  onion_cjson::Root response(reply);
  const char *state = response && cJSON_IsObject(response.get())
                          ? onion_cjson::string_item(response.get(), "state")
                          : nullptr;
  if (!state) {
    notify("notify.cheats.sync.error", "ipc_response");
    return OnPressResult::Consumed;
  }
  const int task_id = onion_cjson::int_item(response.get(), "task_id", 0);
  if (std::strcmp(state, "already_running") == 0) {
    notify("notify.cheats.sync.busy");
  }
  /* Button confirmation has completed before this handler runs. */
  cheat_progress_show(task_id > 0 ? static_cast<uint32_t>(task_id) : 0);
  if (!cheat_progress_open_page()) {
    notify("notify.cheats.sync.error", "navigation");
  }
  return OnPressResult::Consumed;
}

OnPressResult onpress_delete_kstuff(OnPressContext &ctx) {
  (void)ctx;
  unlink(ONION_DIST_USER_DATA_ROOT "/kstuff.elf");
  notify("notify.kstuff.deleted");
  return OnPressResult::Handled;
}

static OnPressResult id_activate_account(OnPressContext &ctx) {
  ctx.dirty = false;
  Activator activator(true);
  if (!activator.Valid()) {
    notify("notify.account.invalid");
    return OnPressResult::Consumed;
  }
  if (!activator.IsNotActivated()) {
    notify("notify.account.already");
    return OnPressResult::Consumed;
  }
  if (!activator.Activate()) {
    notify("notify.account.failed");
    return OnPressResult::Consumed;
  }
  notify("notify.account.activated");
  return OnPressResult::Consumed;
}

static OnPressResult id_lm_test(OnPressContext &ctx) {
  (void)ctx;
  LOG_DEBUG("LM's Test Button Pressed");
  return OnPressResult::Handled;
}

static OnPressResult id_onionhen_credits(OnPressContext &ctx) {
  (void)ctx;
  return OnPressResult::EarlyReturn;
}

/* The rows of "Find me on": show where that is, nothing to save. */
static OnPressResult find_me(const char *where) {
  notify("%s", where);
  return OnPressResult::EarlyReturn;
}
static OnPressResult id_find_me_x(OnPressContext &) {
  return find_me("X: x.com/Alex0_SH");
}
static OnPressResult id_find_me_youtube(OnPressContext &) {
  return find_me("YouTube: AlexZero");
}
static OnPressResult id_find_me_discord(OnPressContext &) {
  return find_me("Discord: alex0sh");
}
static OnPressResult id_find_me_telegram(OnPressContext &) {
  return find_me("Telegram: t.me/missgoodpussy");
}
static OnPressResult id_find_me_tiktok(OnPressContext &) {
  return find_me("TikTok: tiktok.com/@mealexzero");
}

static const OnPressExactEntry kRootExact[] = {
    {"id_download_cheats", id_download_cheats},
    {"id_lm_test", id_lm_test},
    {"id_onionhen_credits", id_onionhen_credits},
    {"id_find_me_x", id_find_me_x},
    {"id_find_me_youtube", id_find_me_youtube},
    {"id_find_me_discord", id_find_me_discord},
    {"id_find_me_telegram", id_find_me_telegram},
    {"id_find_me_tiktok", id_find_me_tiktok},
};

const OnPressExactEntry *onpress_misc_root_exact(size_t *count) {
  *count = sizeof(kRootExact) / sizeof(kRootExact[0]);
  return kRootExact;
}

static const OnPressExactEntry kAccountExact[] = {
    {"id_activate_account", id_activate_account},
};

const OnPressExactEntry *onpress_account_exact(size_t *count) {
  *count = sizeof(kAccountExact) / sizeof(kAccountExact[0]);
  return kAccountExact;
}
