#pragma once

#include "external_symbols.hpp"

#include <string>
#include <vector>

/* The account avatar as a picture or an animated GIF/WebP of the user's
 * choice, in place of the picture ShellUI shows for the signed-in user
 * (Home, Control Center, Settings, Trophies: every UI3 ImageBox loading
 * profilecache:image/face/...?userid=<that user>). Runtime only: the profile
 * cache on disk is never touched, and switching it off brings the user's own
 * picture back. */
namespace AccountAvatar {
void Install(MonoImage *pui);
/* The pictures in the avatars folder, sorted by name. */
const std::vector<std::string> &Files(bool rescan = false);
/* Files of other kinds the last scan of the folder passed over. */
size_t Unsupported();
/* UI thread, once a frame. */
void Tick();
/* The user signed in now, as the setting keeps it ("0x1EA2F4DA"; empty
   when nobody is): whose picture is replaced. */
std::string SignedInUser();
/* The setting changed: load every avatar on screen again. UI thread. */
void Refresh();

/* For other pictures swapped in the same ImageBox.LoadAsync hook
   (game_icons.cpp): loads past the hook, with `option` (or new options) set
   for a still picture or an animated one, decoded progressively and
   looping. */
void LoadImage(MonoObject *box, const std::string &uri, MonoObject *action,
               MonoObject *option, bool set_image, bool animated);
/* The system's own picture, with the system's options. */
void LoadSystemImage(MonoObject *box, const std::string &uri,
                     MonoObject *action, bool set_image);
}
