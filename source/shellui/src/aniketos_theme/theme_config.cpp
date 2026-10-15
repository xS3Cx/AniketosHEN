#include "aniketos_theme/theme_config.hpp"

#include <onion/settings.hpp>

#include <algorithm>

namespace AniketosTheme {
namespace {

float channel(int value) {
  return static_cast<float>(std::clamp(value, 0, 255)) / 255.0f;
}

void set_rgb(int &r, int &g, int &b, int nr, int ng, int nb) {
  r = nr;
  g = ng;
  b = nb;
}

struct PaletteColor { int r; int g; int b; };

constexpr PaletteColor kBackgroundColors[] = {
    {5, 9, 22}, {18, 20, 25}, {7, 18, 54}, {29, 8, 45}, {208, 214, 224},
    {3, 22, 29}, {43, 15, 4}, {5, 30, 18}, {38, 9, 28}, {42, 6, 12},
};
constexpr PaletteColor kGlowColors[] = {
    {232, 240, 255}, {0, 230, 255}, {45, 92, 255}, {185, 76, 255},
    {255, 44, 155}, {255, 38, 60}, {255, 148, 22}, {0, 225, 120},
};

int closest_color(int r, int g, int b, const PaletteColor *colors,
                  int count) {
  int best = 0;
  int best_distance = 0x7fffffff;
  for (int i = 0; i < count; ++i) {
    const int dr = r - colors[i].r;
    const int dg = g - colors[i].g;
    const int db = b - colors[i].b;
    const int distance = dr * dr + dg * dg + db * db;
    if (distance < best_distance) {
      best = i;
      best_distance = distance;
    }
  }
  return best;
}

} // namespace

const char *PresetName(Preset preset) {
  switch (preset) {
  case Preset::Original: return "Original PS5";
  case Preset::AniketosBlue: return "Aniketos Blue";
  case Preset::ElectricCyan: return "Electric Cyan";
  case Preset::DeepSapphire: return "Deep Sapphire";
  case Preset::Crimson: return "Crimson";
  case Preset::PurpleNeon: return "Purple Neon";
  case Preset::Emerald: return "Emerald";
  case Preset::Monochrome: return "Monochrome";
  case Preset::OledBlack: return "OLED Black";
  case Preset::Custom: return "Custom";
  case Preset::Pink: return "Pink";
  case Preset::Rainbow: return "Rainbow";
  case Preset::Count: break;
  }
  return "Custom";
}

const char *AnimationModeName(AnimationMode mode) {
  switch (mode) {
  case AnimationMode::Static: return "Static";
  case AnimationMode::Rainbow: return "Rainbow";
  case AnimationMode::Count: break;
  }
  return "Static";
}

const char *OutlinePresetName(OutlinePreset preset) {
  switch (preset) {
  case OutlinePreset::Red: return "Red";
  case OutlinePreset::Orange: return "Orange";
  case OutlinePreset::Gold: return "Gold";
  case OutlinePreset::Emerald: return "Emerald";
  case OutlinePreset::Cyan: return "Cyan";
  case OutlinePreset::Blue: return "Blue";
  case OutlinePreset::Purple: return "Purple";
  case OutlinePreset::Pink: return "Pink";
  case OutlinePreset::White: return "White";
  case OutlinePreset::Rainbow: return "Rainbow";
  case OutlinePreset::Custom: return "Custom";
  case OutlinePreset::Count: break;
  }
  return "Custom";
}

ThemeConfig ConfigFromSettings(const onion::Settings &settings) {
  ThemeConfig config{};
  config.enabled = settings.shell_background_enabled &&
                   settings.shell_customization_enabled;
  config.preset = static_cast<Preset>(std::clamp(
      settings.shell_background_preset, 0, PresetCount() - 1));
  config.base = {channel(settings.shell_background_base_r),
                 channel(settings.shell_background_base_g),
                 channel(settings.shell_background_base_b)};
  config.glow = {channel(settings.shell_background_glow_r),
                 channel(settings.shell_background_glow_g),
                 channel(settings.shell_background_glow_b)};
  config.glow_intensity =
      static_cast<float>(std::clamp(settings.shell_background_glow_intensity,
                                   0, 200)) /
      100.0f;
  config.animation = static_cast<AnimationMode>(std::clamp(
      settings.shell_background_animation, 0, AnimationModeCount() - 1));
  config.animation_speed = static_cast<float>(std::clamp(
      settings.shell_background_animation_speed, 25, 400)) /
      100.0f;
  config.image_enabled = settings.shell_background_image_enabled &&
                         settings.shell_customization_enabled;
  config.image_path = settings.shell_background_image_path;
  config.image_transition =
      static_cast<NativeShellBackground::ImageTransition>(std::clamp(
          settings.shell_background_image_transition, 0,
          NativeShellBackground::ImageTransitionCount() - 1));
  config.particles = static_cast<NativeShellBackground::ParticleMode>(
      std::clamp(settings.shell_customization_enabled
                     ? settings.shell_background_particles
                     : static_cast<int>(
                           NativeShellBackground::ParticleMode::Default),
                 0, NativeShellBackground::ParticleModeCount() - 1));
  return config;
}

ThemeConfig OutlineConfigFromSettings(const onion::Settings &settings) {
  ThemeConfig config{};
  config.enabled = settings.shell_tiles_outline_enabled;
  config.preset = Preset::Custom;
  config.base = {channel(settings.shell_tiles_outline_r),
                 channel(settings.shell_tiles_outline_g),
                 channel(settings.shell_tiles_outline_b)};
  config.glow = config.base;
  config.animation = static_cast<AnimationMode>(std::clamp(
      settings.shell_tiles_outline_animation, 0, AnimationModeCount() - 1));
  config.animation_speed = static_cast<float>(std::clamp(
      settings.shell_tiles_outline_animation_speed, 25, 400)) / 100.0f;
  return config;
}

int BackgroundColorIndex(const onion::Settings &settings) {
  return closest_color(settings.shell_background_base_r,
                       settings.shell_background_base_g,
                       settings.shell_background_base_b, kBackgroundColors,
                       BackgroundColorCount());
}

int CurrentGlowColor(const onion::Settings &settings) {
  return closest_color(settings.shell_background_glow_r,
                       settings.shell_background_glow_g,
                       settings.shell_background_glow_b, kGlowColors,
                       GlowColorCount());
}

void ApplyBackgroundColor(onion::Settings &settings, BackgroundColor color) {
  const PaletteColor value =
      kBackgroundColors[std::clamp(static_cast<int>(color), 0,
                                   BackgroundColorCount() - 1)];
  set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
          settings.shell_background_base_b, value.r, value.g, value.b);
}

