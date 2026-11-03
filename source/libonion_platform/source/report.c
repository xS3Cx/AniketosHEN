/* Local error reports and their scrubbing. See <onion/report.h>. */

#include <onion/report.h>

#include <onion/fs.h>
#include <onion/log.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { kPathMax = 1024, kNameMax = 128 };

typedef struct Out {
  char *data;
  size_t cap; /* including the terminator */
  size_t len;
} Out;

static void put(Out *out, const char *text, size_t length) {
  if (out->len + 1 >= out->cap)
    return;
  const size_t room = out->cap - 1 - out->len;
  if (length > room)
    length = room;
  memcpy(out->data + out->len, text, length);
  out->len += length;
  out->data[out->len] = '\0';
}

static void put_str(Out *out, const char *text) { put(out, text, strlen(text)); }

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

static bool is_digit(int c) { return c >= '0' && c <= '9'; }

static bool is_hex(int c) {
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_word(int c) {
  return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         c == '_';
}

/* Labels whose value is an identifier, matched case-insensitively at a word
 * start and only when an assignment follows ("=", ":", "=>"). Longer
 * spellings come before their prefixes. */
static const char *const kLabels[] = {
    "username",      "user_name",    "user name",   "user",
    "online_id",     "online-id",    "onlineid",    "online id",
    "account_id",    "accountid",    "account id",  "account",
    "psn_id",        "psn",          "npid",        "np_id",
    "e-mail",        "email",        "mail",        "idps",
    "open_psid",     "openpsid",     "psid",        "serial_number",
    "serial number", "serialno",     "serial",      "nickname",
    "console_name",  "console name", "hostname",    "host",
    "ssid",          "mac",          "ip_address",  "ip address",
    "ipaddr",        "ip_addr",      "ip",
};

static size_t match_ci(const char *text, size_t length, const char *word) {
  size_t i = 0;
  for (; word[i]; ++i) {
    if (i >= length || lower((unsigned char)text[i]) != word[i])
      return 0;
  }
  return i;
}

/* "<label> <sep> <value>" at @at: copies the label and separator, replaces
 * the value. Returns the bytes consumed, 0 if nothing matched. */
static size_t scrub_labelled(Out *out, const char *at, size_t left) {
  for (size_t k = 0; k < sizeof(kLabels) / sizeof(kLabels[0]); ++k) {
    const size_t label = match_ci(at, left, kLabels[k]);
    if (label == 0 || (label < left && is_word((unsigned char)at[label])))
      continue;
    size_t i = label;
    while (i < left && (at[i] == ' ' || at[i] == '\t'))
      ++i;
    size_t separator = 0;
    if (i + 1 < left && at[i] == '=' && at[i + 1] == '>')
      separator = 2;
    else if (i < left && (at[i] == '=' || at[i] == ':'))
      separator = 1;
    if (separator == 0)
      continue;
    i += separator;
    while (i < left && (at[i] == ' ' || at[i] == '\t'))
      ++i;
    size_t end = i;
    if (end < left && (at[end] == '"' || at[end] == '\'')) {
      const char quote = at[end++];
      while (end < left && at[end] != quote && at[end] != '\n')
        ++end;
      if (end < left && at[end] == quote)
        ++end;
    } else {
      while (end < left && at[end] != ' ' && at[end] != '\t' &&
             at[end] != '\n' && at[end] != '\r' && at[end] != ',' &&
             at[end] != ';' && at[end] != ')' && at[end] != ']' &&
             at[end] != '}')
        ++end;
    }
    if (end == i)
      continue; /* nothing after the separator */
    put(out, at, i);
    put_str(out, "[redacted]");
    return end;
  }
  return 0;
}

/* d.d.d.d with 1-3 digits each, not part of a longer dotted run. */
static size_t match_ipv4(const char *at, size_t left) {
  size_t i = 0;
  for (int part = 0; part < 4; ++part) {
    size_t digits = 0;
    while (i < left && is_digit((unsigned char)at[i]) && digits < 4) {
      ++i;
      ++digits;
    }
    if (digits == 0 || digits > 3)
      return 0;
    if (part < 3) {
      if (i >= left || at[i] != '.')
        return 0;
      ++i;
    }
  }
  if (i < left && (is_word((unsigned char)at[i]) ||
                   (at[i] == '.' && i + 1 < left &&
                    is_digit((unsigned char)at[i + 1]))))
    return 0;
  return i;
}

/* MAC (six hex pairs joined by ':' or '-') or IPv6 (hex groups with "::" or
 * at least five ':'). */
static size_t match_hw_or_ipv6(const char *at, size_t left) {
  size_t i = 0, colons = 0, dashes = 0, hex = 0;
  bool double_colon = false;
  while (i < left &&
         (is_hex((unsigned char)at[i]) || at[i] == ':' || at[i] == '-')) {
    if (at[i] == ':') {
      ++colons;
      if (i > 0 && at[i - 1] == ':')
        double_colon = true;
    } else if (at[i] == '-') {
      ++dashes;
    } else {
      ++hex;
    }
    ++i;
  }
  while (i > 0 && (at[i - 1] == ':' || at[i - 1] == '-')) {
    if (at[i - 1] == ':')
      --colons;
    else
      --dashes;
    --i;
  }
  if (i < left && is_word((unsigned char)at[i]))
    return 0;
  if (dashes == 5 && colons == 0 && hex == 12 && i == 17)
    return i; /* aa-bb-cc-dd-ee-ff */
  if (dashes == 0 && hex >= 2 && (colons >= 5 || (double_colon && colons >= 2)))
    return i;
  return 0;
}

static size_t match_word_ci(const char *at, size_t left, const char *word) {
  const size_t n = strlen(word);
  if (n < 3 || n > left)
    return 0;
  for (size_t i = 0; i < n; ++i) {
    if (lower((unsigned char)at[i]) != lower((unsigned char)word[i]))
      return 0;
  }
  if (n < left && is_word((unsigned char)at[n]))
    return 0;
  return n;
}

size_t onion_report_sanitize(const char *in, size_t in_len, char *out_buf,
                             size_t out_cap, const char *const *words) {
  if (!out_buf || out_cap == 0)
    return 0;
  Out out = {out_buf, out_cap, 0};
  out_buf[0] = '\0';
  if (!in)
    return 0;

  static const char kHome[] = "/user/home/";
  size_t i = 0;
  while (i < in_len && out.len + 1 < out.cap) {
    const char *at = in + i;
    const size_t left = in_len - i;
    const bool word_start = i == 0 || !is_word((unsigned char)in[i - 1]);

    if (left > sizeof(kHome) - 1 &&
        memcmp(at, kHome, sizeof(kHome) - 1) == 0 &&
        is_hex((unsigned char)at[sizeof(kHome) - 1])) {
      size_t end = sizeof(kHome) - 1;
      while (end < left && is_hex((unsigned char)at[end]))
        ++end;
      put(&out, at, sizeof(kHome) - 1);
      put_str(&out, "[user]");
      i += end;
      continue;
    }

    if (word_start) {
      size_t used = scrub_labelled(&out, at, left);
      if (used) {
        i += used;
        continue;
      }
      for (const char *const *word = words; word && *word; ++word) {
        used = match_word_ci(at, left, *word);
        if (used)
          break;
      }
      if (used) {
        put_str(&out, "[user]");
        i += used;
        continue;
      }
      if ((used = match_ipv4(at, left)) != 0) {
        put_str(&out, "[ip]");
        i += used;
        continue;
      }
      if ((used = match_hw_or_ipv6(at, left)) != 0) {
        put_str(&out, "[addr]");
        i += used;
        continue;
      }
      /* Long hex or decimal runs: IDPS and OpenPSID are 32 hex digits,
       * account IDs 16+ decimal digits. Addresses written as 0x... are
       * shorter and keep their prefix, so crash frames stay readable. */
      size_t run = 0;
      bool decimal = true;
      while (run < left && is_hex((unsigned char)at[run])) {
        decimal = decimal && is_digit((unsigned char)at[run]);
        ++run;
      }
      if (run > 0 && (run == left || !is_word((unsigned char)at[run])) &&
          (run >= 32 || (decimal && run >= 16))) {
        put_str(&out, decimal ? "[number]" : "[id]");
        i += run;
        continue;
      }
      if (run > 0) {
        put(&out, at, run); /* an ordinary number or word part */
        i += run;
        continue;
      }
    }
    put(&out, at, 1);
    ++i;
  }
  return out.len;
}

static bool valid_kind(const char *kind) {
  if (!kind || !kind[0] || strlen(kind) > 48)
    return false;
  for (const char *p = kind; *p; ++p) {
    if (!(is_digit((unsigned char)*p) || (*p >= 'a' && *p <= 'z') || *p == '-'))
      return false;
  }
  return true;
}

bool onion_report_is_file_name(const char *name) {
  const size_t n = strlen(name);
  return n > 4 && n < kNameMax && name[0] != '.' &&
         strcmp(name + n - 4, ".txt") == 0;
}

static int compare_names(const void *a, const void *b) {
  return strcmp((const char *)a, (const char *)b);
}

/* Keep the newest ONION_REPORT_MAX_FILES; names start with the time. */
static void prune(const char *dir) {
  DIR *handle = opendir(dir);
  if (!handle)
    return;
  enum { kScanMax = 256 };
  static char names[kScanMax][kNameMax];
  size_t count = 0;
  struct dirent *entry;
  while ((entry = readdir(handle)) != NULL && count < kScanMax) {
    if (!onion_report_is_file_name(entry->d_name))
      continue;
    const size_t length = strlen(entry->d_name); /* < kNameMax, checked */
    memcpy(names[count], entry->d_name, length + 1);
    ++count;
  }
  closedir(handle);
  if (count <= ONION_REPORT_MAX_FILES)
    return;
  qsort(names, count, kNameMax, compare_names);
  for (size_t i = 0; i + ONION_REPORT_MAX_FILES < count; ++i) {
    char path[kPathMax];
    if (snprintf(path, sizeof(path), "%s/%s", dir, names[i]) <
        (int)sizeof(path))
      unlink(path);
  }
}

bool onion_report_write(const char *dir, const char *kind, const char *build,
                        const char *body) {
  if (!dir || dir[0] != '/' || !valid_kind(kind))
    return false;

  char *text = malloc(ONION_REPORT_MAX_BYTES + 1);
  if (!text)
    return false;
  const long long now = (long long)time(NULL);
  int header = snprintf(text, ONION_REPORT_MAX_BYTES + 1,
                        ONION_REPORT_MAGIC "\nkind: %s\nbuild: %s\ntime: %lld\n"
                                           "---\n",
                        kind, build && build[0] ? build : "unknown", now);
  if (header <= 0 || header >= (int)ONION_REPORT_MAX_BYTES) {
    free(text);
    return false;
  }
  const size_t length =
      (size_t)header +
      onion_report_sanitize(body ? body : "", body ? strlen(body) : 0,
                            text + header,
                            ONION_REPORT_MAX_BYTES + 1 - (size_t)header, NULL);

  static unsigned sequence = 0;
  char path[kPathMax];
  const int n = snprintf(path, sizeof(path), "%s/%010lld-%d-%u-%s.txt", dir,
                         now, (int)getpid(), sequence++ % 1000u, kind);
  bool ok = n > 0 && (size_t)n < sizeof(path) && mkdir_tree(dir) &&
            write_file_atomic(path, text, length);
  free(text);
  if (ok) {
    LOG_INFO("report written: %s", path);
    prune(dir);
  } else {
    LOG_WARN("report %s could not be written", kind);
  }
  return ok;
}

OnionReportRead onion_report_read(const char *path, char *out, size_t *length) {
  const int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0)
    return ONION_REPORT_READ_FAILED;
  size_t used = 0;
  while (used < ONION_REPORT_MAX_BYTES) {
    const ssize_t got = read(fd, out + used, ONION_REPORT_MAX_BYTES - used);
    if (got == 0)
      break;
    if (got < 0 && errno == EINTR)
      continue;
    if (got < 0) {
      close(fd);
      return ONION_REPORT_READ_FAILED;
    }
    used += (size_t)got;
  }
  close(fd);
  *length = used;
  const size_t magic = strlen(ONION_REPORT_MAGIC);
  return used >= magic && memcmp(out, ONION_REPORT_MAGIC, magic) == 0
             ? ONION_REPORT_READ_OK
             : ONION_REPORT_READ_NOT_OURS;
}

