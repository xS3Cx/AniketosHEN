bool &toggle_target(OptionId id) {
  switch (id) {
  case OptionId::GlobalThemeEnabled:
    return g_settings.vsh_global_theme_enabled;
  case OptionId::OverlayEnabled: return g_settings.home_osd_enabled;
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
  case OptionId::PerformanceOverlayFps: return g_settings.overlay_fps;
  case OptionId::PerformanceOverlayFan: return g_settings.overlay_fan;
  case OptionId::ShellBackgroundEnabled:
    return g_settings.shell_background_enabled;
  case OptionId::ShellBackgroundForceHome:
    return g_settings.shell_background_force_home;
  case OptionId::ShellBackgroundPs3Waves:
    return g_settings.shell_background_ps3_waves;
  case OptionId::ShellTilesCircular: return g_settings.shell_tiles_circular;
  case OptionId::HomeEmulatorsTab: return g_settings.home_tab_emulators;
  case OptionId::HomeTabIcons: return g_settings.home_tab_icons;
  case OptionId::HomeBookmark: return g_settings.home_bookmark;
  case OptionId::HomeHideApps: return g_settings.home_hide_apps;
  case OptionId::OskGlyphKey: return g_settings.osk_glyph_key;
  case OptionId::OskEmoji: return g_settings.osk_emoji;
  case OptionId::MouseControl: return g_settings.mouse_control_enabled;
  case OptionId::ShellTilesOutlineEnabled:
    return g_settings.shell_tiles_outline_enabled;
  case OptionId::ShellTilesOutlineSyncBackground:
    return g_settings.shell_tiles_outline_sync_background;
  case OptionId::ShellClockEnabled: return g_settings.shell_clock_enabled;
  case OptionId::HomeVideoEnabled: return g_settings.home_video_enabled;
  case OptionId::HomeBackgroundGlobal:
    return g_settings.home_background_global;
  case OptionId::HomeVideoSound: return g_settings.home_video_sound;
  case OptionId::HomeImageEnabled: return g_settings.home_image_enabled;
  case OptionId::CustomFontEnabled: return g_settings.custom_font_enabled;
  case OptionId::PadSpeakerMirror: return g_settings.pad_speaker_mirror;
  case OptionId::AccountAvatarEnabled:
    return g_settings.account_avatar_enabled;
  case OptionId::DevkitOverlayInfo: return g_settings.devkit_overlay_info;
  case OptionId::DevkitOverlayExpired:
    return g_settings.devkit_overlay_expired;
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
  case OptionId::LogoColorEnabled: return g_settings.logo_color_enabled;
  case OptionId::MenuBorderEnabled: return g_settings.vsh_border_enabled;
  case OptionId::MenuMove:
  default: return s_moving;
  }
}

int &int_target(OptionId id) {
  switch (id) {
  case OptionId::GlobalThemeAnimation:
    return g_settings.vsh_color_animation;
  case OptionId::GlobalThemeAnimationSpeed:
    return g_settings.vsh_color_animation_speed;
  case OptionId::OverlayRefresh: return g_settings.overlay_refresh_seconds;
  case OptionId::OverlayFpsPrecision: return g_settings.overlay_fps_precision;
  case OptionId::OverlayTempUnit: return g_settings.overlay_temp_unit;
  case OptionId::OverlayMemoryUnit: return g_settings.overlay_memory_unit;
  case OptionId::OverlayHorizontal: return g_settings.overlay_horizontal;
  case OptionId::OverlayVertical: return g_settings.overlay_vertical;
  case OptionId::BuzzerVolume: return g_settings.vsh_buzzer_volume;
  case OptionId::PlayerLeds: return s_player_leds;
  case OptionId::LightBrightness: return s_light_brightness;
  case OptionId::PowerIndicatorColor:
    return g_settings.vsh_power_indicator_color;
  case OptionId::FanThreshold: return g_settings.fan_threshold;
  case OptionId::VibrationStrength:
    return g_settings.controller_vibration_strength;
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
  case OptionId::ShellBackgroundParticles:
    /* a value saved before Bottom/Spread went, read as what it now means */
    g_settings.shell_background_particles = static_cast<int>(
        AniketosTheme::NativeShellBackground::ParticleModeFromSetting(
            g_settings.shell_background_particles));
    return g_settings.shell_background_particles;
  case OptionId::ShellTilesOutlineAnimation:
    return g_settings.shell_tiles_outline_animation;
  case OptionId::ShellTilesOutlineAnimationSpeed:
    return g_settings.shell_tiles_outline_animation_speed;
  case OptionId::HomeParticlesCount: return g_settings.home_particles_count;
  case OptionId::HomeVideoVolume: return g_settings.home_video_volume;
  case OptionId::PadSpeakerVolume: return g_settings.pad_speaker_volume;
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
  case OptionId::MenuAnimation:
  default: return g_settings.vsh_animation;
  }
}

int selector_index_for(OptionId id) {
  switch (id) {
  case OptionId::MenuThemeSelector:
    return theme_choice_index(g_settings.vsh_menu_theme);
  case OptionId::MenuAppearanceSelector:
    return std::clamp(g_settings.vsh_menu_appearance, 0, 1);
  case OptionId::HomeVideoFile:
    return home_video_index();
  case OptionId::CustomFontFile:
    return custom_font_index();
  case OptionId::HomeImageFile:
    return home_image_index();
  case OptionId::AccountAvatarFile:
    return account_avatar_index();
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
  case OptionId::MenuOpacity: return g_settings.vsh_background_opacity;
  case OptionId::MenuWidth:
  default: return g_settings.vsh_menu_width;
  }
}
