/* The toast-only half of a rich notification, for notify.c. */
#pragma once

#include <onion/notify.h>

/* Show @payload as a toast that stays out of the notification list. */
int onion_notify_send_toast(onion_notify_rich_send_fn send, const char *payload);
