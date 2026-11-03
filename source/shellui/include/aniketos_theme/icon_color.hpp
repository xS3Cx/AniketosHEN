#pragma once

#include <string>

struct MonoImage;
struct MonoObject;

namespace AniketosTheme::IconColor {

void Install(MonoImage *pui);
void Tick();
void RequestApply();

bool TintToken(const std::string &key, float *r, float *g, float *b);
/* A menu row was just given its picture by a hook that owns the setter (the
   one Install cannot detour twice): tints it like our own setter hooks do. */
void MenuIconSet(MonoObject *row);

} // namespace AniketosTheme::IconColor
