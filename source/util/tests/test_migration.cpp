/* Host tests for the OnionHEN -> AniketosHEN data migration and the settings
 * lookup that backs it up. Every scenario runs in its own temp directory with
 * a "legacy" root standing in for /data/OnionHEN and a "target" root standing
 * in for /data/AniketosHEN. */
#include "test_harness.h"

#include <onion/distribution.h>
#include <onion/migrate.h>
#include <onion/settings.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct Sandbox {
  std::string base, legacy, target;

  Sandbox() {
    char tmpl[] = "/tmp/onion-migrate-XXXXXX";
    if (mkdtemp(tmpl)) {
      base = tmpl;
      legacy = base + "/" ONION_DIST_LEGACY_DIR_NAME;
      target = base + "/" ONION_DIST_DIR_NAME;
    }
  }
  ~Sandbox() {
    if (!base.empty()) {
      /* Restore traversal rights in case a test dropped them. */
      const std::string command =
          "chmod -R u+rwx '" + base + "' 2>/dev/null; rm -rf '" + base + "'";
      (void)std::system(command.c_str());
    }
  }
  bool ok() const { return !base.empty(); }
};

bool make_dir(const std::string &path) {
  return mkdir(path.c_str(), 0755) == 0;
}

bool write_text(const std::string &path, const std::string &text) {
  FILE *file = std::fopen(path.c_str(), "wb");
  if (!file)
    return false;
  const bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
  return std::fclose(file) == 0 && ok;
}

bool read_text(const std::string &path, std::string *out) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return false;
  out->clear();
  char buffer[4096];
  size_t n;
  while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    out->append(buffer, n);
  std::fclose(file);
  return true;
}

bool exists(const std::string &path) {
  struct stat st {};
  return lstat(path.c_str(), &st) == 0;
}

bool validate_with_settings(const char *path, void *user) {
  if (user)
    ++*static_cast<int *>(user);
  onion::Settings parsed{};
  return onion::settings_load_file(path, &parsed);
}

onion_migrate_status migrate(const Sandbox &box, OnionMigrateReport *report,
                             int *validations = nullptr) {
  /* A small reserve keeps these tests independent of the host's free space;
   * test_space_reserve_and_attempts_are_bounded covers the reserve itself. */
  const OnionMigrateOptions options = {box.legacy.c_str(), box.target.c_str(),
                                       validate_with_settings, validations,
                                       64 * 1024};
  return onion_migrate_legacy_root(&options, report);
}

/* A config as OnionHEN writes it: its own key spelling, non-default values,
 * plus content this build does not know about. */
const char kLegacyConfig[] =
    "# OnionHEN configuration\n"
    "\n"
    "[meta]\n"
    "schema_version=1\n"
    "\n"
    "[toolbox]\n"
    "language=ja\n"
    "\n"
    "[game_menu]\n"
    "# show_onionhen_options adds OnionHEN entries to the game options menu.\n"
    "# Available values: true, false\n"
    "show_onionhen_options=false\n"
    "\n"
    "[cooling]\n"
    "fan_control=temperature_threshold\n"
    "temperature_threshold_celsius=65\n"
    "\n"
    "[kstuff]\n"
    "autoload=false\n"
    "\n"
    "[future_section]\n"
    "# a key from a newer or third-party build\n"
    "unknown_key=keep me\r\n";

bool populate_legacy(const Sandbox &box) {
  return make_dir(box.legacy) && make_dir(box.legacy + "/payloads") &&
         make_dir(box.legacy + "/plugins") && make_dir(box.legacy + "/cheats") &&
         make_dir(box.legacy + "/sprx") && make_dir(box.legacy + "/assets") &&
         make_dir(box.legacy + "/cheats_tmp") &&
         write_text(box.legacy + "/config.ini", kLegacyConfig) &&
         write_text(box.legacy + "/payloads/tool.elf", "ELF-payload") &&
         write_text(box.legacy + "/payloads/tool.elf.json",
                    "{\"version\":1,\"priority\":7,\"delay_seconds\":3}") &&
         write_text(box.legacy + "/payloads/tool.elf.auto_start", "") &&
         write_text(box.legacy + "/plugins/FTPS00001.elf", "ELF-plugin") &&
         write_text(box.legacy + "/cheats/CUSA00001_01.00.json", "{}") &&
         write_text(box.legacy + "/sprx/catalog.ini", "[a]\npath=x\n") &&
         write_text(box.legacy + "/kstuff.elf", "ELF-kstuff") &&
         write_text(box.legacy + "/no_kstuff", "") &&
         /* distribution-owned: must stay behind */
         write_text(box.legacy + "/assets/icon_xml_about.png", "png") &&
         write_text(box.legacy + "/cheats_tmp/partial.zip", "zip") &&
         write_text(box.legacy + "/OnionHEN.log", "log") &&
         write_text(box.legacy + "/OnionHEN_crash.log.1", "log") &&
         write_text(box.legacy + "/onionhen.png", "png") &&
         write_text(box.legacy + "/onionhen.elf", "cache") &&
         write_text(box.legacy + "/CUSA00001_cheats_12", "{}") &&
         write_text(box.legacy + "/payloads/tool.elf.json.tmp.A1b2C3", "tmp");
}

