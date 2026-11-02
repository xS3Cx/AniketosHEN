/* One-time import of an OnionHEN data directory. See <onion/migrate.h>. */

#include "migrate_internal.h"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

enum { kLogMaxBytes = 64 * 1024 };

/* Allocation unit assumed when the volume does not report one. Small files
 * dominate (cheats), and each takes at least one fragment plus its inode and
 * directory entry, so summed byte sizes alone underestimate badly. */
static const unsigned long long kDefaultFragment = 32ull * 1024ull;

const char *onion_migrate_status_name(onion_migrate_status status) {
  switch (status) {
  case ONION_MIGRATE_NO_LEGACY:
    return "no-legacy-data";
  case ONION_MIGRATE_ALREADY_DONE:
    return "already-done";
  case ONION_MIGRATE_COMPLETED:
    return "completed";
  case ONION_MIGRATE_BUSY:
    return "busy";
  case ONION_MIGRATE_GAVE_UP:
    return "gave-up";
  case ONION_MIGRATE_SKIPPED:
    return "skipped";
  case ONION_MIGRATE_FAILED:
  default:
    return "failed";
  }
}

/* For onion_migrate_legacy_data(): which of two outcomes to report. */
static int severity(onion_migrate_status status) {
  switch (status) {
  case ONION_MIGRATE_NO_LEGACY:
    return 0;
  case ONION_MIGRATE_ALREADY_DONE:
    return 1;
  case ONION_MIGRATE_SKIPPED:
    return 2;
  case ONION_MIGRATE_BUSY:
    return 3;
  case ONION_MIGRATE_COMPLETED:
    return 4;
  case ONION_MIGRATE_GAVE_UP:
    return 5;
  case ONION_MIGRATE_FAILED:
  default:
    return 6;
  }
}

const char *
onion_migrate_config_status_name(onion_migrate_config_status status) {
  switch (status) {
  case ONION_MIGRATE_CONFIG_NONE:
    return "none";
  case ONION_MIGRATE_CONFIG_KEPT_EXISTING:
    return "kept-existing";
  case ONION_MIGRATE_CONFIG_MIGRATED:
    return "migrated";
  case ONION_MIGRATE_CONFIG_REJECTED:
    return "rejected";
  case ONION_MIGRATE_CONFIG_FAILED:
  default:
    return "failed";
  }
}

void migrate_fail(MigrateContext *context, int error, const char *fmt, ...) {
  OnionMigrateReport *report = context->report;
  if (error == ENOSPC || error == EROFS || error == EIO
#ifdef EDQUOT
      || error == EDQUOT
#endif
  )
    context->stop = true;
  if (report->error != 0 || report->detail[0] != '\0')
    return;
  report->error = error ? error : EIO;
  va_list args;
  va_start(args, fmt);
  vsnprintf(report->detail, sizeof(report->detail), fmt, args);
  va_end(args);
}

static size_t format_summary(char *out, size_t out_sz,
                             const OnionMigrateOptions *options,
                             const OnionMigrateReport *report) {
  const int n = snprintf(
      out, out_sz,
      "time=%lld status=%s attempt=%u from=%s config=%s files_copied=%u "
      "files_present=%u skipped=%u too_large=%u dirs_created=%u bytes=%llu "
      "to_copy=%u/%llu available=%llu%s%s%s%s\n",
      (long long)time(NULL), onion_migrate_status_name(report->status),
      report->attempt, options->legacy_root,
      onion_migrate_config_status_name(report->config), report->files_copied,
      report->files_present, report->entries_skipped, report->files_too_large,
      report->directories_created, report->bytes_copied, report->files_to_copy,
      report->bytes_to_copy, report->bytes_available,
      report->error ? " error=\"" : "",
      report->error ? strerror(report->error) : "",
      report->error ? "\" detail=" : "",
      report->error ? report->detail : "");
  if (n <= 0)
    return 0;
  return (size_t)n < out_sz ? (size_t)n : out_sz - 1;
}

