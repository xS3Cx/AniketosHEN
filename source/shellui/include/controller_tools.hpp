/* Copyright (C) 2026 AniketosHEN — controller toolbox helpers */
#pragma once

#include <cstdint>

enum class ControllerAnimation : uint8_t {
  Off,
  Breathe,
  Pulse,
  Rainbow,
  Spectrum,
  NeonWave,
};

bool controller_set_lightbar(uint8_t red, uint8_t green, uint8_t blue);
bool controller_reset_lightbar();
void controller_set_custom_channel(bool color_b, uint8_t channel, uint8_t value);
uint8_t controller_get_custom_channel(bool color_b, uint8_t channel);
bool controller_apply_custom_color(bool color_b);
void controller_start_animation(ControllerAnimation animation);
void controller_stop_animation();
void controller_handle_buttons(uint32_t buttons_down);

/* Rumble tests from the Shell Menu. The strength is the
   controller_vibration_strength setting (percent). */
enum class VibrationPattern : uint8_t { Short, Long, Heartbeat, Light, Heavy, Wave };
bool controller_vibrate(VibrationPattern pattern);
void controller_stop_vibration();

/* The white player LEDs under the touch pad and the brightness of the
   controller's lights. They are the system's own per-device settings,
   reached through the controller's device id (read at run time, so no
   firmware-specific value is involved); restore hands both back to what
   the signed-in user has set. */
/* Brightness is shared by the light bar and the player LEDs in hardware,
   so the animations move only the player LEDs. */
enum class PlayerLedAnimation : uint8_t { Off, Chase, Countdown, Blink };
bool controller_set_player_leds(int player);     /* 1..4 */
bool controller_set_light_brightness(int level); /* 0 bright, 1 medium, 2 dim */
void controller_start_player_led_animation(PlayerLedAnimation animation);
bool controller_restore_player_leds();
