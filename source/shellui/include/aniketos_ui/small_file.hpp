#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

namespace AniketosUI {

/* Reads at most max_bytes; a longer file comes back cut. False when it cannot
 * be opened. */
inline bool ReadSmallFile(const std::string &path, size_t max_bytes,
                          std::string &out) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return false;
  out.assign(max_bytes, '\0');
  out.resize(std::fread(out.data(), 1, out.size(), file));
  std::fclose(file);
  return true;
}

inline std::string FileName(const std::string &path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace AniketosUI
