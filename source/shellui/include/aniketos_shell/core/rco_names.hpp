/* Whether a PUI resource file (.rco, magic "RCOF") names a resource
 * (host-testable, no I/O). Its header gives the place of its table of names:
 * the offset at 0x10 and the size at 0x14; each name in it ends in a NUL.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RcoNames {

constexpr size_t kHeaderSize = 0x18;

inline uint32_t U32(const unsigned char *p) {
  return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

/* Where the table of names ends: the bytes from the file's start to read for
   Has(). 0 when `head` is no RCO header. */
inline size_t TableEnd(const unsigned char *head, size_t size) {
  if (size < kHeaderSize || std::memcmp(head, "RCOF", 4) != 0)
    return 0;
  return static_cast<size_t>(U32(head + 0x10)) + U32(head + 0x14);
}

/* `rco` is the file from its start, at least up to TableEnd(). */
inline bool Has(const unsigned char *rco, size_t size, const char *name) {
  const size_t end = TableEnd(rco, size);
  if (!end || end > size)
    return false;
  const unsigned char *first = rco + U32(rco + 0x10);
  const unsigned char *last = rco + end;
  const auto *wanted = reinterpret_cast<const unsigned char *>(name);
  const size_t length = std::strlen(name) + 1; /* with its NUL: whole names */
  return std::search(first, last, wanted, wanted + length) != last;
}

} // namespace RcoNames
