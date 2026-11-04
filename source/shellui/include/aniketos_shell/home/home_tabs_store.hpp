#pragma once

#include "external_symbols.hpp"

#include <string>
#include <vector>

namespace HomeTabsStore {

inline constexpr int kMaxCustomTabs = 2;
inline constexpr int kContentLimitFirst = 7780;
inline constexpr int kContentLimitLast = 7781;
inline constexpr char kMenuUri[] = "OnionHEN?HomeTabsMenu";
inline constexpr char kGlyph[] = "\xEE\xBE\xAA";
inline constexpr char kDefaultGamesIcon[] = "\xEE\xBF\x8E";
inline constexpr char kDefaultMediaIcon[] = "\xEF\xA1\xB1";
inline constexpr char kDefaultCustomIcon[] = "\xEE\xBE\xAA";

enum class StockRow { Games, Media };
enum class IconText { Valid, Empty, Invalid };

struct TabView {
  int slot = 0;
  int limit = 0;
  std::string space;
  std::string id;
  std::string name;
  std::string icon;
  bool visible = true;
};

IconText ParseIconText(const std::string &text, std::string *glyph);
std::string SanitizeIcon(const std::string &icon, const char *fallback);
std::string CleanName(const std::string &typed);
bool IsTabName(const std::string &text);

void Install();
bool Enabled();
bool Allowed(const std::string &title_id);

std::vector<TabView> Tabs(bool visible_only);
std::string TitlesSql(int limit);
bool SlotUsed(int slot);
std::string SlotName(int slot);
std::string SlotIcon(int slot);
int SlotPosition(int slot);
bool SlotVisible(int slot);
bool RestoreIcons();
int FirstFreeSlot();

std::string AddedSql(StockRow row);
std::string HiddenSql(StockRow row);

void SetHome(bool script_in);
bool HandleUri(const std::string &uri, const std::string &menu_title);
void Tick();
bool ConsumeUiChange();

void RequestCreate(int slot);
void RequestRename(int slot);
void RequestIcon(int slot);
void RequestDelete(int slot);
void ToggleVisible(int slot);
void MoveSlot(int slot, int direction);

}
