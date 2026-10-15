#pragma once

#include "aniketos_theme/color.hpp"

#include <cstdint>

namespace AniketosTheme::GlobalTheme {

// The global-customization page owns this source while enabled. Every
// surface consumes the same timestamp, so its animation phase stays aligned.
bool Active();
bool Animated();
RGB Color(uint64_t monotonic_ms);

} // namespace AniketosTheme::GlobalTheme