void ApplyGlowColor(onion::Settings &settings, GlowColor color) {
  const PaletteColor value =
      kGlowColors[std::clamp(static_cast<int>(color), 0, GlowColorCount() - 1)];
  set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
          settings.shell_background_glow_b, value.r, value.g, value.b);
}

void ApplyOutlinePresetToSettings(onion::Settings &settings,
                                  OutlinePreset preset) {
  settings.shell_tiles_outline_preset = static_cast<int>(preset);
  settings.shell_tiles_outline_enabled = true;
  settings.shell_tiles_outline_animation =
      static_cast<int>(AnimationMode::Static);
  switch (preset) {
  case OutlinePreset::Red:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 255, 0, 0);
    break;
  case OutlinePreset::Orange:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 255, 96, 0);
    break;
  case OutlinePreset::Gold:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 255, 196, 0);
    break;
  case OutlinePreset::Emerald:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 0, 220, 116);
    break;
  case OutlinePreset::Cyan:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 0, 230, 255);
    break;
  case OutlinePreset::Blue:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 20, 65, 255);
    break;
  case OutlinePreset::Purple:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 174, 42, 255);
    break;
  case OutlinePreset::Pink:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 255, 38, 148);
    break;
  case OutlinePreset::White:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 235, 235, 240);
    break;
  case OutlinePreset::Rainbow:
    set_rgb(settings.shell_tiles_outline_r, settings.shell_tiles_outline_g,
            settings.shell_tiles_outline_b, 255, 0, 0);
    settings.shell_tiles_outline_animation =
        static_cast<int>(AnimationMode::Rainbow);
    break;
  case OutlinePreset::Custom:
  case OutlinePreset::Count:
    break;
  }
}

void ApplyPresetToSettings(onion::Settings &settings, Preset preset) {
  const Preset previous = static_cast<Preset>(std::clamp(
      settings.shell_background_preset, 0, PresetCount() - 1));
  settings.shell_background_preset = static_cast<int>(preset);
  settings.shell_background_enabled = preset != Preset::Original;
  settings.shell_background_glow_intensity = 100;
  if (previous == Preset::Rainbow && preset != Preset::Rainbow)
    settings.shell_background_animation =
        static_cast<int>(AnimationMode::Static);
  switch (preset) {
  case Preset::Original:
    settings.shell_background_animation =
        static_cast<int>(AnimationMode::Static);
    settings.shell_background_image_enabled = false;
    settings.shell_background_force_home = false;
    settings.shell_background_wave_enabled = false;
    settings.shell_background_particles =
        static_cast<int>(NativeShellBackground::ParticleMode::Default);
    return;
  case Preset::AniketosBlue:
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 5, 9, 22);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 0, 126, 255);
    break;
  case Preset::ElectricCyan:
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 3, 14, 19);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 0, 230, 255);
    break;
  case Preset::DeepSapphire:
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 4, 7, 22);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 20, 65, 255);
    break;
  case Preset::Crimson:
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 20, 4, 8);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 255, 24, 54);
    break;
  case Preset::PurpleNeon:
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 15, 4, 23);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 174, 42, 255);
    break;
  case Preset::Emerald:
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 3, 16, 10);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 0, 220, 116);
    break;
  case Preset::Monochrome:
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 11, 12, 14);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 220, 225, 235);
    break;
  case Preset::OledBlack:
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 0, 0, 0);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 0, 0, 0);
    settings.shell_background_glow_intensity = 0;
    break;
  case Preset::Custom:
    break;
  case Preset::Pink:
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 10, 10, 13);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 255, 38, 148);
    settings.shell_background_animation =
        static_cast<int>(AnimationMode::Static);
    settings.shell_background_particles =
        static_cast<int>(NativeShellBackground::ParticleMode::Spread);
    break;
  case Preset::Rainbow:
    /* Keep the readable PS5 backdrop neutral.  Only LightMat (the native
     * particle/light layer) sweeps through the spectrum in animation.cpp. */
    set_rgb(settings.shell_background_base_r, settings.shell_background_base_g,
            settings.shell_background_base_b, 10, 11, 13);
    set_rgb(settings.shell_background_glow_r, settings.shell_background_glow_g,
            settings.shell_background_glow_b, 235, 235, 240);
    settings.shell_background_animation =
        static_cast<int>(AnimationMode::Rainbow);
    settings.shell_background_particles =
        static_cast<int>(NativeShellBackground::ParticleMode::Spread);
    break;
  case Preset::Count:
    break;
  }
}

} // namespace AniketosTheme
