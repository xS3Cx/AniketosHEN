#pragma once

#include "aniketos_theme/color.hpp"

#include <cstdint>

namespace AniketosTheme::NativeShellBackground {

/* Sony's particle layer as it is, or none. (Its "bottom" and "spread"
   states looked the same as the default and were dropped.) */
enum class ParticleMode : int {
  Default = 0,
  Off,
  Count,
};

/* The setting as saved: 1 (and 3, Off before the other states went) is Off,
   anything else the default. */
constexpr ParticleMode ParticleModeFromSetting(int value) {
  return value == 1 || value == 3 ? ParticleMode::Off : ParticleMode::Default;
}

enum class ImageTransition : int {
  Ripple = 0,
  SlideLeft,
  SlideRight,
  Fade,
  RippleBack,
  Count,
};

// Resolves Sony's global BGLayerPlugin and captures the live LightMat and
// nullable BackgroundBasemat state before AniketosHEN changes either value.
bool Resolve();
bool IsResolved();
uintptr_t LayerIdentity();

// These call the real managed setters, which forward into the native AGC VSH
// renderer. No AniketosHEN PUI background widget is created.
bool Apply(const RGB &base, const RGB &glow, float glow_intensity,
           bool log_change = true);
bool SetParticles(ParticleMode mode);
// Turns on Sony's own ShowWave for the current layer, keeping the value it had.
bool ShowStockWave();
// Puts that value back and drops the image override.
void RestoreStockWave();
// False asks for a retry. A file that cannot be read is logged and counts as
// done: it stays unreadable, and asking every frame would flood the log.
bool ApplyImage(const char *path, ImageTransition transition);
// Suppresses Home/Media focused-title artwork while preserving launch,
// fade-to-black, boot and shutdown transitions.
bool SetForceHomeBackground(bool enabled);
// Stops the persistent transition hook immediately. This is deliberately
// separate from ClearImage so a UI toggle cannot race the next manager tick.
void DisableImageOverride();
bool ClearImage();
bool RestoreOriginal();

constexpr int ParticleModeCount() {
  return static_cast<int>(ParticleMode::Count);
}
constexpr int ImageTransitionCount() {
  return static_cast<int>(ImageTransition::Count);
}
const char *ParticleModeName(ParticleMode mode);
const char *ImageTransitionName(ImageTransition transition);

} // namespace AniketosTheme::NativeShellBackground