/* Best effort: the diagnostic must never turn a result into a failure. */
static void append_log(const OnionMigrateOptions *options,
                       const OnionMigrateReport *report) {
  char path[kPathMax], line[768];
  if (!migrate_join_path(path, sizeof(path), options->target_root,
                         ONION_DIST_MIGRATION_LOG))
    return;
  const size_t length = format_summary(line, sizeof(line), options, report);
  if (length == 0)
    return;

  struct stat st;
  int flags = O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW;
  if (lstat(path, &st) == 0) {
    if (!S_ISREG(st.st_mode))
      return;
    if (st.st_size > (off_t)kLogMaxBytes)
      flags |= O_TRUNC; /* a boot loop on a full disk must not fill it */
  }
  const int fd = open(path, flags, 0666);
  if (fd < 0)
    return;
  (void)migrate_write_all(fd, line, length);
  close(fd);
}

static bool valid_root(const char *path) {
  const size_t length = path ? strlen(path) : 0;
  return length > 1 && length < kPathMax / 2 && path[0] == '/' &&
         path[length - 1] != '/';
}

static bool is_inside(const char *path, const char *root) {
  const size_t length = strlen(root);
  return strncmp(path, root, length) == 0 &&
         (path[length] == '\0' || path[length] == '/');
}

enum { kLockBusy = -1, kLockUnavailable = -2 };

/* One importer per target root. The kernel drops the lock with its holder,
 * so a crashed run never blocks the next one. */
static int lock_root(const char *target_root) {
  char path[kPathMax];
  if (!migrate_join_path(path, sizeof(path), target_root,
                         ONION_MIGRATE_LOCK_NAME))
    return kLockUnavailable;
  const int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
  if (fd < 0)
    return kLockUnavailable;
  for (;;) {
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
      return fd;
    if (errno != EINTR)
      break;
  }
  const int error = errno;
  close(fd);
  if (error == EWOULDBLOCK || error == EAGAIN)
    return kLockBusy;
  LOG_WARN("migration lock unavailable (%s); relying on the caller",
           strerror(error));
  return kLockUnavailable;
}

static unsigned read_attempts(const char *path) {
  const int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0)
    return 0;
  char text[16] = {0};
  ssize_t length;
  do {
    length = read(fd, text, sizeof(text) - 1);
  } while (length < 0 && errno == EINTR);
  close(fd);
  unsigned value = 0;
  for (ssize_t i = 0; i < length && text[i] >= '0' && text[i] <= '9'; ++i)
    value = value < 1000 ? value * 10 + (unsigned)(text[i] - '0') : value;
  /* A file that is there but unreadable counts as one attempt. */
  return length > 0 && value == 0 ? 1 : value;
}

static bool write_attempts(const char *path, unsigned attempts) {
  char text[16];
  const int n = snprintf(text, sizeof(text), "%u\n", attempts);
  return n > 0 && (size_t)n < sizeof(text) &&
         write_file_atomic(path, text, (size_t)n);
}

static void log_outcome(const OnionMigrateOptions *options,
                        const OnionMigrateReport *report) {
  switch (report->status) {
  case ONION_MIGRATE_COMPLETED:
    LOG_INFO("migration %s -> %s: config=%s copied=%u present=%u skipped=%u "
             "too_large=%u",
             options->legacy_root, options->target_root,
             onion_migrate_config_status_name(report->config),
             report->files_copied, report->files_present,
             report->entries_skipped, report->files_too_large);
    break;
  case ONION_MIGRATE_GAVE_UP:
    LOG_ERROR("migration %s -> %s: not retried any more (attempt %u%s%s); "
              "%s is untouched",
              options->legacy_root, options->target_root, report->attempt,
              report->detail[0] ? ": " : "", report->detail,
              options->legacy_root);
    break;
  default:
    LOG_ERROR("migration %s -> %s failed (attempt %u of %u): %s (%s); %s is "
              "untouched",
              options->legacy_root, options->target_root, report->attempt,
              ONION_MIGRATE_MAX_ATTEMPTS, report->detail,
              strerror(report->error), options->legacy_root);
    break;
  }
}

/* Walks the tree writing nothing, then refuses an import that is over the
 * limits or would not leave the reserve free. False stops the import. */
