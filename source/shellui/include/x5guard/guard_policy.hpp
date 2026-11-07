#pragma once

/* x5Guard's pure decisions (no PS5 calls, host-tested in source/util/tests):
   the technical label of a catalog event, the banner text, and the banner
   rate limit. guard.cpp owns the state, the lock and the side effects. */

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace X5Guard::Policy {

constexpr size_t kMaxDetail = 96;   /* bytes of a detail kept at all */
constexpr size_t kMaxLabel = 48;    /* bytes of a label in the banner */
constexpr int64_t kSameEventMs = 2500;
constexpr int64_t kAnyBannerMs = 400;

/* A detail comes from RN JS: one line, no control bytes, bounded. */
inline std::string Clean(const std::string &text) {
  std::string out;
  for (unsigned char c : text) {
    if (out.size() >= kMaxDetail)
      break;
    if (c >= 0x20 && c != 0x7f)
      out += static_cast<char>(c);
  }
  return out;
}

/* update.check is counted and logged, never announced. */
inline bool Silent(const char *event) {
  return event && std::strcmp(event, "update.check") == 0;
}

/* "<what> [<name>]" with the name cut to keep the label short. */
inline std::string Tagged(const char *what, const std::string &name) {
  std::string label = what;
  if (name.empty())
    return label;
  const size_t room = kMaxLabel > label.size() + 3 ? kMaxLabel - label.size() - 3 : 0;
  std::string cut = name;
  if (cut.size() > room) {
    size_t end = room > 2 ? room - 2 : 0;
    while (end > 0 && (static_cast<unsigned char>(cut[end]) & 0xC0) == 0x80)
      --end; /* not inside a UTF-8 character */
    cut = cut.substr(0, end) + "..";
  }
  return label + " [" + cut + "]";
}

/* The technical label of a catalog event (codes: see guard.hpp / spec). */
inline std::string Label(const char *event, const std::string &raw_detail) {
  const std::string detail = Clean(raw_detail);
  const std::string code = event ? event : "";
  struct Entry {
    const char *code;
    const char *what;
    const char *fixed; /* the bracket text, else the detail */
  };
  static const Entry kCatalog[] = {
      {"signin.utility", "PSN sign-in request", "SigninUtility.Open"},
      {"signin.link", "PSN auth deeplink", "psaccounts:signin"},
      {"signin.app", "PSN sign-in app launch", nullptr},
      {"signin.dialog", "Game PSN sign-in dialog", nullptr},
      {"signin.native", "PSN authentication call", nullptr},
      {"update.link", "Firmware update link", nullptr},
      {"update.dialog", "Firmware update dialog", "SystemUpdaterDialog"},
      {"update.app", "Firmware updater launch", nullptr},
      {"update.process", "Firmware updater call", nullptr},
      {"update.auto", "Auto-update registry write", nullptr},
      {"update.feature", "Feature update fetch", nullptr},
      {"update.check", "Firmware update check", nullptr},
      {"notify.drop", "PSN notification", nullptr},
      {"native.errordialog", "PSN error dialog", nullptr},
      {"native.nettest", "PSN connection test", nullptr},
      {"native.signup", "PSN account sign-up", nullptr},
      {"native.launchupdate", "Firmware update launch", nullptr},
  };
  for (const Entry &entry : kCatalog)
    if (code == entry.code)
      return Tagged(entry.what, entry.fixed ? std::string(entry.fixed) : detail);
  if (code.rfind("native.", 0) == 0)
    return Tagged("PSN system call", detail.empty() ? code.substr(7) : detail);
  return Tagged(detail.empty() ? code.c_str() : detail.c_str(), "");
}

/* What the banner names: the stopped call or link itself, uncut ("SigninUtility.Open()",
   "psaccounts:signin", "NUC294"); a method gets its "()". Without a detail,
   the event's label. */
inline std::string BannerName(const char *event, const std::string &detail) {
  if (detail.empty())
    return Label(event, detail);
  const bool method = detail.find('.') != std::string::npos &&
                      detail.find_first_of(": /") == std::string::npos &&
                      detail.back() != ')';
  return method ? detail + "()" : detail;
}

/* The translated template's single %s takes the label. A template without
   exactly one %s (a missing key comes back as the key) falls back to
   "<label> blocked"; never handed to printf, so a bad translation is
   harmless. */
inline std::string BannerText(const std::string &prefix, const char *fmt,
                              const std::string &label) {
  std::string body;
  const std::string tmpl = fmt ? fmt : "";
  const size_t at = tmpl.find("%s");
  if (at != std::string::npos && tmpl.find('%', at + 2) == std::string::npos &&
      tmpl.find('%') == at)
    body = tmpl.substr(0, at) + label + tmpl.substr(at + 2);
  else
    body = label + " blocked";
  return prefix.empty() ? body : prefix + " " + body;
}

/* At most one banner per 2.5 s for the same event+detail and one per 0.4 s
   overall; the rest are dropped (still counted and logged by the caller).
   Not thread-safe: guard.cpp calls it under its lock. */
class RateLimit {
public:
  bool Allow(const std::string &key, int64_t now_ms) {
    if (last_any_ms_ >= 0 && now_ms - last_any_ms_ < kAnyBannerMs)
      return false;
    auto found = last_ms_.find(key);
    if (found != last_ms_.end() && now_ms - found->second < kSameEventMs)
      return false;
    if (last_ms_.size() >= 64) /* bounded: forget the expired ones */
      for (auto it = last_ms_.begin(); it != last_ms_.end();)
        it = now_ms - it->second >= kSameEventMs ? last_ms_.erase(it) : ++it;
    last_ms_[key] = now_ms;
    last_any_ms_ = now_ms;
    return true;
  }

private:
  std::unordered_map<std::string, int64_t> last_ms_;
  int64_t last_any_ms_ = -1;
};

} // namespace X5Guard::Policy
