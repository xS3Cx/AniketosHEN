/* Host tests for local error reports: identifiers never reach the file, and
 * the report directory stays bounded. */
#include "test_harness.h"

#include <onion/report.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *scrub(const char *text, const char *const *words) {
  static char out[4096];
  onion_report_sanitize(text, strlen(text), out, sizeof(out), words);
  return out;
}

static int contains(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != NULL;
}

static int test_console_identifiers_are_removed(void) {
  const char *out = scrub(
      "idps=0123456789abcdef0123456789ABCDEF psid: 00112233445566778899aabbccddeeff\n"
      "OpenPSID 0f1e2d3c4b5a69788796a5b4c3d2e1f0 seen\n"
      "serial number: MO12345678 model CFI-1216A\n",
      NULL);
  TEST_ASSERT_TRUE(!contains(out, "0123456789abcdef"));
  TEST_ASSERT_TRUE(!contains(out, "00112233445566778899"));
  TEST_ASSERT_TRUE(!contains(out, "0f1e2d3c4b5a6978"));
  TEST_ASSERT_TRUE(!contains(out, "MO12345678"));
  TEST_ASSERT_TRUE(contains(out, "model CFI-1216A")); /* not an identifier */
  return 0;
}

static int test_network_addresses_are_removed(void) {
  const char *out = scrub(
      "connect 192.168.0.100:9021 from 10.0.0.7; mac=a4:b1:c2:d3:e4:f5 "
      "alt 00-11-22-33-44-55 v6 fe80::1c2b:3a4d and 2001:db8:0:0:1:0:0:1\n"
      "fw 11.40 version 1.2.3 at 12:34:56\n",
      NULL);
  TEST_ASSERT_TRUE(!contains(out, "192.168.0.100"));
  TEST_ASSERT_TRUE(!contains(out, "10.0.0.7"));
  TEST_ASSERT_TRUE(!contains(out, "a4:b1"));
  TEST_ASSERT_TRUE(!contains(out, "00-11-22"));
  TEST_ASSERT_TRUE(!contains(out, "fe80"));
  TEST_ASSERT_TRUE(!contains(out, "2001:db8"));
  /* Firmware and version numbers and times stay readable. */
  TEST_ASSERT_TRUE(contains(out, "fw 11.40 version 1.2.3 at 12:34:56"));
  return 0;
}

static int test_accounts_and_user_names_are_removed(void) {
  const char *const words[] = {"AlexZero", "kid_player", NULL};
  const char *out = scrub(
      "Current user => AlexZero\n"
      "User Account ID => 1a2b3c4d5e6f7081\n"
      "account_id=8167423412345678901 online_id: \"Some Name\"\n"
      "path /user/home/1a2b3c4d/savedata and kid_player joined, "
      "email=a@b.example\n"
      "alexzero said hi\n",
      words);
  TEST_ASSERT_TRUE(!contains(out, "AlexZero"));
  TEST_ASSERT_TRUE(!contains(out, "alexzero"));
  TEST_ASSERT_TRUE(!contains(out, "1a2b3c4d5e6f7081"));
  TEST_ASSERT_TRUE(!contains(out, "8167423412345678901"));
  TEST_ASSERT_TRUE(!contains(out, "Some Name"));
  TEST_ASSERT_TRUE(!contains(out, "1a2b3c4d/"));
  TEST_ASSERT_TRUE(!contains(out, "kid_player"));
  TEST_ASSERT_TRUE(!contains(out, "a@b.example"));
  TEST_ASSERT_TRUE(contains(out, "/user/home/[user]/savedata"));
  return 0;
}

static int test_crash_frames_stay_readable(void) {
  const char *in =
      "signal 11 received; main AniketosHEN has crashed\n"
      "#0 0x00000008a1b2c3d4 in hook_launch+0x1c\n"
      "rip=0x0000000812345678 rsp=0x00000007ffffe000\n"
      "cannot create cheats/CUSA41097_01.05_5d40eed0.mc4 (File exists)\n";
  const char *out = scrub(in, NULL);
  TEST_ASSERT_STREQ(in, out);
  return 0;
}

static int test_output_is_bounded(void) {
  char big[2048];
  memset(big, 'z', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  char out[64];
  const size_t length =
      onion_report_sanitize(big, strlen(big), out, sizeof(out), NULL);
  TEST_ASSERT_EQ_INT(63, (int)length);
  TEST_ASSERT_EQ_INT(63, (int)strlen(out));
  TEST_ASSERT_EQ_INT(0, (int)onion_report_sanitize("x", 1, out, 0, NULL));
  return 0;
}

static int count_reports(const char *dir) {
  DIR *handle = opendir(dir);
  if (!handle)
    return -1;
  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(handle)) != NULL) {
    const size_t n = strlen(entry->d_name);
    if (n > 4 && strcmp(entry->d_name + n - 4, ".txt") == 0)
      ++count;
  }
  closedir(handle);
  return count;
}