int test_fresh_install_has_nothing_to_migrate(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());

  OnionMigrateReport report{};
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_NO_LEGACY, migrate(box, &report));
  TEST_ASSERT_EQ_INT(0, report.error);
  /* Nothing is created for a console that never ran OnionHEN. */
  TEST_ASSERT_TRUE(!exists(box.target));

  /* ... and the settings lookup reports plain defaults. */
  const std::string config = box.target + "/config.ini";
  const std::string legacy = box.legacy + "/config.ini";
  onion::SettingsPaths paths{config.c_str(), nullptr, legacy.c_str(), nullptr};
  onion::Settings loaded{};
  TEST_ASSERT_TRUE(!onion::settings_load_from(paths, &loaded));
  TEST_ASSERT_EQ_INT(onion::kUiLanguageSystem, loaded.ui_lang);
  TEST_ASSERT_TRUE(loaded.kstuff_autoload);

  /* The default config a fresh install generates loads cleanly and uses the
   * distribution's own key and name. */
  TEST_ASSERT_TRUE(make_dir(box.target));
  TEST_ASSERT_TRUE(onion::settings_save_file(config.c_str(), onion::Settings{}));
  std::string text;
  TEST_ASSERT_TRUE(read_text(config, &text));
  TEST_ASSERT_TRUE(text.find("# AniketosHEN configuration\n") == 0);
  TEST_ASSERT_TRUE(text.find("show_aniketoshen_options=true\n") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("onionhen") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("OnionHEN") == std::string::npos);
  TEST_ASSERT_TRUE(onion::settings_load_from(paths, &loaded));
  return 0;
}

int test_existing_onionhen_install_is_imported(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(populate_legacy(box));

  int validations = 0;
  OnionMigrateReport report{};
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED,
                     migrate(box, &report, &validations));
  TEST_ASSERT_EQ_INT(0, report.error);
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_CONFIG_MIGRATED, report.config);
  TEST_ASSERT_EQ_INT(1, validations);
  TEST_ASSERT_EQ_INT(8, (int)report.files_copied);
  TEST_ASSERT_EQ_INT(0, (int)report.files_present);
  TEST_ASSERT_EQ_INT(4, (int)report.directories_created);
  /* config.ini itself + assets/ + cheats_tmp/ + 2 logs + png + elf cache +
   * cheat exchange file + in-flight temp file */
  TEST_ASSERT_EQ_INT(9, (int)report.entries_skipped);

  /* User data arrived intact. */
  std::string text;
  TEST_ASSERT_TRUE(read_text(box.target + "/payloads/tool.elf", &text));
  TEST_ASSERT_STREQ("ELF-payload", text.c_str());
  TEST_ASSERT_TRUE(read_text(box.target + "/payloads/tool.elf.json", &text));
  TEST_ASSERT_STREQ("{\"version\":1,\"priority\":7,\"delay_seconds\":3}",
                    text.c_str());
  TEST_ASSERT_TRUE(exists(box.target + "/payloads/tool.elf.auto_start"));
  TEST_ASSERT_TRUE(exists(box.target + "/plugins/FTPS00001.elf"));
  TEST_ASSERT_TRUE(exists(box.target + "/cheats/CUSA00001_01.00.json"));
  TEST_ASSERT_TRUE(exists(box.target + "/sprx/catalog.ini"));
  TEST_ASSERT_TRUE(exists(box.target + "/kstuff.elf"));
  TEST_ASSERT_TRUE(exists(box.target + "/no_kstuff"));

  /* Regenerated, cached and logged files stayed behind. */
  TEST_ASSERT_TRUE(!exists(box.target + "/assets"));
  TEST_ASSERT_TRUE(!exists(box.target + "/cheats_tmp"));
  TEST_ASSERT_TRUE(!exists(box.target + "/OnionHEN.log"));
  TEST_ASSERT_TRUE(!exists(box.target + "/OnionHEN_crash.log.1"));
  TEST_ASSERT_TRUE(!exists(box.target + "/onionhen.png"));
  TEST_ASSERT_TRUE(!exists(box.target + "/onionhen.elf"));
  TEST_ASSERT_TRUE(!exists(box.target + "/CUSA00001_cheats_12"));
  TEST_ASSERT_TRUE(!exists(box.target + "/payloads/tool.elf.json.tmp.A1b2C3"));
  TEST_ASSERT_TRUE(!exists(box.target + "/config.ini.migrate.tmp"));

  /* The config was converted, keeps unknown content byte for byte, and loads
   * with the user's values. */
  TEST_ASSERT_TRUE(read_text(box.target + "/config.ini", &text));
  TEST_ASSERT_TRUE(text.find("show_aniketoshen_options=false\n") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("show_onionhen_options") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("[future_section]\n# a key from a newer or "
                             "third-party build\nunknown_key=keep me\r\n") !=
                   std::string::npos);
  onion::Settings loaded{};
  TEST_ASSERT_TRUE(
      onion::settings_load_file((box.target + "/config.ini").c_str(), &loaded));
  TEST_ASSERT_EQ_INT(onion::kUiLanguageJa, loaded.ui_lang);
  TEST_ASSERT_TRUE(!loaded.onionhen_game_opts);
  TEST_ASSERT_TRUE(loaded.enable_fan_speed);
  TEST_ASSERT_EQ_INT(65, loaded.fan_threshold);
  TEST_ASSERT_TRUE(!loaded.kstuff_autoload);

  /* The OnionHEN directory is the user's backup: nothing in it changed. */
  TEST_ASSERT_TRUE(read_text(box.legacy + "/config.ini", &text));
  TEST_ASSERT_STREQ(kLegacyConfig, text.c_str());
  TEST_ASSERT_TRUE(exists(box.legacy + "/payloads/tool.elf"));
  TEST_ASSERT_TRUE(exists(box.legacy + "/OnionHEN.log"));
  TEST_ASSERT_TRUE(exists(box.legacy + "/assets/icon_xml_about.png"));

  /* The marker and a diagnostic line were written; neither holds file data. */
  TEST_ASSERT_TRUE(
      read_text(box.target + "/" ONION_DIST_MIGRATION_MARKER, &text));
  TEST_ASSERT_TRUE(text.find("status=completed") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("config=migrated") != std::string::npos);
  TEST_ASSERT_TRUE(read_text(box.target + "/" ONION_DIST_MIGRATION_LOG, &text));
  TEST_ASSERT_TRUE(text.find("files_copied=8") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("keep me") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("ELF-") == std::string::npos);
  return 0;
}

