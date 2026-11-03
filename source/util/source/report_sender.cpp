/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "report_sender.hpp"

#include "cheats/sync/http_transport_ps5.hpp"

#include <onion/boot_safety.h>
#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>
#include <onion/report.h>
#include <onion/settings.hpp>
#include <onion/version.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" {
int sceUserServiceGetLoginUserIdList(int32_t *list);
int sceUserServiceGetUserName(int user_id, char *name, size_t size);
}

#ifndef ONION_REPORT_URL
#define ONION_REPORT_URL ""
#endif

namespace {

using onion::cheats::sync::HttpRequest;
using onion::cheats::sync::Ps5HttpTransport;
using onion::cheats::sync::SyncStatus;

constexpr const char *kReportUrl = ONION_REPORT_URL;
/* A boot loop must not turn into a flood: a few per session, the rest wait. */
constexpr int kMaxSendsPerSession = 4;
/* Reports nobody could send for this long are dropped. */
constexpr long long kMaxAgeSeconds = 30ll * 24 * 3600;
/* Second try when the network was not up yet at the first one. */
constexpr unsigned kRetryAfterSeconds = 600;

/* How much of the daemon's append-only crash log has been reported. */
constexpr const char *kDaemonCrashOffset = ONION_REPORT_DIR "/.daemon_crash_offset";
/* mtime of the ShellUI injection marker already reported. */
constexpr const char *kShellUiReported = ONION_REPORT_DIR "/.shellui_marker_reported";

long long read_number(const char *path) {
  const int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0)
    return -1;
  char text[32] = {};
  const ssize_t length = read(fd, text, sizeof(text) - 1);
  close(fd);
  return length > 0 ? std::strtoll(text, nullptr, 10) : -1;
}

void write_number(const char *path, long long value) {
  char text[32];
  const int n = std::snprintf(text, sizeof(text), "%lld\n", value);
  if (n > 0 && mkdir_tree(ONION_REPORT_DIR))
    (void)write_file_atomic(path, text, static_cast<size_t>(n));
}

long long file_size(const char *path) {
  struct stat st {};
  return lstat(path, &st) == 0 && S_ISREG(st.st_mode)
             ? static_cast<long long>(st.st_size)
             : -1;
}

void report_with_tail(const char *kind, const char *intro, const char *label,
                      const char *path, size_t tail_bytes,
                      const char *second_label = nullptr,
                      const char *second_path = nullptr) {
  std::vector<char> body(ONION_REPORT_MAX_BYTES, '\0');
  std::snprintf(body.data(), body.size(), "%s\n", intro);
  onion_report_append_tail(body.data(), body.size(), label, path, tail_bytes);
  if (second_label && second_path)
    onion_report_append_tail(body.data(), body.size(), second_label,
                             second_path, tail_bytes);
  (void)onion_report_write(ONION_REPORT_DIR, kind, ONIONHEN_VERSION,
                           body.data());
}

std::string url_host(const char *url) {
  const char *host = std::strstr(url, "://");
  if (!host)
    return {};
  host += 3;
  size_t length = std::strcspn(host, ":/?#");
  return std::string(host, length);
}

/* Names of the users signed in on this console, to scrub from reports. */
std::vector<std::string> local_user_names() {
  std::vector<std::string> names;
  int32_t ids[4] = {-1, -1, -1, -1};
  if (sceUserServiceGetLoginUserIdList(ids) != 0)
    return names;
  for (int32_t id : ids) {
    if (id < 0)
      continue;
    char name[64] = {};
    if (sceUserServiceGetUserName(id, name, sizeof(name)) == 0 && name[0])
      names.emplace_back(name);
  }
  return names;
}

std::vector<std::string> pending_reports() {
  std::vector<std::string> names;
  DIR *dir = opendir(ONION_REPORT_DIR);
  if (!dir)
    return names;
  while (dirent *entry = readdir(dir)) {
    if (onion_report_is_file_name(entry->d_name))
      names.emplace_back(entry->d_name);
  }
  closedir(dir);
  std::sort(names.begin(), names.end()); /* oldest first: names start with the time */
  return names;
}

enum class SendResult { Sent, Dropped, Kept, Stop };

