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
  struct {
    float lr, lg, lb;
  } const p{std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f),
            std::clamp(b, 0.0f, 1.0f)};
  const bool was_global_theme = g_settings.vsh_global_theme_enabled;
  g_settings.vsh_global_theme_enabled = true;

  // Global customization has exactly one color source.  The menu, native
  // ShellUI, clock, controller, and Aura monitor all use this primary edge.
  g_settings.vsh_dual_color = false;
  if (!was_global_theme) {
    g_settings.vsh_color_animation = 3;
    g_settings.vsh_color_animation_speed = 100;
  }
  g_settings.vsh_left_r = p.lr;
  g_settings.vsh_left_g = p.lg;
  g_settings.vsh_left_b = p.lb;
  g_settings.vsh_right_r = p.lr;
  g_settings.vsh_right_g = p.lg;
  g_settings.vsh_right_b = p.lb;

  // Keep Sony's basemat dark enough for labels while LightMat carries the
  // selected hue. Image and wave overrides would conceal that color.
  const auto dark = [](float color) {
    return theme_channel(0.012f + color * 0.055f);
  };
  g_settings.shell_background_enabled = true;
  g_settings.shell_background_preset =
      static_cast<int>(AniketosTheme::Preset::Custom);
  g_settings.shell_background_base_r = dark(p.lr);
  g_settings.shell_background_base_g = dark(p.lg);
  g_settings.shell_background_base_b = dark(p.lb);
  g_settings.shell_background_glow_r = theme_channel(p.lr);
  g_settings.shell_background_glow_g = theme_channel(p.lg);
  g_settings.shell_background_glow_b = theme_channel(p.lb);
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

  const int clock_r = theme_channel(p.lr);
  const int clock_g = theme_channel(p.lg);
  const int clock_b = theme_channel(p.lb);
  g_settings.shell_clock_enabled = true;
  g_settings.shell_clock_time_r = clock_r;
  g_settings.shell_clock_time_g = clock_g;
  g_settings.shell_clock_time_b = clock_b;
  g_settings.shell_clock_date_r = clock_r;
  g_settings.shell_clock_date_g = clock_g;
  g_settings.shell_clock_date_b = clock_b;

  AniketosTheme::NativeShellBackground::DisableImageOverride();
  apply_global_animation(g_settings.vsh_color_animation,
                         g_settings.vsh_color_animation_speed);
  LOG_INFO("[ANIVSH:GLOBAL-THEME] color=%d,%d,%d animation=%d",
           theme_channel(p.lr), theme_channel(p.lg), theme_channel(p.lb),
           g_settings.vsh_color_animation);
}

void apply_system_animation(int mode, int speed) {
  const int selected_mode = std::clamp(
      mode, 0, AniketosTheme::AnimationModeCount() - 1);
  const int selected_speed = std::clamp(speed, 25, 400);
  g_settings.shell_background_animation = selected_mode;
  g_settings.shell_background_animation_speed = selected_speed;
  g_settings.shell_tiles_outline_animation = selected_mode;
  g_settings.shell_tiles_outline_animation_speed = selected_speed;
  g_settings.shell_ui_panels_animation = selected_mode;
  g_settings.shell_ui_panels_animation_speed = selected_speed;
  AniketosTheme::RequestApply();
  ShellClock::RequestRefresh();
}

bool is_system_animation_option(OptionId id) {
  return id == OptionId::SystemGlobalAnimation ||
         id == OptionId::SystemGlobalAnimationSpeed ||
         id == OptionId::GlobalThemeAnimation ||
         id == OptionId::GlobalThemeAnimationSpeed;
}

bool &toggle_target(OptionId id) {
  switch (id) {
  case OptionId::GlobalThemeEnabled:
    return g_settings.vsh_global_theme_enabled;
  case OptionId::OverlayEnabled: return g_settings.home_osd_enabled;
  case OptionId::OverlayBackground: return g_settings.overlay_background;
  case OptionId::OverlayAllCpuUsage: return g_settings.all_cpu_usage;
  case OptionId::OverlayFps: return g_settings.home_osd_fps;
  case OptionId::OverlayResolution: return g_settings.overlay_resolution;
  case OptionId::OverlayCpu: return g_settings.home_osd_cpu;
  case OptionId::OverlaySoc: return g_settings.home_osd_gpu;
  case OptionId::OverlayFan: return g_settings.home_osd_fan;
  case OptionId::OverlayRam: return g_settings.home_osd_ram;
  case OptionId::OverlayFreeRam: return g_settings.overlay_free_ram;
  case OptionId::OverlayStorage: return g_settings.overlay_storage;
  case OptionId::OverlayFirmware: return g_settings.overlay_firmware;
  case OptionId::OverlayUptime: return g_settings.overlay_uptime;
  case OptionId::OverlayIp: return g_settings.home_osd_ip;
  case OptionId::PerformanceOverlayEnabled:
    return g_settings.overlay_enabled;
  case OptionId::PerformanceOverlayBackground:
    return g_settings.overlay_background;
  case OptionId::PerformanceOverlayAllCpuUsage:
    return g_settings.all_cpu_usage;
  case OptionId::PerformanceOverlayCpu: return g_settings.overlay_cpu;
  case OptionId::PerformanceOverlayGpu: return g_settings.overlay_gpu;
  case OptionId::PerformanceOverlayRam: return g_settings.overlay_ram;
  case OptionId::PerformanceOverlayIp: return g_settings.overlay_ip;
  case OptionId::ShellBackgroundEnabled:
    return g_settings.shell_background_enabled;
  case OptionId::ShellBackgroundForceHome:
    return g_settings.shell_background_force_home;
  case OptionId::ShellBackgroundImageEnabled:
    return g_settings.shell_background_image_enabled;
  case OptionId::ShellTilesCircular: return g_settings.shell_tiles_circular;
  case OptionId::ShellTilesPS5CaseMockup:
    return g_settings.shell_tiles_ps5_case_mockup;
  case OptionId::HomeEmulatorsTab: return g_settings.home_tab_emulators;
  case OptionId::HomeBookmark: return g_settings.home_bookmark;
  case OptionId::OskGlyphKey: return g_settings.osk_glyph_key;
  case OptionId::ShellTilesOutlineEnabled:
    return g_settings.shell_tiles_outline_enabled;
  case OptionId::ShellTilesOutlineSyncBackground:
    return g_settings.shell_tiles_outline_sync_background;
  case OptionId::ShellClockEnabled: return g_settings.shell_clock_enabled;
  case OptionId::HomeVideoEnabled: return g_settings.home_video_enabled;
  case OptionId::HomeVideoSound: return g_settings.home_video_sound;
  case OptionId::HomeImageEnabled: return g_settings.home_image_enabled;
  case OptionId::HomeParticlesEnabled:
    return g_settings.home_particles_enabled;
  case OptionId::ShellClockSeconds:
    return g_settings.shell_clock_show_seconds;
  case OptionId::ShellClockDate: return g_settings.shell_clock_show_date;
  case OptionId::ShellClockYear: return g_settings.shell_clock_show_year;
  case OptionId::ShellUIPanelsEnabled:
    return g_settings.shell_ui_panels_enabled;
  case OptionId::MenuControls: return g_settings.vsh_show_controls;
  case OptionId::CustomizationEnabled:
    return g_settings.shell_customization_enabled;
  case OptionId::FanDynamic: return g_settings.enable_fan_speed;
  case OptionId::MenuDualColor: return g_settings.vsh_dual_color;
  case OptionId::MenuBorderEnabled: return g_settings.vsh_border_enabled;
  case OptionId::MenuMove: return s_moving;
  default: return s_moving;
  }
}

