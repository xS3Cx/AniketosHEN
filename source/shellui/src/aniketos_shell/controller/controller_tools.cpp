/* Copyright (C) 2026 AniketosHEN — controller toolbox helpers */
#include "aniketos_shell/controller/controller_tools.hpp"

#include "hooked_funcs.hpp"
#include "aniketos_shell/input/pad_reader.hpp"
#include <onion/platform.h>
#include <time.h>
#include <cstring>
#include <iterator>
#include <algorithm>

extern "C" int sceUserServiceGetForegroundUser(int *user_id);
extern "C" int scePadOpen(int user_id, int type, int index, const void *param);
extern "C" int scePadClose(int handle);

struct ScePadLightBarParam {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t reserved;
};

extern "C" int scePadSetLightBar(int handle, const ScePadLightBarParam *param);
/* motors[0] the large (strong, low) motor, motors[1] the small one */
extern "C" int scePadSetVibration(int handle, const uint8_t *motors);
/* Without this a DualSense ignores rumble sent from ShellUI (measured on
   13.42 consoles); kVibrationMode is the mode that lets it through. */
extern "C" int scePadSetVibrationMode(int handle, int mode);
/* The system's per-device calls take the controller's device id, not a
   handle; ShellUI's own Settings use them the same way. */
extern "C" int scePadGetDeviceId(int handle, void *device_id);
extern "C" int scePadSetUserNumber(uint32_t device_id, int number);
extern "C" int scePadSetLightBarBaseBrightness(uint32_t device_id, int level);
extern "C" int sceUserServiceGetUserNumber(int user_id, int *number);
extern "C" int sceUserServiceGetLightBarBaseBrightness(int user_id, int *level);
extern "C" int scePadResetLightBar(int handle);