static int test_reports_are_written_scrubbed_and_pruned(void) {
  char base[] = "/tmp/onion-report-XXXXXX";
  TEST_ASSERT_TRUE(mkdtemp(base) != NULL);
  char dir[128];
  snprintf(dir, sizeof(dir), "%s/reports", base);

  TEST_ASSERT_TRUE(onion_report_write(dir, "migration-failed", "v1.0",
                                      "from 192.168.1.20: cannot create x"));
  TEST_ASSERT_TRUE(!onion_report_write(dir, "Bad Kind!", "v1.0", "x"));
  TEST_ASSERT_TRUE(!onion_report_write("relative", "ok", "v1.0", "x"));
  TEST_ASSERT_EQ_INT(1, count_reports(dir));

  DIR *handle = opendir(dir);
  TEST_ASSERT_TRUE(handle != NULL);
  struct dirent *entry;
  char path[256] = {0};
  while ((entry = readdir(handle)) != NULL) {
    if (entry->d_name[0] != '.')
      snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
  }
  closedir(handle);
  FILE *file = fopen(path, "rb");
  TEST_ASSERT_TRUE(file != NULL);
  char text[512] = {0};
  (void)!fread(text, 1, sizeof(text) - 1, file);
  fclose(file);
  TEST_ASSERT_TRUE(strncmp(text, ONION_REPORT_MAGIC "\n", strlen(ONION_REPORT_MAGIC) + 1) == 0);
  TEST_ASSERT_TRUE(contains(text, "kind: migration-failed\n"));
  TEST_ASSERT_TRUE(contains(text, "build: v1.0\n"));
  TEST_ASSERT_TRUE(!contains(text, "192.168.1.20"));

  /* A huge body is cut to the per-report cap. */
  char *huge = malloc(64 * 1024);
  TEST_ASSERT_TRUE(huge != NULL);
  memset(huge, 'z', 64 * 1024 - 1);
  huge[64 * 1024 - 1] = '\0';
  for (unsigned i = 0; i < ONION_REPORT_MAX_FILES + 5; ++i)
    TEST_ASSERT_TRUE(onion_report_write(dir, "boot-unfinished", "v1.0", huge));
  free(huge);
  TEST_ASSERT_EQ_INT((int)ONION_REPORT_MAX_FILES, count_reports(dir));
  handle = opendir(dir);
  while ((entry = readdir(handle)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
    struct stat st;
    TEST_ASSERT_TRUE(stat(path, &st) == 0);
    TEST_ASSERT_TRUE(st.st_size <= (off_t)ONION_REPORT_MAX_BYTES);
  }
  closedir(handle);

  char command[160];
  snprintf(command, sizeof(command), "rm -rf '%s'", base);
  (void)system(command);
  return 0;
}

static int test_tail_starts_on_a_line(void) {
  char base[] = "/tmp/onion-tail-XXXXXX";
  TEST_ASSERT_TRUE(mkdtemp(base) != NULL);
  char path[128];
  snprintf(path, sizeof(path), "%s/log.txt", base);
  FILE *file = fopen(path, "wb");
  TEST_ASSERT_TRUE(file != NULL);
  fputs("first line\nsecond line\nthird line\n", file);
  fclose(file);

  char out[256] = "head\n";
  onion_report_append_tail(out, sizeof(out), "log", path, 16);
  TEST_ASSERT_STREQ("head\n== log ==\nthird line\n", out);
  /* Missing files add nothing. */
  const size_t before = strlen(out);
  TEST_ASSERT_EQ_INT((int)before, (int)onion_report_append_tail(
                                      out, sizeof(out), "x", "/nonexistent", 64));
  unlink(path);
  rmdir(base);
  return 0;
}

int test_report_suite(void) {
  int failures = 0;
  failures += onion_test_run("report_console_identifiers",
                             test_console_identifiers_are_removed);
  failures += onion_test_run("report_network_addresses",
                             test_network_addresses_are_removed);
  failures += onion_test_run("report_accounts_and_users",
                             test_accounts_and_user_names_are_removed);
  failures += onion_test_run("report_crash_frames_readable",
                             test_crash_frames_stay_readable);
  failures += onion_test_run("report_output_bounded", test_output_is_bounded);
  failures += onion_test_run("report_write_and_prune",
                             test_reports_are_written_scrubbed_and_pruned);
  failures += onion_test_run("report_tail", test_tail_starts_on_a_line);
  return failures;
}
