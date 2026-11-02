#pragma once

#include <string>

struct MonoImage;

namespace AniketosTheme::IconColor {

void Install(MonoImage *pui);
void Tick();
void RequestApply();

bool TintToken(const std::string &key, float *r, float *g, float *b);

} // namespace AniketosTheme::IconColor
