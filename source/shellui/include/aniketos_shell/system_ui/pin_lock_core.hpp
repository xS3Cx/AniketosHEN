#pragma once

/* The pure part of the PIN lock (host-tested): what a PIN is, the record
 * stored for it, and how long an unlock lasts. No PS5 or Mono here. */

#include <cstdint>
#include <cstdio>
#include <string>

extern "C" {
#include "sha256.h"
}

namespace PinLockCore {

/* An unlock lasts this long after the last time a locked menu was opened. */
constexpr int64_t kIdleSeconds = 10 * 60;
/* A gap this long between two frames means the console was in rest mode. */
constexpr int64_t kRestGapSeconds = 60;

/* 4 to 8 digits. */
inline bool ValidPin(const std::string &pin) {
  if (pin.size() < 4 || pin.size() > 8)
    return false;
  for (const char c : pin)
    if (c < '0' || c > '9')
      return false;
  return true;
}

inline std::string Hex(const uint8_t *bytes, size_t count) {
  std::string out;
  char two[3];
  for (size_t i = 0; i < count; ++i) {
    std::snprintf(two, sizeof(two), "%02x", bytes[i]);
    out += two;
  }
  return out;
}

inline std::string Sha256Hex(const std::string &text) {
  SHA256_CTX ctx;
  uint8_t digest[SHA256_BLOCK_SIZE];
  sha256_init(&ctx);
  sha256_update(&ctx, reinterpret_cast<const uint8_t *>(text.data()),
                text.size());
  sha256_final(&ctx, digest);
  return Hex(digest, sizeof(digest));
}

/* The file's one line: "v1 <salt> <sha256(salt:pin)>". */
inline std::string MakeRecord(const std::string &salt, const std::string &pin) {
  return "v1 " + salt + " " + Sha256Hex(salt + ":" + pin) + "\n";
}

inline bool Matches(const std::string &record, const std::string &pin) {
  char salt[65] = {}, hash[65] = {};
  if (std::sscanf(record.c_str(), "v1 %64s %64s", salt, hash) != 2)
    return false;
  return Sha256Hex(std::string(salt) + ":" + pin) == hash;
}

/* Who unlocked and when it was last used. */
struct Session {
  bool unlocked = false;
  int64_t last_use = 0; /* wall clock seconds */
  int user = -1;
};

/* Still unlocked at `now` for foreground user `user`. A clock that went
 * backwards locks too. */
inline bool StillOpen(const Session &s, int64_t now, int user) {
  return s.unlocked && s.user == user && now >= s.last_use &&
         now - s.last_use <= kIdleSeconds;
}

} // namespace PinLockCore
