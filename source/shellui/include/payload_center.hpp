#pragma once

#include <onion/payload_catalog.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace onion::shellui::payload_center {

struct Entry {
  onion::payload_catalog::Item item;
  std::string source_id;
  std::string source_name;
  std::string source_url;
  std::string selection;
  bool installed = false;
  bool update_available = false;
  bool autostart = false;
  std::string installed_path;
  std::string identity;
  std::string installed_version;
};

void Tick();
void reload(bool force = false);
const std::vector<Entry> &entries();
std::vector<std::string> categories();
const std::vector<onion::payload_catalog::Source> &sources();
const Entry *find_entry(std::string_view selection);
std::string installed_identity(std::string_view selection);
bool entry_matches_query(const Entry &entry);
std::vector<const Entry *> visible_entries();
bool select_visible_index(std::size_t index);
bool start_install(const Entry &entry, bool replace);
bool start_update_for_path(const std::string &payload_path);
bool remove_installed(const std::string &identity, std::string name);
bool start_install_url(const std::string &url, bool replace);
bool start_refresh();
void show_progress(uint32_t task_id);
void request_add_source();
void request_install_url();
void request_search();
bool remove_source(std::string_view source_id);
void clear_cache();
std::string ftp_hint();
std::string last_refresh_text();

void generate_center_xml(std::string &xml);
void generate_autostart_xml(std::string &xml);
void generate_browse_xml(std::string &xml);
void generate_list_xml(std::string &xml);
void generate_detail_xml(std::string &xml);
void generate_settings_xml(std::string &xml);

}
