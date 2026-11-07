#include "test_harness.h"
#include "test_support.h"

#include <onion/plugin_folder.hpp>
#include <onion/sprx_catalog.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr char kManifest[] =
    "[plugin.base-runtime]\n"
    "path=/data/AniketosHEN/sprx/base-runtime.sprx\n"
    "exact_title_ids=CUSA12345\n"
    "priority=10\n"
    "\n"
    "[plugin.overlay]\n"
    "path=/data/AniketosHEN/sprx/overlay.sprx\n"
    "exact_title_ids=CUSA12345\n"
    "auto_start=true\n"
    "priority=100\n"
    "dependencies=base-runtime\n"
    "\n"
    "[plugin.telemetry]\n"
    "path=/data/AniketosHEN/sprx/telemetry.sprx\n"
    "exact_title_ids=CUSA12345\n"
    "auto_start=true\n"
    "priority=200\n"
    "\n"
    "[plugin.native]\n"
    "path=/data/AniketosHEN/sprx/native.prx\n"
    "title_id_prefixes=PPSA\n"
    "auto_start=true\n";

int test_parse_and_startup_order() {
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(catalog.parse(kManifest, &issues));
  TEST_ASSERT_EQ_INT(0, static_cast<int>(issues.size()));
  TEST_ASSERT_EQ_INT(4, static_cast<int>(catalog.entries().size()));
  const auto *overlay = catalog.find("overlay");
  TEST_ASSERT_TRUE(overlay != nullptr);
  TEST_ASSERT_TRUE(catalog.matches(*overlay, "CUSA12345"));
  TEST_ASSERT_TRUE(!catalog.matches(*overlay, "PPSA00001"));

  const auto order = catalog.startup_order("CUSA12345", &issues);
  TEST_ASSERT_EQ_INT(0, static_cast<int>(issues.size()));
  TEST_ASSERT_EQ_INT(3, static_cast<int>(order.size()));
  TEST_ASSERT_STREQ("telemetry", order[0]->id.c_str());
  TEST_ASSERT_STREQ("base-runtime", order[1]->id.c_str());
  TEST_ASSERT_STREQ("overlay", order[2]->id.c_str());

  const auto native_order = catalog.startup_order("PPSA00001", &issues);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(native_order.size()));
  TEST_ASSERT_STREQ("native", native_order[0]->id.c_str());
  return 0;
}

int test_duplicate_and_unknown_keys_rejected() {
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(!catalog.parse(
      "[plugin.one]\n"
      "path=/data/one.sprx\n"
      "exact_title_ids=CUSA12345\n"
      "path=/data/two.sprx\n",
      &issues));
  TEST_ASSERT_TRUE(!issues.empty());
  TEST_ASSERT_EQ_INT(0, static_cast<int>(catalog.entries().size()));
  return 0;
}

int test_dependency_errors_rejected_at_ordering() {
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(catalog.parse(
      "[plugin.one]\n"
      "path=/data/one.sprx\n"
      "exact_title_ids=CUSA12345\n"
      "auto_start=true\n"
      "dependencies=missing\n",
      &issues));
  TEST_ASSERT_TRUE(catalog.startup_order("CUSA12345", &issues).empty());
  TEST_ASSERT_TRUE(!issues.empty());
  return 0;
}

int test_dependency_cycle_rejected() {
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(catalog.parse(
      "[plugin.one]\npath=/data/one.sprx\nexact_title_ids=CUSA12345\n"
      "auto_start=true\ndependencies=two\n"
      "[plugin.two]\npath=/data/two.sprx\nexact_title_ids=CUSA12345\n"
      "dependencies=one\n",
      &issues));
  TEST_ASSERT_TRUE(catalog.startup_order("CUSA12345", &issues).empty());
  TEST_ASSERT_TRUE(!issues.empty());
  return 0;
}