static bool measure_and_check_space(MigrateContext *context,
                                    const char *attempts_path) {
  const OnionMigrateOptions *options = context->options;
  OnionMigrateReport *report = context->report;

  struct statvfs vfs;
  const bool have_vfs = statvfs(options->target_root, &vfs) == 0;
  const unsigned long long unit =
      !have_vfs ? 0 : vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
  context->fragment =
      unit < 512 || unit > 1024ull * 1024ull ? kDefaultFragment : unit;
  migrate_measure_tree(context);

  if (context->over_limit) {
    /* Not a retryable failure: the next start would measure the same. */
    report->status = ONION_MIGRATE_GAVE_UP;
    report->final_attempt = true;
    snprintf(report->detail, sizeof(report->detail),
             "too large to import: more than %u files or %llu MiB",
             ONION_MIGRATE_MAX_FILES,
             ONION_MIGRATE_MAX_BYTES / (1024ull * 1024ull));
    (void)write_attempts(attempts_path, ONION_MIGRATE_MAX_ATTEMPTS);
    return false;
  }

  if (have_vfs) {
    const unsigned long long available =
        (unsigned long long)vfs.f_bavail * unit;
    const unsigned long long reserve =
        options->reserve_bytes ? options->reserve_bytes
                               : ONION_MIGRATE_DEFAULT_RESERVE_BYTES;
    report->bytes_available = available;
    if (context->alloc_needed > 0 &&
        available < context->alloc_needed + reserve)
      migrate_fail(context, ENOSPC, "need %llu bytes free, %llu available",
                   context->alloc_needed + reserve, available);
  }
  return !migrate_failed(context);
}

static void write_marker(MigrateContext *context, const char *marker) {
  OnionMigrateReport *report = context->report;
  char summary[768];
  report->status = ONION_MIGRATE_COMPLETED;
  const size_t length =
      format_summary(summary, sizeof(summary), context->options, report);
  if (!write_file_atomic(marker, summary, length)) {
    report->status = ONION_MIGRATE_FAILED;
    migrate_fail(context, errno, "cannot write %s", ONION_DIST_MIGRATION_MARKER);
  }
}

/* One start of the import, under the lock when there is one. */
static void run_attempt(MigrateContext *context, const char *marker,
                        const char *attempts_path) {
  OnionMigrateReport *report = context->report;
  if (!migrate_failed(context)) {
    /* Counted before any copying: a start that dies part-way (crash, kernel
     * panic, power loss) still uses up its attempt. */
    const unsigned previous = read_attempts(attempts_path);
    if (previous >= ONION_MIGRATE_MAX_ATTEMPTS) {
      report->status = ONION_MIGRATE_GAVE_UP;
      report->attempt = previous;
      return;
    }
    report->attempt = previous + 1;
    if (!write_attempts(attempts_path, report->attempt))
      migrate_fail(context, errno ? errno : EIO, "cannot write %s",
                   ONION_MIGRATE_ATTEMPTS_NAME);
  }

  if (!migrate_failed(context) &&
      measure_and_check_space(context, attempts_path)) {
    migrate_config(context);
    if (!migrate_failed(context))
      migrate_copy_tree(context);
    if (!migrate_failed(context))
      write_marker(context, marker);
  }

  if (report->status == ONION_MIGRATE_COMPLETED) {
    unlink(attempts_path);
  } else if (report->attempt >= ONION_MIGRATE_MAX_ATTEMPTS) {
    report->status = ONION_MIGRATE_GAVE_UP;
    report->final_attempt = true;
  }

  append_log(context->options, report);
  log_outcome(context->options, report);
}

