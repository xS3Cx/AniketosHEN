#pragma once

#include <onion/payload_catalog.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct MonoObject;
struct GamePadData;

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
/* Redraw a page in place on the next frame, then focus `focus`. */
void request_page_refresh(MonoObject *page, std::string focus);

/* Autostart page: binding, and the controller while a payload is moved. */
void bind_autostart_page(MonoObject *page);
void release_autostart_page(MonoObject *page);
/* Any settings page came in front (push or pop). */
void note_page_activated(MonoObject *page);
/* The Autostart page is the one on screen. */
bool autostart_in_front();
void request_autostart_refresh(std::string focus);
/* Opens the OPTIONS menu (delay, remove) of payload `id`, or of the focused
   row when empty, on the next frame. */
void request_autostart_options(std::string id);
bool open_autostart_options(const std::string &id);
void tick_autostart_options();
/* GamePad.GetData filter: X picks a payload up and puts it down, up/down move
   it, OPTIONS opens its menu. Returns true when it changed the pad state. */
bool filter_autostart_pad(GamePadData &data);
/* False right after X put a payload down (that press is not a new pick). */
bool autostart_pick_allowed();

/* Post-install page state. */
void request_post_install(std::string identity, std::string name);
std::string post_install_identity();
std::string post_install_name();
bool post_install_autostart_enabled();
void request_navigate_installed();
void clear_post_install_page();

void generate_center_xml(std::string &xml);
void generate_installed_xml(std::string &xml);
void generate_included_xml(std::string &xml);
void generate_autostart_xml(std::string &xml);
void generate_add_payload_xml(std::string &xml);
void generate_browse_xml(std::string &xml);
void generate_detail_xml(std::string &xml);
void generate_settings_xml(std::string &xml);
void generate_post_install_xml(std::string &xml);

} // namespace onion::shellui::payload_center