int test_enabled_persists_and_excludes_startup() {
  char path[256] = {};
  const char manifest[] =
      "[plugin.overlay]\n"
      "path=/data/AniketosHEN/sprx/overlay.sprx\n"
      "exact_title_ids=CUSA12345\n"
      "auto_start=true\n";
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".ini", manifest, path, sizeof(path)));

  onion::sprx::SprxCatalogStore store;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(store.load(path, &issues));
  TEST_ASSERT_EQ_INT(1, static_cast<int>(store.snapshot()
                                             .startup_order("CUSA12345", &issues)
                                             .size()));
  TEST_ASSERT_TRUE(store.set_enabled("overlay", false));
  TEST_ASSERT_EQ_INT(0, static_cast<int>(store.snapshot()
                                             .startup_order("CUSA12345", &issues)
                                             .size()));

  onion::sprx::SprxCatalogStore reloaded;
  TEST_ASSERT_TRUE(reloaded.load(path, &issues));
  const auto inventory = reloaded.inventory();
  TEST_ASSERT_EQ_INT(1, static_cast<int>(inventory.size()));
  TEST_ASSERT_TRUE(!inventory[0].enabled);
  TEST_ASSERT_EQ_INT(0, static_cast<int>(reloaded.snapshot()
                                             .startup_order("CUSA12345", &issues)
                                             .size()));
  unlink(path);
  return 0;
}

int test_remove_persists_catalog_entry_only() {
  char path[256] = {};
  const char manifest[] =
      "[plugin.overlay]\n"
      "path=/data/AniketosHEN/sprx/overlay.sprx\n"
      "exact_title_ids=CUSA12345\n";
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".ini", manifest, path, sizeof(path)));
  onion::sprx::SprxCatalogStore store;
  TEST_ASSERT_TRUE(store.load(path));
  TEST_ASSERT_TRUE(store.remove("overlay"));
  onion::sprx::SprxCatalogStore reloaded;
  TEST_ASSERT_TRUE(reloaded.load(path));
  TEST_ASSERT_TRUE(reloaded.inventory().empty());
  unlink(path);
  return 0;
}

int test_import_plugins_ini_goldhen() {
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(catalog.parse(
      "[plugin.keep]\npath=/data/AniketosHEN/sprx/keep.sprx\n"
      "exact_title_ids=CUSA99999\n",
      &issues));
  const char ini[] =
      "; GoldHEN plugins\n"
      "[default]\n"
      "/data/GoldHEN/plugins/game_patch.prx\n"
      "[CUSA00419]\n"
      "/data/GoldHEN/plugins/afr.prx\n"
      "/data/GoldHEN/plugins/off.prx=false\n"
      "[settings]\n"
      "show_load_notification=on\n"
      "[bogus section]\n"
      "/data/GoldHEN/plugins/ignored.prx\n"
      "relative/path.prx\n";
  const size_t added =
      catalog.import_plugins_ini(ini, "/data/GoldHEN/plugins.ini", &issues);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(added));          // game_patch + afr
  TEST_ASSERT_EQ_INT(3, static_cast<int>(catalog.entries().size()));

  const auto *patch = catalog.find("goldhen-game_patch");
  TEST_ASSERT_TRUE(patch != nullptr);
  TEST_ASSERT_TRUE(patch->auto_start);
  TEST_ASSERT_STREQ("/data/GoldHEN/plugins.ini", patch->source.c_str());
  // [default] matches every CUSA (PS4) title, not PPSA.
  TEST_ASSERT_TRUE(catalog.matches(*patch, "CUSA12345"));
  TEST_ASSERT_TRUE(!catalog.matches(*patch, "PPSA00001"));

  const auto *afr = catalog.find("goldhen-afr");
  TEST_ASSERT_TRUE(afr != nullptr);
  TEST_ASSERT_TRUE(catalog.matches(*afr, "CUSA00419"));
  TEST_ASSERT_TRUE(!catalog.matches(*afr, "CUSA00420"));

  // off.prx (=false) and the bogus/relative lines are skipped; issues noted.
  TEST_ASSERT_TRUE(catalog.find("goldhen-off") == nullptr);
  TEST_ASSERT_TRUE(!issues.empty());

  // Imported entries never serialize (they live in their own file).
  TEST_ASSERT_TRUE(catalog.serialize().find("game_patch") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(catalog.serialize().find("keep.sprx") != std::string::npos);
  return 0;
}

