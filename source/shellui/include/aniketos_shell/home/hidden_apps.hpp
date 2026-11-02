#pragma once

#include <string>
#include <vector>

namespace HiddenApps {
inline constexpr char kMenuUri[] = "OnionHEN?HideApp";

struct Entry {
  std::string title_id;
  std::string name;
};

bool Allowed(const std::string &title_id);
bool FilterEnabled();
std::string SqlList();
void Hide(const std::string &title_id);
void Show(const std::string &title_id);
void ShowAll();
std::vector<Entry> List();
}

namespace HomeHidden = HiddenApps; // renames.tsv
