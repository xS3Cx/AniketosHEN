/* The file tree of the import: which entries travel and how each arrives. */

#include "migrate_internal.h"

#include <onion/distribution.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
  kMaxDepth = 12,
  kCopyBufferSize = 64 * 1024,
};

static bool has_prefix(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
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

bool migrate_write_all(int fd, const void *data, size_t size) {
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

int migrate_open_temp(const char *temp, mode_t mode) {
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

/* The measuring pass sees the same entries, so only the copy counts them. */
static void skip(MigrateContext *context) {
  if (!context->measure_only)
    context->report->entries_skipped++;
}

static void copy_file(MigrateContext *context, const char *source,
                      const char *destination, const struct stat *expected,
                      const char *relative) {
  char temp[kPathMax];
  const int n = snprintf(temp, sizeof(temp), "%s%s", destination, kTempSuffix);
  if (n <= 0 || (size_t)n >= sizeof(temp)) {
    skip(context);
    return;
  }

  const int in = open(source, O_RDONLY | O_NOFOLLOW);
  if (in < 0) {
    migrate_fail(context, errno, "cannot read %s", relative);
    return;
  }
  /* The entry was classified with lstat(); make sure the object now open is
   * that same regular file and not something swapped in afterwards. */
  struct stat st;
  if (fstat(in, &st) != 0 || !S_ISREG(st.st_mode) ||
      st.st_dev != expected->st_dev || st.st_ino != expected->st_ino) {
    close(in);
    skip(context);
    return;
  }

  const int out = migrate_open_temp(temp, (st.st_mode & 0777) | 0600);
  if (out < 0) {
    migrate_fail(context, errno, "cannot create %s", relative);
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
    if (!migrate_write_all(out, context->buffer, (size_t)got)) {
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
    migrate_fail(context, copy_error, "cannot copy %s", relative);
    return;
  }
  if (!commit_temp(out, temp, destination)) {
    migrate_fail(context, errno, "cannot store %s", relative);
    return;
  }
  context->report->files_copied++;
  context->report->bytes_copied += copied;
}

static void measure_file(MigrateContext *context, const struct stat *st,
                         unsigned long long size) {
  OnionMigrateReport *report = context->report;
  const unsigned long long unit = context->fragment;
  const unsigned long long rounded = (size + unit - 1) / unit * unit;
  const unsigned long long allocated =
      st->st_blocks > 0 ? (unsigned long long)st->st_blocks * 512ull : 0;
  /* One more unit for the inode and the directory entry. */
  context->alloc_needed += (allocated > rounded ? allocated : rounded) + unit;
  report->bytes_to_copy += size;
  report->files_to_copy++;
  if (report->files_to_copy > ONION_MIGRATE_MAX_FILES ||
      report->bytes_to_copy > ONION_MIGRATE_MAX_BYTES)
    context->over_limit = true;
}

static void walk(MigrateContext *context, const char *source_dir,
                 const char *destination_dir, const char *relative_dir,
                 int depth) {
  DIR *dir = opendir(source_dir);
  if (!dir) {
    if (!context->measure_only)
      migrate_fail(context, errno, "cannot list %s",
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
        !migrate_join_path(source, sizeof(source), source_dir, name) ||
        !migrate_join_path(destination, sizeof(destination), destination_dir,
                           name) ||
        snprintf(relative, sizeof(relative), "%s%s%s", relative_dir,
                 relative_dir[0] ? "/" : "", name) >= (int)sizeof(relative) ||
        lstat(source, &st) != 0) {
      skip(context);
      continue;
    }

    const bool is_directory = S_ISDIR(st.st_mode);
    /* Links and special files are never followed or reproduced. */
    if ((!is_directory && !S_ISREG(st.st_mode)) ||
        is_distribution_owned(name, is_directory, depth) ||
        (is_directory && depth + 1 >= kMaxDepth)) {
      skip(context);
      continue;
    }

    struct stat existing;
    const bool exists = lstat(destination, &existing) == 0;

    if (is_directory) {
      if (exists && !S_ISDIR(existing.st_mode)) {
        /* Something else already owns that name here; leave both alone. */
        skip(context);
        continue;
      }
      if (!exists && context->measure_only)
        context->alloc_needed += context->fragment;
      if (!exists && !context->measure_only) {
        if (mkdir(destination, (st.st_mode & 0777) | 0700) != 0 &&
            errno != EEXIST) {
          migrate_fail(context, errno, "cannot create directory %s", relative);
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
    if (context->measure_only)
      measure_file(context, &st, size);
    else
      copy_file(context, source, destination, &st, relative);
  }
  closedir(dir);
}

void migrate_measure_tree(MigrateContext *context) {
  context->measure_only = true;
  walk(context, context->options->legacy_root, context->options->target_root,
       "", 0);
  context->measure_only = false;
}

void migrate_copy_tree(MigrateContext *context) {
  context->buffer = malloc(kCopyBufferSize);
  if (!context->buffer) {
    migrate_fail(context, ENOMEM, "out of memory");
    return;
  }
  walk(context, context->options->legacy_root, context->options->target_root,
       "", 0);
  free(context->buffer);
  context->buffer = NULL;
}
