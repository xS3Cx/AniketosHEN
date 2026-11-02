/* The logo in another colour (host-testable, no Mono/PS5 I/O): its coloured
 * pixels take the colour, its white and grey lettering stays.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace LogoTint {

struct Hsv {
  float h, s, v;
};

inline Hsv ToHsv(float r, float g, float b) {
  const float high = std::max({r, g, b});
  const float range = high - std::min({r, g, b});
  Hsv out{0.0f, high > 0.0f ? range / high : 0.0f, high};
  if (range <= 0.0f)
    return out;
  if (high == r)
    out.h = std::fmod((g - b) / range + 6.0f, 6.0f);
  else if (high == g)
    out.h = (b - r) / range + 2.0f;
  else
    out.h = (r - g) / range + 4.0f;
  return out;
}

/* Hue in sextants, 0..6. */
inline void FromHsv(const Hsv &in, float &r, float &g, float &b) {
  const float chroma = in.v * in.s;
  const float x = chroma * (1.0f - std::fabs(std::fmod(in.h, 2.0f) - 1.0f));
  const float low = in.v - chroma;
  const int sextant = static_cast<int>(in.h) % 6;
  const float table[6][3] = {{chroma, x, 0}, {x, chroma, 0}, {0, chroma, x},
                             {0, x, chroma}, {x, 0, chroma}, {chroma, 0, x}};
  r = table[sextant][0] + low;
  g = table[sextant][1] + low;
  b = table[sextant][2] + low;
}

/* Below this a pixel is the logo's white or grey. */
constexpr float kMinSaturation = 0.15f;

inline uint8_t Byte(float value) {
  return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255));
}

inline void Apply(uint8_t *rgba, size_t pixels, uint8_t red, uint8_t green,
                  uint8_t blue) {
  const Hsv target = ToHsv(red / 255.0f, green / 255.0f, blue / 255.0f);
  for (size_t i = 0; i < pixels; ++i, rgba += 4) {
    Hsv pixel = ToHsv(rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f);
    if (rgba[3] == 0 || pixel.s < kMinSaturation)
      continue;
    pixel.h = target.h;
    pixel.s *= target.s;
    pixel.v *= target.v;
    float r, g, b;
    FromHsv(pixel, r, g, b);
    rgba[0] = Byte(r);
    rgba[1] = Byte(g);
    rgba[2] = Byte(b);
  }
}

} // namespace LogoTint
