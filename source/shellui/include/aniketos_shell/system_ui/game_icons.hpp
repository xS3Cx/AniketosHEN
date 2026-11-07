#pragma once

#include "external_symbols.hpp"

#include <string>

/* A game's or app's icon replaced by a picture of the user's: ShellUI/icons/
 * <TITLEID>.gif (or .webp, .png, .jpg, .jpeg). Every UI3 ImageBox loading
 * that title's icon0 shows it instead; an animated one plays only while Home
 * is in sight, the box is in Home and on screen. Runtime only: the system's
 * icons on disk are never touched. Shares account_avatar's LoadAsync hook. */
namespace GameIcons {
/* From the hook, for every picture: true when it loaded the user's icon in
   place of `uri`, false to let the hook go on. */
bool Load(MonoObject *box, MonoString *uri, MonoObject *action,
          MonoObject *option, bool set_image);
/* UI thread, once a frame. */
void Tick();
/* For Home's script: {"on":bool,"icons":{"TITLEID":"path",...}}; the
   script points those titles' tile rows at the files. */
std::string ScriptConfig();
}
