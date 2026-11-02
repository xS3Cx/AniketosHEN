/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_config.h"

extern "C" {
int sceUserServiceInitialize(const int *priority);
int sceUserServiceGetForegroundUser(int *user_id);
}

namespace {

constexpr int kUserServiceAlreadyInitialized = static_cast<int>(0x80960003);

} // namespace

int32_t bootstrap_foreground_user(void) {
  static const bool ready = [] {
    const int priority = 256;
    const int result = sceUserServiceInitialize(&priority);
    return result == 0 || result == kUserServiceAlreadyInitialized;
  }();
  int user_id = -1;
  return ready && sceUserServiceGetForegroundUser(&user_id) == 0 ? user_id : -1;
}