int &int_target(OptionId id) {
  switch (id) {
  case OptionId::GlobalThemeAnimation:
    return g_settings.vsh_color_animation;
  case OptionId::GlobalThemeAnimationSpeed:
    return g_settings.vsh_color_animation_speed;
  case OptionId::SystemGlobalAnimation:
    return g_settings.shell_background_animation;
  case OptionId::SystemGlobalAnimationSpeed:
    return g_settings.shell_background_animation_speed;
  case OptionId::OverlayRefresh: return g_settings.overlay_refresh_seconds;
  case OptionId::OverlayFpsPrecision: return g_settings.overlay_fps_precision;
  case OptionId::OverlayTempUnit: return g_settings.overlay_temp_unit;
  case OptionId::OverlayMemoryUnit: return g_settings.overlay_memory_unit;
  case OptionId::OverlayHorizontal: return g_settings.overlay_horizontal;
  case OptionId::OverlayVertical: return g_settings.overlay_vertical;
  case OptionId::BuzzerVolume: return g_settings.vsh_buzzer_volume;
  case OptionId::PlayerLeds: return s_player_leds;
  case OptionId::LightBrightness: return s_light_brightness;
  case OptionId::PowerIndicatorBrightness:
    return g_settings.vsh_power_indicator_brightness;
  case OptionId::PowerIndicatorAnimation:
    return g_settings.vsh_power_indicator_animation;
  case OptionId::PowerIndicatorColor:
    return g_settings.vsh_power_indicator_color;
  case OptionId::FanThreshold: return g_settings.fan_threshold;
  case OptionId::VibrationStrength:
    return g_settings.controller_vibration_strength;
  case OptionId::MenuAnimation: return g_settings.vsh_animation;
  case OptionId::MenuFontWeight: return g_settings.vsh_font_weight;
  case OptionId::MenuGradientDirection:
    return g_settings.vsh_gradient_direction;
  case OptionId::MenuColorAnimation:
    return g_settings.vsh_color_animation;
  case OptionId::MenuColorAnimationSpeed:
    return g_settings.vsh_color_animation_speed;
  case OptionId::ShellBackgroundAnimation:
    return g_settings.shell_background_animation;
  case OptionId::ShellBackgroundAnimationSpeed:
    return g_settings.shell_background_animation_speed;
  case OptionId::ShellBackgroundImageTransition:
    return g_settings.shell_background_image_transition;
  case OptionId::ShellBackgroundParticles:
    return g_settings.shell_background_particles;
  case OptionId::ShellTilesOutlineAnimation:
    return g_settings.shell_tiles_outline_animation;
  case OptionId::ShellTilesOutlineAnimationSpeed:
    return g_settings.shell_tiles_outline_animation_speed;
  case OptionId::HomeParticlesCount: return g_settings.home_particles_count;
  case OptionId::HomeVideoVolume: return g_settings.home_video_volume;
  case OptionId::HomeBackgroundBlur: return g_settings.home_bg_blur;
  case OptionId::HomeBackgroundDim: return g_settings.home_bg_dim;
  case OptionId::HomeBackgroundHaze: return g_settings.home_bg_haze;
  case OptionId::HomeBackgroundVignette: return g_settings.home_bg_vignette;
  case OptionId::HomeBackgroundEdges: return g_settings.home_bg_edges;
  case OptionId::HomePresenceDot: return g_settings.home_presence_dot;
  case OptionId::ShellToggleStyle: return g_settings.shell_toggle_style;
  case OptionId::ShellClockHourFormat:
    return g_settings.shell_clock_hour_format;
  case OptionId::ShellClockDateOrder:
    return g_settings.shell_clock_date_order;
  case OptionId::ShellClockMonthStyle:
    return g_settings.shell_clock_month_style;
  case OptionId::ShellClockWeekdayStyle:
    return g_settings.shell_clock_weekday_style;
  case OptionId::ShellClockDateSeparator:
    return g_settings.shell_clock_date_separator;
  case OptionId::ShellClockTimeFontSize:
    return g_settings.shell_clock_time_font_size;
  case OptionId::ShellClockDateFontSize:
    return g_settings.shell_clock_date_font_size;
  case OptionId::ShellClockDateSpacing:
    return g_settings.shell_clock_date_spacing;
  case OptionId::ShellUIPanelsAnimation:
    return g_settings.shell_ui_panels_animation;
  case OptionId::ShellUIPanelsAnimationSpeed:
    return g_settings.shell_ui_panels_animation_speed;
  case OptionId::ShellUIPanelsStrength:
    return g_settings.shell_ui_panels_strength;
  case OptionId::ShellUIPanelsOpacity:
    return g_settings.shell_ui_panels_opacity;
  case OptionId::ShellGlowIntensity:
    return g_settings.shell_background_glow_intensity;
  case OptionId::ShellTilesCornerRadius:
    return g_settings.shell_tiles_corner_radius;
  default: return g_settings.vsh_animation;
  }
}

