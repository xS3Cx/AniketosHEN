#pragma once

#include <string>

struct MonoImage;
struct MonoObject;

namespace AniketosTheme::IconColor {

void Install(MonoImage *pui);
void Tick();
void RequestApply();

/* A style token names an icon colour (panel_tint.cpp asks once per key and
   remembers); such a token takes the chosen colour while the option is on. */
bool TokenIsIcon(const std::string &key);
bool TintIconToken(float *r, float *g, float *b);
/* A menu row was just given its picture by a hook that owns the setter (the
   one Install cannot detour twice): tints it like our own setter hooks do. */
void MenuIconSet(MonoObject *row);

} // namespace AniketosTheme::IconColor
