#include "x5guard/guard.hpp"

#include "shellui_types.hpp"
#include "toolbox_i18n.hpp"
#include "x5guard/guard_banner.hpp"
#include "x5guard/guard_policy.hpp"

#include <onion/distribution.h>
#include <onion/log.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sys/stat.h>

namespace X5Guard {

namespace {

constexpr const char *kLogPath = ONION_DIST_USER_DATA_ROOT "/x5guard.log";
constexpr long kLogCap = 64 * 1024;

std::mutex s_lock; /* RN JS threads and managed detours report at once */
Policy::RateLimit s_limit;
std::atomic<unsigned> s_count{0};
std::string s_last;

const char *ShieldName(Shield shield) {
  switch (shield) {
  case Shield::PsnBlocker:
    return "psn_blocker";
  }
  return "?";
}

int64_t NowMs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

/* Called under s_lock. Starts over past the cap: a record, not an archive. */
void AppendLog(int64_t now_ms, Shield shield, const char *event,
               const std::string &detail, const char *outcome) {
  struct stat st {};
  const bool full = stat(kLogPath, &st) == 0 && st.st_size > kLogCap;
  FILE *file = std::fopen(kLogPath, full ? "w" : "a");
  if (!file)
    return;
  char when[32] = "?";
  const time_t now = time(nullptr);
  tm local{};
  if (localtime_r(&now, &local))
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
  std::fprintf(file, "%s up=%lld.%03llds %s %s \"%s\" %s\n", when,
               (long long)(now_ms / 1000), (long long)(now_ms % 1000),
               ShieldName(shield), event, detail.c_str(), outcome);
  std::fclose(file);
}

} // namespace

bool Enabled(Shield shield) {
  switch (shield) {
  case Shield::PsnBlocker:
    return g_settings.x5guard_psn_blocker;
  }
  return false;
}

void Report(Shield shield, const char *event, const std::string &raw_detail) {
  if (!event)
    event = "";
  const std::string detail = Policy::Clean(raw_detail);
  const bool silent = Policy::Silent(event);
  const bool alerts = g_settings.x5guard_alerts;
  std::string banner;
  const char *outcome = "silent";
  const int64_t now_ms = NowMs();
  {
    std::lock_guard<std::mutex> hold(s_lock);
    s_count.fetch_add(1);
    if (!silent) {
      s_last = Policy::Label(event, detail);
      /* own %s substitution, not format(): a broken translation stays harmless */
      const std::string text = Policy::BannerText(
          BannerPrefix(), toolbox_i18n::tr("x5guard.banner_fmt"),
          Policy::BannerName(event, detail));
      if (!alerts)
        outcome = "alerts-off";
      else if (!s_limit.Allow(std::string(event) + "|" + detail, now_ms))
        outcome = "rate-limited";
      else {
        outcome = "banner";
        banner = text;
      }
    }
    AppendLog(now_ms, shield, event, detail, outcome);
  }
  LOG_INFO("[X5GUARD] %s %s \"%s\" %s (#%u)", ShieldName(shield), event,
           detail.c_str(), outcome, s_count.load());
  if (!banner.empty()) {
    if (ShowBanner(banner))
      QueueAlertSound();
    else
      LOG_WARN("[X5GUARD] banner not taken: %s", banner.c_str());
  }
}

unsigned BlockedCount() { return s_count.load(); }

std::string LastBlocked() {
  std::lock_guard<std::mutex> hold(s_lock);
  return s_last;
}

} // namespace X5Guard
