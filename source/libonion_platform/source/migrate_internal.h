/* What the import shares: migrate.c runs it, migrate_tree.c copies the files,
 * migrate_config.c converts config.ini. See <onion/migrate.h>. */
#pragma once

#include <onion/migrate.h>

#include <stdio.h>
#include <sys/types.h>

enum { kPathMax = 1024 };

static const char kTempSuffix[] = ".migrate.tmp";

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

/* Record the first failure only: it is the one that explains the rest. */
void migrate_fail(MigrateContext *context, int error, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static inline bool migrate_failed(const MigrateContext *context) {
  return context->report->error != 0;
}

static inline bool migrate_join_path(char *out, size_t out_sz, const char *dir,
                                     const char *name) {
  const int n = snprintf(out, out_sz, "%s/%s", dir, name);
  return n > 0 && (size_t)n < out_sz;
}

bool migrate_write_all(int fd, const void *data, size_t size);

/* Create @temp exclusively. A leftover from an interrupted run is ours to
 * remove; anything else at that name (a link, a directory) makes open fail. */
int migrate_open_temp(const char *temp, mode_t mode);

/* Fill in what the copy would take, writing nothing. */
void migrate_measure_tree(MigrateContext *context);

void migrate_copy_tree(MigrateContext *context);

/* Convert and install config.ini; the outcome lands in report->config. */
void migrate_config(MigrateContext *context);