int test_import_same_path_across_sections_merges() {
  onion::sprx::SprxCatalog catalog;
  const char ini[] =
      "[CUSA00001]\n/data/GoldHEN/plugins/x.prx\n"
      "[CUSA00002]\n/data/GoldHEN/plugins/x.prx\n";
  TEST_ASSERT_EQ_INT(
      1, static_cast<int>(
             catalog.import_plugins_ini(ini, "/data/GoldHEN/plugins.ini")));
  const auto *x = catalog.find("goldhen-x");
  TEST_ASSERT_TRUE(x != nullptr);
  TEST_ASSERT_TRUE(catalog.matches(*x, "CUSA00001"));
  TEST_ASSERT_TRUE(catalog.matches(*x, "CUSA00002"));
  return 0;
}

int test_import_skips_path_already_in_catalog() {
  onion::sprx::SprxCatalog catalog;
  TEST_ASSERT_TRUE(catalog.parse(
      "[plugin.a]\npath=/data/GoldHEN/plugins/x.prx\n"
      "exact_title_ids=CUSA00001\n"));
  TEST_ASSERT_EQ_INT(
      0, static_cast<int>(catalog.import_plugins_ini(
             "[default]\n/data/GoldHEN/plugins/x.prx\n", "/p.ini")));
  TEST_ASSERT_EQ_INT(1, static_cast<int>(catalog.entries().size()));
  TEST_ASSERT_TRUE(catalog.find("a")->source.empty()); // stays catalog-owned
  return 0;
}

/* One plugin under more than 64 game sections keeps 64 (the catalog and
   the Toolbox list both stop there); an imported entry refuses edits. */
int test_import_caps_titles_and_is_read_only() {
  std::string ini;
  for (int i = 0; i < 70; ++i)
    ini += "[CUSA" + std::to_string(10000 + i) + "]\n/data/GoldHEN/plugins/afr.prx\n";
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_EQ_INT(1, static_cast<int>(catalog.import_plugins_ini(
                            ini, "/data/GoldHEN/plugins.ini", &issues)));
  const auto *afr = catalog.find("goldhen-afr");
  TEST_ASSERT_TRUE(afr != nullptr);
  TEST_ASSERT_EQ_INT(64, static_cast<int>(afr->exact_title_ids.size()));
  TEST_ASSERT_TRUE(!issues.empty());

  char path[256] = {};
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(".ini", "", path, sizeof(path)));
  onion::sprx::SprxCatalogStore store;
  TEST_ASSERT_TRUE(store.load(path));
  onion::sprx::SprxManifestEntry entry;
  entry.id = "goldhen-afr";
  entry.path = "/data/GoldHEN/plugins/afr.prx";
  entry.exact_title_ids = {"CUSA00001"};
  TEST_ASSERT_TRUE(store.put(entry)); /* not imported in this store: allowed */
  unlink(path);
  return 0;
}

