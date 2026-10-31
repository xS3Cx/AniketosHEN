#pragma once

#include <cstddef>
#include <cstdint>

namespace onion { struct Settings; }

namespace AniketosUI {

struct Color {
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
};

// Renderer-independent presentation for the Shell menu.
struct MenuStyle {
  float x = 960.0f;
  float y = 540.0f;
  float width = 450.0f;
  float border = 4.0f;
  float text_size = 18.0f;
  float scale = 1.0f;
  float background_opacity = 0.75f;
  bool border_enabled = true;
  bool dual_color = true;
  int gradient_direction = 0;
  int color_animation = 0;
  int color_animation_speed = 100;
  Color left{0.0f, 0.494f, 1.0f, 1.0f};
  Color right{0.588f, 0.275f, 1.0f, 1.0f};
};

struct AkariLayout {
  float width = 0.0f;
  float border = 0.0f;
  float outer_width = 0.0f;
  float row = 0.0f;
  float font = 0.0f;
  float padding = 0.0f;
  float body_padding = 0.0f;
  float body_height = 0.0f;
  float header_y = 0.0f;
  float header_bottom = 0.0f;
  float body_y = 0.0f;
  float body_bottom = 0.0f;
  float footer_gap = 0.0f;
  float footer_y = 0.0f;
  float total_height = 0.0f;
  float x = 0.0f;
  float y = 0.0f;
  std::size_t visible = 0;
};

MenuStyle StyleFromShellSettings(const onion::Settings &settings);
float Scale(const MenuStyle &style);
float TextSize(const MenuStyle &style);
float Width(const MenuStyle &style);
float Border(const MenuStyle &style);
AkariLayout CalculateAkariLayout(const MenuStyle &style, float viewport_width,
                                 float viewport_height,
                                 std::size_t option_count,
                                 std::size_t max_visible);

Color AnimatedColor(const MenuStyle &style, bool right, std::uint64_t frame_ms,
                    float alpha = 1.0f);
float GradientT(const MenuStyle &style, float t);
Color Mix(Color left, Color right, float t, float alpha = 1.0f);
Color GradientEdgeColor(const MenuStyle &style, bool right_edge,
                        std::uint64_t frame_ms, float alpha = 1.0f);

} // namespace AniketosUI
