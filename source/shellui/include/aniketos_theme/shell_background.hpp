#pragma once

#include "aniketos_theme/color.hpp"

#include <cstdint>

struct MonoObject;

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
// Returns Sony's already-loaded BGLayerPlugin instance. BGLayerPlugin is a
// native UI3.Scene, so callers can attach UI3 widgets without constructing or
// loading a new managed scene (which is not AOT-safe on the PS5 runtime).
MonoObject *CurrentUi3Scene();

// These call the real managed setters, which forward into the native AGC VSH
// renderer. No AniketosHEN PUI background widget is created.
bool Apply(const RGB &base, const RGB &glow, float glow_intensity,
           bool log_change = true);
bool SetParticles(ParticleMode mode);
// Generates the requested sine-wave field and uploads it to Sony's native
// double-buffered wallpaper texture. This is not an overlay above HomeUI.
bool ApplyProceduralWave(float time_seconds);
void DisableProceduralWave();
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