namespace {

ControllerAnimation s_animation = ControllerAnimation::Off;
uint64_t s_last_animation_ms = 0;
uint64_t s_last_lightbar_log_ms = 0;
/* The light bar as the HEN last left it: our own colour (a static one or
   the current frame of an animation) or the system's. The player number
   also sets the bar's colour (blue, red, green, pink), so after the white
   LEDs change the bar is put back from this. */
bool s_lightbar_ours = false;
ScePadLightBarParam s_lightbar_last{0, 0, 0, 0};

int &custom_channel(bool color_b, uint8_t channel) {
  if (color_b) {
    switch (channel) {
    case 0: return g_settings.controller_custom_b_r;
    case 1: return g_settings.controller_custom_b_g;
    default: return g_settings.controller_custom_b_b;
    }
  }
  switch (channel) {
  case 0: return g_settings.controller_custom_a_r;
  case 1: return g_settings.controller_custom_a_g;
  default: return g_settings.controller_custom_a_b;
  }
}

struct PadLease {
  int handle{-1};
  bool borrowed{};
};

PadLease open_active_pad() {
  // The VSH shortcut reader already owns the foreground DualSense. Opening
  // the same (user,type,index) a second time returns BUSY on retail ShellUI,
  // which made every Toolbox light action silently do nothing.
  const int borrowed = AniketosShell::Input::ActivePadHandle();
  if (borrowed >= 0)
    return {borrowed, true};
  int user_id = -1;
  if (sceUserServiceGetForegroundUser(&user_id) != 0 || user_id < 0) {
    LOG_ERROR("Controller: no foreground user");
    return {};
  }
  const int handle = scePadOpen(user_id, 0, 0, nullptr);
  if (handle < 0)
    LOG_ERROR("Controller: scePadOpen failed: 0x%x", handle);
  return {handle, false};
}

uint64_t monotonic_millis() {
  timespec time {};
  clock_gettime(CLOCK_MONOTONIC, &time);
  return static_cast<uint64_t>(time.tv_sec) * 1000u +
         static_cast<uint64_t>(time.tv_nsec) / 1000000u;
}

uint8_t blend(uint8_t from, uint8_t to, uint8_t amount) {
  return static_cast<uint8_t>((static_cast<uint16_t>(from) * (255 - amount) +
                               static_cast<uint16_t>(to) * amount) /
                              255);
}

void rainbow(uint8_t hue, uint8_t& red, uint8_t& green, uint8_t& blue) {
  const uint8_t sector = hue / 43;
  const uint8_t offset = static_cast<uint8_t>((hue - sector * 43) * 6);
  const uint8_t rise = offset;
  const uint8_t fall = 255 - offset;
  switch (sector) {
  case 0: red = 255; green = rise; blue = 0; break;
  case 1: red = fall; green = 255; blue = 0; break;
  case 2: red = 0; green = 255; blue = rise; break;
  case 3: red = 0; green = fall; blue = 255; break;
  case 4: red = rise; green = 0; blue = 255; break;
  default: red = 255; green = 0; blue = fall; break;
  }
}

/* One step of a rumble pattern: motor levels (0..255 before the strength
   setting is applied) and how long they last. */
struct RumbleStep {
  uint8_t large;
  uint8_t small;
  uint16_t ms;
};

constexpr RumbleStep kShort[] = {{255, 255, 180}};
constexpr RumbleStep kLong[] = {{255, 255, 1200}};
constexpr RumbleStep kHeartbeat[] = {{255, 120, 110}, {0, 0, 110}, {200, 90, 110},
                                     {0, 0, 520}, {255, 120, 110}, {0, 0, 110},
                                     {200, 90, 110}};
constexpr RumbleStep kLight[] = {{0, 255, 700}};
constexpr RumbleStep kHeavy[] = {{255, 0, 700}};
constexpr RumbleStep kWave[] = {{60, 60, 120},  {120, 120, 120}, {190, 190, 120},
                                {255, 255, 160}, {190, 190, 120}, {120, 120, 120},
                                {60, 60, 120}};

const RumbleStep *s_rumble = nullptr;
size_t s_rumble_length = 0;
size_t s_rumble_index = 0;
uint64_t s_rumble_step_end_ms = 0;
int s_vibration_mode_handle = -1;
constexpr int kVibrationMode = 2;

bool send_rumble(uint8_t large, uint8_t small) {
  const PadLease pad = open_active_pad();
  if (pad.handle < 0)
    return false;
  /* set on every start: the system may change it while we are not looking */
  if (large || small || s_vibration_mode_handle != pad.handle) {
    const int mode = scePadSetVibrationMode(pad.handle, kVibrationMode);
    if (mode != 0)
      LOG_ERROR("Controller: scePadSetVibrationMode failed: 0x%x", mode);
    s_vibration_mode_handle = pad.borrowed ? pad.handle : -1;
  }
  const int strength = std::clamp(g_settings.controller_vibration_strength, 0, 100);
  const uint8_t motors[2] = {static_cast<uint8_t>(large * strength / 100),
                             static_cast<uint8_t>(small * strength / 100)};
  const int result = scePadSetVibration(pad.handle, motors);
  if (!pad.borrowed)
    (void)scePadClose(pad.handle);
  if (result != 0)
    LOG_ERROR("Controller: scePadSetVibration failed: 0x%x", result);
  return result == 0;
}

void tick_rumble() {
  if (!s_rumble)
    return;
  const uint64_t now = monotonic_millis();
  if (now < s_rumble_step_end_ms)
    return;
  if (++s_rumble_index >= s_rumble_length) {
    s_rumble = nullptr;
    (void)send_rumble(0, 0);
    return;
  }
  const RumbleStep &step = s_rumble[s_rumble_index];
  s_rumble_step_end_ms = now + step.ms;
  (void)send_rumble(step.large, step.small);
}

/* The player-number colours the system gives the light bar. */
constexpr ScePadLightBarParam kPlayerColors[4] = {
    {0, 0, 255, 0}, {255, 0, 0, 0}, {0, 255, 0, 0}, {255, 0, 255, 0}};

void restore_lightbar_after_player_change() {
  ScePadLightBarParam color = s_lightbar_last;
  if (!s_lightbar_ours) {
    /* under the system: the colour of the user's own number */
    int user_id = -1, number = 1;
    if (sceUserServiceGetForegroundUser(&user_id) != 0 || user_id < 0 ||
        sceUserServiceGetUserNumber(user_id, &number) != 0)
      number = 1;
    color = kPlayerColors[std::clamp(number, 1, 4) - 1];
  }
  const PadLease pad = open_active_pad();
  if (pad.handle < 0)
    return;
  (void)scePadSetLightBar(pad.handle, &color);
  if (!pad.borrowed)
    (void)scePadClose(pad.handle);
}

/* The device id of the active controller, read from it each time (cheap),
   so a controller that reconnects or is swapped is followed. */
bool active_device_id(uint32_t *device_id) {
  const PadLease pad = open_active_pad();
  if (pad.handle < 0)
    return false;
  unsigned char buffer[16] = {};
  const int result = scePadGetDeviceId(pad.handle, buffer);
  if (!pad.borrowed)
    (void)scePadClose(pad.handle);
  if (result != 0) {
    LOG_ERROR("Controller: scePadGetDeviceId failed: 0x%x", result);
    return false;
  }
  std::memcpy(device_id, buffer, sizeof(*device_id));
  return true;
}

PlayerLedAnimation s_led_animation = PlayerLedAnimation::Off;
uint64_t s_led_next_ms = 0;
int s_led_phase = 0;

bool apply_player_number(int player) {
  uint32_t device = 0;
  if (!active_device_id(&device))
    return false;
  const int result = scePadSetUserNumber(device, std::clamp(player, 1, 4));
  if (result != 0)
    LOG_ERROR("Controller: scePadSetUserNumber failed: 0x%x", result);
  restore_lightbar_after_player_change();
  return result == 0;
}

bool apply_brightness(int level) {
  uint32_t device = 0;
  if (!active_device_id(&device))
    return false;
  const int result =
      scePadSetLightBarBaseBrightness(device, std::clamp(level, 0, 2));
  if (result != 0)
    LOG_ERROR("Controller: scePadSetLightBarBaseBrightness failed: 0x%x", result);
  return result == 0;
}

void tick_player_leds() {
  if (s_led_animation == PlayerLedAnimation::Off)
    return;
  const uint64_t now = monotonic_millis();
  if (now < s_led_next_ms)
    return;
  switch (s_led_animation) {
  case PlayerLedAnimation::Chase: /* 1 2 3 4 3 2 1 ... */
    s_led_phase = (s_led_phase + 1) % 6;
    (void)apply_player_number(s_led_phase < 4 ? s_led_phase + 1
                                              : 7 - s_led_phase);
    s_led_next_ms = now + 260;
    break;
  case PlayerLedAnimation::Countdown: /* 4 3 2 1, then again */
    s_led_phase = (s_led_phase + 1) % 4;
    (void)apply_player_number(4 - s_led_phase);
    s_led_next_ms = now + 700;
    break;
  case PlayerLedAnimation::Blink: /* all four, then one, and again */
    s_led_phase = (s_led_phase + 1) % 2;
    (void)apply_player_number(s_led_phase ? 4 : 1);
    s_led_next_ms = now + 400;
    break;
  case PlayerLedAnimation::Off:
    break;
  }
}

void tick_animation() {
  if (s_animation == ControllerAnimation::Off)
    return;
  const uint64_t now = monotonic_millis();
  if (now - s_last_animation_ms < 60)
    return;
  s_last_animation_ms = now;

  uint8_t red = 0, green = 0, blue = 0;
  const uint8_t phase = static_cast<uint8_t>((now / 12) & 0xff);
  const uint8_t triangle = phase < 128 ? phase * 2 : (255 - phase) * 2;
  switch (s_animation) {
  case ControllerAnimation::Breathe:
    red = blend(4, controller_get_custom_channel(false, 0), triangle);
    green = blend(4, controller_get_custom_channel(false, 1), triangle);
    blue = blend(4, controller_get_custom_channel(false, 2), triangle);
    break;
  case ControllerAnimation::Pulse:
    red = blend(controller_get_custom_channel(false, 0),
                controller_get_custom_channel(true, 0), triangle);
    green = blend(controller_get_custom_channel(false, 1),
                  controller_get_custom_channel(true, 1), triangle);
    blue = blend(controller_get_custom_channel(false, 2),
                 controller_get_custom_channel(true, 2), triangle);
    break;
  case ControllerAnimation::Rainbow:
    rainbow(static_cast<uint8_t>(now / 8), red, green, blue);
    break;
  case ControllerAnimation::Spectrum:
    rainbow(static_cast<uint8_t>(now / 3), red, green, blue);
    break;
  case ControllerAnimation::NeonWave:
    red = blend(20, 180, triangle);
    green = blend(20, 40, triangle);
    blue = blend(100, 255, triangle);
    break;
  case ControllerAnimation::Off:
    return;
  }
  (void)controller_set_lightbar(red, green, blue);
}

} // namespace

