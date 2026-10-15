/* Local error reports.
 *
 * A report is a short plain-text file under ONION_REPORT_DIR, written when
 * something went wrong that the maintainers should hear about: a data import
 * that failed or was given up, a start that never finished (a kernel panic or
 * a crash during the start), a crash log left by a previous session.
 *
 * Util sends pending reports to the maintainers' relay when the build has a
 * report URL and the user has not turned sending off (config.ini, [reports]
 * send=false). Otherwise they stay on the console, capped in number and size,
 * for the user to look at or attach by hand.
 *
 * Text is scrubbed when it is written and again before it is sent: console
 * identifiers (IDPS, OpenPSID, serial number), IP and MAC addresses, account
 * IDs, /user/home/<id> paths and values logged next to user, account, PSN or
 * e-mail labels are replaced. Util also scrubs the names of the console's
 * local users, which only it can look up.
 */
#pragma once

#include <onion/distribution.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_REPORT_DIR ONION_DIST_DATA_ROOT "/reports"
/* Whole report file, header included. */
#define ONION_REPORT_MAX_BYTES (12u * 1024u)
/* Reports kept on the console; the oldest go first. */
#define ONION_REPORT_MAX_FILES 16u
#define ONION_REPORT_MAGIC ONION_DIST_NAME " report v1"

/* Copy @in (of @in_len bytes) to @out with identifiers replaced. @words is an
 * optional NULL-terminated list of further words to replace (local user
 * names); words shorter than three characters are ignored. Output is always
 * NUL-terminated and truncated to fit. Returns the output length. */
size_t onion_report_sanitize(const char *in, size_t in_len, char *out,
                             size_t out_cap, const char *const *words);

/* Write one report into @dir: a header naming the distribution, @kind
 * (letters, digits and '-' only) and @build, then @body, scrubbed and cut to
 * ONION_REPORT_MAX_BYTES. Old reports beyond ONION_REPORT_MAX_FILES are
 * removed. Best effort; returns false when nothing was written. */
bool onion_report_write(const char *dir, const char *kind, const char *build,
                        const char *body);

/* Append up to @max_bytes from the end of the file at @path to @out (of
 * @out_cap bytes, NUL-terminated, appended after its current text), starting
 * at a line boundary, under a "== <label> ==" heading. Missing files add
 * nothing. Returns the new length of @out. */
size_t onion_report_append_tail(char *out, size_t out_cap, const char *label,
                                const char *path, size_t max_bytes);

#ifdef __cplusplus
}
#endif