int test_repeat_launches_do_not_migrate_again(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(populate_legacy(box));

  OnionMigrateReport report{};
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED, migrate(box, &report));

  /* The user now changes a setting in AniketosHEN and removes a payload... */
  const std::string config = box.target + "/config.ini";
  onion::Settings changed{};
  TEST_ASSERT_TRUE(onion::settings_load_file(config.c_str(), &changed));
  changed.ui_lang = onion::kUiLanguageDe;
  TEST_ASSERT_TRUE(onion::settings_save_file(config.c_str(), changed));
  TEST_ASSERT_EQ_INT(0, unlink((box.target + "/payloads/tool.elf").c_str()));
  /* ... while OnionHEN's copy changes too (e.g. it was booted once more). */
  TEST_ASSERT_TRUE(write_text(box.legacy + "/payloads/new.elf", "ELF-new"));

  std::string log_before;
  TEST_ASSERT_TRUE(
      read_text(box.target + "/" ONION_DIST_MIGRATION_LOG, &log_before));

  for (int launch = 0; launch < 3; ++launch) {
    int validations = 0;
    TEST_ASSERT_EQ_INT(ONION_MIGRATE_ALREADY_DONE,
                       migrate(box, &report, &validations));
    TEST_ASSERT_EQ_INT(0, validations);
    TEST_ASSERT_EQ_INT(0, (int)report.files_copied);
  }

  /* Preferences survived, the deleted payload was not resurrected, and
   * nothing new was pulled in behind the user's back. */
  onion::Settings loaded{};
  TEST_ASSERT_TRUE(onion::settings_load_file(config.c_str(), &loaded));
  TEST_ASSERT_EQ_INT(onion::kUiLanguageDe, loaded.ui_lang);
  TEST_ASSERT_TRUE(!exists(box.target + "/payloads/tool.elf"));
  TEST_ASSERT_TRUE(!exists(box.target + "/payloads/new.elf"));
  std::string log_after;
  TEST_ASSERT_TRUE(
      read_text(box.target + "/" ONION_DIST_MIGRATION_LOG, &log_after));
  TEST_ASSERT_STREQ(log_before.c_str(), log_after.c_str());
  return 0;
}

int test_existing_aniketoshen_data_is_never_overwritten(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(populate_legacy(box));

  /* AniketosHEN was already in use (no marker: e.g. OnionHEN was installed
   * afterwards, or the first migration attempt was interrupted). */
  onion::Settings current{};
  current.ui_lang = onion::kUiLanguageFr;
  current.fan_threshold = 80;
  TEST_ASSERT_TRUE(make_dir(box.target));
  TEST_ASSERT_TRUE(make_dir(box.target + "/payloads"));
  TEST_ASSERT_TRUE(onion::settings_save_file(
      (box.target + "/config.ini").c_str(), current));
  TEST_ASSERT_TRUE(write_text(box.target + "/payloads/tool.elf", "ELF-newer"));
  std::string config_before;
  TEST_ASSERT_TRUE(read_text(box.target + "/config.ini", &config_before));

  int validations = 0;
  OnionMigrateReport report{};
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED,
                     migrate(box, &report, &validations));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_CONFIG_KEPT_EXISTING, report.config);
  TEST_ASSERT_EQ_INT(0, validations);
  TEST_ASSERT_EQ_INT(1, (int)report.files_present);
  TEST_ASSERT_EQ_INT(7, (int)report.files_copied);

  std::string text;
  TEST_ASSERT_TRUE(read_text(box.target + "/config.ini", &text));
  TEST_ASSERT_STREQ(config_before.c_str(), text.c_str());
  TEST_ASSERT_TRUE(read_text(box.target + "/payloads/tool.elf", &text));
  TEST_ASSERT_STREQ("ELF-newer", text.c_str());
  /* Files AniketosHEN did not have yet were still brought across. */
  TEST_ASSERT_TRUE(exists(box.target + "/plugins/FTPS00001.elf"));

  /* And the loader keeps preferring AniketosHEN's own file. */
  const std::string config = box.target + "/config.ini";
  const std::string legacy = box.legacy + "/config.ini";
  onion::SettingsPaths paths{config.c_str(), nullptr, legacy.c_str(), nullptr};
  onion::Settings loaded{};
  TEST_ASSERT_TRUE(onion::settings_load_from(paths, &loaded));
  TEST_ASSERT_EQ_INT(onion::kUiLanguageFr, loaded.ui_lang);
  TEST_ASSERT_EQ_INT(80, loaded.fan_threshold);
  return 0;
}

