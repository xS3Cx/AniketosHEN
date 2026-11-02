#pragma once

#include <string>

/* More on a game's or app's Information screen (Options > Information):
   rows of AniketosHEN's own, each label starting with a star, under the
   system's rows: Title ID, Content ID, region, platform, master version,
   required system software, SDK, install and metadata paths, the folder a
   dumped game is mounted from, install time and last use.

   That screen is its own React Native app (rnps-content-information,
   NPXS40017), started anew for every title with
   "pscontentinfo:info?titleId=...". A script evaluated before its bundle
   (see content_info_inject_js.inc, put in by script_injector) reads the values
   from the app database through the module the screen itself uses and appends
   the rows. The native side gives it the labels and the mount.lnk targets
   of /user/app; nothing is written anywhere. */
namespace ContentInfo {

/* The script for the Information screen, and the settings it is given. */
const char *InjectedScript();
std::string ScriptConfig();

} // namespace ContentInfo
