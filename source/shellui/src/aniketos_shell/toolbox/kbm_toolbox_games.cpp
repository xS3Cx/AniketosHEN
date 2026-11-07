/* Copyright (C) 2026 AniketosHEN
 *
 * The games the Keyboard and Mouse main page lists, and their names.
 */
#include "kbm_toolbox_internal.hpp"

#include "aniketos_shell/games/game_title.hpp"

#include "aniketos_ui/small_file.hpp"
#include "onion_cjson.hpp"

#include <dirent.h>

#include <algorithm>
#include <cctype>

namespace kbm_toolbox {

std::vector<Game> s_games;

namespace {

namespace GameTitle = AniketosShell::GameTitle;

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

/* PS4 titles keep param.sfo in the system's appmeta. */
std::string ps4_sfo(const std::string &id) {
  std::string sfo = read_param_file("/user/appmeta/" + id + "/param.sfo");
  return sfo.empty() ? read_param_file("/system_data/priv/appmeta/" + id + "/param.sfo")
                     : sfo;
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
    name = GameTitle::SfoString(ps4_sfo(id), "TITLE");
  name = clean_text(name);
  return name.empty() ? id : name;
}

std::string lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

} // namespace

bool is_game(const std::string &id) {
  std::string json = read_param_file("/user/appmeta/" + id + "/param.json");
  if (json.empty())
    json = read_param_file("/user/app/" + id + "/sce_sys/param.json");
  return GameTitle::IsGame(id, json, ps4_sfo(id));
}

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
