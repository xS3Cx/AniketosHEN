#pragma once

namespace AniketosShell::Easing {

// 0 is linear. 1..24 preserve Akari's Sine..Back ordering.
float Apply(int type, float t);
const char *Name(int type);

} // namespace AniketosShell::Easing
