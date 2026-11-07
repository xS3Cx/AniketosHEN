/* Copyright (C) 2026 OnionHEN / LightningMods */

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace onion::sprx {

struct SprxManifestEntry {
  std::string id;
  std::string path;
  std::vector<std::string> exact_title_ids;
  std::vector<std::string> title_id_prefixes;
  bool enabled = true;
  bool auto_start = false;
  int32_t priority = 0;
  std::vector<std::string> dependencies;
  /** Empty for catalog.ini; else the GoldHEN-style plugins.ini it came from. */
  std::string source;
};

struct SprxCatalogIssue {
  size_t line = 0;
  std::string plugin_id;
  std::string message;
};

struct SprxOperationResult {
  bool success = false;
  std::string error;

  explicit operator bool() const { return success; }
};

struct SprxInventoryEntry {
  std::string id;
  std::string path;
  bool enabled = true;
  bool auto_start = false;
  int32_t priority = 0;
  bool matches_current_target = false;
  bool loaded_for_current_target = false;
  std::string source;
  std::vector<std::string> exact_title_ids;
  std::vector<std::string> title_id_prefixes;
  /** Last load failure in a game this session; empty after a success. */
  std::string last_error;
};

/**
 * Strict, dependency-aware catalog for externally installed SPRX/PRX files.
 *
 * Manifest format:
 *
 *   [plugin.fps-overlay]
 *   path=/data/AniketosHEN/sprx/fps-overlay.sprx
 *   exact_title_ids=CUSA12345,CUSA67890
 *   title_id_prefixes=PPSA
 *   auto_start=true
 *   priority=100
 *   dependencies=common-runtime
 */
class SprxCatalog final {
public:
  bool parse(std::string_view text,
             std::vector<SprxCatalogIssue> *issues = nullptr);
  bool load_file(std::string_view path,
                 std::vector<SprxCatalogIssue> *issues = nullptr);

  const std::vector<SprxManifestEntry> &entries() const noexcept {
    return entries_;
  }

  const SprxManifestEntry *find(std::string_view id) const noexcept;

  /**
   * Appends the entries of a GoldHEN-style plugins.ini (read-only import):
   *
   *   [default]            every PS4 game (CUSA title IDs)
   *   /data/GoldHEN/plugins/game_patch.prx
   *   [CUSA00001]          one title (PS4 CUSA or PS5 PPSA)
   *   /data/GoldHEN/plugins/afr.prx=true
   *
   * Lenient like GoldHEN: bad lines are reported and skipped. One entry per
   * path, file order is load order, paths already in the catalog are skipped.
   * Imported entries are never serialized; their ids start with the source's
   * folder name ("goldhen-afr"). Returns the number of entries added.
   */
  size_t import_plugins_ini(std::string_view text, std::string_view source,
                            std::vector<SprxCatalogIssue> *issues = nullptr);

  bool set_enabled(std::string_view id, bool enabled) noexcept;
  bool remove(std::string_view id) noexcept;
  /** Replaces the entry with the same ID, or appends it. */
  void put(SprxManifestEntry entry);
  std::string serialize() const;

  bool matches(const SprxManifestEntry &entry,
               std::string_view title_id) const noexcept;

  /**
   * Returns deterministic startup order for a Title ID. Dependencies are
   * included even when they are not marked auto_start. An empty result means
   * no matching auto-start entries or a dependency/cycle validation failure.
   */
  std::vector<const SprxManifestEntry *>
  startup_order(std::string_view title_id,
                std::vector<SprxCatalogIssue> *issues = nullptr) const;

private:
  std::vector<SprxManifestEntry> entries_;
};

/**
 * Catalog ID for a module file dropped in the plugins folder: its name
 * without the .sprx/.prx suffix, other characters than the ID's turned into
 * '_', cut to 31. Empty for any other file.
 */
std::string plugin_id_for_file(std::string_view file_name);

/** Serialized catalog persistence. Mutations are atomically written to disk. */
class SprxCatalogStore final {
public:
  /** imports: plugins.ini files read after the catalog (missing ones skipped). */
  bool load(std::string_view path,
            std::vector<SprxCatalogIssue> *issues = nullptr,
            std::vector<std::string> imports = {});
  bool reload(std::vector<SprxCatalogIssue> *issues = nullptr);

  std::vector<SprxInventoryEntry> inventory() const;
  /** Returns a consistent copy; callers never observe mutable store state. */
  SprxCatalog snapshot() const;
  SprxOperationResult set_enabled(std::string_view id, bool enabled);
  SprxOperationResult remove(std::string_view id);
  /**
   * Adds or replaces an entry; an entry without any title ID or prefix is
   * removed instead. Refused when the result would not parse back.
   */
  SprxOperationResult put(SprxManifestEntry entry);

  std::string path() const;
  bool loaded() const;

private:
  SprxOperationResult persist_locked();
  bool load_locked(std::vector<SprxCatalogIssue> *issues);

  mutable std::mutex mutex_;
  SprxCatalog catalog_;
  std::string path_;
  std::vector<std::string> imports_;
  bool loaded_ = false;
};

} // namespace onion::sprx
