/* Start-up safety: instance lock and boot level. See <onion/boot_safety.h>. */

#include <onion/boot_safety.h>

#include <onion/fs.h>
#include <onion/log.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

enum { kStateMaxBytes = 256, kPathMax = 1024 };

static const char kPhaseStarting[] = "starting";
static const char kPhaseRunning[] = "running";
static const char kPhaseEnded[] = "ended";

const char *onion_boot_level_name(onion_boot_level level) {
  switch (level) {
  case ONION_BOOT_NORMAL:
    return "normal";
  case ONION_BOOT_SAFE:
    return "safe";
  case ONION_BOOT_MINIMAL:
    return "minimal";
  case ONION_BOOT_RECOVERY:
  default:
    return "recovery";
  }
}

/* @parent gets the directory of @path, empty for a file in the root or a bare
 * name. False when @path is empty or too long. */
static bool parent_dir(const char *path, char parent[kPathMax]) {
  const int n = snprintf(parent, kPathMax, "%s", path ? path : "");
  if (n <= 0 || n >= kPathMax)
    return false;
  char *slash = strrchr(parent, '/');
  if (slash && slash != parent)
    *slash = '\0';
  else
    parent[0] = '\0';
  return true;
}

/* The lock file's directory lives in /system_tmp, which a fresh boot does not
 * have yet. */
static bool make_parent(const char *path) {
  char parent[kPathMax];
  return parent_dir(path, parent) && (!parent[0] || mkdir_tree(parent));
}

static int open_lock(const char *path, bool create) {
  if (!path || path[0] != '/') {
    errno = EINVAL;
    return -1;
  }
  if (!create)
    return open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
  if (!make_parent(path))
    return -1;
  return open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
}

static int lock_fd(int fd, const char *path) {
  for (;;) {
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
      return fd; /* held until this process ends */
    if (errno == EINTR)
      continue;
    const int error = errno;
    close(fd);
    if (error == EWOULDBLOCK || error == EAGAIN)
      return ONION_INSTANCE_LOCK_BUSY;
    LOG_WARN("instance lock %s: flock failed (%s)", path, strerror(error));
    return ONION_INSTANCE_LOCK_UNAVAILABLE;
  }
}

/* flock() locks belong to the open file, so a second open of the same file
 * must be refused while the first holds the lock. If it is not, flock is not
 * doing its job on this filesystem and must not be relied on. */
static bool flock_effective(const char *path) {
  const int probe = open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
  if (probe < 0)
    return true; /* cannot tell; the lock itself succeeded */
  const bool refused = flock(probe, LOCK_EX | LOCK_NB) != 0 &&
                       (errno == EWOULDBLOCK || errno == EAGAIN);
  close(probe); /* also drops the probe's lock if it got one */
  return refused;
}

/* Fallback when flock() cannot be trusted: a bound loopback port. The kernel
 * frees it with its owner. Only used then, so a foreign program on that port
 * cannot block normal starts. */
static int port_lock(void) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return ONION_INSTANCE_LOCK_UNAVAILABLE;
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(ONION_BOOT_LOCK_FALLBACK_PORT);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) == 0)
    return fd; /* kept open for the life of the process */
  const int error = errno;
  close(fd);
  return error == EADDRINUSE ? ONION_INSTANCE_LOCK_BUSY
                             : ONION_INSTANCE_LOCK_UNAVAILABLE;
}

int onion_instance_lock_acquire(const char *path) {
  const int fd = open_lock(path, true);
  if (fd < 0) {
    LOG_WARN("instance lock %s: open failed (%s); trying the port lock",
             path ? path : "(null)", strerror(errno));
    return port_lock();
  }
  const int held = lock_fd(fd, path);
  if (held == ONION_INSTANCE_LOCK_BUSY)
    return held;
  if (held >= 0 && flock_effective(path))
    return held;
  LOG_WARN("instance lock %s: flock unusable here; using the port lock",
           path);
  const int port = port_lock();
  if (port == ONION_INSTANCE_LOCK_BUSY && held >= 0)
    close(held);
  return port == ONION_INSTANCE_LOCK_BUSY ? port : (held >= 0 ? held : port);
}