int test_malformed_legacy_config_is_not_installed(void) {
  static const char *const kBroken[] = {
      "",                                  /* empty */
      "this is not an ini file\n",         /* no schema */
      "[meta]\nschema_version=99\n",       /* schema this build cannot read */
      "[meta\nschema_version=1",           /* truncated mid-write */
  };
  for (const char *broken : kBroken) {
    Sandbox box;
    TEST_ASSERT_TRUE(box.ok());
    TEST_ASSERT_TRUE(make_dir(box.legacy));
    TEST_ASSERT_TRUE(make_dir(box.legacy + "/payloads"));
    TEST_ASSERT_TRUE(write_text(box.legacy + "/config.ini", broken));
    TEST_ASSERT_TRUE(write_text(box.legacy + "/payloads/tool.elf", "ELF"));

    OnionMigrateReport report{};
    /* A config that can never load is not worth retrying on every boot: the
     * rest of the data is imported and the run counts as complete. */
    TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED, migrate(box, &report));
    TEST_ASSERT_EQ_INT(ONION_MIGRATE_CONFIG_REJECTED, report.config);
    TEST_ASSERT_TRUE(!exists(box.target + "/config.ini"));
    TEST_ASSERT_TRUE(!exists(box.target + "/config.ini.migrate.tmp"));
    TEST_ASSERT_TRUE(exists(box.target + "/payloads/tool.elf"));

    /* The original is still there for the user to repair or inspect. */
    std::string text;
    TEST_ASSERT_TRUE(read_text(box.legacy + "/config.ini", &text));
    TEST_ASSERT_STREQ(broken, text.c_str());

    /* AniketosHEN starts on defaults instead of failing. */
    const std::string config = box.target + "/config.ini";
    const std::string legacy = box.legacy + "/config.ini";
    onion::SettingsPaths paths{config.c_str(), nullptr, legacy.c_str(),
                               nullptr};
    onion::Settings loaded{};
    TEST_ASSERT_TRUE(!onion::settings_load_from(paths, &loaded));
    TEST_ASSERT_EQ_INT(onion::kUiLanguageSystem, loaded.ui_lang);

    std::string log;
    TEST_ASSERT_TRUE(read_text(box.target + "/" ONION_DIST_MIGRATION_LOG, &log));
    TEST_ASSERT_TRUE(log.find("config=rejected") != std::string::npos);
  }

  /* Binary junk and oversized files are rejected before any conversion. */
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(make_dir(box.legacy));
  TEST_ASSERT_TRUE(write_text(box.legacy + "/config.ini",
                              std::string("[meta]\nschema_version=1\n\0x", 26)));
  int validations = 0;
  OnionMigrateReport report{};
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED,
                     migrate(box, &report, &validations));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_CONFIG_REJECTED, report.config);
  TEST_ASSERT_EQ_INT(0, validations);

  Sandbox big;
  TEST_ASSERT_TRUE(big.ok());
  TEST_ASSERT_TRUE(make_dir(big.legacy));
  TEST_ASSERT_TRUE(write_text(big.legacy + "/config.ini",
                              std::string(300 * 1024, '#')));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED, migrate(big, &report));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_CONFIG_REJECTED, report.config);
  TEST_ASSERT_TRUE(!exists(big.target + "/config.ini"));
  return 0;
}

int test_write_failure_keeps_original_and_is_retried(void) {
  if (geteuid() == 0)
    return 0; /* permission bits do not stop root; covered on non-root hosts */

  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(populate_legacy(box));

  /* The target exists but cannot be written, as on a read-only or failing
   * volume. */
  TEST_ASSERT_TRUE(make_dir(box.target));
  TEST_ASSERT_EQ_INT(0, chmod(box.target.c_str(), 0555));

  OnionMigrateReport report{};
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_FAILED, migrate(box, &report));
  TEST_ASSERT_TRUE(report.error != 0);
  TEST_ASSERT_TRUE(report.detail[0] != '\0');
  /* The diagnostic names the file (here the attempt counter, the first thing
   * written), never its content or an absolute path. */
  TEST_ASSERT_TRUE(std::strstr(report.detail, ONION_MIGRATE_ATTEMPTS_NAME) !=
                   nullptr);
  TEST_ASSERT_TRUE(std::strstr(report.detail, box.base.c_str()) == nullptr);
  TEST_ASSERT_TRUE(!exists(box.target + "/" ONION_DIST_MIGRATION_MARKER));
  TEST_ASSERT_TRUE(!exists(box.target + "/config.ini"));

  /* Nothing was lost... */
  std::string text;
  TEST_ASSERT_TRUE(read_text(box.legacy + "/config.ini", &text));
  TEST_ASSERT_STREQ(kLegacyConfig, text.c_str());
  TEST_ASSERT_TRUE(exists(box.legacy + "/payloads/tool.elf"));

  /* ... and the installation is still usable: with no AniketosHEN config on
   * disk, the loader reads OnionHEN's in place, legacy key included. */
  const std::string config = box.target + "/config.ini";
  const std::string legacy = box.legacy + "/config.ini";
  onion::SettingsPaths paths{config.c_str(), nullptr, legacy.c_str(), nullptr};
  onion::Settings loaded{};
  TEST_ASSERT_TRUE(onion::settings_load_from(paths, &loaded));
  TEST_ASSERT_STREQ(legacy.c_str(), onion::settings_last_loaded_path());
  TEST_ASSERT_EQ_INT(onion::kUiLanguageJa, loaded.ui_lang);
  TEST_ASSERT_TRUE(!loaded.onionhen_game_opts);
  TEST_ASSERT_TRUE(!loaded.kstuff_autoload);

  /* Once the volume is writable again the next boot finishes the job. */
  TEST_ASSERT_EQ_INT(0, chmod(box.target.c_str(), 0755));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED, migrate(box, &report));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_CONFIG_MIGRATED, report.config);
  TEST_ASSERT_EQ_INT(8, (int)report.files_copied);
  TEST_ASSERT_TRUE(onion::settings_load_from(paths, &loaded));
  TEST_ASSERT_STREQ(config.c_str(), onion::settings_last_loaded_path());
  return 0;
}

int test_interrupted_migration_resumes(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(populate_legacy(box));

  /* State left by a run that lost power part-way: some files complete, one
   * temp file half-written, no marker. */
  TEST_ASSERT_TRUE(make_dir(box.target));
  TEST_ASSERT_TRUE(make_dir(box.target + "/payloads"));
  TEST_ASSERT_TRUE(write_text(box.target + "/payloads/tool.elf", "ELF-payload"));
  TEST_ASSERT_TRUE(
      write_text(box.target + "/payloads/tool.elf.json.migrate.tmp", "{\"ver"));
  TEST_ASSERT_TRUE(write_text(box.target + "/config.ini.migrate.tmp", "[met"));

  OnionMigrateReport report{};
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED, migrate(box, &report));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_CONFIG_MIGRATED, report.config);
  TEST_ASSERT_EQ_INT(1, (int)report.files_present);
  TEST_ASSERT_EQ_INT(7, (int)report.files_copied);

  /* Half-written temp files were replaced by complete files, not promoted. */
  std::string text;
  TEST_ASSERT_TRUE(read_text(box.target + "/payloads/tool.elf.json", &text));
  TEST_ASSERT_STREQ("{\"version\":1,\"priority\":7,\"delay_seconds\":3}",
                    text.c_str());
  TEST_ASSERT_TRUE(!exists(box.target + "/payloads/tool.elf.json.migrate.tmp"));
  TEST_ASSERT_TRUE(!exists(box.target + "/config.ini.migrate.tmp"));
  onion::Settings loaded{};
  TEST_ASSERT_TRUE(
      onion::settings_load_file((box.target + "/config.ini").c_str(), &loaded));
  TEST_ASSERT_EQ_INT(onion::kUiLanguageJa, loaded.ui_lang);
  return 0;
}

