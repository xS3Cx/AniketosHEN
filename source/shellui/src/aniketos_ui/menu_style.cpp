#include "aniketos_ui/menu_style.hpp"

#include <onion/settings.hpp>

#include <algorithm>
#include <cmath>

namespace AniketosUI {
namespace {

Color hsv(float hue, float saturation, float value, float alpha) {
  while (hue >= 360.0f)
    hue -= 360.0f;
  while (hue < 0.0f)
    hue += 360.0f;
  const float chroma = value * saturation;
  const float part = hue / 60.0f;
  const float x = chroma * (1.0f - std::fabs(std::fmod(part, 2.0f) - 1.0f));
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  if (part < 1.0f) {
    r = chroma; g = x;
  } else if (part < 2.0f) {
    r = x; g = chroma;
  } else if (part < 3.0f) {
    g = chroma; b = x;
  } else if (part < 4.0f) {
    g = x; b = chroma;
  } else if (part < 5.0f) {
    r = x; b = chroma;
  } else {
    r = chroma; b = x;
  }
  const float m = value - chroma;
  return {r + m, g + m, b + m, alpha};
}

} // namespace

MenuStyle StyleFromShellSettings(const onion::Settings &settings) {
  MenuStyle style{};
  style.x = settings.vsh_menu_x;
  style.y = settings.vsh_menu_y;
  style.width = settings.vsh_menu_width;
  style.border = settings.vsh_border;
  style.text_size = settings.vsh_text_size;
  style.scale = settings.vsh_menu_scale;
  style.background_opacity = settings.vsh_background_opacity;
  style.border_enabled = settings.vsh_border_enabled;
  style.dual_color = settings.vsh_dual_color;
  style.gradient_direction = settings.vsh_gradient_direction;
  style.color_animation = settings.vsh_color_animation;
  style.color_animation_speed = settings.vsh_color_animation_speed;
  style.left = {settings.vsh_left_r, settings.vsh_left_g,
                settings.vsh_left_b, 1.0f};
  style.right = {settings.vsh_right_r, settings.vsh_right_g,
                 settings.vsh_right_b, 1.0f};
  return style;
}

float Scale(const MenuStyle &style) {
  return std::clamp(style.scale, 0.60f, 1.50f);
}

float TextSize(const MenuStyle &style) {
  return std::max(11.0f, std::round(style.text_size * Scale(style)));
}

float Width(const MenuStyle &style) {
  return std::round(std::clamp(style.width, 300.0f, 900.0f) * Scale(style));
}

float Border(const MenuStyle &style) {
  return style.border_enabled
             ? std::max(1.0f, std::round(std::clamp(style.border, 0.0f, 15.0f) *
                                         Scale(style)))
             : 0.0f;
}

AkariLayout CalculateAkariLayout(const MenuStyle &style, float viewport_width,
                                 float viewport_height,
                                 std::size_t option_count,
                                 std::size_t max_visible) {
  AkariLayout out{};
  out.width = Width(style);
  out.border = Border(style);
  out.outer_width = out.width + out.border * 2.0f;
  out.row = TextSize(style);
  out.padding = out.row * 0.5f;
  out.visible = std::min(max_visible, option_count);
  out.body_height =
      std::max(out.row, out.row * static_cast<float>(out.visible));
  out.header_y = out.border;
  out.header_bottom = out.header_y + out.row;
  out.body_y = out.header_bottom + out.border;
  out.body_bottom = out.body_y + out.body_height;
  // Akari's footer is a separate framed block below the option body. Keep a
  // small visible gap between both complete frames instead of sharing the
  // body's bottom line.
  out.footer_gap = std::round(4.0f * Scale(style));
  out.footer_y =
      out.body_bottom + out.border + out.footer_gap + out.border;
  out.total_height = out.footer_y + out.row + out.border;
  out.x = std::clamp(style.x - out.outer_width * 0.5f, 0.0f,
                     std::max(0.0f, viewport_width - out.outer_width));
  out.y = std::clamp(style.y - (out.body_y + out.body_height * 0.5f), 0.0f,
                     std::max(0.0f, viewport_height - out.total_height));
  return out;
}

Color AnimatedColor(const MenuStyle &style, bool right, std::uint64_t frame_ms,
                    float alpha) {
  Color base = right ? style.right : style.left;
  base.a = alpha;
  const int mode = std::clamp(style.color_animation, 0, 4);
  if (!mode)
    return base;
  const float speed =
      std::clamp(style.color_animation_speed, 25, 400) / 100.0f;
  const float phase = static_cast<float>(frame_ms % 120000u) * 0.001f * speed;
  switch (mode) {
  case 1:
    return hsv(phase * 70.0f, 1.0f, 1.0f, alpha);
  case 2:
    return hsv(phase * 70.0f + (right ? 120.0f : 0.0f), 1.0f, 1.0f,
               alpha);
  case 3: {
    const float pulse = 0.62f + 0.38f *
        (std::sin(phase * 3.0f) * 0.5f + 0.5f);
    return {base.r * pulse, base.g * pulse, base.b * pulse, alpha};
  }
  case 4:
    return hsv((right ? 238.0f : 188.0f) +
                   std::sin(phase * 1.8f + (right ? 1.2f : 0.0f)) * 16.0f,
               0.90f, 1.0f, alpha);
  default:
    return base;
  }
}

float GradientT(const MenuStyle &style, float t) {
  return style.gradient_direction == 1 ? 1.0f - t : t;
}

Color Mix(Color left, Color right, float t, float alpha) {
  return {left.r + (right.r - left.r) * t,
          left.g + (right.g - left.g) * t,
          left.b + (right.b - left.b) * t, alpha};
}

Color GradientEdgeColor(const MenuStyle &style, bool right_edge,
                        std::uint64_t frame_ms, float alpha) {
  const Color left = AnimatedColor(style, false, frame_ms, alpha);
  if (!style.dual_color)
    return left;
  const Color right = AnimatedColor(style, true, frame_ms, alpha);
  return Mix(left, right, GradientT(style, right_edge ? 1.0f : 0.0f),
             alpha);
}

} // namespace AniketosUI
