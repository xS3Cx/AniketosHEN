/* One-time import of an OnionHEN data directory. See <onion/migrate.h>. */

#include <onion/migrate.h>

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

enum {
  kPathMax = 1024,
  kMaxDepth = 12,
  kCopyBufferSize = 64 * 1024,
  kConfigMaxBytes = 256 * 1024,
  kLogMaxBytes = 64 * 1024,
};

/* Allocation unit assumed when the volume does not report one. Small files
 * dominate (cheats), and each takes at least one fragment plus its inode and
 * directory entry, so summed byte sizes alone underestimate badly. */
static const unsigned long long kDefaultFragment = 32ull * 1024ull;

static const char kTempSuffix[] = ".migrate.tmp";
static const char kLegacyOptionsKey[] = "show_onionhen_options";
static const char kOptionsKey[] = "show_aniketoshen_options";
static const char kLegacyOptionsComment[] =
    "# show_onionhen_options adds OnionHEN entries to the game options menu.";
static const char kOptionsComment[] =
    "# show_aniketoshen_options adds " ONION_DIST_NAME
    " entries to the game options menu.";

typedef struct MigrateContext {
  const OnionMigrateOptions *options;
  OnionMigrateReport *report;
  unsigned char *buffer;
  unsigned long long fragment;     /* allocation unit of the target volume */
  unsigned long long alloc_needed; /* estimated allocation for the copy */
  bool measure_only;
  bool over_limit; /* measured past the size limits: stop measuring */
  bool stop; /* storage-level failure: no point continuing */
} MigrateContext;

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

static bool join_path(char *out, size_t out_sz, const char *dir,
                      const char *name) {
  const int n = snprintf(out, out_sz, "%s/%s", dir, name);
  return n > 0 && (size_t)n < out_sz;
}