SendResult send_one(Ps5HttpTransport &http, const std::string &host,
                    const std::string &name,
                    const std::vector<const char *> &words) {
  const std::string path = std::string(ONION_REPORT_DIR "/") + name;
  struct stat st {};
  if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return SendResult::Dropped;
  if (static_cast<long long>(std::time(nullptr)) -
          static_cast<long long>(st.st_mtime) >
      kMaxAgeSeconds) {
    unlink(path.c_str());
    return SendResult::Dropped;
  }
  std::vector<char> text(ONION_REPORT_MAX_BYTES);
  size_t text_length = 0;
  const OnionReportRead state =
      onion_report_read(path.c_str(), text.data(), &text_length);
  if (state == ONION_REPORT_READ_FAILED) {
    LOG_WARN("report %s not read; kept for a later start", name.c_str());
    return SendResult::Kept;
  }
  if (state != ONION_REPORT_READ_OK) {
    unlink(path.c_str()); /* not ours or damaged: never sent */
    return SendResult::Dropped;
  }

  /* Scrubbed when written; again here, now with the local user names. */
  std::vector<char> body(text_length + 64);
  const size_t length = onion_report_sanitize(
      text.data(), text_length, body.data(), body.size(), words.data());

  HttpRequest req;
  req.url = kReportUrl;
  req.method = "POST";
  req.content_type = "text/plain; charset=utf-8";
  req.user_agent = ONION_DIST_NAME "/" ONIONHEN_VERSION;
  req.host_allow = host.c_str();
  req.body = body.data();
  req.body_len = length;
  req.timeout_ms = 15000;
  req.max_body_bytes = 4096; /* the relay answers with a short status */
  const SyncStatus status =
      http.perform(req, [](const void *, size_t) { return SyncStatus::Ok; });
  if (status == SyncStatus::Ok) {
    LOG_INFO("report sent: %s", name.c_str());
    unlink(path.c_str());
    return SendResult::Sent;
  }
  LOG_WARN("report %s not sent (status %d); kept for a later start",
           name.c_str(), static_cast<int>(status));
  return SendResult::Stop;
}

/* Returns false when sending should be tried again later this session. */
bool send_pending() {
  if (!kReportUrl[0]) {
    LOG_INFO("reports: this build has no report URL; kept in " ONION_REPORT_DIR);
    return true;
  }
  onion::Settings settings{};
  (void)onion::settings_load(&settings);
  if (!settings.reports_send) {
    LOG_INFO("reports: sending is off ([reports] send=false)");
    return true;
  }
  const std::vector<std::string> names = pending_reports();
  if (names.empty())
    return true;

  const std::string host = url_host(kReportUrl);
  const std::vector<std::string> users = local_user_names();
  std::vector<const char *> words;
  for (const std::string &user : users)
    words.push_back(user.c_str());
  words.push_back(nullptr);

  Ps5HttpTransport http;
  int sent = 0;
  for (const std::string &name : names) {
    if (sent >= kMaxSendsPerSession)
      break;
    const SendResult result = send_one(http, host, name, words);
    if (result == SendResult::Stop)
      return false;
    sent += result == SendResult::Sent;
  }
  return true;
}

void *sender_thread(void *) {
  sleep(ONION_BOOT_RUNNING_AFTER_SECONDS);
  /* The stack has been up this long: the start finished. */
  if (!onion_boot_guard_mark_running(ONION_BOOT_STATE_PATH))
    LOG_WARN("could not mark this start as running");
  if (!send_pending()) {
    sleep(kRetryAfterSeconds);
    (void)send_pending();
  }
  return nullptr;
}

} // namespace

void report_collect_previous_session() {
  /* util's own crash log is cleared right after this. */
  if (file_size(ONION_DIST_UTIL_CRASH_LOG_PATH) > 0)
    report_with_tail("util-crash", "The util service crashed in the previous "
                                   "session.",
                     "util crash log", ONION_DIST_UTIL_CRASH_LOG_PATH, 3000,
                     "util log", ONION_DIST_UTIL_LOG_PATH);

  /* The daemon's crash log is append-only: report what is new since the last
     report. A shorter file was rotated or removed; start over. */
  const long long size = file_size(ONION_DIST_CRASH_LOG_PATH);
  const long long stored = read_number(kDaemonCrashOffset);
  if (size >= 0) {
    /* No offset yet is a first run: nothing is new. */
    const long long reported = stored < 0 ? size : stored > size ? 0 : stored;
    if (size > reported) {
      const size_t fresh = static_cast<size_t>(
          std::min<long long>(size - reported, 4000));
      report_with_tail("daemon-crash", "The " ONION_DIST_NAME " daemon "
                                       "crashed in an earlier session.",
                       "daemon crash log", ONION_DIST_CRASH_LOG_PATH, fresh);
    }
    if (size != stored)
      write_number(kDaemonCrashOffset, size);
  }

  /* ShellUI died inside the Home script injection. Its next start counts that
     as a failed start and deletes the marker; until then report it once. */
  struct stat st {};
  if (lstat(ONION_DIST_JS_PENDING_PATH, &st) == 0 &&
      read_number(kShellUiReported) != static_cast<long long>(st.st_mtime)) {
    report_with_tail("shellui-crash",
                     "ShellUI stopped while the Home script was being "
                     "injected.",
                     "shellui log", ONION_DIST_SHELLUI_LOG_PATH, 4000);
    write_number(kShellUiReported, static_cast<long long>(st.st_mtime));
  }
}

void report_sender_start() {
  pthread_t thread = nullptr;
  if (pthread_create(&thread, nullptr, sender_thread, nullptr) == 0)
    pthread_detach(thread);
  else
    LOG_ERROR("report sender thread could not start");
}