bool onion_instance_lock_busy(const char *path) {
  /* A probe creates nothing: without the file nobody holds the lock. */
  const int fd = open_lock(path, false);
  if (fd < 0)
    return false;
  const int held = lock_fd(fd, path);
  if (held == ONION_INSTANCE_LOCK_BUSY)
    return true;
  bool busy = false;
  if (held < 0 || !flock_effective(path)) {
    const int port = port_lock();
    busy = port == ONION_INSTANCE_LOCK_BUSY;
    if (port >= 0)
      close(port);
  }
  if (held >= 0)
    close(held); /* releases the probe's lock */
  return busy;
}

typedef struct BootState {
  bool present;
  int level;
  bool running;
  bool ended; /* stopped on purpose before "running" */
} BootState;

static bool parse_int_after(const char *text, const char *key, int *out) {
  const char *at = strstr(text, key);
  if (!at)
    return false;
  at += strlen(key);
  if (*at < '0' || *at > '9')
    return false;
  int value = 0;
  for (; *at >= '0' && *at <= '9' && value < 1000; ++at)
    value = value * 10 + (*at - '0');
  *out = value;
  return true;
}

static BootState read_state(const char *path) {
  BootState state = {false, ONION_BOOT_NORMAL, true, false};
  const int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0)
    return state;
  char text[kStateMaxBytes + 1];
  ssize_t length;
  do {
    length = read(fd, text, kStateMaxBytes);
  } while (length < 0 && errno == EINTR);
  close(fd);
  if (length <= 0)
    return state;
  text[length] = '\0';

  int level = 0;
  if (!parse_int_after(text, "level=", &level))
    return state; /* unreadable: treat as a clean previous start */
  state.present = true;
  state.level = level > ONION_BOOT_RECOVERY ? ONION_BOOT_RECOVERY : level;
  state.running = strstr(text, "phase=running") != NULL;
  state.ended = strstr(text, "phase=ended") != NULL;
  return state;
}

/* The rename of a state file has to survive the panic it is written for. A
 * failure here only loses that extra safety: the state is written. */
static void sync_parent(const char *path) {
  char parent[kPathMax];
  if (!parent_dir(path, parent) || !parent[0])
    return;
  const int fd = open(parent, O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    (void)fsync(fd);
    close(fd);
  }
}

static bool write_state(const char *path, int level, const char *phase) {
  char text[kStateMaxBytes];
  const int n = snprintf(text, sizeof(text), "level=%d\nphase=%s\n", level,
                         phase);
  if (n <= 0 || (size_t)n >= sizeof(text) || !make_parent(path) ||
      !write_file_atomic(path, text, (size_t)n))
    return false;
  sync_parent(path);
  return true;
}

static bool path_exists(const char *path) {
  struct stat st;
  return path && lstat(path, &st) == 0;
}

bool onion_boot_guard_begin(const char *state_path, const char *session_path,
                            OnionBootGuard *out) {
  OnionBootGuard local;
  if (!out)
    out = &local;
  memset(out, 0, sizeof(*out));
  if (!state_path || !session_path)
    return false;

  const BootState previous = read_state(state_path);
  const bool same_session = path_exists(session_path);
  int level = previous.level;
  if (previous.present && previous.ended) {
    /* Rest mode, power-off or restart chosen before "running": says nothing
       about a crash, nor that the level can step down. */
    out->previous_ended = true;
  } else if (previous.present && !previous.running) {
    out->previous_unfinished = true;
    out->previous_rebooted = !same_session;
    /* Only a start cut short by a restart counts against the next one. */
    if (!same_session && level < ONION_BOOT_RECOVERY)
      ++level;
  } else if (previous.present && level > ONION_BOOT_NORMAL) {
    --level; /* the previous start finished: one step back towards normal */
  }
  out->previous = (onion_boot_level)previous.level;
  out->level = (onion_boot_level)level;

  if (!same_session && make_parent(session_path)) {
    const int fd =
        open(session_path, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd >= 0)
      close(fd);
  }
  return write_state(state_path, level, kPhaseStarting);
}

bool onion_boot_guard_mark_ended(const char *state_path) {
  if (!state_path)
    return false;
  const BootState state = read_state(state_path);
  if (!state.present || state.running || state.ended)
    return true; /* nothing in progress: a finished start stays finished */
  return write_state(state_path, state.level, kPhaseEnded);
}

bool onion_boot_guard_mark_running(const char *state_path) {
  if (!state_path)
    return false;
  const BootState state = read_state(state_path);
  if (state.present && state.running)
    return true; /* nothing to change: spare the write */
  return write_state(state_path, state.level, kPhaseRunning);
}