onion_migrate_status
onion_migrate_legacy_root(const OnionMigrateOptions *options,
                          OnionMigrateReport *report) {
  OnionMigrateReport local;
  if (!report)
    report = &local;
  memset(report, 0, sizeof(*report));
  report->status = ONION_MIGRATE_FAILED;

  MigrateContext context = {.options = options, .report = report};
  if (!options || !valid_root(options->legacy_root) ||
      !valid_root(options->target_root) ||
      is_inside(options->legacy_root, options->target_root) ||
      is_inside(options->target_root, options->legacy_root)) {
    migrate_fail(&context, EINVAL, "invalid migration roots");
    return report->status;
  }

  char marker[kPathMax], attempts_path[kPathMax];
  struct stat st;
  if (!migrate_join_path(marker, sizeof(marker), options->target_root,
                         ONION_DIST_MIGRATION_MARKER) ||
      !migrate_join_path(attempts_path, sizeof(attempts_path),
                         options->target_root, ONION_MIGRATE_ATTEMPTS_NAME)) {
    migrate_fail(&context, ENAMETOOLONG, "invalid migration roots");
    return report->status;
  }
  if (lstat(marker, &st) == 0 && S_ISREG(st.st_mode)) {
    report->status = ONION_MIGRATE_ALREADY_DONE;
    return report->status;
  }

  /* lstat: an OnionHEN "directory" that is really a link is not followed. */
  if (lstat(options->legacy_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
    report->status = ONION_MIGRATE_NO_LEGACY;
    return report->status;
  }

  if (mkdir(options->target_root, 0777) != 0 && errno != EEXIST) {
    migrate_fail(&context, errno, "cannot create %s", options->target_root);
  } else if (lstat(options->target_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
    migrate_fail(&context, ENOTDIR, "%s is not a directory",
                 options->target_root);
  }

  const int lock = migrate_failed(&context) ? kLockUnavailable
                                            : lock_root(options->target_root);
  if (lock == kLockBusy) {
    report->status = ONION_MIGRATE_BUSY;
    LOG_WARN("migration %s -> %s: another process is importing; left alone",
             options->legacy_root, options->target_root);
    return report->status;
  }

  run_attempt(&context, marker, attempts_path);
  if (lock >= 0)
    close(lock);
  return report->status;
}

bool onion_migrate_pending(const char *legacy_root, const char *target_root) {
  char marker[kPathMax], attempts[kPathMax];
  struct stat st;
  if (!valid_root(legacy_root) || !valid_root(target_root) ||
      !migrate_join_path(marker, sizeof(marker), target_root,
                         ONION_DIST_MIGRATION_MARKER) ||
      !migrate_join_path(attempts, sizeof(attempts), target_root,
                         ONION_MIGRATE_ATTEMPTS_NAME))
    return false;
  if (lstat(marker, &st) == 0)
    return false;
  if (lstat(legacy_root, &st) != 0 || !S_ISDIR(st.st_mode))
    return false;
  return read_attempts(attempts) < ONION_MIGRATE_MAX_ATTEMPTS;
}

onion_migrate_status
onion_migrate_legacy_data(onion_migrate_validate_fn validate_config,
                          void *validate_user, OnionMigrateReport reports[2]) {
  static const char *const kRoots[2][2] = {
      {ONION_DIST_LEGACY_DATA_ROOT, ONION_DIST_DATA_ROOT},
      {ONION_DIST_LEGACY_USER_DATA_ROOT, ONION_DIST_USER_DATA_ROOT},
  };
  OnionMigrateReport local[2];
  if (!reports)
    reports = local;

  onion_migrate_status worst = ONION_MIGRATE_NO_LEGACY;
  for (size_t i = 0; i < 2; ++i) {
    onion_migrate_status status;
    if (i > 0 && worst != ONION_MIGRATE_NO_LEGACY &&
        worst != ONION_MIGRATE_ALREADY_DONE &&
        worst != ONION_MIGRATE_COMPLETED) {
      memset(&reports[i], 0, sizeof(reports[i]));
      reports[i].status = status = ONION_MIGRATE_SKIPPED;
    } else {
      const OnionMigrateOptions options = {
          .legacy_root = kRoots[i][0],
          .target_root = kRoots[i][1],
          .validate_config = validate_config,
          .validate_user = validate_user,
      };
      status = onion_migrate_legacy_root(&options, &reports[i]);
    }
    if (severity(status) > severity(worst))
      worst = status;
  }
  return worst;
}
