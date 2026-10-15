#include "aniketos_theme/color.hpp"

#include <algorithm>
#include <cmath>

namespace AniketosTheme {

RGB ClampRGB(RGB color) {
  color.r = std::clamp(color.r, 0.0f, 1.0f);
  color.g = std::clamp(color.g, 0.0f, 1.0f);
  color.b = std::clamp(color.b, 0.0f, 1.0f);
  return color;
}

HSV RGBToHSV(RGB color) {
  color = ClampRGB(color);
  const float maximum = std::max({color.r, color.g, color.b});
  const float minimum = std::min({color.r, color.g, color.b});
  const float delta = maximum - minimum;

  HSV result{};
  result.v = maximum;
  result.s = maximum > 0.0f ? delta / maximum : 0.0f;
  if (delta <= 0.000001f)
    return result;

  if (maximum == color.r)
    result.h = 60.0f * std::fmod((color.g - color.b) / delta, 6.0f);
  else if (maximum == color.g)
    result.h = 60.0f * ((color.b - color.r) / delta + 2.0f);
  else
    result.h = 60.0f * ((color.r - color.g) / delta + 4.0f);
  if (result.h < 0.0f)
    result.h += 360.0f;
  return result;
}

RGB HSVToRGB(HSV color) {
  color.h = std::fmod(color.h, 360.0f);
  if (color.h < 0.0f)
    color.h += 360.0f;
  color.s = std::clamp(color.s, 0.0f, 1.0f);
  color.v = std::clamp(color.v, 0.0f, 1.0f);

  const float chroma = color.v * color.s;
  const float sector = color.h / 60.0f;
  const float x = chroma * (1.0f - std::fabs(std::fmod(sector, 2.0f) - 1.0f));
  const float match = color.v - chroma;
  RGB result{};
  if (sector < 1.0f)
    result = {chroma, x, 0.0f};
  else if (sector < 2.0f)
    result = {x, chroma, 0.0f};
  else if (sector < 3.0f)
    result = {0.0f, chroma, x};
  else if (sector < 4.0f)
    result = {0.0f, x, chroma};
  else if (sector < 5.0f)
    result = {x, 0.0f, chroma};
  else
    result = {chroma, 0.0f, x};
  result.r += match;
  result.g += match;
  result.b += match;
  return ClampRGB(result);
}

RGB TintPreservingBrightness(RGB source, RGB theme, float strength) {
  source = ClampRGB(source);
  const HSV source_hsv = RGBToHSV(source);
  HSV target_hsv = RGBToHSV(theme);
  target_hsv.v = std::clamp(
      target_hsv.v * (0.35f + 0.65f * source_hsv.v), 0.0f, 1.0f);
  const RGB themed = HSVToRGB(target_hsv);
  strength = std::clamp(strength, 0.0f, 1.0f);
  return ClampRGB({source.r + (themed.r - source.r) * strength,
                   source.g + (themed.g - source.g) * strength,
                   source.b + (themed.b - source.b) * strength});
}

} // namespace AniketosTheme
