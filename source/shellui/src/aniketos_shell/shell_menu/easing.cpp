#include "aniketos_shell/shell_menu/easing.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace AniketosShell::Easing {
namespace {

constexpr float kPi = 3.1415926545f;

float in_sine(float t) { return std::sin(1.5707963f * t); }
float out_sine(float t) { return 1.0f + std::sin(1.5707963f * (t - 1.0f)); }
float in_out_sine(float t) {
  return 0.5f * (1.0f + std::sin(kPi * (t - 0.5f)));
}
float in_quad(float t) { return t * t; }
float out_quad(float t) { return t * (2.0f - t); }
float in_out_quad(float t) {
  return t < 0.5f ? 2.0f * t * t : t * (4.0f - 2.0f * t) - 1.0f;
}
float in_cubic(float t) { return t * t * t; }
float out_cubic(float t) {
  t -= 1.0f;
  return 1.0f + t * t * t;
}
float in_out_cubic(float t) {
  return t < 0.5f ? 4.0f * t * t * t
                  : (t - 1.0f) * (2.0f * t - 2.0f) *
                            (2.0f * t - 2.0f) +
                        1.0f;
}
float in_quart(float t) { return t * t * t * t; }
float out_quart(float t) {
  t -= 1.0f;
  return 1.0f - t * t * t * t;
}
float in_out_quart(float t) {
  if (t < 0.5f)
    return 8.0f * t * t * t * t;
  t -= 1.0f;
  return 1.0f - 8.0f * t * t * t * t;
}
float in_quint(float t) { return t * t * t * t * t; }
float out_quint(float t) {
  t -= 1.0f;
  return 1.0f + t * t * t * t * t;
}
float in_out_quint(float t) {
  if (t < 0.5f)
    return 16.0f * t * t * t * t * t;
  t -= 1.0f;
  return 1.0f + 16.0f * t * t * t * t * t;
}
float in_expo(float t) { return (std::pow(2.0f, 8.0f * t) - 1.0f) / 255.0f; }
float out_expo(float t) { return 1.0f - std::pow(2.0f, -8.0f * t); }
float in_out_expo(float t) {
  return t < 0.5f ? (std::pow(2.0f, 16.0f * t) - 1.0f) / 510.0f
                  : 1.0f - 0.5f * std::pow(2.0f, -16.0f * (t - 0.5f));
}
float in_circ(float t) { return 1.0f - std::sqrt(1.0f - t); }
float out_circ(float t) { return std::sqrt(t); }
float in_out_circ(float t) {
  return t < 0.5f ? (1.0f - std::sqrt(1.0f - 2.0f * t)) * 0.5f
                  : (1.0f + std::sqrt(2.0f * t - 1.0f)) * 0.5f;
}
float in_back(float t) { return t * t * (2.70158f * t - 1.70158f); }
float out_back(float t) {
  t -= 1.0f;
  return 1.0f + t * t * (2.70158f * t + 1.70158f);
}
float in_out_back(float t) {
  if (t < 0.5f)
    return t * t * (7.0f * t - 2.5f) * 2.0f;
  t -= 1.0f;
  return 1.0f + t * t * 2.0f * (7.0f * t + 2.5f);
}

using Fn = float (*)(float);
constexpr Fn kFunctions[] = {
    in_sine,      out_sine,      in_out_sine, in_quad,
    out_quad,     in_out_quad,   in_cubic,    out_cubic,
    in_out_cubic, in_quart,      out_quart,   in_out_quart,
    in_quint,     out_quint,     in_out_quint, in_expo,
    out_expo,     in_out_expo,   in_circ,     out_circ,
    in_out_circ,  in_back,       out_back,    in_out_back,
};

} // namespace

float Apply(int type, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  if (type <= 0 || type > static_cast<int>(std::size(kFunctions)))
    return t;
  return kFunctions[type - 1](t);
}

} // namespace AniketosShell::Easing
