#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

/* Whether an installed title is a game, from what the title says of itself:
 * PS5 games are PPSA titles whose param.json has applicationCategoryType 0
 * (YouTube and other apps say 65536); PS4 games are CUSA titles whose
 * param.sfo CATEGORY is gd/gp (gdo/gpo: PS2 classics), apps say gde, gdk...
 * System apps, media apps and homebrew use other title ids. A file nobody
 * could read leaves the id to decide. */
namespace AniketosShell::GameTitle {

/* The string entry `key` of a param.sfo, empty without it. */
inline std::string SfoString(const std::string &d, const char *key) {
  const auto u32 = [&](size_t at) -> uint32_t {
    if (at + 4 > d.size())
      return 0;
    const auto *b = reinterpret_cast<const uint8_t *>(d.data()) + at;
    return b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24;
  };
  if (d.size() < 20 || d.compare(0, 4, std::string("\0PSF", 4)) != 0)
    return {};
  const std::string wanted(key, std::strlen(key) + 1);
  const uint32_t keys = u32(8), data = u32(12), count = u32(16);
  for (uint32_t i = 0; i < count && 20 + i * 16 + 16 <= d.size(); ++i) {
    const size_t e = 20 + i * 16;
    const size_t k = keys + (u32(e) & 0xffff);
    const size_t v = data + u32(e + 12);
    if (k >= d.size() || d.compare(k, wanted.size(), wanted) != 0)
      continue;
    const uint32_t len = u32(e + 4);
    if (v + len > d.size())
      return {};
    return std::string(d.c_str() + v, strnlen(d.c_str() + v, len));
  }
  return {};
}

/* applicationCategoryType of a param.json; -1 without it. */
inline long JsonCategoryType(const std::string &json) {
  size_t at = json.find("\"applicationCategoryType\"");
  if (at == std::string::npos)
    return -1;
  at = json.find(':', at);
  if (at == std::string::npos)
    return -1;
  const char *start = json.c_str() + at + 1;
  char *end = nullptr;
  const long value = std::strtol(start, &end, 10);
  return end == start ? -1 : value;
}

inline bool IsGame(const std::string &id, const std::string &param_json,
                   const std::string &param_sfo) {
  if (id.compare(0, 4, "PPSA") == 0) {
    const long type = JsonCategoryType(param_json);
    return type == -1 || type == 0;
  }
  if (id.compare(0, 4, "CUSA") == 0) {
    const std::string category = SfoString(param_sfo, "CATEGORY");
    return category.empty() || category == "gd" || category == "gp" ||
           category == "gdo" || category == "gpo";
  }
  return false;
}

} // namespace AniketosShell::GameTitle