bool controller_set_lightbar(uint8_t red, uint8_t green, uint8_t blue) {
  const PadLease pad = open_active_pad();
  if (pad.handle < 0)
    return false;
  const ScePadLightBarParam lightbar{red, green, blue, 0};
  const int result = scePadSetLightBar(pad.handle, &lightbar);
  if (result == 0) {
    s_lightbar_ours = true;
    s_lightbar_last = lightbar;
  }
  if (!pad.borrowed)
    (void)scePadClose(pad.handle);
  if (result != 0)
    LOG_ERROR("Controller: scePadSetLightBar failed: 0x%x", result);
  else {
    const uint64_t now = monotonic_millis();
    if (now - s_last_lightbar_log_ms >= 2000) {
      s_last_lightbar_log_ms = now;
      LOG_INFO("Controller: lightbar applied handle=%d borrowed=%d rgb=%u,%u,%u",
               pad.handle, pad.borrowed ? 1 : 0, red, green, blue);
    }
  }
  return result == 0;
}

bool controller_reset_lightbar() {
  const PadLease pad = open_active_pad();
  if (pad.handle < 0)
    return false;
  const int result = scePadResetLightBar(pad.handle);
  if (result == 0)
    s_lightbar_ours = false;
  if (!pad.borrowed)
    (void)scePadClose(pad.handle);
  if (result != 0)
    LOG_ERROR("Controller: scePadResetLightBar failed: 0x%x", result);
  else
    LOG_INFO("Controller: lightbar reset handle=%d borrowed=%d", pad.handle,
             pad.borrowed ? 1 : 0);
  return result == 0;
}