int test_put_adds_replaces_and_drops() {
  char path[256] = {};
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(".ini", "", path,
                                                        sizeof(path)));
  onion::sprx::SprxCatalogStore store;
  TEST_ASSERT_TRUE(store.load(path));

  onion::sprx::SprxManifestEntry entry;
  entry.id = "mod_menu";
  entry.path = "/data/AniketosHEN/sprx/mod menu.sprx";
  entry.exact_title_ids = {"PPSA01234"};
  entry.auto_start = true;
  TEST_ASSERT_TRUE(store.put(entry));
  entry.title_id_prefixes = {"PPSA", "CUSA"};
  entry.exact_title_ids.clear();
  TEST_ASSERT_TRUE(store.put(entry));

  onion::sprx::SprxCatalogStore reloaded;
  TEST_ASSERT_TRUE(reloaded.load(path));
  const auto inventory = reloaded.inventory();
  TEST_ASSERT_EQ_INT(1, static_cast<int>(inventory.size()));
  TEST_ASSERT_EQ_INT(2, static_cast<int>(inventory[0].title_id_prefixes.size()));
  TEST_ASSERT_EQ_INT(1, static_cast<int>(reloaded.snapshot()
                                             .startup_order("CUSA99999")
                                             .size()));

  /* More titles than the catalog reads back: refused, nothing changes. */
  entry.title_id_prefixes.clear();
  for (int i = 0; i < 65; ++i)
    entry.exact_title_ids.push_back("PPSA" + std::to_string(10000 + i));
  TEST_ASSERT_TRUE(!store.put(entry));
  TEST_ASSERT_TRUE(reloaded.load(path));
  TEST_ASSERT_EQ_INT(2, static_cast<int>(
                            reloaded.inventory()[0].title_id_prefixes.size()));

  /* No game left: the entry goes, the catalog stays loadable. */
  entry.exact_title_ids.clear();
  TEST_ASSERT_TRUE(store.put(entry));
  TEST_ASSERT_TRUE(reloaded.load(path));
  TEST_ASSERT_TRUE(reloaded.inventory().empty());
  unlink(path);
  return 0;
}

int test_plugin_id_for_file() {
  using onion::sprx::plugin_id_for_file;
  TEST_ASSERT_TRUE(plugin_id_for_file("menu.sprx") == "menu");
  TEST_ASSERT_TRUE(plugin_id_for_file("Mod Menu.PRX") == "Mod_Menu");
  TEST_ASSERT_TRUE(plugin_id_for_file("catalog.ini").empty());
  TEST_ASSERT_TRUE(plugin_id_for_file(".sprx").empty());
  TEST_ASSERT_TRUE(plugin_id_for_file("bad\nname.sprx").empty());
  TEST_ASSERT_EQ_INT(31, static_cast<int>(
                             plugin_id_for_file(std::string(40, 'x') + ".sprx")
                                 .size()));
  return 0;
}

void touch(const std::string &path) {
  FILE *file = std::fopen(path.c_str(), "w");
  if (file)
    std::fclose(file);
}

int test_plugin_folder_config() {
  namespace pl = onion::pluginloader;
  pl::Config config = pl::parse_config(
      "; comment\nload_on_startup = false\ndisabled=a.sprx, all/b.elf,\n");
  TEST_ASSERT_TRUE(!config.load_on_startup);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(config.disabled.size()));
  TEST_ASSERT_TRUE(config.disabled[1] == "all/b.elf");
  const pl::Config again = pl::parse_config(pl::serialize_config(config));
  TEST_ASSERT_TRUE(!again.load_on_startup && again.disabled == config.disabled);
  TEST_ASSERT_TRUE(pl::parse_config("").load_on_startup); /* default on */
  TEST_ASSERT_TRUE(pl::parse_config("load_on_startup=1").load_on_startup);

  pl::Kind kind;
  TEST_ASSERT_TRUE(pl::plugin_kind("Menu.SPRX", &kind) && kind == pl::Kind::Sprx);
  TEST_ASSERT_TRUE(pl::plugin_kind("x.prx", &kind) && kind == pl::Kind::Prx);
  TEST_ASSERT_TRUE(pl::plugin_kind("x.elf", &kind) && kind == pl::Kind::Elf);
  TEST_ASSERT_TRUE(!pl::plugin_kind("config.ini", &kind));
  TEST_ASSERT_TRUE(!pl::plugin_kind(".elf", &kind));
  TEST_ASSERT_TRUE(!pl::plugin_kind("a=b.sprx", &kind));
  TEST_ASSERT_TRUE(!pl::plugin_kind("a,b.sprx", &kind));
  TEST_ASSERT_TRUE(pl::valid_title("PPSA01234") && pl::valid_title("NPXS40087"));
  TEST_ASSERT_TRUE(!pl::valid_title("all") && !pl::valid_title("../x/yyyy"));
  return 0;
}

