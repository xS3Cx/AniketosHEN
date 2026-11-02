void apply_global_animation(int mode, int speed) {
  const int selected_mode = std::clamp(mode, 0, 4);
  const int selected_speed = std::clamp(speed, 25, 400);
  const int system_mode = selected_mode == 1 || selected_mode == 2 ||
                                  selected_mode == 4
      ? static_cast<int>(AniketosTheme::AnimationMode::Rainbow)
      : static_cast<int>(AniketosTheme::AnimationMode::Static);
  g_settings.vsh_color_animation = selected_mode;
  g_settings.vsh_color_animation_speed = selected_speed;
  g_settings.shell_background_animation = system_mode;
  g_settings.shell_background_animation_speed = selected_speed;
  g_settings.shell_tiles_outline_animation = system_mode;
  g_settings.shell_tiles_outline_animation_speed = selected_speed;
  g_settings.shell_ui_panels_animation = system_mode;
  g_settings.shell_ui_panels_animation_speed = selected_speed;
  AniketosTheme::RequestApply();
  ShellClock::RequestRefresh();
}

/* One colour for every themed surface: the menu, the system background, the
   focus outline, the panels and the clock. r, g and b are 0..1. */
void apply_global_theme(float r, float g, float b) {
  r = std::clamp(r, 0.0f, 1.0f);
  g = std::clamp(g, 0.0f, 1.0f);
  b = std::clamp(b, 0.0f, 1.0f);
  const bool was_global_theme = g_settings.vsh_global_theme_enabled;
  g_settings.vsh_global_theme_enabled = true;

  // Global customization has exactly one color source.  The menu, native
  // ShellUI, clock, controller, and Aura monitor all use this primary edge.
  g_settings.vsh_dual_color = false;
  if (!was_global_theme) {
    g_settings.vsh_color_animation = 3;
    g_settings.vsh_color_animation_speed = 100;
  }
  g_settings.vsh_left_r = r;
  g_settings.vsh_left_g = g;
  g_settings.vsh_left_b = b;
  g_settings.vsh_right_r = r;
  g_settings.vsh_right_g = g;
  g_settings.vsh_right_b = b;

  // Keep Sony's basemat dark enough for labels while LightMat carries the
  // selected hue. Image and wave overrides would conceal that color.
  const auto dark = [](float color) {
    return theme_channel(0.012f + color * 0.055f);
  };
  const int red = theme_channel(r), green = theme_channel(g),
            blue = theme_channel(b);
  g_settings.shell_background_enabled = true;
  g_settings.shell_background_preset =
      static_cast<int>(AniketosTheme::Preset::Custom);
  g_settings.shell_background_base_r = dark(r);
  g_settings.shell_background_base_g = dark(g);
  g_settings.shell_background_base_b = dark(b);
  g_settings.shell_background_glow_r = red;
  g_settings.shell_background_glow_g = green;
  g_settings.shell_background_glow_b = blue;
  g_settings.shell_background_glow_intensity = 100;
  g_settings.shell_background_image_enabled = false;
  g_settings.shell_background_wave_enabled = false;

  // The global FocusRenderWidget hook follows this glow on every native
  // focus layer, including tiles, switchers, toasts, and Settings controls.
  g_settings.shell_tiles_outline_enabled = true;
  g_settings.shell_tiles_outline_sync_background = true;
  g_settings.shell_tiles_outline_preset =
      static_cast<int>(AniketosTheme::OutlinePreset::Custom);
  g_settings.shell_tiles_outline_r = g_settings.shell_background_glow_r;
  g_settings.shell_tiles_outline_g = g_settings.shell_background_glow_g;
  g_settings.shell_tiles_outline_b = g_settings.shell_background_glow_b;

  // Shared UI3 window backgrounds retain their native brightness and alpha
  // hierarchy while taking the same hue as the global theme.
  g_settings.shell_ui_panels_enabled = true;
  g_settings.shell_ui_panels_r = g_settings.shell_background_glow_r;
  g_settings.shell_ui_panels_g = g_settings.shell_background_glow_g;
  g_settings.shell_ui_panels_b = g_settings.shell_background_glow_b;
  g_settings.shell_ui_panels_strength = 100;

  g_settings.shell_clock_enabled = true;
  g_settings.shell_clock_time_r = red;
  g_settings.shell_clock_time_g = green;
  g_settings.shell_clock_time_b = blue;
  g_settings.shell_clock_date_r = red;
  g_settings.shell_clock_date_g = green;
  g_settings.shell_clock_date_b = blue;

  AniketosTheme::NativeShellBackground::DisableImageOverride();
  apply_global_animation(g_settings.vsh_color_animation,
                         g_settings.vsh_color_animation_speed);
  LOG_INFO("[ANIVSH:GLOBAL-THEME] color=%d,%d,%d animation=%d", red, green, blue,
           g_settings.vsh_color_animation);
}

void reset_highlight_for_theme_change() {
  s_highlight_y = 0.0f;
  s_highlight_from = 0.0f;
  s_highlight_to = 0.0f;
  s_highlight_started_ms = 0;
  // Keep the current PageState selection, but invalidate the pooled panel's
  // visual owner so the next renderer snaps to that row instead of carrying
  // a highlight from the previous theme.
  s_highlight_initialized = false;
  s_highlight_selection_key = 0;
  s_highlight_scroll = 0;
  s_page_transition = PageTransitionState::Idle;
  s_page_transition_started_ms = 0;
  s_page_transition_start_geometry = {};
  s_page_transition_target_geometry = {};
  s_apparition_animation_layout.valid = false;
  s_last_menu_animation_frame_ms = 0;
  // A theme switch is a style change, not an open/close request.  Keeping the
  // old reveal animation active is what made the panel resize/jump during the
  // switch.
  if (s_animation == AnimationState::Opening)
    s_animation = AnimationState::Visible;
}

/* Every part of the interface's look takes the settings as they are now:
   after the master switch or a reset. Home is reloaded for the tile shape
   and the scripts, Control Center for the avatar dot. */
void reapply_customization() {
  if (AniketosTheme::ConfigFromSettings(g_settings).enabled)
    AniketosTheme::RequestApply();
  else
    AniketosTheme::RequestRestore();
  ShellClock::RequestRefresh();
  HomeVideo::Restart();
  HomeVideo::RestartImage();
  shellui_request_home_reload();
  shellui_request_presence_reload(1500, false);
}
