#include "aniketos_theme/animation.hpp"

#include <algorithm>
#include <cmath>

namespace AniketosTheme {
AnimationFrame EvaluateAnimation(const ThemeConfig &config,
                                 float elapsed_seconds) {
  AnimationFrame frame{config.base, config.glow};
  const float time = std::max(0.0f, elapsed_seconds) *
                     std::clamp(config.animation_speed, 0.25f, 4.0f);

  if (config.animation == AnimationMode::Rainbow) {
    // One complete spectrum sweep every eight seconds at 100% speed. Preserve
    // the user's brightness while guaranteeing enough chroma to make Rainbow
    // visible even when it is enabled over a monochrome preset.
    const float hue = std::fmod(time * 45.0f, 360.0f);
    HSV base = RGBToHSV(config.base);
    HSV glow = RGBToHSV(config.glow);
    glow.h = std::fmod(glow.h + hue, 360.0f);
    glow.s = std::max(glow.s, 0.88f);
    glow.v = std::max(glow.v, 0.70f);
    if (config.preset != Preset::Rainbow) {
      base.h = std::fmod(base.h + hue, 360.0f);
      base.s = std::max(base.s, 0.72f);
      base.v = std::max(base.v, 0.055f);
      frame.base = HSVToRGB(base);
    }
    frame.glow = HSVToRGB(glow);
  }

  return frame;
}

} // namespace AniketosTheme
