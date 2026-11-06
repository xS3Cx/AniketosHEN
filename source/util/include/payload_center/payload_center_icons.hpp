#pragma once

#include <onion/payload_catalog.hpp>

#include <string>
#include <vector>

namespace onion::payload_center {

struct IconSourceItem {
  std::string icon_key;
  std::string name;
  std::string url;
};

bool icons_refresh_due(const std::string &source_url);
bool refresh_icons(const std::string &source_url, const std::string &icons_url,
                   const std::vector<IconSourceItem> &items,
                   std::string *error);

}
