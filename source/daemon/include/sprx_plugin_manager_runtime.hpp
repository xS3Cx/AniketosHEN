#pragma once

#include <onion/distribution.h>
#include <cstdint>
#include <string_view>
#include <sys/types.h>
#include <vector>

#include <onion/sprx_catalog.hpp>

namespace onion::daemon::sprx_plugins {

inline constexpr const char *kCatalogPath =
    ONION_DIST_DATA_ROOT "/sprx/catalog.ini";
/* GoldHEN-style plugins.ini files, merged read-only after the catalog. */
inline constexpr const char *kPluginsIniPath =
    ONION_DIST_DATA_ROOT "/plugins.ini";
inline constexpr const char *kGoldHenPluginsIniPath =
    "/data/GoldHEN/plugins.ini";

void start();
void reconcile();
std::vector<onion::sprx::SprxInventoryEntry> inventory();
onion::sprx::SprxOperationResult set_enabled(std::string_view id,
                                              bool enabled);
/** Removes only the manifest entry; it never deletes the module file. */
onion::sprx::SprxOperationResult remove(std::string_view id);
void on_big_app_started(pid_t pid, uint32_t app_id,
                        std::string_view title_id);
void on_big_app_exited(pid_t pid, uint32_t app_id,
                       std::string_view title_id);
void stop();

} // namespace onion::daemon::sprx_plugins
