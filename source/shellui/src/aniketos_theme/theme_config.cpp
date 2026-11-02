#include "aniketos_theme/theme_config.hpp"

#include <onion/settings.hpp>

#include <algorithm>

namespace AniketosTheme {
namespace {

float channel(int value) {
  return static_cast<float>(std::clamp(value, 0, 255)) / 255.0f;
}

AnimationMode animation_mode(int value) {
  return static_cast<AnimationMode>(
      std::clamp(value, 0, AnimationModeCount() - 1));
}

float animation_speed(int percent) {
  return static_cast<float>(std::clamp(percent, 25, 400)) / 100.0f;
}

struct Rgb8 {
  int r, g, b;
};

void set_background_colors(onion::Settings &settings, Rgb8 base, Rgb8 glow) {
  settings.shell_background_base_r = base.r;
  settings.shell_background_base_g = base.g;
  settings.shell_background_base_b = base.b;
  settings.shell_background_glow_r = glow.r;
  settings.shell_background_glow_g = glow.g;
  settings.shell_background_glow_b = glow.b;
}

} // namespace

const char *AnimationModeName(AnimationMode mode) {
  switch (mode) {
  case AnimationMode::Static: return "Static";
  case AnimationMode::Rainbow: return "Rainbow";
  case AnimationMode::Count: break;
  }
  return "Static";
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
  config.animation = animation_mode(settings.shell_background_animation);
  config.animation_speed =
      animation_speed(settings.shell_background_animation_speed);
  config.image_enabled = settings.shell_background_image_enabled &&
                         settings.shell_customization_enabled;
  config.image_path = settings.shell_background_image_path;
  config.image_transition =
      static_cast<NativeShellBackground::ImageTransition>(std::clamp(
          settings.shell_background_image_transition, 0,
          NativeShellBackground::ImageTransitionCount() - 1));
  config.particles =
      settings.shell_customization_enabled
          ? NativeShellBackground::ParticleModeFromSetting(
                settings.shell_background_particles)
          : NativeShellBackground::ParticleMode::Default;
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
  config.animation = animation_mode(settings.shell_tiles_outline_animation);
  config.animation_speed =
      animation_speed(settings.shell_tiles_outline_animation_speed);
  return config;
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
    break;
  case Preset::AniketosBlue:
    set_background_colors(settings, {5, 9, 22}, {0, 126, 255});
    break;
  case Preset::ElectricCyan:
    set_background_colors(settings, {3, 14, 19}, {0, 230, 255});
    break;
  case Preset::DeepSapphire:
    set_background_colors(settings, {4, 7, 22}, {20, 65, 255});
    break;
  case Preset::Crimson:
    set_background_colors(settings, {20, 4, 8}, {255, 24, 54});
    break;
  case Preset::PurpleNeon:
    set_background_colors(settings, {15, 4, 23}, {174, 42, 255});
    break;
  case Preset::Emerald:
    set_background_colors(settings, {3, 16, 10}, {0, 220, 116});
    break;
  case Preset::Monochrome:
    set_background_colors(settings, {11, 12, 14}, {220, 225, 235});
    break;
  case Preset::OledBlack:
    set_background_colors(settings, {0, 0, 0}, {0, 0, 0});
    settings.shell_background_glow_intensity = 0;
    break;
  case Preset::Custom:
    break;
  case Preset::Pink:
    set_background_colors(settings, {10, 10, 13}, {255, 38, 148});
    settings.shell_background_animation =
        static_cast<int>(AnimationMode::Static);
    settings.shell_background_particles =
        static_cast<int>(NativeShellBackground::ParticleMode::Default);
    break;
  case Preset::Rainbow:
    /* Keep the readable PS5 backdrop neutral.  Only LightMat (the native
     * particle/light layer) sweeps through the spectrum in animation.cpp. */
    set_background_colors(settings, {10, 11, 13}, {235, 235, 240});
    settings.shell_background_animation =
        static_cast<int>(AnimationMode::Rainbow);
    settings.shell_background_particles =
        static_cast<int>(NativeShellBackground::ParticleMode::Default);
    break;
  case Preset::Count:
    break;
  }
}

} // namespace AniketosTheme
