/* Host tests for the start-up safety module: the single-instance lock and the
 * boot level that steps up after starts cut short by a restart. */
#include "test_harness.h"

#include <onion/boot_safety.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct Paths {
  char base[64];
  char lock[128];
  char state[128];
  char session[128];
} Paths;

static int make_paths(Paths *p) {
  snprintf(p->base, sizeof(p->base), "/tmp/onion-boot-XXXXXX");
  if (!mkdtemp(p->base))
    return 0;
  snprintf(p->lock, sizeof(p->lock), "%s/tmp/bootstrap.lock", p->base);
  snprintf(p->state, sizeof(p->state), "%s/data/.boot_state", p->base);
  snprintf(p->session, sizeof(p->session), "%s/tmp/boot_session", p->base);
  return 1;
}

static void remove_paths(const Paths *p) {
  char command[160];
  snprintf(command, sizeof(command), "rm -rf '%s'", p->base);
  (void)system(command);
}

/* A console restart clears /system_tmp, here the session marker. */
static void restart(const Paths *p) { unlink(p->session); }

static int test_second_instance_is_refused(void) {
  Paths p;
  TEST_ASSERT_TRUE(make_paths(&p));
  const int held = onion_instance_lock_acquire(p.lock);
  TEST_ASSERT_TRUE(held >= 0);

  /* Another process (flock is per open file, so a child stands in for a
   * second bootstrapper) must be refused while the first holds the lock. */
  const pid_t child = fork();
  if (child == 0) {
    const int second = onion_instance_lock_acquire(p.lock);
    const int probe_busy = onion_instance_lock_busy(p.lock) ? 1 : 0;
    _exit(second == ONION_INSTANCE_LOCK_BUSY && probe_busy ? 0 : 1);
  }
  int status = 0;
  TEST_ASSERT_TRUE(waitpid(child, &status, 0) == child);
  TEST_ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  /* Released with its holder: the next start gets it. */
  close(held);
  const int again = onion_instance_lock_acquire(p.lock);
  TEST_ASSERT_TRUE(again >= 0);
  close(again);
  TEST_ASSERT_TRUE(!onion_instance_lock_busy(p.lock));
  remove_paths(&p);
  return 0;
}

static int test_lock_released_when_holder_dies(void) {
  Paths p;
  TEST_ASSERT_TRUE(make_paths(&p));
  const pid_t child = fork();
  if (child == 0) {
    (void)onion_instance_lock_acquire(p.lock);
    abort(); /* a crashed bootstrapper */
  }
  int status = 0;
  TEST_ASSERT_TRUE(waitpid(child, &status, 0) == child);
  const int fd = onion_instance_lock_acquire(p.lock);
  TEST_ASSERT_TRUE(fd >= 0);
  close(fd);
  remove_paths(&p);
  return 0;
}

static int test_unusable_lock_file_falls_back_to_the_port(void) {
  /* No lock file possible: the loopback port lock stands in (or, if that
   * port is taken on this host, the start is still not refused). */
  const int fd = onion_instance_lock_acquire("relative/lock");
  TEST_ASSERT_TRUE(fd >= 0 || fd == ONION_INSTANCE_LOCK_UNAVAILABLE ||
                   fd == ONION_INSTANCE_LOCK_BUSY);
  if (fd >= 0) {
    /* While it is held, a second start through the same path is refused. */
    TEST_ASSERT_EQ_INT(ONION_INSTANCE_LOCK_BUSY,
                       onion_instance_lock_acquire("relative/lock"));
    close(fd);
  }
  TEST_ASSERT_TRUE(!onion_instance_lock_busy(NULL));
  return 0;
}

static int test_first_start_is_normal(void) {
  Paths p;
  TEST_ASSERT_TRUE(make_paths(&p));
  OnionBootGuard guard;
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_EQ_INT(ONION_BOOT_NORMAL, guard.level);
  TEST_ASSERT_TRUE(!guard.previous_unfinished);
  struct stat st;
  TEST_ASSERT_TRUE(stat(p.session, &st) == 0);
  remove_paths(&p);
  return 0;
}

static int test_restarts_inside_starts_step_up_and_cap(void) {
  Paths p;
  TEST_ASSERT_TRUE(make_paths(&p));
  OnionBootGuard guard;
  const onion_boot_level expected[] = {ONION_BOOT_NORMAL, ONION_BOOT_SAFE,
                                       ONION_BOOT_MINIMAL, ONION_BOOT_RECOVERY,
                                       ONION_BOOT_RECOVERY};
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
    TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
    TEST_ASSERT_EQ_INT(expected[i], guard.level);
    TEST_ASSERT_EQ_INT(i > 0, guard.previous_unfinished);
    TEST_ASSERT_EQ_INT(i > 0, guard.previous_rebooted);
    restart(&p); /* kernel panic before util marked the start running */
  }
  remove_paths(&p);
  return 0;
}

