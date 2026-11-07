#pragma once

#include <onion/sprx_catalog.hpp>
#include <onion/sprx_loader.hpp>

#include <string>
#include <string_view>
#include <mutex>
#include <vector>

namespace onion::sprx {

struct SprxTarget {
  pid_t pid = -1;
  std::string title_id;
};

class ISprxTargetProvider {
public:
  virtual ~ISprxTargetProvider() = default;
  virtual bool current(SprxTarget *out) noexcept = 0;
};

struct SprxPluginResult {
  std::string id;
  LoadResult load;
  bool skipped_dependency = false;
};

struct SprxStartupReport {
  bool catalog_loaded = false;
  std::vector<SprxCatalogIssue> issues;
  std::vector<SprxPluginResult> results;
};

/** Coordinates catalog selection and loading for one current game target. */
class SprxPluginManager final {
public:
  SprxPluginManager(IRemoteSprxRuntime &runtime,
                    ITargetAccessPolicy &access_policy,
                    ISprxTargetProvider &target_provider) noexcept;

  /** imports: GoldHEN-style plugins.ini files merged read-only. */
  bool load_catalog(std::string_view path,
                    std::vector<SprxCatalogIssue> *issues = nullptr,
                    std::vector<std::string> imports = {});
  SprxStartupReport start();
  SprxStartupReport reconcile();
  /** Loads these modules, in this order, into the current game when it is
   *  title_id (the plugin folders; no catalog involved). */
  SprxStartupReport load(std::string_view title_id,
                         const std::vector<SprxManifestEntry> &entries);
  std::vector<SprxInventoryEntry> inventory() const;
  SprxOperationResult set_enabled(std::string_view id, bool enabled);
  SprxOperationResult remove(std::string_view id);
  SprxOperationResult put(SprxManifestEntry entry);
  void stop() noexcept;

  SprxCatalog catalog() const { return catalog_store_.snapshot(); }

private:
  SprxStartupReport start_locked();
  void load_locked(const SprxTarget &target,
                   const std::vector<const SprxManifestEntry *> &order,
                   SprxStartupReport &report);

  IRemoteSprxRuntime &runtime_;
  ITargetAccessPolicy &access_policy_;
  ISprxTargetProvider &target_provider_;
  SprxCatalogStore catalog_store_;
  mutable std::mutex mutex_;
};

} // namespace onion::sprx
