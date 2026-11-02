#include "aniketos_shell/games/game_options_menu.hpp"

#include "aniketos_shell/games/trophy_tools.hpp"
#include "aniketos_shell/home/hidden_apps.hpp"
#include "aniketos_shell/home/home_bookmark.hpp"
#include "hooked_funcs.hpp"
#include "toolbox_i18n.hpp"

#include <onion/log.h>

#include <cctype>
#include <chrono>
#include <cstring>

namespace GameOptionsMenu {
namespace {
constexpr auto kMenuGap = std::chrono::milliseconds(500);
bool s_block_added = false;
std::chrono::steady_clock::time_point s_last_entry;

bool is_anchor(const std::string &id) {
  return id == "MENU_ID_APPLICATION_DELETE" || id == "MENU_ID_CHECK_PATCH";
}

/* The title an Options menu entry is about, read from its URL: a
   "titleId=" parameter (any case, "npTitleId=" too) or the first of the
   "titleIds" list some dialogs carry, percent-encoded or not. */
std::string menu_entry_title(const std::string &url) {
  std::string text;
  for (size_t i = 0; i < url.size(); ++i) {
    if (url[i] == '%' && i + 2 < url.size() &&
        std::isxdigit(static_cast<unsigned char>(url[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(url[i + 2]))) {
      text += static_cast<char>(std::stoi(url.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      text += url[i];
    }
  }
  std::string lower = text;
  for (char &c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const char *key : {"titleid=", "\"titleids\":[\""}) {
    const size_t at = lower.find(key);
    if (at == std::string::npos)
      continue;
    const std::string id = text.substr(at + std::strlen(key), 9);
    bool clean = id.size() == 9;
    for (const char c : id)
      clean = clean && std::isalnum(static_cast<unsigned char>(c));
    if (clean)
      return id;
  }
  return std::string();
}

/* The row presses read the title from g_ui later, so an entry's title is
   kept here even when no row of ours is offered for it. */
std::string note_title(const Entry &entry) {
  const std::string title = menu_entry_title(Mono_to_String(entry.action_url));
  if (!title.empty())
    g_ui.current_menu_tid = title;
  return title;
}

void add_row(const Entry &entry, const char *id, const char *label,
             const char *uri) {
  createJson(entry.inst, entry.array, mono_string_new(Root_Domain, id),
             mono_string_new(Root_Domain, label),
             mono_string_new(Root_Domain, uri), entry.action_id, nullptr,
             entry.sub_menu, entry.enable);
}

void add_bookmark_row(const Entry &entry, const std::string &title) {
  if (!HomeBookmark::Allowed(title))
    return;
  const std::string label = std::string(HomeBookmark::kGlyph) + " " +
                            toolbox_i18n::tr("bookmark.menu_tabs");
  add_row(entry, "MENU_ID_ANIKETOS_BOOKMARK", label.c_str(),
          HomeBookmark::kMenuUri);
}
} // namespace

void OnEntry(const Entry &entry) {
  const auto now = std::chrono::steady_clock::now();
  if (now - s_last_entry > kMenuGap)
    s_block_added = false;
  s_last_entry = now;

  if (s_block_added || !is_anchor(entry.id))
    return;
  const std::string title = note_title(entry);
  add_row(entry, "MENU_ID_CHEATS", toolbox_i18n::tr("cheats.game_menu"),
          "OnionHEN?Cheats_not_open");
  add_row(entry, "MENU_ID_TROPHY_UNLOCK_ALL",
          toolbox_i18n::tr("trophy.game_menu"), TrophyTools::kUnlockAllUri);
  add_bookmark_row(entry, title);
  if (HiddenApps::FilterEnabled() && HiddenApps::Allowed(title))
    add_row(entry, "MENU_ID_ANIKETOS_HIDE",
            toolbox_i18n::tr("hideapp.game_menu"), HiddenApps::kMenuUri);
  s_block_added = true;
  LOG_INFO("[GAMEMENU] Aniketos block at %s (title %s)", entry.id.c_str(),
           title.c_str());
}

void OnEntryOptionsOff(const Entry &entry) {
  if (entry.id == "MENU_ID_APPLICATION_DELETE")
    add_bookmark_row(entry, note_title(entry));
}

} // namespace GameOptionsMenu
