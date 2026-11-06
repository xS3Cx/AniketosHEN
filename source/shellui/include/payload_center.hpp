#pragma once

#include <onion/payload_catalog.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct MonoObject;

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
  std::string icon_key;
};

void Tick();
void reload(bool force = false);
const std::vector<Entry> &entries();
int updates_available();
const std::vector<onion::payload_catalog::Source> &sources();
const Entry *find_entry(std::string_view selection);
const Entry *find_installed_identity(std::string_view identity);
std::string installed_identity(std::string_view selection);
bool entry_matches_query(const Entry &entry);
std::vector<const Entry *> visible_entries();
bool select_visible_index(std::size_t index);
bool start_install(const Entry &entry, bool replace);
bool start_update_for_path(const std::string &payload_path);
bool remove_installed(const std::string &identity, std::string name);
bool start_refresh();
void show_progress(uint32_t task_id);
void request_add_source();
void request_search();
bool remove_source(std::string_view source_id);
void clear_cache();
std::string last_refresh_text();

/* Installed page binding for in-place refresh. */
void bind_installed_page(MonoObject *page);
void release_installed_page(MonoObject *page);
bool refresh_installed_page();

/* Post-install page state. */
void request_post_install(std::string identity, std::string name);
std::string post_install_identity();
std::string post_install_name();
bool post_install_autostart_enabled();
void request_navigate_installed();
void clear_post_install_page();

void generate_installed_xml(std::string &xml);
void generate_autostart_xml(std::string &xml);
void generate_order_xml(std::string &xml);
void generate_browse_xml(std::string &xml);
void generate_detail_xml(std::string &xml);
void generate_settings_xml(std::string &xml);
void generate_post_install_xml(std::string &xml);

} // namespace onion::shellui::payload_center
