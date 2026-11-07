#include "test_harness.h"

#include "plugin_sprx_pages.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"

#include <string>

using namespace onion::shellui::plugin_pages;

static int test_plugin_list_is_flat_links(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  std::vector<PluginInventoryItem> plugins = {
      {.plugin_id = "FTPS00001",
       .version = "1.00",
       .name = "FTP Server",
       .running = true,
       .auto_start = true},
      {.plugin_id = "DPIV00001",
       .version = "1.00",
       .name = "DPI",
       .running = false,
       .auto_start = false},
  };

  ps5ui::Page page("id_plugins", "Plugins");
  append_plugin_list_links(page, plugins);
  const std::string xml = page.build();

  TEST_ASSERT_TRUE(xml.find("id=\"id_external_plugins\"") == std::string::npos);
  TEST_ASSERT_TRUE(xml.find("setting_list id=\"id_external_plugin_") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(
      xml.find("file=\"plugin_FTPS00001.xml\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("file=\"plugin_DPIV00001.xml\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Running · Auto-start\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Stopped\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id_external_plugin_run_") == std::string::npos);
  return 0;
}

static int test_plugin_config_order(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  const PluginInventoryItem plugin{.plugin_id = "FTPS00001",
                                   .version = "1.00",
                                   .name = "FTP Server",
                                   .running = true,
                                   .auto_start = false};
  onion::shellui::dynamic_ui::PluginSettingsLink settings{
      .plugin_id = "FTPS00001",
      .control_id = "id_settings",
      .title = "FTP Settings",
      .resource = "onion_ui_ftp.xml",
      .description = "Ports and auth",
  };

  ps5ui::Page page("id_plugin_config", plugin.name);
  fill_plugin_config(page, plugin, &settings);
  const std::string xml = page.build();

  const auto run_at = xml.find("id=\"id_external_plugin_run_FTPS00001\"");
  const auto auto_at =
      xml.find("id=\"id_external_plugin_autostart_FTPS00001\"");
  const auto settings_at =
      xml.find("id=\"id_external_plugin_settings_FTPS00001\"");
  const auto delete_at = xml.find("id=\"id_external_plugin_delete_FTPS00001\"");
  TEST_ASSERT_TRUE(run_at != std::string::npos);
  TEST_ASSERT_TRUE(auto_at != std::string::npos);
  TEST_ASSERT_TRUE(settings_at != std::string::npos);
  TEST_ASSERT_TRUE(delete_at != std::string::npos);
  TEST_ASSERT_TRUE(run_at < auto_at);
  TEST_ASSERT_TRUE(auto_at < settings_at);
  TEST_ASSERT_TRUE(settings_at < delete_at);
  TEST_ASSERT_TRUE(xml.find("file=\"onion_ui_ftp.xml\"") != std::string::npos);
  return 0;
}

static int test_sprx_game_list(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  std::vector<SprxGame> games = {
      {"PPSA01234", "Astro Bot", "/user/appmeta/PPSA01234/icon0.png", 2, true},
      {"CUSA00001", "Old Game", "", 1, false},
      {"PPSA09999", "Empty", "", 0, true},
  };
  ps5ui::Page page("id_sprx", "Game plugins");
  append_sprx_game_links(page, games);
  const std::string xml = page.build();
  TEST_ASSERT_TRUE(xml.find("file=\"sprx_PPSA01234.xml\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("icon0.png") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Plugins: 2 · On\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Plugins: 1 · Off\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"No plugins\"") != std::string::npos);
  return 0;
}

static int test_sprx_game_page(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  namespace pl = onion::pluginloader;
  pl::Config config;
  config.load_on_startup = false;
  config.disabled = {"all/fps.elf"};
  std::vector<pl::Plugin> plugins = {
      {"menu.sprx", "/data/AniketosHEN/pluginloader/CUSA00001/menu.sprx",
       pl::Kind::Sprx, true},
      {"all/fps.elf", "/data/AniketosHEN/pluginloader/all/fps.elf",
       pl::Kind::Elf, false},
  };
  const pl::Status status = {{"menu.sprx", "loaded"},
                             {"all/fps.elf", "start failed"}};
  std::vector<SprxInventoryItem> legacy = {
      {.id = "goldhen-afr", .path = "/data/GoldHEN/plugins/afr.prx",
       .prefixes = {"CUSA"}, .source = "/data/GoldHEN/plugins.ini"},
      {.id = "other", .path = "/data/x.sprx", .titles = {"PPSA01234"}},
      {.id = "off", .path = "/data/y.sprx", .enabled = false,
       .titles = {"CUSA00001"}},
  };
  ps5ui::Page page("id_sprx_config", "Old Game");
  fill_sprx_game(page, "CUSA00001", config, plugins, status, legacy);
  const std::string xml = page.build();

  const auto startup = xml.find("id=\"id_external_sprx_on_CUSA00001\"");
  const auto menu = xml.find("id=\"id_external_sprx_plugin_CUSA00001_0\"");
  const auto fps = xml.find("id=\"id_external_sprx_plugin_CUSA00001_1\"");
  TEST_ASSERT_TRUE(startup != std::string::npos && startup < menu && menu < fps);
  TEST_ASSERT_TRUE(xml.find("SPRX · Loaded at the last start") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("ELF · all games · Last load failed: start failed") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("title=\"fps.elf\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("goldhen-afr · Listed in /data/GoldHEN/plugins.ini") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("other ·") == std::string::npos);
  TEST_ASSERT_TRUE(xml.find("off ·") == std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id_external_sprx_empty") == std::string::npos);

  std::string title;
  size_t row = 0;
  TEST_ASSERT_TRUE(split_sprx_plugin_control_id(
      sprx_plugin_control_id("PPSA01234", 12), title, row));
  TEST_ASSERT_TRUE(title == "PPSA01234" && row == 12);
  TEST_ASSERT_TRUE(!split_sprx_plugin_control_id(
      "id_external_sprx_plugin_PPSA01234_x", title, row));

  ps5ui::Page empty("id_sprx_config", "Empty");
  fill_sprx_game(empty, "PPSA01234", {}, {}, {}, {});
  TEST_ASSERT_TRUE(empty.build().find(
                       "/data/AniketosHEN/pluginloader/PPSA01234") !=
                   std::string::npos);
  return 0;
}

extern "C" int test_plugin_sprx_pages_suite(void) {
  int fails = 0;
  fails += onion_test_run("plugin_pages.flat_list", test_plugin_list_is_flat_links);
  fails += onion_test_run("plugin_pages.config_order", test_plugin_config_order);
  fails += onion_test_run("sprx_pages.game_list", test_sprx_game_list);
  fails += onion_test_run("sprx_pages.game_page", test_sprx_game_page);
  return fails;
}
