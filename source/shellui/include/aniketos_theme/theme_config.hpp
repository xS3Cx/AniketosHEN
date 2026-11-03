#pragma once

#include "aniketos_theme/color.hpp"
#include "aniketos_theme/shell_background.hpp"

namespace onion { struct Settings; }

namespace AniketosTheme {

enum class Preset : int {
  Original = 0,
  AniketosBlue,
  ElectricCyan,
  DeepSapphire,
  Crimson,
  PurpleNeon,
  Emerald,
  Monochrome,
  OledBlack,
  Custom,
  Pink,
  Rainbow,
  Count,
};

enum class AnimationMode : int {
  Static = 0,
  Rainbow,
  Count,
};

enum class OutlinePreset : int {
  Red = 0,
  Orange,
  Gold,
  Emerald,
  Cyan,
  Blue,
  Purple,
  Pink,
  White,
  Rainbow,
  Custom,
  Count,
};

struct ThemeConfig {
  bool enabled = false;
  Preset preset = Preset::Original;
  RGB base{};
  RGB glow{};
  float glow_intensity = 1.0f;
  AnimationMode animation = AnimationMode::Static;
  float animation_speed = 1.0f;
  bool image_enabled = false;
  /* Points into the settings: the config is built every frame, so it is not
     copied. */
  const char *image_path = "";
  NativeShellBackground::ImageTransition image_transition =
      NativeShellBackground::ImageTransition::Fade;
  NativeShellBackground::ParticleMode particles =
      NativeShellBackground::ParticleMode::Default;
};

constexpr int PresetCount() { return static_cast<int>(Preset::Count); }
constexpr int AnimationModeCount() {
  return static_cast<int>(AnimationMode::Count);
}
const char *AnimationModeName(AnimationMode mode);
ThemeConfig ConfigFromSettings(const onion::Settings &settings);
ThemeConfig OutlineConfigFromSettings(const onion::Settings &settings);
void ApplyPresetToSettings(onion::Settings &settings, Preset preset);

} // namespace AniketosTheme
