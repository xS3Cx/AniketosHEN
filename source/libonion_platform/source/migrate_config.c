/* config.ini for the import: reads OnionHEN's, renames its one key, validates
 * the result and installs it. */

#include "migrate_internal.h"

#include <onion/distribution.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { kConfigMaxBytes = 256 * 1024 };

static const char kLegacyOptionsKey[] = "show_onionhen_options";
static const char kOptionsKey[] = "show_aniketoshen_options";
static const char kLegacyOptionsComment[] =
    "# show_onionhen_options adds OnionHEN entries to the game options menu.";
static const char kOptionsComment[] =
    "# show_aniketoshen_options adds " ONION_DIST_NAME
    " entries to the game options menu.";

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

static void put(char *buffer, size_t *used, const char *text, size_t length) {
  memcpy(buffer + *used, text, length);
  *used += length;
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
    size_t key_offset = 0;
    if (line.length > 0 && line.text[0] == '[')
      in_game_menu = line_is_section(line.text, line.length, "game_menu");

    if (!has_current_key && in_game_menu &&
        line_assigns(line.text, line.length, kLegacyOptionsKey, &key_offset)) {
      const size_t tail = key_offset + strlen(kLegacyOptionsKey);
      put(buffer, &used, line.text, key_offset);
      put(buffer, &used, kOptionsKey, strlen(kOptionsKey));
      put(buffer, &used, line.text + tail, line.length - tail);
    } else if (!has_current_key &&
               line_is(line.text, line.length, kLegacyOptionsComment)) {
      put(buffer, &used, kOptionsComment, strlen(kOptionsComment));
    } else {
      put(buffer, &used, line.text, line.length);
    }
    put(buffer, &used, line.text + line.length, line.raw - line.length);
    offset += line.raw;
  }
  buffer[used] = '\0';
  *out = buffer;
  *out_len = used;
  return true;
}

static void config_failed(MigrateContext *context, int error,
                          const char *action) {
  context->report->config = ONION_MIGRATE_CONFIG_FAILED;
  migrate_fail(context, error, "cannot %s %s", action, ONION_DIST_CONFIG_NAME);
}

/* Returns the text (caller frees), or NULL with report->config set. */
static char *read_config(MigrateContext *context, const char *source,
                         size_t *text_length) {
  const int in = open(source, O_RDONLY | O_NOFOLLOW);
  if (in < 0) {
    config_failed(context, errno, "read");
    return NULL;
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
    config_failed(context, read_error, "read");
    return NULL;
  }
  /* Grew past the cap while being read, or holds NUL bytes: not an INI. */
  if (length > (size_t)kConfigMaxBytes || memchr(text, '\0', length) != NULL) {
    free(text);
    context->report->config = ONION_MIGRATE_CONFIG_REJECTED;
    return NULL;
  }
  *text_length = length;
  return text;
}

static bool write_temp(MigrateContext *context, const char *temp,
                       const char *text, size_t length) {
  const int out = migrate_open_temp(temp, 0666);
  if (out < 0) {
    config_failed(context, errno, "create");
    return false;
  }
  bool ok = migrate_write_all(out, text, length) && fsync(out) == 0;
  if (close(out) != 0)
    ok = false;
  if (!ok) {
    const int error = errno;
    unlink(temp);
    config_failed(context, error, "write");
  }
  return ok;
}

void migrate_config(MigrateContext *context) {
  const OnionMigrateOptions *options = context->options;
  OnionMigrateReport *report = context->report;
  char source[kPathMax], destination[kPathMax], temp[kPathMax];
  if (!migrate_join_path(source, sizeof(source), options->legacy_root,
                         ONION_DIST_CONFIG_NAME) ||
      !migrate_join_path(destination, sizeof(destination), options->target_root,
                         ONION_DIST_CONFIG_NAME) ||
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
    if (errno == ENOENT)
      report->config = ONION_MIGRATE_CONFIG_NONE;
    else
      config_failed(context, errno, "inspect");
    return;
  }
  if (!S_ISREG(st.st_mode) || st.st_size < 0 ||
      st.st_size > (off_t)kConfigMaxBytes) {
    report->config = ONION_MIGRATE_CONFIG_REJECTED;
    return;
  }

  size_t length = 0;
  char *text = read_config(context, source, &length);
  if (!text)
    return;

  char *converted = NULL;
  size_t converted_length = 0;
  const bool ok_text =
      onion_migrate_config_text(text, length, &converted, &converted_length);
  free(text);
  if (!ok_text) {
    config_failed(context, ENOMEM, "convert");
    return;
  }

  const bool written = write_temp(context, temp, converted, converted_length);
  free(converted);
  if (!written)
    return;

  /* Commit only a file this build can actually load. */
  if (options->validate_config &&
      !options->validate_config(temp, options->validate_user)) {
    unlink(temp);
    report->config = ONION_MIGRATE_CONFIG_REJECTED;
    return;
  }
  if (rename(temp, destination) != 0) {
    const int error = errno;
    unlink(temp);
    config_failed(context, error, "store");
    return;
  }
  report->config = ONION_MIGRATE_CONFIG_MIGRATED;
}