static int test_finished_starts_step_back_down(void) {
  Paths p;
  TEST_ASSERT_TRUE(make_paths(&p));
  OnionBootGuard guard;
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
    restart(&p);
  }
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_EQ_INT(ONION_BOOT_RECOVERY, guard.level);

  const onion_boot_level expected[] = {ONION_BOOT_MINIMAL, ONION_BOOT_SAFE,
                                       ONION_BOOT_NORMAL, ONION_BOOT_NORMAL};
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
    TEST_ASSERT_TRUE(onion_boot_guard_mark_running(p.state));
    restart(&p); /* an ordinary power-off after a good start */
    TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
    TEST_ASSERT_TRUE(!guard.previous_unfinished);
    TEST_ASSERT_EQ_INT(expected[i], guard.level);
  }
  remove_paths(&p);
  return 0;
}

static int test_reload_in_same_session_keeps_level(void) {
  Paths p;
  TEST_ASSERT_TRUE(make_paths(&p));
  OnionBootGuard guard;
  /* Two quick reloads of the stack without a restart: a normal thing to do,
   * not a sign of a crash. */
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_TRUE(guard.previous_unfinished);
  TEST_ASSERT_TRUE(!guard.previous_rebooted);
  TEST_ASSERT_EQ_INT(ONION_BOOT_NORMAL, guard.level);
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_EQ_INT(ONION_BOOT_NORMAL, guard.level);
  remove_paths(&p);
  return 0;
}

static int test_power_action_before_running_keeps_level(void) {
  Paths p;
  TEST_ASSERT_TRUE(make_paths(&p));
  OnionBootGuard guard;
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  restart(&p); /* a crash: the next start is safe */
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_EQ_INT(ONION_BOOT_SAFE, guard.level);

  /* The user puts the console in rest mode, turns it off or restarts it
   * before util marked the start running: not a crash. */
  TEST_ASSERT_TRUE(onion_boot_guard_mark_ended(p.state));
  restart(&p);
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_TRUE(guard.previous_ended);
  TEST_ASSERT_TRUE(!guard.previous_unfinished);
  TEST_ASSERT_EQ_INT(ONION_BOOT_SAFE, guard.level);

  /* A start that then finishes steps back down as before. */
  TEST_ASSERT_TRUE(onion_boot_guard_mark_running(p.state));
  restart(&p);
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_TRUE(!guard.previous_ended);
  TEST_ASSERT_EQ_INT(ONION_BOOT_NORMAL, guard.level);
  remove_paths(&p);
  return 0;
}

static int test_power_action_after_running_changes_nothing(void) {
  Paths p;
  TEST_ASSERT_TRUE(make_paths(&p));
  /* Nothing recorded yet: nothing to mark. */
  TEST_ASSERT_TRUE(onion_boot_guard_mark_ended(p.state));
  OnionBootGuard guard;
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_TRUE(onion_boot_guard_mark_running(p.state));
  TEST_ASSERT_TRUE(onion_boot_guard_mark_ended(p.state));
  restart(&p);
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_TRUE(!guard.previous_ended);
  TEST_ASSERT_TRUE(!guard.previous_unfinished);
  TEST_ASSERT_EQ_INT(ONION_BOOT_NORMAL, guard.level);
  remove_paths(&p);
  return 0;
}

static int test_garbage_state_is_treated_as_clean(void) {
  Paths p;
  TEST_ASSERT_TRUE(make_paths(&p));
  char dir[160];
  snprintf(dir, sizeof(dir), "%s/data", p.base);
  TEST_ASSERT_TRUE(mkdir(dir, 0755) == 0);
  FILE *file = fopen(p.state, "wb");
  TEST_ASSERT_TRUE(file != NULL);
  fputs("\x01\x02garbage", file);
  fclose(file);
  OnionBootGuard guard;
  TEST_ASSERT_TRUE(onion_boot_guard_begin(p.state, p.session, &guard));
  TEST_ASSERT_EQ_INT(ONION_BOOT_NORMAL, guard.level);
  TEST_ASSERT_TRUE(!guard.previous_unfinished);
  TEST_ASSERT_STREQ("recovery", onion_boot_level_name(ONION_BOOT_RECOVERY));
  remove_paths(&p);
  return 0;
}

int test_boot_safety_suite(void) {
  int failures = 0;
  failures += onion_test_run("boot_lock_second_instance_refused",
                             test_second_instance_is_refused);
  failures += onion_test_run("boot_lock_released_on_crash",
                             test_lock_released_when_holder_dies);
  failures += onion_test_run("boot_lock_port_fallback",
                             test_unusable_lock_file_falls_back_to_the_port);
  failures += onion_test_run("boot_level_first_start", test_first_start_is_normal);
  failures += onion_test_run("boot_level_steps_up_after_restarts",
                             test_restarts_inside_starts_step_up_and_cap);
  failures += onion_test_run("boot_level_steps_down_after_good_starts",
                             test_finished_starts_step_back_down);
  failures += onion_test_run("boot_level_same_session_reload",
                             test_reload_in_same_session_keeps_level);
  failures += onion_test_run("boot_level_power_action_keeps_level",
                             test_power_action_before_running_keeps_level);
  failures += onion_test_run("boot_level_power_action_after_running",
                             test_power_action_after_running_changes_nothing);
  failures += onion_test_run("boot_level_garbage_state",
                             test_garbage_state_is_treated_as_clean);
  return failures;
}
