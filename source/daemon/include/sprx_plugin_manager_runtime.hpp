#pragma once

#include <onion/distribution.h>
#include <cstdint>
#include <string_view>
#include <sys/types.h>
#include <vector>

#include <onion/sprx_catalog.hpp>

namespace onion::daemon::sprx_plugins {

/* Players drop .sprx/.prx files here; the catalog sits beside them. */
inline constexpr const char *kFolder = ONION_DIST_DATA_ROOT "/sprx";
inline constexpr const char *kCatalogPath =
    ONION_DIST_DATA_ROOT "/sprx/catalog.ini";
/* GoldHEN-style plugins.ini files, merged read-only after the catalog. */
inline constexpr const char *kPluginsIniPath =
    ONION_DIST_DATA_ROOT "/plugins.ini";
inline constexpr const char *kGoldHenPluginsIniPath =
    "/data/GoldHEN/plugins.ini";

/* Plugin folders: <root>/<TITLEID>/ and <root>/all/ (onion/plugin_folder.hpp). */
inline constexpr const char *kPluginLoaderRoot =
    ONION_DIST_DATA_ROOT "/pluginloader";

void start();
void reconcile();
/** Catalog entries, then files of kFolder that no entry names yet (those
 *  have no title IDs). */
std::vector<onion::sprx::SprxInventoryEntry> inventory();
onion::sprx::SprxOperationResult set_enabled(std::string_view id,
                                              bool enabled);
/** Removes the manifest entry, and the module file when it is in kFolder. */
onion::sprx::SprxOperationResult remove(std::string_view id);
/** Loads the catalog's plugins, then the title's folder plugins (.sprx/.prx
 *  into the game, .elf started through the private elfldr). */
void on_big_app_started(pid_t pid, uint32_t app_id,
                        std::string_view title_id);
void on_big_app_exited(pid_t pid, uint32_t app_id,
                       std::string_view title_id);
void stop();

} // namespace onion::daemon::sprx_plugins
