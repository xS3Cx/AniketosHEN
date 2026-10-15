#pragma once

#include "aniketos_theme/color.hpp"
#include "aniketos_theme/shell_background.hpp"

#include <string>

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

enum class BackgroundColor : int {
  Midnight = 0,
  Graphite,
  Blue,
  Purple,
  SoftWhite,
  Cyan,
  Orange,
  Lime,
  Pink,
  Crimson,
  Count,
};

enum class GlowColor : int {
  Ice = 0,
  Cyan,
  Blue,
  Purple,
  Pink,
  Crimson,
  Orange,
  Lime,
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
  std::string image_path;
  NativeShellBackground::ImageTransition image_transition =
      NativeShellBackground::ImageTransition::Fade;
  NativeShellBackground::ParticleMode particles =
      NativeShellBackground::ParticleMode::Default;
};

constexpr int PresetCount() { return static_cast<int>(Preset::Count); }
constexpr int AnimationModeCount() {
  return static_cast<int>(AnimationMode::Count);
}
constexpr int OutlinePresetCount() {
  return static_cast<int>(OutlinePreset::Count);
}
constexpr int BackgroundColorCount() {
  return static_cast<int>(BackgroundColor::Count);
}
constexpr int GlowColorCount() { return static_cast<int>(GlowColor::Count); }
const char *PresetName(Preset preset);
const char *AnimationModeName(AnimationMode mode);
const char *OutlinePresetName(OutlinePreset preset);
ThemeConfig ConfigFromSettings(const onion::Settings &settings);
ThemeConfig OutlineConfigFromSettings(const onion::Settings &settings);
void ApplyPresetToSettings(onion::Settings &settings, Preset preset);
void ApplyOutlinePresetToSettings(onion::Settings &settings,
                                  OutlinePreset preset);
int BackgroundColorIndex(const onion::Settings &settings);
int CurrentGlowColor(const onion::Settings &settings);
void ApplyBackgroundColor(onion::Settings &settings, BackgroundColor color);
void ApplyGlowColor(onion::Settings &settings, GlowColor color);

} // namespace AniketosTheme