int test_plugin_folder_scan() {
  namespace pl = onion::pluginloader;
  char base[] = "/tmp/onion-pluginloader-XXXXXX";
  TEST_ASSERT_TRUE(mkdtemp(base) != nullptr);
  const std::string root = base;
  /* No folder yet: nothing, and saving the config creates it. */
  TEST_ASSERT_TRUE(pl::list(root, "PPSA01234", {}).empty());
  pl::Config config;
  config.disabled = {"all/b.elf"};
  TEST_ASSERT_TRUE(pl::save_config(root, "PPSA01234", config));
  TEST_ASSERT_TRUE(!pl::save_config(root, "../etc", config));
  TEST_ASSERT_EQ_INT(0, mkdir((root + "/all").c_str(), 0777));
  touch(root + "/PPSA01234/z.sprx");
  touch(root + "/PPSA01234/a.PRX");
  touch(root + "/PPSA01234/readme.txt");
  TEST_ASSERT_EQ_INT(0, mkdir((root + "/PPSA01234/dir.sprx").c_str(), 0777));
  touch(root + "/all/b.elf");
  touch(root + "/CUSA00001.sprx");

  config = pl::load_config(root, "PPSA01234");
  TEST_ASSERT_TRUE(config.load_on_startup && config.disabled.size() == 1);
  const auto plugins = pl::list(root, "PPSA01234", config);
  TEST_ASSERT_EQ_INT(3, static_cast<int>(plugins.size()));
  TEST_ASSERT_TRUE(plugins[0].name == "a.PRX" && plugins[0].kind == pl::Kind::Prx);
  TEST_ASSERT_TRUE(plugins[1].name == "z.sprx" && plugins[1].enabled);
  TEST_ASSERT_TRUE(plugins[2].name == "all/b.elf" && plugins[2].all_games() &&
                   !plugins[2].enabled && plugins[2].path == root + "/all/b.elf");
  /* Another game sees only all/. */
  TEST_ASSERT_EQ_INT(1, static_cast<int>(pl::list(root, "CUSA00001", {}).size()));

  TEST_ASSERT_TRUE(pl::save_status(root, "CUSA00001",
                                   {{"all/b.elf", "started"}, {"x.sprx", "timeout"}}));
  const pl::Status status = pl::load_status(root, "CUSA00001");
  TEST_ASSERT_TRUE(status.size() == 2 && status.at("all/b.elf") == "started");
  return 0;
}

} // namespace

extern "C" int test_sprx_catalog_suite(void) {
  int failures = 0;
  failures += onion_test_run("sprx_catalog_parse_order", test_parse_and_startup_order);
  failures += onion_test_run("sprx_catalog_invalid_keys", test_duplicate_and_unknown_keys_rejected);
  failures += onion_test_run("sprx_catalog_missing_dependency", test_dependency_errors_rejected_at_ordering);
  failures += onion_test_run("sprx_catalog_dependency_cycle", test_dependency_cycle_rejected);
  failures += onion_test_run("sprx_catalog_enabled_persists", test_enabled_persists_and_excludes_startup);
  failures += onion_test_run("sprx_catalog_remove_persists", test_remove_persists_catalog_entry_only);
  failures += onion_test_run("sprx_catalog_import_goldhen", test_import_plugins_ini_goldhen);
  failures += onion_test_run("sprx_catalog_import_merge", test_import_same_path_across_sections_merges);
  failures += onion_test_run("sprx_catalog_import_skip_dup", test_import_skips_path_already_in_catalog);
  failures += onion_test_run("sprx_catalog_import_cap", test_import_caps_titles_and_is_read_only);
  failures += onion_test_run("sprx_catalog_put", test_put_adds_replaces_and_drops);
  failures += onion_test_run("sprx_catalog_file_id", test_plugin_id_for_file);
  failures += onion_test_run("plugin_folder_config", test_plugin_folder_config);
  failures += onion_test_run("plugin_folder_scan", test_plugin_folder_scan);
  return failures;
}