int test_links_and_bad_roots_are_not_followed(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(make_dir(box.legacy));
  TEST_ASSERT_TRUE(make_dir(box.legacy + "/payloads"));
  TEST_ASSERT_TRUE(write_text(box.legacy + "/payloads/tool.elf", "ELF"));

  /* A secret outside both roots, reachable only through links. */
  const std::string outside = box.base + "/outside";
  TEST_ASSERT_TRUE(make_dir(outside));
  TEST_ASSERT_TRUE(write_text(outside + "/secret.txt", "secret"));
  TEST_ASSERT_EQ_INT(
      0, symlink(outside.c_str(), (box.legacy + "/linked_dir").c_str()));
  TEST_ASSERT_EQ_INT(0, symlink((outside + "/secret.txt").c_str(),
                                (box.legacy + "/payloads/link.elf").c_str()));
  TEST_ASSERT_EQ_INT(0, symlink((outside + "/secret.txt").c_str(),
                                (box.legacy + "/config.ini").c_str()));

  OnionMigrateReport report{};
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED, migrate(box, &report));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_CONFIG_REJECTED, report.config);
  TEST_ASSERT_EQ_INT(1, (int)report.files_copied);
  TEST_ASSERT_TRUE(exists(box.target + "/payloads/tool.elf"));
  TEST_ASSERT_TRUE(!exists(box.target + "/linked_dir"));
  TEST_ASSERT_TRUE(!exists(box.target + "/payloads/link.elf"));
  TEST_ASSERT_TRUE(!exists(box.target + "/config.ini"));

  /* A legacy root that is itself a link is treated as absent. */
  Sandbox linked;
  TEST_ASSERT_TRUE(linked.ok());
  TEST_ASSERT_EQ_INT(0, symlink(outside.c_str(), linked.legacy.c_str()));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_NO_LEGACY, migrate(linked, &report));
  TEST_ASSERT_TRUE(!exists(linked.target));

  /* Relative, nested and identical roots are refused outright. */
  const struct {
    const char *legacy, *target;
  } kBad[] = {
      {nullptr, "/tmp/a"},
      {"relative/OnionHEN", "/tmp/a"},
      {"/tmp/a", "/tmp/a"},
      {"/tmp/a", "/tmp/a/b"},
      {"/tmp/a/b", "/tmp/a"},
      {"/tmp/a/", "/tmp/b"},
  };
  for (const auto &bad : kBad) {
    const OnionMigrateOptions options = {bad.legacy, bad.target, nullptr,
                                         nullptr, 0};
    TEST_ASSERT_EQ_INT(ONION_MIGRATE_FAILED,
                       onion_migrate_legacy_root(&options, &report));
    TEST_ASSERT_EQ_INT(EINVAL, report.error);
  }
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_FAILED,
                     onion_migrate_legacy_root(nullptr, nullptr));
  return 0;
}

/* What two users hit: three bootstrappers importing one tree at once removed
 * each other's temp files ("cannot create" / "cannot store" on random cheat
 * files) and then went on to start the stack three times over. One importer
 * may run; the others must step aside without touching anything. */
int test_concurrent_importers_do_not_collide(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(populate_legacy(box));
  for (int i = 0; i < 1500; ++i) {
    char name[96];
    std::snprintf(name, sizeof(name), "/cheats/PPSA%05d_01.000.004_%08x.json",
                  i, (unsigned)i * 2654435761u);
    TEST_ASSERT_TRUE(write_text(box.legacy + name, std::string(900, 'c')));
  }

  enum { kRacers = 3 };
  pid_t children[kRacers];
  for (int i = 0; i < kRacers; ++i) {
    children[i] = fork();
    if (children[i] == 0) {
      OnionMigrateReport report{};
      const onion_migrate_status status = migrate(box, &report);
      _exit(status == ONION_MIGRATE_COMPLETED  ? 10
            : status == ONION_MIGRATE_BUSY     ? 11
            : status == ONION_MIGRATE_ALREADY_DONE ? 12
                                                : 13);
    }
  }
  int completed = 0;
  for (int i = 0; i < kRacers; ++i) {
    int status = 0;
    TEST_ASSERT_TRUE(waitpid(children[i], &status, 0) == children[i]);
    TEST_ASSERT_TRUE(WIFEXITED(status));
    const int code = WEXITSTATUS(status);
    TEST_ASSERT_TRUE(code == 10 || code == 11 || code == 12); /* never failed */
    completed += code == 10;
  }
  TEST_ASSERT_EQ_INT(1, completed);

  /* Every file arrived exactly once and intact, and no temp file is left. */
  std::string text;
  for (int i = 0; i < 1500; i += 97) {
    char name[96];
    std::snprintf(name, sizeof(name), "/cheats/PPSA%05d_01.000.004_%08x.json",
                  i, (unsigned)i * 2654435761u);
    TEST_ASSERT_TRUE(read_text(box.target + name, &text));
    TEST_ASSERT_EQ_INT(900, (int)text.size());
  }
  const std::string count =
      "find '" + box.target + "' -name '*" + ".migrate.tmp' | grep -q .";
  TEST_ASSERT_TRUE(std::system(count.c_str()) != 0);
  TEST_ASSERT_TRUE(exists(box.target + "/" ONION_DIST_MIGRATION_MARKER));
  TEST_ASSERT_TRUE(!exists(box.target + "/" ONION_MIGRATE_ATTEMPTS_NAME));
  return 0;
}

/* Not enough room once the reserve is kept: nothing is copied, each start
 * counts one attempt, the third gives up for good, and later starts are a
 * no-op. */
