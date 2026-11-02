/* Toast-only rich notifications. See notify_toast.h. */

#include "notify_toast.h"

#include "cJSON.hpp"

static onion_notify_user_fn s_toast_user_fn = NULL;

void onion_notify_set_toast_user(onion_notify_user_fn fn) {
  s_toast_user_fn = fn;
}

/* A toast-only notification is not stored, so the overlay reads the toast
   itself (viewTemplateType, viewData, ...) from the payload's top level,
   where a logged one carries it under rawData for the list to keep: the
   logged shape sent toast-only is dropped as "unknown viewTemplateType".
   Returns the rawData object as its own payload (free with cJSON_free), or
   NULL when the payload has none. */
static char *toast_payload(const char *payload) {
  cJSON *root = cJSON_Parse(payload);
  cJSON *raw = root ? cJSON_DetachItemFromObjectCaseSensitive(root, "rawData")
                    : NULL;
  char *toast = cJSON_IsObject(raw) ? cJSON_PrintUnformatted(raw) : NULL;
  cJSON_Delete(raw);
  cJSON_Delete(root);
  return toast;
}

int onion_notify_send_toast(onion_notify_rich_send_fn send, const char *payload) {
  /* shown to the user it names, so the user at the console */
  int32_t user_id = 0xFE;
  if (s_toast_user_fn) {
    const int32_t user = s_toast_user_fn();
    if (user >= 0)
      user_id = user;
  }
  char *toast = toast_payload(payload);
  const int result = send(user_id, false, toast ? toast : payload);
  cJSON_free(toast);
  return result;
}