size_t onion_report_append_tail(char *out, size_t out_cap, const char *label,
                                const char *path, size_t max_bytes) {
  if (!out || out_cap == 0)
    return 0;
  size_t length = strnlen(out, out_cap - 1);
  out[length] = '\0';
  if (!path || max_bytes == 0)
    return length;
  const int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0)
    return length;
  struct stat st;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
    close(fd);
    return length;
  }
  /* A cut reads one byte more: the byte before the tail tells whether the
   * tail starts on a line of its own. */
  const bool cut = st.st_size > (off_t)max_bytes;
  const off_t start = cut ? st.st_size - (off_t)max_bytes - 1 : 0;
  const size_t want = cut ? max_bytes + 1 : max_bytes;
  char *chunk = malloc(want + 1);
  ssize_t got = -1;
  if (chunk && lseek(fd, start, SEEK_SET) == start) {
    do {
      got = read(fd, chunk, want);
    } while (got < 0 && errno == EINTR);
  }
  close(fd);
  if (got <= 0) {
    free(chunk);
    return length;
  }
  chunk[got] = '\0';
  const char *text = chunk;
  if (cut) {
    const char *newline = memchr(chunk, '\n', (size_t)got);
    text = newline ? newline + 1 : chunk + 1;
  }
  /* A log holds no NUL bytes; stop at one if a torn write left any. */
  const size_t text_length = strnlen(text, (size_t)got - (size_t)(text - chunk));

  Out sink = {out, out_cap, length};
  put_str(&sink, "== ");
  put_str(&sink, label ? label : path);
  put_str(&sink, " ==\n");
  put(&sink, text, text_length);
  if (text_length > 0 && text[text_length - 1] != '\n')
    put_str(&sink, "\n");
  free(chunk);
  return sink.len;
}