int test_space_reserve_and_attempts_are_bounded(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(populate_legacy(box));
  const OnionMigrateOptions options = {box.legacy.c_str(), box.target.c_str(),
                                       validate_with_settings, nullptr,
                                       1ull << 62};
  OnionMigrateReport report{};
  for (unsigned attempt = 1; attempt <= ONION_MIGRATE_MAX_ATTEMPTS; ++attempt) {
    const onion_migrate_status status =
        onion_migrate_legacy_root(&options, &report);
    TEST_ASSERT_EQ_INT((int)attempt, (int)report.attempt);
    TEST_ASSERT_TRUE(std::strstr(report.detail, "bytes free") != nullptr);
    TEST_ASSERT_EQ_INT(0, (int)report.files_copied);
    TEST_ASSERT_TRUE(!exists(box.target + "/config.ini"));
    if (attempt < ONION_MIGRATE_MAX_ATTEMPTS) {
      TEST_ASSERT_EQ_INT(ONION_MIGRATE_FAILED, status);
      TEST_ASSERT_TRUE(!report.final_attempt);
    } else {
      TEST_ASSERT_EQ_INT(ONION_MIGRATE_GAVE_UP, status);
      TEST_ASSERT_TRUE(report.final_attempt);
    }
  }
  std::string log;
  TEST_ASSERT_TRUE(read_text(box.target + "/" ONION_DIST_MIGRATION_LOG, &log));
  for (int launch = 0; launch < 3; ++launch) {
    TEST_ASSERT_EQ_INT(ONION_MIGRATE_GAVE_UP,
                       onion_migrate_legacy_root(&options, &report));
    TEST_ASSERT_TRUE(!report.final_attempt);
  }
  std::string log_after;
  TEST_ASSERT_TRUE(
      read_text(box.target + "/" ONION_DIST_MIGRATION_LOG, &log_after));
  TEST_ASSERT_STREQ(log.c_str(), log_after.c_str()); /* quiet from now on */

  /* A start that died mid-copy (power loss, kernel panic) also used up its
   * attempt: the counter is written before the copy begins. */
  Sandbox crashed;
  TEST_ASSERT_TRUE(crashed.ok());
  TEST_ASSERT_TRUE(populate_legacy(crashed));
  TEST_ASSERT_TRUE(make_dir(crashed.target));
  TEST_ASSERT_TRUE(write_text(crashed.target + "/" ONION_MIGRATE_ATTEMPTS_NAME,
                              "3\n"));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_GAVE_UP, migrate(crashed, &report));
  TEST_ASSERT_TRUE(!exists(crashed.target + "/payloads"));
  return 0;
}

bool make_sparse(const std::string &path, off_t size) {
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return false;
  const bool ok = ftruncate(fd, size) == 0;
  return close(fd) == 0 && ok;
}

int test_size_limits_and_game_dumps(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(populate_legacy(box));
  /* Game dumps and oversized single files stay behind. */
  TEST_ASSERT_TRUE(make_dir(box.legacy + "/games"));
  TEST_ASSERT_TRUE(make_dir(box.legacy + "/games/PPSA01234-app"));
  TEST_ASSERT_TRUE(write_text(box.legacy + "/games/PPSA01234-app/eboot.bin",
                              "ELF-game"));
  TEST_ASSERT_TRUE(make_sparse(box.legacy + "/payloads/huge.pkg",
                               (off_t)ONION_MIGRATE_MAX_FILE_BYTES + 1));

  OnionMigrateReport report{};
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_COMPLETED, migrate(box, &report));
  TEST_ASSERT_EQ_INT(8, (int)report.files_copied);
  TEST_ASSERT_EQ_INT(1, (int)report.files_too_large);
  TEST_ASSERT_TRUE(!exists(box.target + "/games"));
  TEST_ASSERT_TRUE(!exists(box.target + "/payloads/huge.pkg"));
  TEST_ASSERT_TRUE(exists(box.legacy + "/payloads/huge.pkg"));

  /* A tree over the total limit is not copied at all, and not retried. */
  Sandbox big;
  TEST_ASSERT_TRUE(big.ok());
  TEST_ASSERT_TRUE(populate_legacy(big));
  const off_t part = (off_t)(ONION_MIGRATE_MAX_FILE_BYTES - 1);
  for (int i = 0; (unsigned long long)i * (unsigned long long)part <=
                  ONION_MIGRATE_MAX_BYTES;
       ++i)
    TEST_ASSERT_TRUE(make_sparse(big.legacy + "/plugins/blob" +
                                     std::to_string(i) + ".bin",
                                 part));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_GAVE_UP, migrate(big, &report));
  TEST_ASSERT_TRUE(report.final_attempt);
  TEST_ASSERT_TRUE(std::strstr(report.detail, "too large") != nullptr);
  TEST_ASSERT_TRUE(!exists(big.target + "/plugins"));
  TEST_ASSERT_TRUE(!exists(big.target + "/config.ini"));
  TEST_ASSERT_EQ_INT(ONION_MIGRATE_GAVE_UP, migrate(big, &report));
  TEST_ASSERT_TRUE(!report.final_attempt);
  return 0;
}

