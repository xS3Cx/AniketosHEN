/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_notify.h"

#include <onion/notify.h>

#include <stdarg.h>

void bootstrap_notify(const char *text, ...) {
  va_list args;
  va_start(args, text);
  onion_notify_v(/*show_watermark=*/0, text, args);
  va_end(args);
}

void bootstrap_notify_starting(bool custom_icon_ready) {
  /* a toast only: start-up messages are not kept in the notification list */
  onion_notify_rich_toast("notify.brand", "notify.boot.starting",
                          custom_icon_ready ? ONION_NOTIFY_ICON_PATH : "",
                          "download", "588193128");
}
