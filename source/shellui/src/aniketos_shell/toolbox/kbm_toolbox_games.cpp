/* Copyright (C) 2026 AniketosHEN
 *
 * The games the Keyboard and Mouse main page lists, and their names.
 */
#include "kbm_toolbox_internal.hpp"

#include "aniketos_ui/small_file.hpp"
#include "onion_cjson.hpp"

#include <dirent.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace kbm_toolbox {

std::vector<Game> s_games;

namespace {

std::string read_param_file(const std::string &path) {
  std::string text;
  AniketosUI::ReadSmallFile(path, 256 * 1024, text);
  return text;
}

/* PS5 titles: param.json localizedParameters[defaultLanguage].titleName. */
std::string name_from_json(const std::string &text) {
  onion_cjson::Root root(text);
  if (!root)
    return {};
  cJSON *loc = onion_cjson::item(root.get(), "localizedParameters");
  const char *lang = onion_cjson::string_item(loc, "defaultLanguage", "en-US");
  return onion_cjson::string_item(onion_cjson::item(loc, lang), "titleName", "");
}

/* PS4 titles: the TITLE entry of param.sfo. */
std::string name_from_sfo(const std::string &d) {
  auto u32 = [&](size_t at) -> uint32_t {
    if (at + 4 > d.size())
      return 0;
    const auto *b = reinterpret_cast<const uint8_t *>(d.data()) + at;
    return b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24;
  };
  if (d.size() < 20 || d.compare(0, 4, std::string("\0PSF", 4)) != 0)
    return {};
  const uint32_t keys = u32(8), data = u32(12), count = u32(16);
  for (uint32_t i = 0; i < count && 20 + i * 16 + 16 <= d.size(); ++i) {
    const size_t e = 20 + i * 16;
    const size_t k = keys + (u32(e) & 0xffff);
    const size_t v = data + u32(e + 12);
    if (k >= d.size() || d.compare(k, 6, std::string("TITLE\0", 6)) != 0)
      continue;
    const uint32_t len = u32(e + 4);
    if (v + len > d.size())
      return {};
    return std::string(d.c_str() + v, strnlen(d.c_str() + v, len));
  }
  return {};
}

/* Valid UTF-8 without control characters: a homebrew title's bytes end up
   in the page XML. */
std::string clean_text(const std::string &in) {
  std::string out;
  for (size_t i = 0; i < in.size();) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3
               : (c >> 3) == 30 ? 4 : 0;
    bool ok = len && i + len <= in.size();
    for (size_t k = 1; ok && k < len; ++k)
      ok = (static_cast<unsigned char>(in[i + k]) & 0xc0) == 0x80;
    if (!ok) {
      ++i;
      continue;
    }
    if (len > 1 || (c >= 0x20 && c != 0x7f))
      out.append(in, i, len);
    i += len;
  }
  return out;
}

std::string title_name(const std::string &id) {
  std::string name = name_from_json(read_param_file("/user/appmeta/" + id + "/param.json"));
  if (name.empty())
    name = name_from_json(read_param_file("/user/app/" + id + "/sce_sys/param.json"));
  if (name.empty())
    name = name_from_sfo(read_param_file("/user/appmeta/" + id + "/param.sfo"));
  name = clean_text(name);
  return name.empty() ? id : name;
}

std::string lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

} // namespace

void scan_games() {
  s_games.clear();
  DIR *dir = opendir("/user/app");
  if (!dir)
    return;
  while (dirent *entry = readdir(dir)) {
    const std::string id = entry->d_name;
    if (id.size() == 9 && kb::IsValidTitleId(id))
      s_games.push_back({id, title_name(id)});
  }
  closedir(dir);
  std::sort(s_games.begin(), s_games.end(), [](const Game &a, const Game &b) {
    return lower(a.name) < lower(b.name);
  });
}

std::string game_name(const std::string &title) {
  if (title == "default")
    return tr("kbm.default");
  for (const Game &g : s_games)
    if (g.id == title)
      return g.name;
  return title_name(title);
}

} // namespace kbm_toolbox