int test_config_text_conversion(void) {
  char *out = nullptr;
  size_t length = 0;

  /* Only the [game_menu] key and its generated comment change. */
  const std::string input(kLegacyConfig);
  TEST_ASSERT_TRUE(
      onion_migrate_config_text(input.data(), input.size(), &out, &length));
  std::string converted(out, length);
  std::free(out);
  std::string expected = input;
  const std::string old_comment =
      "# show_onionhen_options adds OnionHEN entries to the game options menu.";
  expected.replace(expected.find(old_comment), old_comment.size(),
                   "# show_aniketoshen_options adds AniketosHEN entries to "
                   "the game options menu.");
  const std::string old_key = "show_onionhen_options=false";
  expected.replace(expected.find(old_key), old_key.size(),
                   "show_aniketoshen_options=false");
  TEST_ASSERT_STREQ(expected.c_str(), converted.c_str());
  /* The header comment naming OnionHEN is the user's file history; kept. */
  TEST_ASSERT_TRUE(converted.find("# OnionHEN configuration\n") == 0);

  /* Spacing, CRLF, a missing final newline and look-alike keys survive. */
  const std::string spaced =
      "[other]\r\nshow_onionhen_options=1\r\n"
      "[game_menu]\r\n\tshow_onionhen_options = true\r\n"
      "show_onionhen_options_extra=1\r\n"
      "[tail]\r\nshow_onionhen_options=0";
  TEST_ASSERT_TRUE(
      onion_migrate_config_text(spaced.data(), spaced.size(), &out, &length));
  converted.assign(out, length);
  std::free(out);
  TEST_ASSERT_STREQ("[other]\r\nshow_onionhen_options=1\r\n"
                    "[game_menu]\r\n\tshow_aniketoshen_options = true\r\n"
                    "show_onionhen_options_extra=1\r\n"
                    "[tail]\r\nshow_onionhen_options=0",
                    converted.c_str());

  /* A file that already has the current key is passed through untouched, and
   * converting twice changes nothing. */
  const std::string both = "[game_menu]\nshow_aniketoshen_options=true\n"
                           "show_onionhen_options=false\n";
  TEST_ASSERT_TRUE(
      onion_migrate_config_text(both.data(), both.size(), &out, &length));
  converted.assign(out, length);
  std::free(out);
  TEST_ASSERT_STREQ(both.c_str(), converted.c_str());
  TEST_ASSERT_TRUE(onion_migrate_config_text(expected.data(), expected.size(),
                                             &out, &length));
  converted.assign(out, length);
  std::free(out);
  TEST_ASSERT_STREQ(expected.c_str(), converted.c_str());

  TEST_ASSERT_TRUE(onion_migrate_config_text(nullptr, 0, &out, &length));
  TEST_ASSERT_EQ_INT(0, (int)length);
  std::free(out);
  TEST_ASSERT_TRUE(!onion_migrate_config_text(nullptr, 4, &out, &length));
  TEST_ASSERT_TRUE(!onion_migrate_config_text("x", 1, nullptr, &length));
  return 0;
}

int test_settings_key_alias_and_lookup_order(void) {
  Sandbox box;
  TEST_ASSERT_TRUE(box.ok());
  TEST_ASSERT_TRUE(make_dir(box.legacy));
  TEST_ASSERT_TRUE(make_dir(box.target));
  const std::string primary = box.target + "/config.ini";
  const std::string shellui = box.target + "/shellui.ini";
  const std::string legacy = box.legacy + "/config.ini";
  const std::string legacy_shellui = box.legacy + "/shellui.ini";
  onion::SettingsPaths paths{primary.c_str(), shellui.c_str(), legacy.c_str(),
                             legacy_shellui.c_str()};
  onion::Settings loaded{};

  /* Both spellings of the game-options key are read; the current one wins
   * regardless of the order they appear in. */
  TEST_ASSERT_TRUE(write_text(
      primary, "[meta]\nschema_version=1\n[game_menu]\n"
               "show_onionhen_options=false\n"));
  TEST_ASSERT_TRUE(onion::settings_load_file(primary.c_str(), &loaded));
  TEST_ASSERT_TRUE(!loaded.onionhen_game_opts);
  TEST_ASSERT_TRUE(write_text(
      primary, "[meta]\nschema_version=1\n[game_menu]\n"
               "show_aniketoshen_options=false\nshow_onionhen_options=true\n"));
  TEST_ASSERT_TRUE(onion::settings_load_file(primary.c_str(), &loaded));
  TEST_ASSERT_TRUE(!loaded.onionhen_game_opts);
  TEST_ASSERT_TRUE(write_text(
      primary, "[meta]\nschema_version=1\n[game_menu]\n"
               "show_onionhen_options=false\nshow_aniketoshen_options=true\n"));
  TEST_ASSERT_TRUE(onion::settings_load_file(primary.c_str(), &loaded));
  TEST_ASSERT_TRUE(loaded.onionhen_game_opts);
  TEST_ASSERT_EQ_INT(0, unlink(primary.c_str()));

  /* Lookup order: primary, shellui, then the OnionHEN paths. */
  onion::Settings marker{};
  marker.fan_threshold = 61;
  TEST_ASSERT_TRUE(onion::settings_save_file(legacy_shellui.c_str(), marker));
  TEST_ASSERT_TRUE(onion::settings_load_from(paths, &loaded));
  TEST_ASSERT_EQ_INT(61, loaded.fan_threshold);
  marker.fan_threshold = 62;
  TEST_ASSERT_TRUE(onion::settings_save_file(legacy.c_str(), marker));
  TEST_ASSERT_TRUE(onion::settings_load_from(paths, &loaded));
  TEST_ASSERT_EQ_INT(62, loaded.fan_threshold);
  marker.fan_threshold = 63;
  TEST_ASSERT_TRUE(onion::settings_save_file(shellui.c_str(), marker));
  TEST_ASSERT_TRUE(onion::settings_load_from(paths, &loaded));
  TEST_ASSERT_EQ_INT(63, loaded.fan_threshold);
  marker.fan_threshold = 64;
  TEST_ASSERT_TRUE(onion::settings_save_file(primary.c_str(), marker));
  TEST_ASSERT_TRUE(onion::settings_load_from(paths, &loaded));
  TEST_ASSERT_EQ_INT(64, loaded.fan_threshold);

  /* An AniketosHEN config that exists but is unusable yields defaults; it is
   * never silently replaced by OnionHEN's older file. */
  TEST_ASSERT_EQ_INT(0, unlink(shellui.c_str()));
  TEST_ASSERT_TRUE(write_text(primary, "[meta]\nschema_version=99\n"));
  TEST_ASSERT_TRUE(!onion::settings_load_from(paths, &loaded));
  TEST_ASSERT_EQ_INT(onion::kFanAutomaticThresholdCelsius,
                     loaded.fan_threshold);
  TEST_ASSERT_STREQ("", onion::settings_last_loaded_path());

  /* The canonical paths are this distribution's; OnionHEN's are fallbacks. */
  TEST_ASSERT_STREQ("/data/AniketosHEN/config.ini", onion::kConfigPathPrimary);
  TEST_ASSERT_STREQ("/user/data/AniketosHEN/config.ini",
                    onion::kConfigPathShellui);
  TEST_ASSERT_STREQ("/data/OnionHEN/config.ini",
                    onion::kLegacyConfigPathPrimary);
  TEST_ASSERT_STREQ("/user/data/OnionHEN/config.ini",
                    onion::kLegacyConfigPathShellui);
  const onion::SettingsPaths defaults{};
  TEST_ASSERT_STREQ(onion::kConfigPathPrimary, defaults.primary);
  TEST_ASSERT_STREQ(onion::kLegacyConfigPathPrimary, defaults.legacy_primary);
  return 0;
}

