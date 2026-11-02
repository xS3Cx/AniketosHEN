/* write_file_atomic_if_changed: see <onion/fs.h>. */

#include <onion/fs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool file_holds(const char *path, const void *data, size_t size) {
  struct stat st;
  if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
      (unsigned long long)st.st_size != (unsigned long long)size)
    return false;
  const int fd = open(path, O_RDONLY);
  if (fd < 0)
    return false;
  const uint8_t *expected = (const uint8_t *)data;
  uint8_t buffer[4096];
  size_t offset = 0;
  bool same = true;
  while (same && offset < size) {
    const size_t want =
        size - offset < sizeof(buffer) ? size - offset : sizeof(buffer);
    const ssize_t got = read(fd, buffer, want);
    if (got < 0 && errno == EINTR)
      continue;
    if (got <= 0) {
      same = false;
      break;
    }
    same = memcmp(buffer, expected + offset, (size_t)got) == 0;
    offset += (size_t)got;
  }
  close(fd);
  return same && offset == size;
}

bool write_file_atomic_if_changed(const char *path, const void *data,
                                  size_t size) {
  if (!path || path[0] == '\0' || (size > 0 && !data))
    return false;
  if (file_holds(path, data, size))
    return true;
  return write_file_atomic(path, data, size);
}
