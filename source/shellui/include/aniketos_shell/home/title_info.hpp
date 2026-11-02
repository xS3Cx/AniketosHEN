#pragma once

#include "aniketos_ui/small_file.hpp"
#include "toolbox_i18n.hpp"

#include <cctype>
#include <string>
#include <vector>

/* An installed title as the Home features see it: its id, and the param.json
 * behind it. */
namespace AniketosShell::TitleInfo {

inline bool Valid(const std::string &id) {
  if (id.size() != 9)
    return false;
  for (const char c : id)
    if (!std::isalnum(static_cast<unsigned char>(c)))
      return false;
  return true;
}

/* A title of the user's, not one of the system's own tiles. */
inline bool Usable(const std::string &id) {
  return Valid(id) && id.compare(0, 4, "NPXS") != 0;
}

/* Empty when the title has no readable name. */
inline std::string ReadName(const std::string &id) {
  std::string json;
  if (!AniketosUI::ReadSmallFile("/user/app/" + id + "/sce_sys/param.json",
                                 64 * 1024, json))
    return {};
  size_t at = json.find("\"titleName\"");
  if (at == std::string::npos)
    return {};
  at = json.find(':', at);
  at = at == std::string::npos ? at : json.find('"', at);
  if (at == std::string::npos)
    return {};
  const size_t end = json.find('"', at + 1);
  return end == std::string::npos ? std::string()
                                  : json.substr(at + 1, end - at - 1);
}

/* A text of the catalogue with a name put in. */
inline std::string WithName(const char *key, const std::string &name) {
  const std::string text = toolbox_i18n::tr(key);
  const size_t at = text.find("%s");
  return at == std::string::npos
             ? text
             : text.substr(0, at) + name + text.substr(at + 2);
}

/* "'ID','ID'" for an SQL list, empty without a valid id: only letters and
 * digits may reach a literal. */
inline std::string SqlList(const std::vector<std::string> &ids) {
  std::string out;
  for (const std::string &id : ids) {
    if (!Valid(id))
      continue;
    if (!out.empty())
      out += ",";
    out += "'" + id + "'";
  }
  return out;
}

} // namespace AniketosShell::TitleInfo