int selector_index_for(OptionId id) {
  switch (id) {
  case OptionId::MenuThemeSelector: {
    constexpr int themes[] = {0, 1, 2, 4, 5};
    for (int i = 0; i < static_cast<int>(std::size(themes)); ++i)
      if (themes[i] == g_settings.vsh_menu_theme)
        return i;
    return 0;
  }
  case OptionId::MenuAppearanceSelector:
    return std::clamp(g_settings.vsh_menu_appearance, 0, 1);
  case OptionId::ShellBackgroundImageFile:
    return std::max(0, current_background_index());
  case OptionId::HomeVideoFile:
    return home_video_index();
  case OptionId::HomeImageFile:
    return home_image_index();
  case OptionId::OverlayPosition:
    return g_settings.overlay_pos == 2 ? 1 : 0;
  case OptionId::PerformanceOverlayPosition:
    return g_settings.overlay_pos == 2 ? 1 : 0;
  default:
    return std::max(0, int_target(id));
  }
}

float &float_target(OptionId id) {
  switch (id) {
  case OptionId::OverlayTextSize: return g_settings.overlay_text_size;
  case OptionId::MenuScale: return g_settings.vsh_menu_scale;
  case OptionId::MenuBorder: return g_settings.vsh_border;
  case OptionId::MenuTextSize: return g_settings.vsh_text_size;
  case OptionId::MenuWidth: return g_settings.vsh_menu_width;
  case OptionId::MenuOpacity: return g_settings.vsh_background_opacity;
  default: return g_settings.vsh_menu_width;
  }
}

bool is_shell_background_option(OptionId id) {
  switch (id) {
  case OptionId::ShellBackgroundEnabled:
  case OptionId::ShellBackgroundForceHome:
  case OptionId::ShellBackgroundAnimation:
  case OptionId::ShellBackgroundAnimationSpeed:
  case OptionId::ShellBackgroundImageEnabled:
  case OptionId::ShellBackgroundImageTransition:
  case OptionId::ShellBackgroundReloadImage:
  case OptionId::ShellBackgroundParticles:
  case OptionId::ColorBackground:
  case OptionId::ColorGlow:
  case OptionId::ShellGlowIntensity:
  case OptionId::ShellRestoreOriginal:
    return true;
  default:
    return false;
  }
}

bool is_shell_tile_option(OptionId id) {
  switch (id) {
  case OptionId::ShellTilesCircular:
  case OptionId::ShellTilesPS5CaseMockup:
  case OptionId::ShellTilesOutlineEnabled:
  case OptionId::ShellTilesOutlineSyncBackground:
  case OptionId::ShellTilesOutlineAnimation:
  case OptionId::ShellTilesOutlineAnimationSpeed:
  case OptionId::ColorOutline:
    return true;
  default:
    return false;
  }
}

bool is_shell_clock_option(OptionId id) {
  switch (id) {
  case OptionId::ShellClockEnabled:
  case OptionId::ShellClockSeconds:
  case OptionId::ShellClockDate:
  case OptionId::ShellClockYear:
  case OptionId::ShellClockHourFormat:
  case OptionId::ShellClockDateOrder:
  case OptionId::ShellClockMonthStyle:
  case OptionId::ShellClockWeekdayStyle:
  case OptionId::ShellClockDateSeparator:
  case OptionId::ShellClockTimeFontSize:
  case OptionId::ShellClockDateFontSize:
  case OptionId::ShellClockDateSpacing:
  case OptionId::ColorClockTime:
  case OptionId::ColorClockDate:
    return true;
  default:
    return false;
  }
}

bool is_shell_ui_panel_option(OptionId id) {
  switch (id) {
  case OptionId::ShellUIPanelsEnabled:
  case OptionId::ShellUIPanelsAnimation:
  case OptionId::ShellUIPanelsAnimationSpeed:
  case OptionId::ColorPanels:
  case OptionId::ShellUIPanelsStrength:
  case OptionId::ShellUIPanelsOpacity:
    return true;
  default:
    return false;
  }
}

