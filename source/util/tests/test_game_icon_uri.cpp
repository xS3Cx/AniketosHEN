/* Host tests for telling a game's icon from other pictures (no PS5/Mono). */
#include "test_harness.h"

#include "aniketos_shell/system_ui/game_icon_uri.hpp"

using namespace GameIconUri;

namespace {

int test_icon_paths_name_their_title() {
  TEST_ASSERT_TRUE(TitleId("/user/appmeta/PPSA01234/icon0.png") == "PPSA01234");
  TEST_ASSERT_TRUE(TitleId("file:///user/appmeta/external/CUSA00001/icon0.png?ts=17") ==
                   "CUSA00001");
  TEST_ASSERT_TRUE(TitleId("/system_data/priv/appmeta/NPXS40000/ICON0.PNG") ==
                   "NPXS40000");
  TEST_ASSERT_TRUE(TitleId("file:///user/cache/PPSA01234_icon0.png?ts=1") ==
                   "PPSA01234");
  return 0;
}

int test_other_pictures_name_none() {
  TEST_ASSERT_TRUE(TitleId("").empty());
  TEST_ASSERT_TRUE(TitleId("icon0.png").empty());
  TEST_ASSERT_TRUE(TitleId("/icon0.png").empty());
  TEST_ASSERT_TRUE(TitleId("/user/appmeta/PPSA01234/pic0.png").empty());
  TEST_ASSERT_TRUE(TitleId("/user/appmeta/ppsa01234/icon0.png").empty());
  TEST_ASSERT_TRUE(TitleId("/user/appmeta/PPSA0123/icon0.png").empty());
  TEST_ASSERT_TRUE(TitleId("profilecache:image/face/x.png?userid=1").empty());
  TEST_ASSERT_TRUE(TitleId("/a/PPSA01234.png").empty());
  TEST_ASSERT_TRUE(TitleId("/a/b?/PPSA01234/icon0.png").empty());
  return 0;
}

int test_only_small_animations_play() {
  AvatarImage::Info info;
  info.format = AvatarImage::Format::Gif;
  info.width = 256;
  info.height = 256;
  info.frames = 64;
  TEST_ASSERT_TRUE(Plays(info));
  info.frames = 65; /* past 16 MB decoded */
  TEST_ASSERT_TRUE(!Plays(info));
  info.frames = 1;
  TEST_ASSERT_TRUE(!Plays(info));
  info.width = info.height = 400; /* past what the system decodes */
  info.frames = 4;
  TEST_ASSERT_TRUE(!Plays(info));
  return 0;
}

} // namespace

extern "C" int test_game_icon_uri_suite(void) {
  int failures = 0;
  failures += onion_test_run("game_icon_uri.icon_paths_name_their_title",
                             test_icon_paths_name_their_title);
  failures += onion_test_run("game_icon_uri.other_pictures_name_none",
                             test_other_pictures_name_none);
  failures += onion_test_run("game_icon_uri.only_small_animations_play",
                             test_only_small_animations_play);
  return failures;
}
