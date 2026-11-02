#pragma once

#include <string>
#include <vector>

namespace HomeHidden {
inline constexpr char kMenuUri[] = "OnionHEN?HideApp";

struct Entry {
  std::string title_id;
  std::string name;
};

bool Allowed(const std::string &title_id);
bool FilterEnabled();
std::string SqlList();
bool IsHidden(const std::string &title_id);
void Hide(const std::string &title_id);
void Show(const std::string &title_id);
void ShowAll();
std::vector<Entry> List();
}