int test_distribution_identity(void) {
  TEST_ASSERT_STREQ("AniketosHEN", ONION_DIST_NAME);
  TEST_ASSERT_STREQ("OnionHEN", ONION_DIST_LEGACY_NAME);
  TEST_ASSERT_STREQ("/data/AniketosHEN", ONION_DIST_DATA_ROOT);
  TEST_ASSERT_STREQ("/user/data/AniketosHEN", ONION_DIST_USER_DATA_ROOT);
  TEST_ASSERT_STREQ("/data/OnionHEN", ONION_DIST_LEGACY_DATA_ROOT);
  TEST_ASSERT_STREQ("/user/data/OnionHEN", ONION_DIST_LEGACY_USER_DATA_ROOT);
  TEST_ASSERT_STREQ("/data/AniketosHEN/AniketosHEN.log", ONION_DIST_LOG_PATH);
  TEST_ASSERT_STREQ("/data/AniketosHEN/AniketosHEN_crash.log",
                    ONION_DIST_CRASH_LOG_PATH);
  TEST_ASSERT_STREQ(".migrated_from_OnionHEN", ONION_DIST_MIGRATION_MARKER);
  /* Upstream is credited by its real location, not rebranded. */
  TEST_ASSERT_STREQ("https://github.com/aydencharles/onionHEN",
                    ONION_DIST_UPSTREAM_URL);
  TEST_ASSERT_STREQ("completed",
                    onion_migrate_status_name(ONION_MIGRATE_COMPLETED));
  TEST_ASSERT_STREQ("kept-existing", onion_migrate_config_status_name(
                                         ONION_MIGRATE_CONFIG_KEPT_EXISTING));
  return 0;
}

/* The Settings version banner is stored XOR-obfuscated in prx.cpp, which
 * the host cannot compile. Decode the literal from the source text so a
 * change of ONION_DIST_NAME cannot silently leave the old banner behind. */
int test_shellui_version_banner_matches_identity(void) {
  std::string source;
  TEST_ASSERT_TRUE(read_text(
      std::string(ONION_TEST_DIR) + "/../../shellui/src/prx.cpp", &source));
  const std::string anchor = "const char enc_ver[] = \"";
  const size_t begin = source.find(anchor);
  TEST_ASSERT_TRUE(begin != std::string::npos);
  const size_t end = source.find('"', begin + anchor.size());
  TEST_ASSERT_TRUE(end != std::string::npos);
  const std::string literal =
      source.substr(begin + anchor.size(), end - begin - anchor.size());

  const char key[] = "SISTR0_I_SEE_YOU";
  std::string decoded;
  TEST_ASSERT_EQ_INT(0, (int)(literal.size() % 4));
  for (size_t i = 0; i < literal.size(); i += 4) {
    TEST_ASSERT_TRUE(literal.compare(i, 2, "\\x") == 0);
    const int byte = (int)std::strtol(literal.substr(i + 2, 2).c_str(), nullptr, 16);
    decoded.push_back((char)(byte ^ key[(i / 4) % (sizeof(key) - 1)]));
  }
  TEST_ASSERT_STREQ(ONION_DIST_NAME " ", decoded.c_str());
  return 0;
}

} // namespace

extern "C" int test_migration_suite(void) {
  int failures = 0;
  failures += onion_test_run("migration_fresh_install",
                             test_fresh_install_has_nothing_to_migrate);
  failures += onion_test_run("migration_existing_onionhen_install",
                             test_existing_onionhen_install_is_imported);
  failures += onion_test_run("migration_repeat_launches",
                             test_repeat_launches_do_not_migrate_again);
  failures += onion_test_run("migration_existing_aniketoshen_data",
                             test_existing_aniketoshen_data_is_never_overwritten);
  failures += onion_test_run("migration_malformed_legacy_config",
                             test_malformed_legacy_config_is_not_installed);
  failures += onion_test_run("migration_write_failure",
                             test_write_failure_keeps_original_and_is_retried);
  failures += onion_test_run("migration_interrupted_resume",
                             test_interrupted_migration_resumes);
  failures += onion_test_run("migration_links_and_bad_roots",
                             test_links_and_bad_roots_are_not_followed);
  failures += onion_test_run("migration_concurrent_importers",
                             test_concurrent_importers_do_not_collide);
  failures += onion_test_run("migration_reserve_and_attempts",
                             test_space_reserve_and_attempts_are_bounded);
  failures += onion_test_run("migration_size_limits",
                             test_size_limits_and_game_dumps);
  failures += onion_test_run("migration_config_text",
                             test_config_text_conversion);
  failures += onion_test_run("migration_settings_alias_and_order",
                             test_settings_key_alias_and_lookup_order);
  failures += onion_test_run("migration_distribution_identity",
                             test_distribution_identity);
  failures += onion_test_run("migration_shellui_version_banner",
                             test_shellui_version_banner_matches_identity);
  return failures;
}
