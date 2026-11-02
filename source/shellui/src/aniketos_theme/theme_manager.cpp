#include "aniketos_theme/theme_manager.hpp"
#include "aniketos_theme/customization.hpp"

#include "external_symbols.hpp"
#include "aniketos_theme/animation.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/shell_background.hpp"
#include "aniketos_theme/theme_config.hpp"
#include "aniketos_theme/clock/shell_clock.hpp"
#include "aniketos_ui/monotonic_clock.hpp"

#include <onion/log.h>
#include <onion/settings.hpp>

#include <cstdint>
#include <string>

namespace AniketosTheme {
namespace {

constexpr unsigned kInitialRetryFrames = 60;
constexpr unsigned kContextPollFrames = 120;
constexpr uint64_t kAnimationFrameMs = 50; // 20 Hz: smooth without Mono spam.
unsigned s_frames = kInitialRetryFrames;
uintptr_t s_applied_layer = 0;
bool s_apply_requested = false;
bool s_restore_requested = false;
bool s_custom_was_applied = false;
AnimationMode s_animation_mode = AnimationMode::Static;
uint64_t s_animation_started_ms = 0;
uint64_t s_last_animation_frame_ms = 0;
int s_applied_particle_mode =
    static_cast<int>(NativeShellBackground::ParticleMode::Default);
bool s_image_was_applied = false;
std::string s_applied_image_path;
int s_applied_image_transition = -1;
bool s_force_home_initialized = false;
bool s_applied_force_home = false;
bool s_wave_was_applied = false;
/* Home was on screen at the last tick (its clock was showing), and until
   when its returning title picture is cleared. */
bool s_home_was_visible = false;
uint64_t s_force_home_until_ms = 0;
uint64_t s_last_home_clear_ms = 0;
constexpr uint64_t kHomeReturnClearMs = 2000;

bool wave_setting() {
  return CustomizationOn() && g_settings.shell_background_wave_enabled;
}

bool force_home_setting() {
  return CustomizationOn() && g_settings.shell_background_force_home;
}

void forget_applied_image() {
  s_image_was_applied = false;
  s_applied_image_path.clear();
  s_applied_image_transition = -1;
}

void apply_requested_state(const ThemeConfig &config, uint64_t now_ms,
                           bool animation_frame) {
  if (!animation_frame)
    LOG_INFO("[ANITHEME:APPLY] enabled=%d image=%d path='%s' force_home=%d "
             "particles=%d wave=%d",
             config.enabled ? 1 : 0, config.image_enabled ? 1 : 0,
             config.image_path.c_str(), force_home_setting() ? 1 : 0,
             static_cast<int>(config.particles), wave_setting() ? 1 : 0);
  if (!config.enabled) {
    if ((s_restore_requested || s_custom_was_applied) &&
        NativeShellBackground::RestoreOriginal())
      s_custom_was_applied = false;
  } else {
    RGB base = config.base;
    RGB glow = config.glow;
    if (GlobalTheme::Active()) {
      // Global customization is a single clocked source shared with the
      // menu, focus outline, clock, controller, and Aura monitor.
      glow = GlobalTheme::Color(now_ms);
      base = {glow.r * 0.067f, glow.g * 0.067f, glow.b * 0.067f};
    } else if (config.animation != AnimationMode::Static) {
      if (!s_animation_started_ms)
        s_animation_started_ms = now_ms;
      const float elapsed =
          static_cast<float>(now_ms - s_animation_started_ms) / 1000.0f;
      const AnimationFrame frame = EvaluateAnimation(config, elapsed);
      base = frame.base;
      glow = frame.glow;
    }
    if (!NativeShellBackground::Apply(base, glow, config.glow_intensity,
                                      !animation_frame))
      return;
    s_custom_was_applied = true;
  }
  if (animation_frame)
    return;

  const auto requested_particles = wave_setting()
                                       ? NativeShellBackground::ParticleMode::Off
                                       : config.particles;
  const int particle_mode = static_cast<int>(requested_particles);
  if (particle_mode != s_applied_particle_mode) {
    if (!NativeShellBackground::SetParticles(requested_particles))
      return;
    s_applied_particle_mode = particle_mode;
  }

  const int transition = static_cast<int>(config.image_transition);
  if (config.image_enabled) {
    if (!s_image_was_applied || config.image_path != s_applied_image_path ||
        transition != s_applied_image_transition) {
      if (!NativeShellBackground::ApplyImage(config.image_path.c_str(),
                                             config.image_transition))
        return;
      s_image_was_applied = true;
      s_applied_image_path = config.image_path;
      s_applied_image_transition = transition;
    }
  } else if (!wave_setting()) {
    NativeShellBackground::DisableImageOverride();
    /* A ShellUI layer replacement can invalidate our bookkeeping while the
     * native mapper still displays the old texture.  A requested restore
     * therefore always emits Sony's empty fade transition. */
    if (s_image_was_applied || s_restore_requested) {
      if (!NativeShellBackground::ClearImage())
        return;
      forget_applied_image();
    }
  }

  s_restore_requested = false;
  s_apply_requested = false;
  s_applied_layer = NativeShellBackground::LayerIdentity();
}

} // namespace

void Tick() {
  if (!Root_Domain)
    return;
  const uint64_t now_ms = AniketosUI::MonotonicMs();
  const ThemeConfig config = ConfigFromSettings(g_settings);
  if (config.animation != s_animation_mode) {
    s_animation_mode = config.animation;
    s_animation_started_ms = now_ms;
    s_last_animation_frame_ms = 0;
    if (config.enabled && config.animation != AnimationMode::Static)
      LOG_INFO("[ANITHEME:ANIM] started mode=%s speed=%.2fx",
               AnimationModeName(config.animation), config.animation_speed);
  }

  const bool animation_active = config.enabled &&
      (config.animation != AnimationMode::Static || GlobalTheme::Animated());
  const bool animation_due = animation_active &&
      (!s_last_animation_frame_ms ||
       now_ms - s_last_animation_frame_ms >= kAnimationFrameMs);
  const bool wave_active = wave_setting();
  const bool force_home = force_home_setting() || wave_active;
  /* Home coming back into view (from Settings, a game, the Control Center)
     puts the focused title's picture up again without the transition the
     hook holds back, so with Home's background forced it is cleared again. */
  float home_opacity = 0.0f;
  const bool home_visible =
      ShellClock::GetNativeClockOpacity(&home_opacity) && home_opacity > 0.5f;
  if (home_visible && !s_home_was_visible) {
    /* Home brings the picture back a moment after it shows, so it is
       cleared for a little while rather than once. */
    s_force_home_until_ms = now_ms + kHomeReturnClearMs;
    if (force_home && s_force_home_initialized)
      LOG_INFO("[ANITHEME:IMAGE] Home back in view; forcing its background");
  }
  s_home_was_visible = home_visible;
  /* every quarter second within that while, not every frame */
  const bool home_clear_due = force_home && home_visible &&
                              now_ms < s_force_home_until_ms &&
                              now_ms - s_last_home_clear_ms >= 250;
  const bool context_due = ++s_frames >=
      (NativeShellBackground::IsResolved() ? kContextPollFrames
                                           : kInitialRetryFrames);
  if (!context_due && !animation_due && !s_apply_requested &&
      !s_restore_requested && !home_clear_due)
    return;
  if (context_due)
    s_frames = 0;
  if (!NativeShellBackground::Resolve())
    return;
  const uintptr_t layer = NativeShellBackground::LayerIdentity();
  const bool context_changed = layer != s_applied_layer;
  // A force-home transition can synchronously re-enter Sony's renderer and
  // replace its layer. Keep a one-shot apply pending before entering it so the
  // image/particle state is retried on the following UI frame even if this
  // invocation observes that transition in progress.
  if (context_changed)
    s_apply_requested = true;
  if (!s_force_home_initialized || force_home != s_applied_force_home ||
      (context_changed && force_home) || home_clear_due) {
    if (home_clear_due)
      s_last_home_clear_ms = now_ms;
    if (!NativeShellBackground::SetForceHomeBackground(force_home))
      return;
    s_applied_force_home = force_home;
    s_force_home_initialized = true;
  }
  if (!wave_active && s_wave_was_applied) {
    NativeShellBackground::RestoreStockWave();
    s_wave_was_applied = false;
    s_applied_particle_mode = -1;
    s_apply_requested = true;
  }
  if (wave_active &&
      (!s_wave_was_applied || context_changed || s_apply_requested))
    s_wave_was_applied =
        NativeShellBackground::ShowStockWave() || s_wave_was_applied;
  const bool custom_background_requested =
      config.enabled || config.image_enabled ||
      config.particles != NativeShellBackground::ParticleMode::Default ||
      force_home || wave_active;
  if (animation_due)
    s_last_animation_frame_ms = now_ms;
  /* First: an animation frame skips the particles and the image, and a full
     apply draws that frame as well. */
  if (s_apply_requested || s_restore_requested ||
      (context_changed && custom_background_requested)) {
    if (context_changed) {
      s_applied_particle_mode = config.particles ==
              NativeShellBackground::ParticleMode::Default
          ? static_cast<int>(NativeShellBackground::ParticleMode::Default)
          : -1;
      forget_applied_image();
    }
    apply_requested_state(config, now_ms, false);
  } else if (animation_due) {
    apply_requested_state(config, now_ms, true);
  } else if (context_changed) {
    s_applied_layer = layer;
  }
}

void RequestApply() { s_apply_requested = true; }

void RequestImageReload() {
  forget_applied_image();
  s_apply_requested = true;
}

void RequestRestore() {
  s_restore_requested = true;
  s_apply_requested = true;
}

} // namespace AniketosTheme