bool is_global_theme_owned_option(OptionId id) {
  if (is_shell_background_option(id) || is_shell_tile_option(id) ||
      is_shell_ui_panel_option(id))
    return true;
  switch (id) {
  case OptionId::MenuDualColor:
  case OptionId::MenuGradientDirection:
  case OptionId::MenuColorAnimation:
  case OptionId::MenuColorAnimationSpeed:
  case OptionId::ColorMenuLeft:
  case OptionId::ColorMenuRight:
  case OptionId::ShellClockEnabled:
  case OptionId::ColorClockTime:
  case OptionId::ColorClockDate:
    return true;
  default:
    return false;
  }
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

bool is_overlay_option(OptionId id) {
  switch (id) {
  case OptionId::OverlayEnabled:
  case OptionId::OverlayFps:
  case OptionId::OverlayResolution:
  case OptionId::OverlayCpu:
  case OptionId::OverlaySoc:
  case OptionId::OverlayFan:
  case OptionId::OverlayRam:
  case OptionId::OverlayFreeRam:
  case OptionId::OverlayStorage:
  case OptionId::OverlayFirmware:
  case OptionId::OverlayUptime:
  case OptionId::OverlayIp:
  case OptionId::OverlayBackground:
  case OptionId::OverlayAllCpuUsage:
  case OptionId::OverlayPosition:
  case OptionId::OverlayRefresh:
  case OptionId::OverlayFpsPrecision:
  case OptionId::OverlayTempUnit:
  case OptionId::OverlayMemoryUnit:
  case OptionId::OverlayTextSize:
  case OptionId::OverlayHorizontal:
  case OptionId::OverlayVertical:
  case OptionId::PerformanceOverlayEnabled:
  case OptionId::PerformanceOverlayBackground:
  case OptionId::PerformanceOverlayAllCpuUsage:
  case OptionId::PerformanceOverlayCpu:
  case OptionId::PerformanceOverlayGpu:
  case OptionId::PerformanceOverlayRam:
  case OptionId::PerformanceOverlayIp:
    return true;
  case OptionId::PerformanceOverlayPosition:
    return true;
  default:
    return false;
  }
}


/* Writes the colour at a gradient-slider position to whatever the slider
   edits, and shows it there at once where that is not left to the common
   handling in change_option(). */
void set_color(OptionId id, int position) {
  const AniketosTheme::RGB rgb =
      color_with(color_of(id), color_mode(id), position);
  const int r = theme_channel(rgb.r), g = theme_channel(rgb.g), b = theme_channel(rgb.b);
  const auto put = [](int &tr, int &tg, int &tb, int r, int g, int b) {
    tr = r;
    tg = g;
    tb = b;
  };
  switch (id) {
  case OptionId::ColorGlobal:
    apply_global_theme(rgb.r, rgb.g, rgb.b);
    break;
  case OptionId::ColorMenuLeft:
    g_settings.vsh_left_r = rgb.r;
    g_settings.vsh_left_g = rgb.g;
    g_settings.vsh_left_b = rgb.b;
    break;
  case OptionId::ColorMenuRight:
    g_settings.vsh_right_r = rgb.r;
    g_settings.vsh_right_g = rgb.g;
    g_settings.vsh_right_b = rgb.b;
    break;
  case OptionId::ColorController:
    controller_set_custom_channel(false, 0, static_cast<uint8_t>(r));
    controller_set_custom_channel(false, 1, static_cast<uint8_t>(g));
    controller_set_custom_channel(false, 2, static_cast<uint8_t>(b));
    controller_stop_animation();
    (void)controller_apply_custom_color(false);
    break;
  case OptionId::ColorBackground:
    put(g_settings.shell_background_base_r, g_settings.shell_background_base_g,
        g_settings.shell_background_base_b, r, g, b);
    break;
  case OptionId::ColorGlow:
    put(g_settings.shell_background_glow_r, g_settings.shell_background_glow_g,
        g_settings.shell_background_glow_b, r, g, b);
    break;
  case OptionId::ColorOutline:
    put(g_settings.shell_tiles_outline_r, g_settings.shell_tiles_outline_g,
        g_settings.shell_tiles_outline_b, r, g, b);
    break;
  case OptionId::ColorPanels:
    put(g_settings.shell_ui_panels_r, g_settings.shell_ui_panels_g,
        g_settings.shell_ui_panels_b, r, g, b);
    break;
  case OptionId::ColorClockTime:
    put(g_settings.shell_clock_time_r, g_settings.shell_clock_time_g,
        g_settings.shell_clock_time_b, r, g, b);
    break;
  case OptionId::ColorClockDate:
    put(g_settings.shell_clock_date_r, g_settings.shell_clock_date_g,
        g_settings.shell_clock_date_b, r, g, b);
    break;
  default:
    break;
  }
}

void change_option(const Option &option, int direction) {
  if (option.id == OptionId::MenuThemeSelector) {
    g_settings.vsh_menu_theme =
        cycle_theme(g_settings.vsh_menu_theme, direction);
    reset_highlight_for_theme_change();
    request_save();
    LOG_INFO("[ANIVSH:THEME] selected=%s visual_only=1",
             theme_display_name(g_settings.vsh_menu_theme));
    s_dirty = true;
    return;
  } else if (option.id == OptionId::MenuAppearanceSelector) {
    const int current = std::clamp(g_settings.vsh_menu_appearance, 0, 1);
    g_settings.vsh_menu_appearance = current == 0 ? 1 : 0;
    request_save();
    LOG_INFO("[ANIVSH:APPEARANCE] selected=%s",
             g_settings.vsh_menu_appearance == 1 ? "light" : "dark");
    s_dirty = true;
    return;
  } else if (option.id == OptionId::GlobalThemeAnimation) {
    apply_global_animation(g_settings.vsh_color_animation + direction,
                           g_settings.vsh_color_animation_speed);
    request_save();
    s_dirty = true;
    return;
  } else if (option.id == OptionId::GlobalThemeAnimationSpeed) {
    apply_global_animation(g_settings.vsh_color_animation,
                           g_settings.vsh_color_animation_speed +
                               direction * option.step_i);
    request_save();
    s_dirty = true;
    return;
  } else if (option.id == OptionId::SystemGlobalAnimation) {
    apply_system_animation(g_settings.shell_background_animation + direction,
                           g_settings.shell_background_animation_speed);
    request_save();
    s_dirty = true;
    return;
  } else if (option.id == OptionId::HomeImageFile) {
    const std::vector<std::string> &images = HomeVideo::Images();
    if (images.empty())
      return;
    const int next = std::clamp(home_image_index() + direction, 0,
                                static_cast<int>(images.size()) - 1);
    const std::string &chosen = images[static_cast<size_t>(next)];
    if (chosen == g_settings.home_image_path)
      return;
    g_settings.home_image_path = chosen;
    HomeVideo::RestartImage();
    request_save();
    s_dirty = true;
    return;
  } else if (option.id == OptionId::HomeVideoFile) {
    const std::vector<std::string> &videos = HomeVideo::Videos();
    if (videos.empty())
      return;
    const int next = std::clamp(home_video_index() + direction, 0,
                                static_cast<int>(videos.size()) - 1);
    const std::string &chosen = videos[static_cast<size_t>(next)];
    if (chosen == g_settings.home_video_path)
      return;
    g_settings.home_video_path = chosen;
    HomeVideo::Restart();
    LOG_INFO("[ANIVSH:HOME-VIDEO] selected '%s' enabled=%d", chosen.c_str(),
             g_settings.home_video_enabled ? 1 : 0);
    request_save();
    s_dirty = true;
    return;
  } else if (option.id == OptionId::ShellBackgroundImageFile) {
    scan_background_images();
    if (s_background_images.empty())
      return;
    const int current = current_background_index();
    const int next = current < 0
        ? (direction < 0 ? static_cast<int>(s_background_images.size()) - 1
                         : 0)
        : std::clamp(current + direction, 0,
                     static_cast<int>(s_background_images.size()) - 1);
    if (s_background_images[next] ==
        g_settings.shell_background_image_path)
      return;
    g_settings.shell_background_image_path = s_background_images[next];
    if (g_settings.shell_background_image_enabled)
      AniketosTheme::RequestImageReload();
    LOG_INFO("[ANIVSH:BACKGROUND] selected image='%s' enabled=%d",
             g_settings.shell_background_image_path.c_str(),
             g_settings.shell_background_image_enabled ? 1 : 0);
    request_save();
    s_dirty = true;
    return;
  } else if (option.id == OptionId::ShellTilesCircular) {
    g_settings.shell_tiles_circular = !g_settings.shell_tiles_circular;
    request_save();
    shellui_request_home_reload();
    notify(g_settings.shell_tiles_circular ? "notify.vsh.tiles.circular"
                                           : "notify.vsh.tiles.stock");
    s_dirty = true;
    return;
  } else if (option.id == OptionId::ShellTilesPS5CaseMockup) {
    g_settings.shell_tiles_ps5_case_mockup =
        !g_settings.shell_tiles_ps5_case_mockup;
    request_save();
    shellui_request_home_reload();
    notify(g_settings.shell_tiles_ps5_case_mockup
               ? "notify.vsh.tiles.ps5_case_mockup"
               : "notify.vsh.tiles.ps5_case_stock");
    s_dirty = true;
    return;
  } else if (option.type == OptionType::ColorSlider) {
    const int position = std::clamp(std::atoi(option.value.c_str()) + direction,
                                    0, option.maximum_i);
    set_color(option.id, position);
  } else if (option.type == OptionType::SliderInt) {
    int &target = int_target(option.id);
    const int previous = target;
    target = std::clamp(target + direction * option.step_i, option.minimum_i,
                        option.maximum_i);
    if (option.id == OptionId::FanThreshold) {
      if (g_settings.enable_fan_speed &&
          !AniketosFan::SetTarget(g_settings.fan_threshold))
        notify("notify.fan.adjust_failed");
      request_save();
    } else if (option.id == OptionId::ShellTilesCornerRadius &&
               (target != previous || !g_settings.shell_tiles_circular)) {
      /* Choosing a rounding turns the rounded tiles on. Home takes the
         shape as it loads; the reload waits for the slider to rest, so
         holding a direction reloads it once, not at every step, and not
         at all against either end. */
      g_settings.shell_tiles_circular = true;
      shellui_request_home_reload_after(400);
      LOG_INFO("[HOME-TILES] rounding=%d%%",
               g_settings.shell_tiles_corner_radius);
    } else if (option.id == OptionId::HomeVideoVolume && target > previous) {
      /* Turning the volume up turns the video's sound on. */
      g_settings.home_video_sound = true;
    }
  } else if (option.type == OptionType::SliderFloat) {
    float &target = float_target(option.id);
    target = std::clamp(target + direction * option.step_f, option.minimum_f,
                        option.maximum_f);
  } else if (option.id == OptionId::OverlayPosition) {
    const int current = g_settings.overlay_pos == 2 ? 1 : 0;
    const int next = std::clamp(current + direction, 0, 1);
    g_settings.overlay_pos = next == 1 ? 2 : 0;
    request_save();
    s_overlay_dirty = true;
    s_dirty = true;
    return;
  } else if (option.id == OptionId::PerformanceOverlayPosition) {
    const int current = g_settings.overlay_pos == 2 ? 1 : 0;
    const int next = std::clamp(current + direction, 0, 1);
    g_settings.overlay_pos = next == 1 ? 2 : 0;
    request_save();
    s_overlay_dirty = true;
    s_dirty = true;
    return;
  } else if (option.type == OptionType::Strings) {
    int &target = int_target(option.id);
    target = std::clamp(target + direction, 0, option.string_count - 1);
    if (option.id == OptionId::PlayerLeds) {
      if (!controller_set_player_leds(target + 1))
        notify("notify.controller.leds_failed");
    } else if (option.id == OptionId::LightBrightness) {
      if (!controller_set_light_brightness(target))
        notify("notify.controller.leds_failed");
    } else if (option.id == OptionId::BuzzerVolume) {
      (void)Buzzer::SetVolumeAndConfirm(
          static_cast<Buzzer::Volume>(target));
      request_save();
    } else if (option.id == OptionId::PowerIndicatorAnimation) {
      if (target == static_cast<int>(PowerIndicator::Animation::Normal))
        PowerIndicator::RestoreNormal(g_settings.vsh_power_indicator_brightness);
      request_save();
    } else if (option.id == OptionId::PowerIndicatorBrightness) {
      (void)PowerIndicator::SetBrightness(target);
      request_save();
    } else if (option.id == OptionId::PowerIndicatorColor) {
      (void)PowerIndicator::SetColor(
          static_cast<PowerIndicator::Color>(target));
      g_settings.vsh_power_indicator_animation =
          static_cast<int>(PowerIndicator::Animation::Normal);
      request_save();
    } else if (option.id == OptionId::ShellToggleStyle) {
      request_save();
    } else if (option.id == OptionId::HomePresenceDot) {
      request_save();
      shellui_request_presence_reload(1500, true);
    }
  }
  if (is_system_animation_option(option.id))
    apply_system_animation(g_settings.shell_background_animation,
                           g_settings.shell_background_animation_speed);
  if (is_shell_background_option(option.id)) {
    if (option.id != OptionId::ShellBackgroundAnimation &&
        option.id != OptionId::ShellBackgroundAnimationSpeed &&
        option.id != OptionId::ShellBackgroundImageTransition &&
        option.id != OptionId::ShellBackgroundParticles) {
      g_settings.shell_background_enabled = true;
      g_settings.shell_background_preset =
          static_cast<int>(AniketosTheme::Preset::Custom);
    }
    if (g_settings.shell_background_enabled)
      AniketosTheme::RequestApply();
    else
      AniketosTheme::RequestRestore();
  }
  if (is_shell_tile_option(option.id))
    g_settings.shell_tiles_outline_enabled = true;
  if (is_shell_tile_option(option.id) &&
      option.id != OptionId::ShellTilesOutlineEnabled &&
      option.id != OptionId::ShellTilesOutlineSyncBackground) {
    g_settings.shell_tiles_outline_sync_background = false;
    g_settings.shell_tiles_outline_preset =
        static_cast<int>(AniketosTheme::OutlinePreset::Custom);
  }
  if (is_shell_clock_option(option.id))
    ShellClock::RequestRefresh();
  if (is_shell_ui_panel_option(option.id))
    g_settings.shell_ui_panels_enabled = true;
  if (g_settings.vsh_global_theme_enabled &&
      is_global_theme_owned_option(option.id))
    g_settings.vsh_global_theme_enabled = false;
  Telemetry::SetRefreshSeconds(g_settings.overlay_refresh_seconds);
  if (is_overlay_option(option.id))
    s_overlay_dirty = true;
  // All mutable menu options persist immediately.  HEN unload/injection must
  // never discard a setting simply because the user did not visit Save Config.
  request_save();
  s_dirty = true;
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

/* Back to the defaults: every [shell_menu] setting (but the screen format,
   which the menu does not offer) and the overlay settings the menu edits. */
void reset_vsh_settings() {
  const onion::Settings defaults{};
  const int screen_format = g_settings.screen_format;
#define ONION_SHELL_SETTING_BOOL(name, def) g_settings.name = defaults.name;
#define ONION_SHELL_SETTING_INT(name, def) g_settings.name = defaults.name;
#define ONION_SHELL_SETTING_FLOAT(name, def) g_settings.name = defaults.name;
#define ONION_SHELL_SETTING_STRING(name, def) g_settings.name = defaults.name;
#include <onion/shell_menu_settings.def>
#undef ONION_SHELL_SETTING_BOOL
#undef ONION_SHELL_SETTING_INT
#undef ONION_SHELL_SETTING_FLOAT
#undef ONION_SHELL_SETTING_STRING
  g_settings.screen_format = screen_format;
  g_settings.overlay_background = defaults.overlay_background;
  g_settings.all_cpu_usage = defaults.all_cpu_usage;
  g_settings.overlay_enabled = defaults.overlay_enabled;
  g_settings.overlay_ram = defaults.overlay_ram;
  g_settings.overlay_cpu = defaults.overlay_cpu;
  g_settings.overlay_gpu = defaults.overlay_gpu;
  g_settings.overlay_ip = defaults.overlay_ip;
  g_settings.overlay_fps = defaults.overlay_fps;
  g_settings.overlay_pos = defaults.overlay_pos;
  controller_stop_animation();
  PowerIndicator::RestoreNormal(g_settings.vsh_power_indicator_brightness);
  reset_highlight_for_theme_change();
  reapply_customization();
  Telemetry::SetRefreshSeconds(g_settings.overlay_refresh_seconds);
  s_dirty = true;
}

void activate(const Option &option) {
  if (option.type == OptionType::Submenu) {
    begin_page_transition(PageTransitionAction::Push, option.target);
  } else if (option.type == OptionType::ThemeChoice) {
    const int selected_theme = option.id == OptionId::MenuThemeApparition
                                   ? 5
                               : option.id == OptionId::MenuThemeAnomaly ? 2
                                                                         : 0;
    if (g_settings.vsh_menu_theme != selected_theme) {
      g_settings.vsh_menu_theme = selected_theme;
      request_save();
      reset_highlight_for_theme_change();
      LOG_INFO("[ANIVSH:THEME] selected=%s visual_only=1",
               theme_display_name(selected_theme));
    }
  } else if (option.type == OptionType::Toggle) {
    bool &target = toggle_target(option.id);
    target = !target;
    if (option.id == OptionId::ShellTilesCircular ||
        option.id == OptionId::ShellTilesPS5CaseMockup) {
      // The toggle path is used by Cross/Activate.  The old implementation
      // only reloaded HomeUI from change_option(), so the setting changed in
      // memory while the already-mounted game tiles stayed circular.
      request_save();
      shellui_request_home_reload();
      if (option.id == OptionId::ShellTilesPS5CaseMockup) {
        notify(target ? "notify.vsh.tiles.ps5_case_mockup"
                      : "notify.vsh.tiles.ps5_case_stock");
      } else {
        notify(target ? "notify.vsh.tiles.circular" : "notify.vsh.tiles.stock");
      }
      s_dirty = true;
      return;
    }
    if (option.id == OptionId::FanDynamic) {
      g_settings.fan_threshold =
          std::clamp(g_settings.fan_threshold,
                     onion::kFanThresholdMinCelsius,
                     onion::kFanThresholdMaxCelsius);
      if (!AniketosFan::SetEnabled(target, g_settings.fan_threshold))
        notify("notify.fan.adjust_failed");
      request_save();
    }
    if (option.id == OptionId::HomeBookmark) {
      /* Home takes the tab in, or leaves it out, as it starts */
      request_save();
      shellui_request_home_reload();
      LOG_INFO("[BOOKMARK] enabled=%d", target ? 1 : 0);
    }
    if (option.id == OptionId::HomeEmulatorsTab) {
      /* the same for the Emulators tab, whose titles go back to Games */
      request_save();
      shellui_request_home_reload();
      LOG_INFO("[HOMETABS] emulators tab enabled=%d", target ? 1 : 0);
    }
    if (option.id == OptionId::OskGlyphKey) {
      /* the keyboard takes it in when it is next laid out */
      request_save();
      LOG_INFO("[OSK] glyph key enabled=%d", target ? 1 : 0);
    }
    if (option.id == OptionId::CustomizationEnabled) {
      request_save();
      reapply_customization();
      LOG_INFO("[ANIVSH:CUSTOMIZATION] enabled=%d", target ? 1 : 0);
    }
    if (option.id == OptionId::GlobalThemeEnabled) {
      if (target)
        apply_global_theme(g_settings.vsh_left_r, g_settings.vsh_left_g,
                           g_settings.vsh_left_b);
      else
        LOG_INFO("[ANIVSH:GLOBAL-THEME] disabled; keeping applied colors");
    }
    if (is_shell_clock_option(option.id))
      ShellClock::RequestRefresh();
    if (option.id == OptionId::MenuMove)
      s_moving = target;
    if (option.id == OptionId::HomeImageEnabled) {
      HomeVideo::RestartImage();
      if (target && HomeVideo::Images(true).empty())
        notify("notify.vsh.home_image.none");
    }
    if (option.id == OptionId::HomeVideoEnabled) {
      /* An explicit switch-on also retries after a failure or a crash. */
      HomeVideo::Restart();
      if (target && HomeVideo::Videos(true).empty())
        notify("notify.vsh.home_video.none");
    }
    if (option.id == OptionId::ShellBackgroundEnabled) {
      if (target) {
        if (g_settings.shell_background_preset ==
            static_cast<int>(AniketosTheme::Preset::Original))
          g_settings.shell_background_preset =
              static_cast<int>(AniketosTheme::Preset::Custom);
        AniketosTheme::RequestApply();
      } else {
        /* The main switch owns the entire custom background.  Leaving image,
         * force-home or particles enabled made the old wallpaper survive an
         * apparent disable. */
        AniketosTheme::ApplyPresetToSettings(
            g_settings, AniketosTheme::Preset::Original);
        AniketosTheme::NativeShellBackground::DisableImageOverride();
        AniketosTheme::RequestRestore();
      }
    } else if (option.id == OptionId::ShellBackgroundImageEnabled) {
      if (target) {
        AniketosTheme::RequestImageReload();
      } else {
        AniketosTheme::NativeShellBackground::DisableImageOverride();
        AniketosTheme::RequestApply();
      }
    } else if (option.id == OptionId::ShellBackgroundForceHome) {
      AniketosTheme::RequestApply();
    }
    if (option.id != OptionId::GlobalThemeEnabled &&
        g_settings.vsh_global_theme_enabled &&
        is_global_theme_owned_option(option.id))
      g_settings.vsh_global_theme_enabled = false;
  } else if (option.type == OptionType::Strings ||
             option.type == OptionType::SliderInt ||
             option.type == OptionType::SliderFloat ||
             option.type == OptionType::ColorSlider) {
    change_option(option, 1);
  } else if (option.type == OptionType::Action) {
    switch (option.id) {
    case OptionId::SaveConfig:
      if (flush_save())
        notify("notify.vsh.config.saved");
      break;
    case OptionId::LoadConfig:
      if (LoadSettings()) {
        Telemetry::SetRefreshSeconds(g_settings.overlay_refresh_seconds);
        AniketosTheme::RequestApply();
        s_overlay_dirty = true;
        notify("notify.vsh.config.loaded");
      }
      break;
    case OptionId::ShellRestoreOriginal:
      AniketosTheme::ApplyPresetToSettings(
          g_settings, AniketosTheme::Preset::Original);
      AniketosTheme::RequestRestore();
      (void)flush_save();
      notify("notify.vsh.background.restored");
      break;
    case OptionId::ShellBackgroundReloadImage:
      if (g_settings.shell_background_image_enabled) {
        AniketosTheme::RequestImageReload();
        notify("notify.vsh.background.reload_requested");
      } else {
        notify("notify.vsh.background.enable_first");
      }
      break;
    case OptionId::HomeImageRefresh:
      notify("notify.vsh.background.found_count",
             HomeVideo::Images(true).size());
      s_dirty = true;
      break;
    case OptionId::HomeVideoRefresh:
      notify("notify.vsh.home_video.found_count",
             HomeVideo::Videos(true).size());
      s_dirty = true;
      break;
    case OptionId::ShellBackgroundRefreshImages:
      scan_background_images(true);
      notify("notify.vsh.background.found_count", s_background_images.size());
      break;
    case OptionId::BuzzerNormal:
      (void)Buzzer::PlayTest(Buzzer::Pattern::Short);
      break;
    case OptionId::BuzzerShort:
      (void)Buzzer::PlayTest(Buzzer::Pattern::Short);
      break;
    case OptionId::BuzzerError:
      (void)Buzzer::PlayTest(Buzzer::Pattern::Error);
      break;
    case OptionId::BuzzerLong:
      (void)Buzzer::PlayTest(Buzzer::Pattern::Long);
      break;
    case OptionId::PowerIndicatorRestoreNormal:
      g_settings.vsh_power_indicator_color =
          static_cast<int>(PowerIndicator::Color::White);
      g_settings.vsh_power_indicator_animation =
          static_cast<int>(PowerIndicator::Animation::Normal);
      PowerIndicator::RestoreNormal(g_settings.vsh_power_indicator_brightness);
      (void)flush_save();
      break;
    case OptionId::RevealSerialNumber:
      if (s_show_serial) {
        s_show_serial = false;
        break;
      }
      {
        /* Asked every time: the serial number and the two identifiers single
           this console out, and are not to end up in a screenshot or a
           stream by accident. The menu steps aside for the system's dialog
           and comes back when it is answered (Tick). */
        SystemDialogs::AlertOptions dialog;
        dialog.style = SystemDialogs::AlertStyle::Popup;
        dialog.title = tr("vsh.info.reveal.title");
        dialog.message = tr("vsh.info.reveal.message");
        dialog.positive = tr("vsh.info.reveal.show");
        dialog.negative = tr("vsh.info.reveal.cancel");
        std::string failure;
        Hide();
        if (SystemDialogs::ShowAlert(dialog, &failure)) {
          s_confirming_reveal = true;
        } else {
          LOG_WARN("[ANIVSH] reveal dialog: %s", failure.c_str());
          Show(); /* no dialog, no reveal */
        }
      }
      return;
    case OptionId::ControllerRestore:
      controller_stop_animation();
      break;
    case OptionId::ControllerBreathe:
      controller_start_animation(ControllerAnimation::Breathe);
      break;
    case OptionId::ControllerPulse:
      controller_start_animation(ControllerAnimation::Pulse);
      break;
    case OptionId::ControllerRainbow:
      controller_start_animation(ControllerAnimation::Rainbow);
      break;
    case OptionId::ControllerSpectrum:
      controller_start_animation(ControllerAnimation::Spectrum);
      break;
    case OptionId::ControllerNeonWave:
      controller_start_animation(ControllerAnimation::NeonWave);
      break;
    case OptionId::ControllerStopAnimation:
      controller_stop_animation();
      break;
    case OptionId::VibrationShort:
    case OptionId::VibrationLong:
    case OptionId::VibrationHeartbeat:
    case OptionId::VibrationLight:
    case OptionId::VibrationHeavy:
    case OptionId::VibrationWave: {
      const VibrationPattern pattern =
          option.id == OptionId::VibrationShort      ? VibrationPattern::Short
          : option.id == OptionId::VibrationLong     ? VibrationPattern::Long
          : option.id == OptionId::VibrationHeartbeat ? VibrationPattern::Heartbeat
          : option.id == OptionId::VibrationLight    ? VibrationPattern::Light
          : option.id == OptionId::VibrationHeavy    ? VibrationPattern::Heavy
                                                     : VibrationPattern::Wave;
      if (!controller_vibrate(pattern))
        notify("notify.controller.vibration_failed");
      break;
    }
    case OptionId::VibrationStop:
      controller_stop_vibration();
      break;
    case OptionId::LedChase:
      controller_start_player_led_animation(PlayerLedAnimation::Chase);
      break;
    case OptionId::LedCountdown:
      controller_start_player_led_animation(PlayerLedAnimation::Countdown);
      break;
    case OptionId::LedBlink:
      controller_start_player_led_animation(PlayerLedAnimation::Blink);
      break;
    case OptionId::LedRestore:
      if (!controller_restore_player_leds())
        notify("notify.controller.leds_failed");
      break;
    case OptionId::ResetConfig:
      reset_vsh_settings();
      s_overlay_dirty = true;
      (void)flush_save();
      notify("notify.vsh.config.reset");
      break;
    case OptionId::MenuResetPosition:
      s_moving = false;
      s_last_move_tick_ms = 0;
      g_settings.vsh_menu_x = s_viewport_valid
                                  ? s_viewport_width * 0.5f
                                  : kReferenceWidth * 0.5f;
      g_settings.vsh_menu_y = s_viewport_valid
                                  ? s_viewport_height * 0.5f
                                  : kReferenceHeight * 0.5f;
      (void)flush_save();
      break;
    case OptionId::HomeBookmarkRename:
      /* the system's prompt, on Home once this menu is gone */
      Hide();
      HomeBookmark::RequestRename();
      break;
    case OptionId::HomeTabRenameGames:
    case OptionId::HomeTabRenameMedia:
      Hide();
      HomeTabs::RequestRename(option.id == OptionId::HomeTabRenameGames
                                  ? HomeTabs::StockTab::Games
                                  : HomeTabs::StockTab::Media);
      break;
    case OptionId::HomeTabRestoreNames:
      /* Home reloads behind the menu, as Refresh Home does */
      HomeTabs::RestoreNames();
      break;
    case OptionId::RefreshHome:
      // Keep the overlay open until ReloadApp consumes this Cross press. Its
      // input-suppression hook then prevents the same button from activating
      // the focused Settings tile underneath the refreshed Home scene.
      shellui_request_home_reload();
      notify("notify.vsh.power.home_refreshed");
      break;
    case OptionId::RestartShell:
      (void)flush_save();
      Hide();
      if (!AniketosRuntime::service_available(false, "shell-restart") ||
          !IPC_Client::getInstance(false).RestartShellUI())
        notify("notify.vsh.power.failed");
      break;
    case OptionId::RestMode:
      (void)flush_save();
      Hide();
      /* No daemon notice needed: it follows Rest Mode through SceSysCore. */
      if (sceApplicationSystemSuspend() != 0)
        notify("notify.vsh.power.failed");
      break;
    case OptionId::RebootConsole:
      (void)flush_save();
      Hide();
      if (sceSystemServiceRequestReboot() != 0)
        notify("notify.vsh.power.failed");
      break;
    case OptionId::PowerOff:
      (void)flush_save();
      Hide();
      if (sceSystemServiceRequestPowerOff() != 0)
        notify("notify.vsh.power.failed");
      break;
    case OptionId::UnloadStack:
      if (!s_unload_armed) {
        s_unload_armed = true;
        notify("notify.vsh.unload.confirm");
      } else {
        LOG_INFO("[ANIVSH:AKARI] confirmed stack shutdown");
        if (AniketosRuntime::service_available(false, "stack-shutdown"))
          IPC_Client::getInstance(false).ShutdownStack();
      }
      break;
    default:
      break;
    }
  }
  if (is_overlay_option(option.id))
    s_overlay_dirty = true;
  // Actions and toggles can change settings too (including global theme and
  // native Shell customisation), so persist their final state consistently.
  if (option.type != OptionType::Submenu)
    request_save();
  s_dirty = true;
  LOG_INFO("[ANIVSH:AKARI] select page=%s option=%s",
           page_title(s_stack.back().page), option.title.c_str());
}

} // namespace
