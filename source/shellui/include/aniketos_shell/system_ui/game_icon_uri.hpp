/* Which game a picture ShellUI loads is the icon of, and whether a custom icon
 * may play (host-testable, no Mono/PS5 I/O). */
#pragma once

#include "aniketos_shell/system_ui/avatar_image.hpp"

#include <cctype>
#include <cstdint>
#include <string>

namespace GameIconUri {

/* One icon decoded whole: 256x256 for 64 frames. Several play at once, so far
   below the avatar's limits. */
constexpr uint64_t kMaxDecodedBytes = 16 * 1024 * 1024;

/* "PPSA01234", "CUSA00001", "NPXS40000": four capitals and five digits. */
inline bool IsTitleId(const std::string &text) {
  if (text.size() != 9)
    return false;
  for (size_t i = 0; i < 9; ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (i < 4 ? !(ch >= 'A' && ch <= 'Z') : !std::isdigit(ch))
      return false;
  }
  return true;
}

/* The title of an icon0 picture: ".../appmeta/PPSA01234/icon0.png?ts=1" and
   ".../PPSA01234_icon0.png" give "PPSA01234"; any other picture gives "". */
inline std::string TitleId(const std::string &uri) {
  const std::string path = uri.substr(0, uri.find_first_of("?#"));
  const size_t slash = path.rfind('/');
  if (slash == std::string::npos || slash == 0)
    return {};
  std::string name = path.substr(slash + 1);
  for (char &ch : name)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (name.compare(0, 5, "icon0") == 0) {
    const size_t start = path.rfind('/', slash - 1);
    const std::string parent =
        path.substr(start == std::string::npos ? 0 : start + 1,
                    slash - (start == std::string::npos ? 0 : start + 1));
    return IsTitleId(parent) ? parent : std::string();
  }
  /* Home's tile rows point at the user's own file (home_inject_js):
     ".../icons/PPSA01234.gif", or its fitted copy ".../PPSA01234_fit.gif". */
  if (name.size() > 10 && (name[9] == '.' || name.compare(9, 5, "_fit.") == 0)) {
    std::string stem = path.substr(slash + 1, 9);
    for (char &ch : stem)
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const size_t start = path.rfind('/', slash - 1);
    const std::string parent = path.substr(start + 1, slash - start - 1);
    if ((parent == "icons" || parent == "cache") && IsTitleId(stem))
      return stem;
  }
  const std::string own = path.substr(slash + 1, 9);
  return name.size() >= 15 && name.compare(9, 6, "_icon0") == 0 &&
                 IsTitleId(own)
             ? own
             : std::string();
}

/* Whether a custom icon plays: animated, within what the system decodes, and
   within kMaxDecodedBytes. Otherwise it shows still. */
inline bool Plays(const AvatarImage::Info &info) {
  return info.frames >= 2 && AvatarImage::Check(info) == AvatarImage::Problem::None &&
         static_cast<uint64_t>(info.width) * info.height * 4 * info.frames <=
             kMaxDecodedBytes;
}

} // namespace GameIconUri