void controller_set_custom_channel(bool color_b, uint8_t channel, uint8_t value) {
  if (channel < 3)
    custom_channel(color_b, channel) = value;
}

uint8_t controller_get_custom_channel(bool color_b, uint8_t channel) {
  return channel < 3 ? static_cast<uint8_t>(custom_channel(color_b, channel))
                     : 0;
}

bool controller_apply_custom_color(bool color_b) {
  s_animation = ControllerAnimation::Off;
  return controller_set_lightbar(controller_get_custom_channel(color_b, 0),
                                 controller_get_custom_channel(color_b, 1),
                                 controller_get_custom_channel(color_b, 2));
}

void controller_start_animation(ControllerAnimation animation) {
  s_animation = animation;
  s_last_animation_ms = 0;
}

void controller_stop_animation() {
  s_animation = ControllerAnimation::Off;
  (void)controller_reset_lightbar();
}

bool controller_vibrate(VibrationPattern pattern) {
  switch (pattern) {
  case VibrationPattern::Short: s_rumble = kShort; s_rumble_length = std::size(kShort); break;
  case VibrationPattern::Long: s_rumble = kLong; s_rumble_length = std::size(kLong); break;
  case VibrationPattern::Heartbeat:
    s_rumble = kHeartbeat;
    s_rumble_length = std::size(kHeartbeat);
    break;
  case VibrationPattern::Light: s_rumble = kLight; s_rumble_length = std::size(kLight); break;
  case VibrationPattern::Heavy: s_rumble = kHeavy; s_rumble_length = std::size(kHeavy); break;
  case VibrationPattern::Wave: s_rumble = kWave; s_rumble_length = std::size(kWave); break;
  }
  s_rumble_index = 0;
  s_rumble_step_end_ms = monotonic_millis() + s_rumble[0].ms;
  const bool sent = send_rumble(s_rumble[0].large, s_rumble[0].small);
  LOG_INFO("Controller: vibration pattern=%d sent=%d strength=%d",
           static_cast<int>(pattern), sent ? 1 : 0,
           g_settings.controller_vibration_strength);
  if (!sent)
    s_rumble = nullptr;
  return sent;
}

bool controller_set_player_leds(int player) {
  s_led_animation = PlayerLedAnimation::Off;
  const bool ok = apply_player_number(player);
  LOG_INFO("Controller: player LEDs=%d ok=%d", player, ok ? 1 : 0);
  return ok;
}

bool controller_set_light_brightness(int level) {
  s_led_animation = PlayerLedAnimation::Off;
  const bool ok = apply_brightness(level);
  LOG_INFO("Controller: light brightness=%d ok=%d", level, ok ? 1 : 0);
  return ok;
}

void controller_start_player_led_animation(PlayerLedAnimation animation) {
  LOG_INFO("Controller: player LED animation=%d", static_cast<int>(animation));
  s_led_animation = animation;
  s_led_phase = 0;
  s_led_next_ms = 0;
}

bool controller_restore_player_leds() {
  s_led_animation = PlayerLedAnimation::Off;
  int user_id = -1, number = 1, level = 0;
  if (sceUserServiceGetForegroundUser(&user_id) != 0 || user_id < 0)
    return false;
  if (sceUserServiceGetUserNumber(user_id, &number) != 0 || number < 1)
    number = 1;
  if (sceUserServiceGetLightBarBaseBrightness(user_id, &level) != 0)
    level = 0;
  LOG_INFO("Controller: player LEDs restored number=%d brightness=%d", number,
           level);
  const bool leds = apply_player_number(number);
  return apply_brightness(level) && leds;
}

void controller_stop_vibration() {
  s_rumble = nullptr;
  (void)send_rumble(0, 0);
}

void controller_handle_buttons(uint32_t buttons_down) {
  tick_animation();
  tick_rumble();
  tick_player_leds();
  (void)buttons_down;
}