static bool has_prefix(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* Record the first failure only: it is the one that explains the rest. */
static void fail(MigrateContext *context, int error, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void fail(MigrateContext *context, int error, const char *fmt, ...) {
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

static bool failed(const MigrateContext *context) {
  return context->report->error != 0;
}

/* Files OnionHEN regenerates, caches or logs. They are not user data and are
 * either rebuilt by this distribution under its own names or meaningless to
 * it, so they stay behind in the OnionHEN directory. */
static bool is_distribution_owned(const char *name, bool is_directory,
                                  int depth) {
  if (strstr(name, ".tmp.") != NULL || strstr(name, kTempSuffix) != NULL)
    return true; /* in-flight atomic-write temp files, at any depth */
  if (depth != 0)
    return false;
  if (is_directory)
    return strcmp(name, "assets") == 0 || strcmp(name, "cheats_tmp") == 0 ||
           strcmp(name, "cheats_staging") == 0 ||
           /* game dumps: tens of GB, not HEN data, nothing here reads them */
           strcmp(name, "games") == 0 ||
           /* this distribution's own error reports */
           strcmp(name, "reports") == 0;
  if (strcmp(name, ONION_DIST_CONFIG_NAME) == 0)
    return true; /* converted separately by migrate_config() */
  if (strcmp(name, ONION_MIGRATE_LOCK_NAME) == 0 ||
      strcmp(name, ONION_MIGRATE_ATTEMPTS_NAME) == 0 ||
      strcmp(name, ".boot_state") == 0)
    return true; /* bookkeeping */
  if (strcmp(name, "onionhen.png") == 0 || strcmp(name, "onionhen.elf") == 0)
    return true; /* notification icon, bootstrapper cache */
  if (has_prefix(name, ONION_DIST_LEGACY_NAME) && strstr(name, ".log") != NULL)
    return true; /* OnionHEN.log, OnionHEN_crash.log, rotations */
  if (strstr(name, "_cheats_") != NULL)
    return true; /* per-request ShellUI cheat list exchange files */
  return strcmp(name, ONION_DIST_MIGRATION_MARKER) == 0 ||
         strcmp(name, ONION_DIST_MIGRATION_LOG) == 0;
}

static bool write_all(int fd, const void *data, size_t size) {
  const unsigned char *cursor = (const unsigned char *)data;
  while (size > 0) {
    const ssize_t written = write(fd, cursor, size);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0) {
      if (written == 0)
        errno = EIO;
      return false;
    }
    cursor += (size_t)written;
    size -= (size_t)written;
  }
  return true;
}

/* Create @temp exclusively. A leftover from an interrupted run is ours to
 * remove; anything else at that name (a link, a directory) makes open fail. */
static int open_temp(const char *temp, mode_t mode) {
  struct stat st;
  if (lstat(temp, &st) == 0 && S_ISREG(st.st_mode))
    unlink(temp);
  return open(temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
}

static bool commit_temp(int fd, const char *temp, const char *destination) {
  bool ok = fsync(fd) == 0;
  if (close(fd) != 0)
    ok = false;
  if (ok && rename(temp, destination) != 0)
    ok = false;
  if (!ok) {
    const int saved = errno;
    unlink(temp);
    errno = saved;
  }
  return ok;
}

static void copy_file(MigrateContext *context, const char *source,
                      const char *destination, const struct stat *expected,
                      const char *relative) {
  char temp[kPathMax];
  const int n = snprintf(temp, sizeof(temp), "%s%s", destination, kTempSuffix);
  if (n <= 0 || (size_t)n >= sizeof(temp)) {
    context->report->entries_skipped++;
    return;
  }

  const int in = open(source, O_RDONLY | O_NOFOLLOW);
  if (in < 0) {
    fail(context, errno, "cannot read %s", relative);
    return;
  }
  /* The entry was classified with lstat(); make sure the object now open is
   * that same regular file and not something swapped in afterwards. */
  struct stat st;
  if (fstat(in, &st) != 0 || !S_ISREG(st.st_mode) ||
      st.st_dev != expected->st_dev || st.st_ino != expected->st_ino) {
    close(in);
    context->report->entries_skipped++;
    return;
  }

  const int out = open_temp(temp, (st.st_mode & 0777) | 0600);
  if (out < 0) {
    fail(context, errno, "cannot create %s", relative);
    close(in);
    return;
  }

  unsigned long long copied = 0;
  bool ok = true;
  for (;;) {
    const ssize_t got = read(in, context->buffer, kCopyBufferSize);
    if (got < 0 && errno == EINTR)
      continue;
    if (got < 0) {
      ok = false;
      break;
    }
    if (got == 0)
      break;
    if (!write_all(out, context->buffer, (size_t)got)) {
      ok = false;
      break;
    }
    copied += (unsigned long long)got;
  }
  const int copy_error = errno;
  close(in);

  if (!ok) {
    close(out);
    unlink(temp);
    fail(context, copy_error, "cannot copy %s", relative);
    return;
  }
  if (!commit_temp(out, temp, destination)) {
    fail(context, errno, "cannot store %s", relative);
    return;
  }
  context->report->files_copied++;
  context->report->bytes_copied += copied;
}

static void walk(MigrateContext *context, const char *source_dir,
                 const char *destination_dir, const char *relative_dir,
                 int depth) {
  DIR *dir = opendir(source_dir);
  if (!dir) {
    if (!context->measure_only)
      fail(context, errno, "cannot list %s",
           relative_dir[0] ? relative_dir : ".");
    return;
  }

  struct dirent *entry;
  while (!context->stop && !context->over_limit &&
         (entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    char source[kPathMax], destination[kPathMax], relative[kPathMax];
    struct stat st;
    if (strchr(name, '/') != NULL ||
        !join_path(source, sizeof(source), source_dir, name) ||
        !join_path(destination, sizeof(destination), destination_dir, name) ||
        snprintf(relative, sizeof(relative), "%s%s%s", relative_dir,
                 relative_dir[0] ? "/" : "", name) >= (int)sizeof(relative) ||
        lstat(source, &st) != 0) {
      if (!context->measure_only)
        context->report->entries_skipped++;
      continue;
    }

    const bool is_directory = S_ISDIR(st.st_mode);
    /* Links and special files are never followed or reproduced. */
    if ((!is_directory && !S_ISREG(st.st_mode)) ||
        is_distribution_owned(name, is_directory, depth) ||
        (is_directory && depth + 1 >= kMaxDepth)) {
      if (!context->measure_only)
        context->report->entries_skipped++;
      continue;
    }

    struct stat existing;
    const bool exists = lstat(destination, &existing) == 0;

    if (is_directory) {
      if (exists && !S_ISDIR(existing.st_mode)) {
        /* Something else already owns that name here; leave both alone. */
        if (!context->measure_only)
          context->report->entries_skipped++;
        continue;
      }
      if (!exists && context->measure_only)
        context->alloc_needed += context->fragment;
      if (!exists && !context->measure_only) {
        if (mkdir(destination, (st.st_mode & 0777) | 0700) != 0 &&
            errno != EEXIST) {
          fail(context, errno, "cannot create directory %s", relative);
          continue;
        }
        context->report->directories_created++;
      }
      walk(context, source, destination, relative, depth + 1);
      continue;
    }

    if (exists) {
      /* Never overwrite: whatever is under the target is the newer truth. */
      if (!context->measure_only)
        context->report->files_present++;
      continue;
    }
    const unsigned long long size =
        st.st_size > 0 ? (unsigned long long)st.st_size : 0;
    if (size > ONION_MIGRATE_MAX_FILE_BYTES) {
      /* A dump or an archive, not HEN data: it stays where it is. */
      if (!context->measure_only)
        context->report->files_too_large++;
      continue;
    }
    if (context->measure_only) {
      OnionMigrateReport *report = context->report;
      const unsigned long long unit = context->fragment;
      const unsigned long long rounded = (size + unit - 1) / unit * unit;
      const unsigned long long allocated =
          st.st_blocks > 0 ? (unsigned long long)st.st_blocks * 512ull : 0;
      /* One more unit for the inode and the directory entry. */
      context->alloc_needed += (allocated > rounded ? allocated : rounded) + unit;
      report->bytes_to_copy += size;
      report->files_to_copy++;
      if (report->files_to_copy > ONION_MIGRATE_MAX_FILES ||
          report->bytes_to_copy > ONION_MIGRATE_MAX_BYTES)
        context->over_limit = true;
      continue;
    }
    copy_file(context, source, destination, &st, relative);
  }
  closedir(dir);
}

/* ---- config.ini ---------------------------------------------------------- */

static bool line_is(const char *line, size_t length, const char *text) {
  return length == strlen(text) && memcmp(line, text, length) == 0;
}

/* Matches "<spaces>key<spaces>=" and reports where the key itself starts. */
static bool line_assigns(const char *line, size_t length, const char *key,
                         size_t *key_offset) {
  const size_t key_length = strlen(key);
  size_t i = 0;
  while (i < length && (line[i] == ' ' || line[i] == '\t'))
    ++i;
  if (length - i < key_length || memcmp(line + i, key, key_length) != 0)
    return false;
  size_t j = i + key_length;
  while (j < length && (line[j] == ' ' || line[j] == '\t'))
    ++j;
  if (j >= length || line[j] != '=')
    return false;
  if (key_offset)
    *key_offset = i;
  return true;
}

static bool line_is_section(const char *line, size_t length, const char *name) {
  size_t i = 0;
  while (i < length && (line[i] == ' ' || line[i] == '\t'))
    ++i;
  const size_t name_length = strlen(name);
  return length - i >= name_length + 2 && line[i] == '[' &&
         memcmp(line + i + 1, name, name_length) == 0 &&
         line[i + 1 + name_length] == ']';
}

typedef struct ConfigLine {
  const char *text; /* without the line terminator */
  size_t length;    /* excluding a trailing '\r' */
  size_t raw;       /* bytes consumed from the input, terminator included */
} ConfigLine;

static ConfigLine next_line(const char *in, size_t in_len, size_t offset) {
  ConfigLine line = {in + offset, 0, 0};
  const char *end = memchr(in + offset, '\n', in_len - offset);
  line.raw = end ? (size_t)(end - (in + offset)) + 1 : in_len - offset;
  line.length = end ? line.raw - 1 : line.raw;
  if (line.length > 0 && line.text[line.length - 1] == '\r')
    --line.length;
  return line;
}

bool onion_migrate_config_text(const char *in, size_t in_len, char **out,
                               size_t *out_len) {
  if (!out || !out_len || (!in && in_len > 0))
    return false;

  /* If the file already carries the current key, the legacy line is left as
   * it is: the loader prefers the current key, and two lines assigning the
   * same key would only make the file ambiguous to a human reader. */
  bool in_game_menu = false;
  bool has_current_key = false;
  size_t renames = 0;
  for (size_t offset = 0; offset < in_len;) {
    const ConfigLine line = next_line(in, in_len, offset);
    if (line.length > 0 && line.text[0] == '[')
      in_game_menu = line_is_section(line.text, line.length, "game_menu");
    else if (in_game_menu &&
             line_assigns(line.text, line.length, kOptionsKey, NULL))
      has_current_key = true;
    else if (in_game_menu &&
             line_assigns(line.text, line.length, kLegacyOptionsKey, NULL))
      ++renames;
    else if (line_is(line.text, line.length, kLegacyOptionsComment))
      ++renames;
    offset += line.raw;
  }

  const size_t growth = sizeof(kOptionsComment) + sizeof(kOptionsKey);
  char *buffer = malloc(in_len + renames * growth + 1);
  if (!buffer)
    return false;

  size_t used = 0;
  in_game_menu = false;
  for (size_t offset = 0; offset < in_len;) {
    const ConfigLine line = next_line(in, in_len, offset);
    const size_t terminator = line.raw - line.length;
    size_t key_offset = 0;
    if (line.length > 0 && line.text[0] == '[')
      in_game_menu = line_is_section(line.text, line.length, "game_menu");

    if (!has_current_key && in_game_menu &&
        line_assigns(line.text, line.length, kLegacyOptionsKey, &key_offset)) {
      const size_t tail = key_offset + strlen(kLegacyOptionsKey);
      memcpy(buffer + used, line.text, key_offset);
      used += key_offset;
      memcpy(buffer + used, kOptionsKey, strlen(kOptionsKey));
      used += strlen(kOptionsKey);
      memcpy(buffer + used, line.text + tail, line.length - tail);
      used += line.length - tail;
    } else if (!has_current_key &&
               line_is(line.text, line.length, kLegacyOptionsComment)) {
      memcpy(buffer + used, kOptionsComment, strlen(kOptionsComment));
      used += strlen(kOptionsComment);
    } else {
      memcpy(buffer + used, line.text, line.length);
      used += line.length;
    }
    memcpy(buffer + used, line.text + line.length, terminator);
    used += terminator;
    offset += line.raw;
  }
  buffer[used] = '\0';
  *out = buffer;
  *out_len = used;
  return true;
}

static void migrate_config(MigrateContext *context) {
  OnionMigrateReport *report = context->report;
  char source[kPathMax], destination[kPathMax], temp[kPathMax];
  if (!join_path(source, sizeof(source), context->options->legacy_root,
                 ONION_DIST_CONFIG_NAME) ||
      !join_path(destination, sizeof(destination),
                 context->options->target_root, ONION_DIST_CONFIG_NAME) ||
      snprintf(temp, sizeof(temp), "%s%s", destination, kTempSuffix) >=
          (int)sizeof(temp)) {
    report->config = ONION_MIGRATE_CONFIG_REJECTED;
    return;
  }

  struct stat st;
  if (lstat(destination, &st) == 0) {
    report->config = ONION_MIGRATE_CONFIG_KEPT_EXISTING;
    return;
  }
  if (lstat(source, &st) != 0) {
    if (errno == ENOENT) {
      report->config = ONION_MIGRATE_CONFIG_NONE;
    } else {
      report->config = ONION_MIGRATE_CONFIG_FAILED;
      fail(context, errno, "cannot inspect %s", ONION_DIST_CONFIG_NAME);
    }
    return;
  }
  if (!S_ISREG(st.st_mode) || st.st_size < 0 ||
      st.st_size > (off_t)kConfigMaxBytes) {
    report->config = ONION_MIGRATE_CONFIG_REJECTED;
    return;
  }

  const int in = open(source, O_RDONLY | O_NOFOLLOW);
  if (in < 0) {
    report->config = ONION_MIGRATE_CONFIG_FAILED;
    fail(context, errno, "cannot read %s", ONION_DIST_CONFIG_NAME);
    return;
  }
  char *text = malloc((size_t)kConfigMaxBytes + 1);
  size_t length = 0;
  bool readable = text != NULL;
  while (readable && length <= (size_t)kConfigMaxBytes) {
    const ssize_t got = read(in, text + length, (size_t)kConfigMaxBytes + 1 - length);
    if (got < 0 && errno == EINTR)
      continue;
    if (got < 0)
      readable = false;
    if (got <= 0)
      break;
    length += (size_t)got;
  }
  const int read_error = text ? errno : ENOMEM;
  close(in);
  if (!readable) {
    free(text);
    report->config = ONION_MIGRATE_CONFIG_FAILED;
    fail(context, read_error, "cannot read %s", ONION_DIST_CONFIG_NAME);
    return;
  }
  /* Grew past the cap while being read, or holds NUL bytes: not an INI. */
  if (length > (size_t)kConfigMaxBytes || memchr(text, '\0', length) != NULL) {
    free(text);
    report->config = ONION_MIGRATE_CONFIG_REJECTED;
    return;
  }

  char *converted = NULL;
  size_t converted_length = 0;
  const bool ok_text =
      onion_migrate_config_text(text, length, &converted, &converted_length);
  free(text);
  if (!ok_text) {
    report->config = ONION_MIGRATE_CONFIG_FAILED;
    fail(context, ENOMEM, "cannot convert %s", ONION_DIST_CONFIG_NAME);
    return;
  }

  const int out = open_temp(temp, 0666);
  if (out < 0) {
    free(converted);
    report->config = ONION_MIGRATE_CONFIG_FAILED;
    fail(context, errno, "cannot create %s", ONION_DIST_CONFIG_NAME);
    return;
  }
  const bool written = write_all(out, converted, converted_length);
  const int write_error = errno;
  free(converted);
  if (!written || fsync(out) != 0) {
    const int error = written ? errno : write_error;
    close(out);
    unlink(temp);
    report->config = ONION_MIGRATE_CONFIG_FAILED;
    fail(context, error, "cannot write %s", ONION_DIST_CONFIG_NAME);
    return;
  }
  if (close(out) != 0) {
    const int error = errno;
    unlink(temp);
    report->config = ONION_MIGRATE_CONFIG_FAILED;
    fail(context, error, "cannot write %s", ONION_DIST_CONFIG_NAME);
    return;
  }

  /* Commit only a file this build can actually load. */
  if (context->options->validate_config &&
      !context->options->validate_config(temp,
                                         context->options->validate_user)) {
    unlink(temp);
    report->config = ONION_MIGRATE_CONFIG_REJECTED;
    return;
  }
  if (rename(temp, destination) != 0) {
    const int error = errno;
    unlink(temp);
    report->config = ONION_MIGRATE_CONFIG_FAILED;
    fail(context, error, "cannot store %s", ONION_DIST_CONFIG_NAME);
    return;
  }
  report->config = ONION_MIGRATE_CONFIG_MIGRATED;
}

/* ---- orchestration ------------------------------------------------------- */

static int format_summary(char *out, size_t out_sz,
                          const OnionMigrateOptions *options,
                          const OnionMigrateReport *report) {
  return snprintf(
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
}

/* Best effort: the diagnostic must never turn a result into a failure. */
static void append_log(const OnionMigrateOptions *options,
                       const OnionMigrateReport *report) {
  char path[kPathMax], line[768];
  if (!join_path(path, sizeof(path), options->target_root,
                 ONION_DIST_MIGRATION_LOG))
    return;
  const int n = format_summary(line, sizeof(line), options, report);
  if (n <= 0)
    return;
  const size_t length = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;

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
  (void)write_all(fd, line, length);
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
  if (!join_path(path, sizeof(path), target_root, ONION_MIGRATE_LOCK_NAME))
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
    fail(&context, EINVAL, "invalid migration roots");
    return report->status;
  }

  char marker[kPathMax], attempts_path[kPathMax];
  struct stat st;
  if (!join_path(marker, sizeof(marker), options->target_root,
                 ONION_DIST_MIGRATION_MARKER) ||
      !join_path(attempts_path, sizeof(attempts_path), options->target_root,
                 ONION_MIGRATE_ATTEMPTS_NAME)) {
    fail(&context, ENAMETOOLONG, "invalid migration roots");
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
    fail(&context, errno, "cannot create %s", options->target_root);
  } else if (lstat(options->target_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
    fail(&context, ENOTDIR, "%s is not a directory", options->target_root);
  }

  int lock = kLockUnavailable;
  if (!failed(&context)) {
    lock = lock_root(options->target_root);
    if (lock == kLockBusy) {
      report->status = ONION_MIGRATE_BUSY;
      LOG_WARN("migration %s -> %s: another process is importing; left alone",
               options->legacy_root, options->target_root);
      return report->status;
    }

    /* Counted before any copying: a start that dies part-way (crash, kernel
     * panic, power loss) still uses up its attempt. */
    const unsigned previous = read_attempts(attempts_path);
    if (previous >= ONION_MIGRATE_MAX_ATTEMPTS) {
      report->status = ONION_MIGRATE_GAVE_UP;
      report->attempt = previous;
      if (lock >= 0)
        close(lock);
      return report->status;
    }
    report->attempt = previous + 1;
    if (!write_attempts(attempts_path, report->attempt))
      fail(&context, errno ? errno : EIO, "cannot write %s",
           ONION_MIGRATE_ATTEMPTS_NAME);
  }

  if (!failed(&context)) {
    struct statvfs vfs;
    const bool have_vfs = statvfs(options->target_root, &vfs) == 0;
    unsigned long long fragment =
        have_vfs ? (unsigned long long)(vfs.f_frsize ? vfs.f_frsize
                                                     : vfs.f_bsize)
                 : 0;
    if (fragment < 512 || fragment > 1024ull * 1024ull)
      fragment = kDefaultFragment;
    context.fragment = fragment;

    context.measure_only = true;
    walk(&context, options->legacy_root, options->target_root, "", 0);
    context.measure_only = false;

    if (context.over_limit) {
      /* Not a retryable failure: the next start would measure the same. */
      report->status = ONION_MIGRATE_GAVE_UP;
      report->final_attempt = true;
      snprintf(report->detail, sizeof(report->detail),
               "too large to import: more than %u files or %llu MiB",
               ONION_MIGRATE_MAX_FILES,
               ONION_MIGRATE_MAX_BYTES / (1024ull * 1024ull));
      (void)write_attempts(attempts_path, ONION_MIGRATE_MAX_ATTEMPTS);
      append_log(options, report);
      log_outcome(options, report);
      if (lock >= 0)
        close(lock);
      return report->status;
    }

    if (have_vfs) {
      const unsigned long long available =
          (unsigned long long)vfs.f_bavail * (unsigned long long)vfs.f_frsize;
      const unsigned long long reserve =
          options->reserve_bytes ? options->reserve_bytes
                                 : ONION_MIGRATE_DEFAULT_RESERVE_BYTES;
      report->bytes_available = available;
      if (context.alloc_needed > 0 &&
          available < context.alloc_needed + reserve)
        fail(&context, ENOSPC, "need %llu bytes free, %llu available",
             context.alloc_needed + reserve, available);
    }
  }

  if (!failed(&context))
    migrate_config(&context);

  if (!failed(&context)) {
    context.buffer = malloc(kCopyBufferSize);
    if (!context.buffer)
      fail(&context, ENOMEM, "out of memory");
    else
      walk(&context, options->legacy_root, options->target_root, "", 0);
    free(context.buffer);
    context.buffer = NULL;
  }

  if (!failed(&context)) {
    char summary[768];
    report->status = ONION_MIGRATE_COMPLETED;
    const int n = format_summary(summary, sizeof(summary), options, report);
    const size_t length =
        n <= 0 ? 0 : ((size_t)n < sizeof(summary) ? (size_t)n : sizeof(summary) - 1);
    if (!write_file_atomic(marker, summary, length)) {
      report->status = ONION_MIGRATE_FAILED;
      fail(&context, errno, "cannot write %s", ONION_DIST_MIGRATION_MARKER);
    }
  }

  if (report->status == ONION_MIGRATE_COMPLETED) {
    unlink(attempts_path);
  } else if (report->attempt >= ONION_MIGRATE_MAX_ATTEMPTS) {
    report->status = ONION_MIGRATE_GAVE_UP;
    report->final_attempt = true;
  }

  append_log(options, report);
  log_outcome(options, report);
  if (lock >= 0)
    close(lock);
  return report->status;
}

bool onion_migrate_pending(const char *legacy_root, const char *target_root) {
  char marker[kPathMax], attempts[kPathMax];
  struct stat st;
  if (!valid_root(legacy_root) || !valid_root(target_root) ||
      !join_path(marker, sizeof(marker), target_root,
                 ONION_DIST_MIGRATION_MARKER) ||
      !join_path(attempts, sizeof(attempts), target_root,
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
