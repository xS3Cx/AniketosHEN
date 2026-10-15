/* Copyright (C) 2026 AniketosHEN contributors */

#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/customization.hpp"

#include <onion/settings.hpp>

#include <algorithm>
#include <cmath>

extern onion::Settings g_settings;

namespace AniketosTheme::GlobalTheme {
namespace {

float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float phase(uint64_t monotonic_ms) {
  // All Global Theme consumers run from different native hooks. Quantize the
  // source clock so a single ShellUI display frame produces one exact color
  // for menu, background, clock, focus, notifications and Aura.
  monotonic_ms -= monotonic_ms % 16u;
  const float speed =
      std::clamp(g_settings.vsh_color_animation_speed, 25, 400) / 100.0f;
  return static_cast<float>(monotonic_ms % 120000u) * 0.001f * speed;
}

} // namespace

bool Active() { return (AniketosTheme::CustomizationOn() && g_settings.vsh_global_theme_enabled); }

bool Animated() {
  return Active() && std::clamp(g_settings.vsh_color_animation, 0, 4) != 0;
}

RGB Color(uint64_t monotonic_ms) {
  const RGB base{clamp01(g_settings.vsh_left_r),
                 clamp01(g_settings.vsh_left_g),
                 clamp01(g_settings.vsh_left_b)};
  if (!Active())
    return base;

  const float animation_phase = phase(monotonic_ms);
  switch (std::clamp(g_settings.vsh_color_animation, 0, 4)) {
  case 1: // Solid rainbow, identical to AniketosUI::AnimatedColor.
  case 2: // A single global color has no second gradient edge.
    return HSVToRGB({animation_phase * 70.0f, 1.0f, 1.0f});
  case 3: {
    const float brightness = 0.62f + 0.38f *
        (std::sin(animation_phase * 3.0f) * 0.5f + 0.5f);
    return {base.r * brightness, base.g * brightness,
            base.b * brightness};
  }
  case 4:
    return HSVToRGB({188.0f + std::sin(animation_phase * 1.8f) * 16.0f,
                     0.90f, 1.0f});
  case 0:
  default:
    return base;
  }
}

} // namespace AniketosTheme::GlobalTheme
