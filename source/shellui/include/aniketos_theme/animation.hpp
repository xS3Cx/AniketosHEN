#pragma once

#include "aniketos_theme/theme_config.hpp"

namespace AniketosTheme {

struct AnimationFrame {
  RGB base{};
  RGB glow{};
};

// elapsed_seconds is monotonic runtime time since the current animation mode
// started. The function is pure so animation math can be validated on host.
AnimationFrame EvaluateAnimation(const ThemeConfig &config,
                                 float elapsed_seconds);

} // namespace AniketosTheme
