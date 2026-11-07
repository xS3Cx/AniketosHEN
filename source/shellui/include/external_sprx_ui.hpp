#pragma once

#include "ps5_settings_ui.hpp"

#include <string>
#include <string_view>

/* Toolbox > Game Tools > Game plugins: the installed games, and for each one
   its plugin folders' switches (onion/plugin_folder.hpp), written by ShellUI
   straight into <title>/config.ini. catalog.ini / plugins.ini entries are
   shown read-only. */
namespace onion::shellui::external_sprx {

struct DispatchResult {
  bool owned = false;
  bool success = false;
  std::string id; /* the title */
};

bool append_inventory(ps5ui::Page &page);
/* id: the title ID of the game page. */
void generate_config_xml(std::string &xml_buffer, std::string_view id);
/* The value of a switch of the game page on screen. */
bool control_value(std::string_view control_id, std::string &out);
DispatchResult dispatch(std::string_view control_id, std::string_view value);

} // namespace onion::shellui::external_sprx
