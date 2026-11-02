#include "icon_color_internal.hpp"

#include "aniketos_theme/color.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_theme/icon_color.hpp"
#include "aniketos_ui/monotonic_clock.hpp"

#include <algorithm>
#include <cmath>

namespace AniketosTheme::IconColor {
namespace {

uint64_t s_rainbow_started_ms = 0;

UIColor static_color() {
  return {std::clamp(g_settings.system_icon_color_r, 0, 255) / 255.0f,
          std::clamp(g_settings.system_icon_color_g, 0, 255) / 255.0f,
          std::clamp(g_settings.system_icon_color_b, 0, 255) / 255.0f, 1.0f, 0};
}

bool token_is_icon(const std::string &key) {
  const std::string lower = ascii_lower(key);
  return contains(lower, "icon") && !contains(lower, "background") &&
         !contains(lower, "surface") && !contains(lower, "basemat");
}

} // namespace

bool feature_on() {
  return CustomizationOn() && g_settings.system_icon_color_enabled;
}

UIColor current_color() {
  if (g_settings.system_icon_color_animation != 1)
    return static_color();
  const uint64_t now = AniketosUI::MonotonicMs();
  if (!s_rainbow_started_ms)
    s_rainbow_started_ms = now;
  const float speed =
      std::clamp(g_settings.system_icon_color_animation_speed, 25, 400) /
      100.0f;
  const float elapsed =
      static_cast<float>(now - s_rainbow_started_ms) / 1000.0f;
  float hue = std::fmod(elapsed * 70.0f * speed, 360.0f);
  if (hue < 0.0f)
    hue += 360.0f;
  const RGB rgb = HSVToRGB({hue, 1.0f, 1.0f});
  return {rgb.r, rgb.g, rgb.b, 1.0f, 0};
}

void restart_rainbow() { s_rainbow_started_ms = 0; }

bool TintToken(const std::string &key, float *r, float *g, float *b) {
  if (!r || !g || !b || !feature_on() || !token_is_icon(key))
    return false;
  const UIColor color = current_color();
  *r = color.r;
  *g = color.g;
  *b = color.b;
  return true;
}

} // namespace AniketosTheme::IconColor
