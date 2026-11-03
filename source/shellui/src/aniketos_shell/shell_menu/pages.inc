/* Each family below fills `options` for the pages it owns and leaves the rest
   alone; every page belongs to one family. */

void hidden_apps_page(std::vector<Option> &options) {
  const std::vector<HiddenApps::Entry> hidden = HiddenApps::List();
  options.push_back(toggle(tr("vsh.hidden.enable"), OptionId::HomeHideApps,
                           g_settings.home_hide_apps));
  for (const HiddenApps::Entry &entry : hidden) {
    Option row = action(
        TitleInfo::WithName("vsh.hidden.show_fmt", entry.name).c_str(),
        OptionId::HiddenAppShow);
    row.external_key = entry.title_id;
    options.push_back(row);
  }
  if (hidden.size() > 1)
    options.push_back(
        action(tr("vsh.hidden.show_all"), OptionId::HiddenAppsShowAll));
  if (hidden.empty())
    options.push_back(value(tr("vsh.hidden.none"), ""));
  options.push_back(value(tr("vsh.hidden.hint"), ""));
}

/* Who the console is, its software, its hardware, then memory, disk and
   network; the button that shows the hidden numbers comes last. */
void information_page(std::vector<Option> &options) {
  const Telemetry::Snapshot telemetry = Telemetry::GetSnapshot();
  const auto reading = [&options](const char *title, std::string text) {
    options.push_back(value(title, std::move(text)));
  };
  /* 32 hex digits are wider than the usual value column, so these two rows
     ask for a wide one: name on the left, all the digits on the right, as
     the serial number is. */
  const auto identifier = [&options](const char *name, const std::string &hex,
                                     size_t keep) {
    if (hex.size() != 32)
      return;
    std::string digits = hex;
    if (!s_show_serial)
      for (size_t at = keep * 2; at < digits.size(); ++at)
        digits[at] = '*';
    Option row = value(name, digits);
    row.wide_value = true;
    options.push_back(row);
  };

  if (telemetry.system_name_valid)
    reading(tr("vsh.info.system_name"), telemetry.system_name);
  if (telemetry.model_valid)
    reading(tr("vsh.info.model"), telemetry.model);
  if (telemetry.serial_valid)
    reading(tr("vsh.info.serial"),
            s_show_serial ? telemetry.serial : masked_serial(telemetry.serial));
  identifier(tr("vsh.info.idps"), telemetry.idps, 4);
  identifier(tr("vsh.info.open_psid"), telemetry.open_psid, 0);
  reading(tr("vsh.info.firmware"), telemetry.firmware.empty()
                                       ? tr("vsh.info.unavailable")
                                       : telemetry.firmware);
  if (!telemetry.kernel_build.empty())
    reading(tr("vsh.info.kernel"), short_kernel_build(telemetry.kernel_build));
  if (!telemetry.soc.empty())
    reading(tr("vsh.info.soc"), telemetry.soc);
  if (telemetry.cpu_threads > 0)
    reading(tr("vsh.info.cpu_threads"), std::to_string(telemetry.cpu_threads));
  if (telemetry.cpu_frequency_valid)
    reading(tr("vsh.info.cpu_frequency"),
            std::to_string(telemetry.cpu_frequency_hz / 1000000u) + " MHz");
  if (telemetry.base_clock_hz > 0)
    reading(tr("vsh.info.base_clock"),
            number(static_cast<float>(telemetry.base_clock_hz) / 1e6f, 1) +
                " MHz");
  reading(tr("vsh.info.disc_drive"), telemetry.disc_drive
                                         ? tr("vsh.info.detected")
                                         : tr("vsh.info.not_detected"));
  if (telemetry.memory_valid) {
    reading(tr("vsh.info.system_memory"),
            std::to_string(telemetry.memory_used_mb) + " / " +
                std::to_string(telemetry.memory_total_mb) + " MB");
    reading(tr("vsh.info.free_memory"),
            std::to_string(telemetry.memory_free_mb) + " MB");
  }
  if (telemetry.swap_bytes > 0)
    reading(tr("vsh.info.swap"),
            number(static_cast<float>(telemetry.swap_bytes) /
                       (1024.0f * 1024.0f * 1024.0f),
                   1) + " GB");
  if (telemetry.storage_valid)
    reading(tr("vsh.info.storage"),
            /* what is left, of how much */
            storage_amount(telemetry.storage_free_bytes,
                           telemetry.storage_total_bytes) + " (" +
                number(100.0f - telemetry.storage_used_percent, 1) + "%)");
  if (telemetry.uptime_valid)
    reading(tr("vsh.info.uptime"), uptime_text(telemetry.uptime_seconds));
  if (telemetry.local_ip_valid)
    reading(tr("vsh.info.local_ip"), telemetry.local_ip);
  if (telemetry.serial_valid)
    options.push_back(action(s_show_serial ? tr("vsh.info.hide_serial")
                                           : tr("vsh.info.show_serial"),
                             OptionId::RevealSerialNumber));
}

void fan_page(std::vector<Option> &options) {
  const Telemetry::Snapshot telemetry = Telemetry::GetSnapshot();
  options = {
      toggle(tr("vsh.m.fan_manual"), OptionId::FanDynamic,
             g_settings.enable_fan_speed),
      slider_int(tr("vsh.m.fan_threshold"), OptionId::FanThreshold,
                 std::clamp(g_settings.fan_threshold,
                            onion::kFanThresholdMinCelsius,
                            onion::kFanThresholdMaxCelsius),
                 onion::kFanThresholdMinCelsius,
                 onion::kFanThresholdMaxCelsius, 1),
  };
  if (telemetry.cpu_temperature_valid)
    options.push_back(value(tr("vsh.info.cpu_temp"),
                            std::to_string(telemetry.cpu_temperature_c) +
                                kCelsiusSuffix));
  if (telemetry.soc_temperature_valid)
    options.push_back(value(tr("vsh.info.soc_temp"),
                            std::to_string(telemetry.soc_temperature_c) +
                                kCelsiusSuffix));
  if (telemetry.fan_duty_valid)
    options.push_back(value(tr("vsh.m.fan_speed"),
                            number(telemetry.fan_duty_percent, 1) + "%"));
}

void console_pages(Page page, std::vector<Option> &options) {
  switch (page) {
  case Page::ConsoleManagement:
    options = {submenu(Page::Information), submenu(Page::HiddenApps),
               submenu(Page::Power), submenu(Page::Fan),
               submenu(Page::PowerIndicator), submenu(Page::Buzzer)};
    break;
  case Page::HiddenApps:
    hidden_apps_page(options);
    break;
  case Page::Information:
    information_page(options);
    break;
  case Page::Power:
    /* restart and power off ask for a second press */
    options = {
        action(tr("vsh.m.rest_mode"), OptionId::RestMode),
        action(tr("vsh.power.reboot_console"), OptionId::RebootConsole),
        action(tr("vsh.power.power_off"), OptionId::PowerOff),
    };
    break;
  case Page::Fan:
    fan_page(options);
    break;
  case Page::PowerIndicator: {
    const char *colors[] = {tr("vsh.indicator.white"),
                            tr("vsh.indicator.blue"),
                            tr("vsh.indicator.orange")};
    options = {choice(tr("vsh.indicator.color"), OptionId::PowerIndicatorColor,
                      colors, g_settings.vsh_power_indicator_color)};
    break;
  }
  case Page::Buzzer: {
    const char *volumes[] = {tr("vsh.buzzer.volume_high"),
                             tr("vsh.buzzer.volume_medium"),
                             tr("vsh.buzzer.volume_low")};
    options = {
        choice(tr("vsh.buzzer.volume"), OptionId::BuzzerVolume, volumes,
               g_settings.vsh_buzzer_volume),
        action(tr("vsh.debug.buzzer.short"), OptionId::BuzzerShort),
        action(tr("vsh.debug.buzzer.long"), OptionId::BuzzerLong),
        action(tr("vsh.debug.buzzer.error"), OptionId::BuzzerError),
    };
    break;
  }
  default:
    break;
  }
}

void controller_pages(Page page, std::vector<Option> &options) {
  switch (page) {
  case Page::Controller: {
    /* The light bar takes the colour as the sliders move; colour 2 is the
       pulse animation's second colour (controller_tools). */
    const char *levels[] = {tr("controller.leds.bright"),
                            tr("controller.leds.medium"),
                            tr("controller.leds.dim")};
    options = {
        color_slider(tr("controller.lightbar"), OptionId::ColorController),
        color_slider(tr("vsh.m.lightbar_color2"), OptionId::ColorController2),
        submenu(Page::ControllerAnimations),
        /* the light bar and the player lights share one brightness */
        choice(tr("controller.leds.brightness"), OptionId::LightBrightness,
               levels, s_light_brightness),
        submenu(Page::ControllerLeds),
        submenu(Page::ControllerVibration),
        toggle(tr("controller.pad_speaker_mirror"), OptionId::PadSpeakerMirror,
               g_settings.pad_speaker_mirror),
        slider_int(tr("controller.pad_speaker_volume"),
                   OptionId::PadSpeakerVolume, g_settings.pad_speaker_volume,
                   0, 100, 5),
        action(tr("controller.restore"), OptionId::ControllerRestore),
    };
    break;
  }
  case Page::ControllerAnimations:
    options = {
        action(tr("controller.animation.breathe"), OptionId::ControllerBreathe),
        action(tr("controller.animation.pulse"), OptionId::ControllerPulse),
        action(tr("controller.animation.rainbow"), OptionId::ControllerRainbow),
        action(tr("controller.animation.spectrum"),
               OptionId::ControllerSpectrum),
        action(tr("controller.animation.neon"), OptionId::ControllerNeonWave),
    };
    break;
  case Page::ControllerLeds: {
    const char *players[] = {"1", "2", "3", "4"};
    options = {
        choice(tr("vsh.m.player_number"), OptionId::PlayerLeds, players,
               s_player_leds),
        action(tr("controller.leds.chase"), OptionId::LedChase),
        action(tr("controller.leds.countdown"), OptionId::LedCountdown),
        action(tr("controller.leds.blink"), OptionId::LedBlink),
        action(tr("vsh.m.player_lights_restore"), OptionId::LedRestore),
    };
    break;
  }
  case Page::ControllerVibration:
    options = {
        slider_int(tr("controller.vibration.strength"),
                   OptionId::VibrationStrength,
                   g_settings.controller_vibration_strength, 10, 100, 10),
        action(tr("controller.vibration.short"), OptionId::VibrationShort),
        action(tr("controller.vibration.long"), OptionId::VibrationLong),
        action(tr("controller.vibration.heartbeat"),
               OptionId::VibrationHeartbeat),
        action(tr("controller.vibration.light"), OptionId::VibrationLight),
        action(tr("controller.vibration.heavy"), OptionId::VibrationHeavy),
        action(tr("controller.vibration.wave"), OptionId::VibrationWave),
        action(tr("controller.vibration.stop"), OptionId::VibrationStop),
    };
    break;
  default:
    break;
  }
}

void home_pages(Page page, std::vector<Option> &options) {
  switch (page) {
  case Page::HomeScreen:
    options = {submenu(Page::HomeVideo), submenu(Page::ShellClock),
               submenu(Page::HomeTabs), submenu(Page::ShellTiles),
               submenu(Page::HomeProfile)};
    break;
  case Page::HomeVideo:
    /* a video, else a picture, with effects and particles over it */
    options = {submenu(Page::HomeBgVideo),
               submenu(Page::HomeBgImage),
               submenu(Page::HomeBgEffects),
               submenu(Page::HomeParticles),
               /* the video or picture behind the system apps as well */
               toggle(tr("vsh.home_video.global"),
                      OptionId::HomeBackgroundGlobal,
                      g_settings.home_background_global)};
    break;
  case Page::HomeBgVideo:
    options = {
        toggle(tr("vsh.home_video.enable"), OptionId::HomeVideoEnabled,
               g_settings.home_video_enabled),
        file_choice(tr("vsh.home_video.file"), OptionId::HomeVideoFile,
                    HomeVideo::Videos(), g_settings.home_video_path,
                    tr("vsh.home_video.no_videos")),
        toggle(tr("vsh.home_video.sound"), OptionId::HomeVideoSound,
               g_settings.home_video_sound),
        slider_int(tr("vsh.home_video.volume"), OptionId::HomeVideoVolume,
                   g_settings.home_video_volume, 0, 100, 5),
        action(tr("vsh.home_video.refresh"), OptionId::HomeVideoRefresh),
        /* where to put files, as seen over FTP or on a USB drive */
        value(tr("vsh.home_video.folder"),
              ONION_DIST_DATA_ROOT "/ShellUI/videos"),
        value(tr("vsh.home_video.usb_folder"),
              "USB: /" ONION_DIST_DIR_NAME "/videos"),
    };
    break;
  case Page::HomeBgImage:
    /* the still image, shown when no video is */
    options = {
        toggle(tr("vsh.home_image.enable"), OptionId::HomeImageEnabled,
               g_settings.home_image_enabled),
        file_choice(tr("vsh.home_image.file"), OptionId::HomeImageFile,
                    HomeVideo::Images(), g_settings.home_image_path,
                    tr("vsh.bg.no_images")),
        action(tr("vsh.bg.refresh_list"), OptionId::HomeImageRefresh),
        value(tr("vsh.home_image.folder"),
              ONION_DIST_DATA_ROOT "/ShellUI/background"),
    };
    break;
  case Page::HomeBgEffects:
    /* layers over the video or picture, applied as the sliders move */
    options = {slider_int(tr("vsh.home_bg.dim"), OptionId::HomeBackgroundDim,
                          g_settings.home_bg_dim, 0, 100, 5),
               slider_int(tr("vsh.home_bg.haze"), OptionId::HomeBackgroundHaze,
                          g_settings.home_bg_haze, 0, 100, 5),
               slider_int(tr("vsh.home_bg.vignette"),
                          OptionId::HomeBackgroundVignette,
                          g_settings.home_bg_vignette, 0, 100, 5),
               slider_int(tr("vsh.home_bg.edges"),
                          OptionId::HomeBackgroundEdges,
                          g_settings.home_bg_edges, 0, 100, 5)};
    break;
  case Page::HomeParticles:
    /* particles floating over the wallpaper or video: the built-in styles, or
       the pictures in the particles folder */
    options = {toggle(tr("vsh.home_particles.enable"),
                      OptionId::HomeParticlesEnabled,
                      g_settings.home_particles_enabled),
               strings(tr("vsh.home_particles.style"),
                       OptionId::HomeParticlesStyle,
                       tr(HomeParticles::StyleName(
                           g_settings.home_particles_style)),
                       HomeParticles::StyleCount()),
               slider_int(tr("vsh.home_particles.count"),
                          OptionId::HomeParticlesCount,
                          g_settings.home_particles_count, 20, 150, 10),
               slider_int(tr("vsh.home_particles.hue"),
                          OptionId::HomeParticlesHue,
                          g_settings.home_particles_hue, 0, 355, 5),
               slider_int(tr("vsh.home_particles.speed"),
                          OptionId::HomeParticlesSpeed,
                          g_settings.home_particles_speed, 25, 300, 25),
               value(tr("vsh.home_particles.folder"),
                     ONION_DIST_DATA_ROOT "/ShellUI/particles")};
    break;
  case Page::ShellClock: {
    const char *hour_formats[] = {tr("vsh.clock.system"),
                                  tr("vsh.clock.24_hour"),
                                  tr("vsh.clock.12_hour")};
    const char *date_orders[] = {tr("vsh.clock.system"), "DD/MM/YYYY",
                                 "MM/DD/YYYY", "YYYY/MM/DD"};
    const char *month_styles[] = {tr("vsh.clock.numeric"),
                                  tr("vsh.clock.short"),
                                  tr("vsh.clock.long")};
    const char *weekday_styles[] = {tr("vsh.clock.off"),
                                    tr("vsh.clock.short"),
                                    tr("vsh.clock.long")};
    const char *separators[] = {tr("vsh.clock.system"), "/", ".", "-",
                                tr("vsh.clock.space"),
                                tr("vsh.clock.middle_dot")};
    /* the time, then the date */
    options = {
        toggle(tr("vsh.clock.enable"), OptionId::ShellClockEnabled,
               g_settings.shell_clock_enabled),
        choice(tr("vsh.clock.hour_format"), OptionId::ShellClockHourFormat,
               hour_formats, g_settings.shell_clock_hour_format),
        toggle(tr("vsh.clock.seconds"), OptionId::ShellClockSeconds,
               g_settings.shell_clock_show_seconds),
        slider_int(tr("vsh.clock.time_font_size"),
                   OptionId::ShellClockTimeFontSize,
                   g_settings.shell_clock_time_font_size, 24, 56, 1),
        color_slider(tr("vsh.clock.time_color"), OptionId::ColorClockTime),
        toggle(tr("vsh.clock.date"), OptionId::ShellClockDate,
               g_settings.shell_clock_show_date),
        choice(tr("vsh.clock.date_order"), OptionId::ShellClockDateOrder,
               date_orders, g_settings.shell_clock_date_order),
        choice(tr("vsh.clock.month_style"), OptionId::ShellClockMonthStyle,
               month_styles, g_settings.shell_clock_month_style),
        choice(tr("vsh.clock.weekday_style"), OptionId::ShellClockWeekdayStyle,
               weekday_styles, g_settings.shell_clock_weekday_style),
        toggle(tr("vsh.clock.year"), OptionId::ShellClockYear,
               g_settings.shell_clock_show_year),
        choice(tr("vsh.clock.date_separator"),
               OptionId::ShellClockDateSeparator, separators,
               g_settings.shell_clock_date_separator),
        slider_int(tr("vsh.clock.date_font_size"),
                   OptionId::ShellClockDateFontSize,
                   g_settings.shell_clock_date_font_size, 10, 28, 1),
        slider_int(tr("vsh.clock.date_spacing"),
                   OptionId::ShellClockDateSpacing,
                   g_settings.shell_clock_date_spacing, -80, 160, 1),
        color_slider(tr("vsh.clock.date_color"), OptionId::ColorClockDate),
    };
    break;
  }
  case Page::HomeTabs:
    /* Home's tabs in their order: Games and Media under names of the
       user's, Emulators, the bookmark tab (home_tabs, home_bookmark) */
    options = {
        action(tr("vsh.tiles.rename_games"), OptionId::HomeTabRenameGames),
        action(tr("vsh.tiles.rename_media"), OptionId::HomeTabRenameMedia),
        action(tr("vsh.tiles.restore_tab_names"),
               OptionId::HomeTabRestoreNames),
        toggle(tr("vsh.tiles.emulators_tab"), OptionId::HomeEmulatorsTab,
               g_settings.home_tab_emulators),
        toggle(tr("vsh.tiles.tab_icons"), OptionId::HomeTabIcons,
               g_settings.home_tab_icons),
        toggle(tr("vsh.tiles.bookmark"), OptionId::HomeBookmark,
               g_settings.home_bookmark),
        action(tr("vsh.tiles.rename_bookmark"), OptionId::HomeBookmarkRename),
    };
    break;
  case Page::ShellTiles:
    options = {
        toggle(tr("vsh.tiles.circular"), OptionId::ShellTilesCircular,
               g_settings.shell_tiles_circular),
        /* percent of a full circle; 30 is about Sony's corner */
        slider_int(tr("vsh.tiles.rounding"), OptionId::ShellTilesCornerRadius,
                   g_settings.shell_tiles_corner_radius, 0, 100, 5),
    };
    break;
  case Page::HomeProfile: {
    /* the dot on the user's avatar, as if signed in (script_injector) */
    const char *dots[] = {tr("vsh.home_presence.system"),
                          tr("vsh.home_presence.online"),
                          tr("vsh.home_presence.busy"),
                          tr("vsh.home_presence.offline")};
    /* the user's picture as a picture or animated GIF/WebP of theirs */
    options = {
        toggle(tr("vsh.account_avatar.enable"), OptionId::AccountAvatarEnabled,
               g_settings.account_avatar_enabled),
        file_choice(tr("vsh.account_avatar.file"), OptionId::AccountAvatarFile,
                    AccountAvatar::Files(), g_settings.account_avatar_path,
                    tr("vsh.account_avatar.no_files")),
        action(tr("vsh.account_avatar.refresh"),
               OptionId::AccountAvatarRefresh),
        value(tr("vsh.account_avatar.folder"),
              ONION_DIST_DATA_ROOT "/ShellUI/avatars"),
        value(tr("vsh.account_avatar.supported"), "GIF, WebP, PNG, JPG"),
        value(tr("vsh.account_avatar.animated_limit"),
              tr("vsh.account_avatar.animated_limit_value")),
        choice(tr("vsh.home_presence.dot"), OptionId::HomePresenceDot, dots,
               g_settings.home_presence_dot),
    };
    break;
  }
  default:
    break;
  }
}

/* The console's own interface. */
void system_look_pages(Page page, std::vector<Option> &options) {
  switch (page) {
  case Page::SystemCustomize: {
    /* the look of the system's own on/off switches (toggle_styles.cpp) */
    const int count = AniketosTheme::ToggleStyles::Count();
    const int style = std::clamp(g_settings.shell_toggle_style, 0, count - 1);
    const char *name = AniketosTheme::ToggleStyles::Name(style);
    options = {submenu(Page::GlobalCustomization),
               submenu(Page::ShellBackground),
               submenu(Page::ShellOutline),
               submenu(Page::ShellUIPanels),
               submenu(Page::IconColor),
               strings(tr("vsh.toggles.style"), OptionId::ShellToggleStyle,
                       name ? name : tr("vsh.toggles.system"), count),
               toggle(tr("vsh.font.enable"), OptionId::CustomFontEnabled,
                      g_settings.custom_font_enabled),
               file_choice(tr("vsh.font.file"), OptionId::CustomFontFile,
                           AniketosTheme::CustomFont::Files(),
                           g_settings.custom_font_path, tr("vsh.font.none")),
               value(tr("vsh.font.folder"), ONION_DIST_DATA_ROOT "/fonts")};
    break;
  }
  case Page::GlobalCustomization:
    options = {
        toggle(tr("vsh.global_theme.enable"), OptionId::GlobalThemeEnabled,
               g_settings.vsh_global_theme_enabled),
        color_slider(tr("vsh.global_theme.color"), OptionId::ColorGlobal),
        color_animation_choice(tr("vsh.common.animation"),
                               OptionId::GlobalThemeAnimation),
        animation_speed_slider(OptionId::GlobalThemeAnimationSpeed,
                               g_settings.vsh_color_animation_speed),
        toggle(tr("vsh.logo.enable"), OptionId::LogoColorEnabled,
               g_settings.logo_color_enabled),
        color_slider(tr("vsh.logo.color"), OptionId::ColorLogo),
    };
    break;
  case Page::ShellBackground: {
    const AniketosTheme::ThemeConfig config =
        AniketosTheme::ConfigFromSettings(g_settings);
    options = {
        toggle(tr("vsh.bg.enable_native"), OptionId::ShellBackgroundEnabled,
               g_settings.shell_background_enabled),
        toggle(tr("vsh.bg.force_home"), OptionId::ShellBackgroundForceHome,
               g_settings.shell_background_force_home),
        /* the PS3 XMB wave switch (OptionId::ShellBackgroundPs3Waves) stays
           out of the menu until the wave is drawn */
        color_slider(tr("vsh.bg.background_color"), OptionId::ColorBackground),
        color_slider(tr("vsh.bg.glow_particles_color"), OptionId::ColorGlow),
        slider_int(tr("vsh.col.glow_intensity"), OptionId::ShellGlowIntensity,
                   g_settings.shell_background_glow_intensity, 0, 200, 5),
        strings(tr("vsh.common.animation"), OptionId::ShellBackgroundAnimation,
                animation_mode_name(g_settings.shell_background_animation),
                AniketosTheme::AnimationModeCount()),
        animation_speed_slider(OptionId::ShellBackgroundAnimationSpeed,
                               g_settings.shell_background_animation_speed),
        strings(tr("vsh.bg.native_particles"),
                OptionId::ShellBackgroundParticles,
                AniketosTheme::NativeShellBackground::ParticleModeName(
                    config.particles),
                AniketosTheme::NativeShellBackground::ParticleModeCount()),
        action(tr("vsh.bg.restore_original"), OptionId::ShellRestoreOriginal),
    };
    break;
  }
  case Page::ShellOutline:
    options = {
        toggle(tr("vsh.tiles.custom_outline"),
               OptionId::ShellTilesOutlineEnabled,
               g_settings.shell_tiles_outline_enabled),
        toggle(tr("vsh.tiles.sync_background"),
               OptionId::ShellTilesOutlineSyncBackground,
               g_settings.shell_tiles_outline_sync_background),
    };
    if (g_settings.shell_tiles_outline_sync_background) {
      options.push_back(value(
          tr("vsh.tiles.active_source"), tr("vsh.tiles.source_background")));
      break;
    }
    options.push_back(
        color_slider(tr("vsh.col.outline.color"), OptionId::ColorOutline));
    options.push_back(
        strings(tr("vsh.tiles.outline_animation"),
                OptionId::ShellTilesOutlineAnimation,
                animation_mode_name(g_settings.shell_tiles_outline_animation),
                AniketosTheme::AnimationModeCount()));
    options.push_back(
        animation_speed_slider(OptionId::ShellTilesOutlineAnimationSpeed,
                               g_settings.shell_tiles_outline_animation_speed));
    break;
  case Page::ShellUIPanels:
    options = {
        toggle(tr("vsh.panels.custom"),
               OptionId::ShellUIPanelsEnabled,
               g_settings.shell_ui_panels_enabled),
        color_slider(tr("vsh.panels.color"), OptionId::ColorPanels),
        slider_int(tr("vsh.panels.tint_strength"),
                   OptionId::ShellUIPanelsStrength,
                   g_settings.shell_ui_panels_strength, 0, 100, 5),
        slider_int(tr("vsh.panels.opacity"), OptionId::ShellUIPanelsOpacity,
                   g_settings.shell_ui_panels_opacity, 0, 200, 5),
        strings(tr("vsh.common.animation"), OptionId::ShellUIPanelsAnimation,
                animation_mode_name(g_settings.shell_ui_panels_animation),
                AniketosTheme::AnimationModeCount()),
        animation_speed_slider(OptionId::ShellUIPanelsAnimationSpeed,
                               g_settings.shell_ui_panels_animation_speed),
    };
    break;
  case Page::IconColor: {
    const int animation =
        g_settings.system_icon_color_animation == 1 ? 1 : 0;
    options = {
        toggle(tr("vsh.icon_color.enable"), OptionId::IconColorEnabled,
               g_settings.system_icon_color_enabled),
        color_slider(tr("vsh.icon_color.color"), OptionId::ColorIcon),
        strings(tr("vsh.common.animation"), OptionId::IconColorAnimation,
                animation == 1 ? tr("vsh.icon_color.rainbow")
                               : tr("vsh.animation.static"),
                2),
        slider_int(tr("vsh.common.animation_speed"),
                   OptionId::IconColorAnimationSpeed,
                   g_settings.system_icon_color_animation_speed,
                   25, 400, 25),
    };
    break;
  }
  default:
    break;
  }
}

void overlay_pages(Page page, std::vector<Option> &options) {
  switch (page) {
  case Page::OSD:
    options = {submenu(Page::HomeScreenOSD), submenu(Page::PerformanceOSD)};
    break;
  case Page::HomeScreenOSD:
    options = {
        toggle(tr("vsh.m.osd_home_enable"), OptionId::OverlayEnabled,
               g_settings.home_osd_enabled),
        toggle(tr("vsh.overlay.fps"), OptionId::OverlayFps,
               g_settings.home_osd_fps),
        toggle(tr("vsh.overlay.viewport"), OptionId::OverlayResolution,
               g_settings.overlay_resolution),
        toggle(tr("vsh.overlay.cpu"), OptionId::OverlayCpu,
               g_settings.home_osd_cpu),
        toggle(tr("vsh.overlay.soc"), OptionId::OverlaySoc,
               g_settings.home_osd_gpu),
        toggle(tr("vsh.overlay.fan"), OptionId::OverlayFan,
               g_settings.home_osd_fan),
        toggle(tr("vsh.overlay.ram"), OptionId::OverlayRam,
               g_settings.home_osd_ram),
        toggle(tr("vsh.overlay.free_ram"), OptionId::OverlayFreeRam,
               g_settings.overlay_free_ram),
        toggle(tr("vsh.overlay.storage"), OptionId::OverlayStorage,
               g_settings.overlay_storage),
        toggle(tr("vsh.overlay.firmware"), OptionId::OverlayFirmware,
               g_settings.overlay_firmware),
        toggle(tr("vsh.overlay.uptime"), OptionId::OverlayUptime,
               g_settings.overlay_uptime),
        toggle(tr("vsh.overlay.ip"), OptionId::OverlayIp,
               g_settings.home_osd_ip),
        submenu(Page::HomeScreenOSDSettings),
    };
    break;
  case Page::HomeScreenOSDSettings: {
    const char *temperature[] = {tr("vsh.unit.celsius"),
                                 tr("vsh.unit.fahrenheit"),
                                 tr("vsh.unit.kelvin")};
    const char *memory[] = {"KB", "MB", "GB"};
    const char *horizontal[] = {tr("vsh.pos.left"), tr("vsh.pos.center"),
                                tr("vsh.pos.right")};
    const char *vertical[] = {tr("vsh.pos.top"), tr("vsh.pos.center"),
                              tr("vsh.pos.bottom")};
    options = {
        choice(tr("vsh.ovs.horizontal"), OptionId::OverlayHorizontal,
               horizontal, g_settings.overlay_horizontal),
        choice(tr("vsh.ovs.vertical"), OptionId::OverlayVertical, vertical,
               g_settings.overlay_vertical),
        slider_float(tr("vsh.common.text_size"), OptionId::OverlayTextSize,
                     g_settings.overlay_text_size, 12.0f, 48.0f, 1.0f, 0),
        choice(tr("vsh.ovs.temp_type"), OptionId::OverlayTempUnit, temperature,
               g_settings.overlay_temp_unit),
        choice(tr("vsh.ovs.ram_type"), OptionId::OverlayMemoryUnit, memory,
               g_settings.overlay_memory_unit),
        slider_int(tr("vsh.ovs.fps_precision"), OptionId::OverlayFpsPrecision,
                   g_settings.overlay_fps_precision, 0, 5, 1),
        slider_int(tr("vsh.ovs.refresh"), OptionId::OverlayRefresh,
                   g_settings.overlay_refresh_seconds, 1, 10, 1),
    };
    break;
  }
  case Page::PerformanceOSD: {
    const char *positions[] = {tr("vsh.pos.top"), tr("vsh.pos.bottom")};
    options = {
        toggle(tr("vsh.game_overlay.enabled"),
               OptionId::PerformanceOverlayEnabled,
               g_settings.overlay_enabled),
        toggle(tr("vsh.game_overlay.background"),
               OptionId::PerformanceOverlayBackground,
               g_settings.overlay_background),
        choice(tr("vsh.game_overlay.position"),
               OptionId::PerformanceOverlayPosition, positions,
               g_settings.overlay_pos == 2 ? 1 : 0),
        toggle(tr("vsh.m.game_fps"), OptionId::PerformanceOverlayFps,
               g_settings.overlay_fps),
        toggle(tr("vsh.game_overlay.cpu"), OptionId::PerformanceOverlayCpu,
               g_settings.overlay_cpu),
        toggle(tr("vsh.game_overlay.all_cpu"),
               OptionId::PerformanceOverlayAllCpuUsage,
               g_settings.all_cpu_usage),
        toggle(tr("vsh.game_overlay.gpu"), OptionId::PerformanceOverlayGpu,
               g_settings.overlay_gpu),
        toggle(tr("vsh.game_overlay.memory"), OptionId::PerformanceOverlayRam,
               g_settings.overlay_ram),
        toggle(tr("vsh.m.game_fan"), OptionId::PerformanceOverlayFan,
               g_settings.overlay_fan),
        toggle(tr("vsh.game_overlay.ip"), OptionId::PerformanceOverlayIp,
               g_settings.overlay_ip),
        /* the bar is built when a game starts */
        value(tr("vsh.m.game_note"), ""),
    };
    break;
  }
  default:
    break;
  }
}

/* This menu only. */
void menu_look_pages(Page page, std::vector<Option> &options) {
  switch (page) {
  case Page::Customize: {
    const char *weights[] = {tr("vsh.clock.font_light"),
                             tr("vsh.clock.font_regular"),
                             tr("vsh.clock.font_bold")};
    options = {
        strings(tr("vsh.theme.style"), OptionId::MenuThemeSelector,
                theme_display_name(g_settings.vsh_menu_theme),
                kThemeChoiceCount),
        strings(tr("vsh.appearance.mode"), OptionId::MenuAppearanceSelector,
                tr(g_settings.vsh_menu_appearance == 1
                       ? "vsh.appearance.light"
                       : "vsh.appearance.dark"),
                2),
        submenu(Page::Colors),
        color_animation_choice(tr("vsh.animation.color_mode"),
                               OptionId::MenuColorAnimation),
        animation_speed_slider(OptionId::MenuColorAnimationSpeed,
                               g_settings.vsh_color_animation_speed),
        strings(tr("vsh.m.motion"), OptionId::MenuAnimation,
                easing_display_name(g_settings.vsh_animation).c_str(), 25),
        toggle(tr("vsh.border.enable"), OptionId::MenuBorderEnabled,
               g_settings.vsh_border_enabled),
        slider_float(tr("vsh.set.border_thickness"), OptionId::MenuBorder,
                     g_settings.vsh_border, 1.0f, 15.0f, 1.0f, 0),
        slider_float(tr("vsh.set.bg_opacity"), OptionId::MenuOpacity,
                     g_settings.vsh_background_opacity, 0, 1, 0.05f, 2),
        slider_float(tr("vsh.common.text_size"), OptionId::MenuTextSize,
                     g_settings.vsh_text_size, 12.0f, 48.0f, 1.0f, 0),
        choice(tr("vsh.text.font_weight"), OptionId::MenuFontWeight, weights,
               g_settings.vsh_font_weight),
        submenu(Page::ScalePosition),
        toggle(tr("vsh.m.shortcut_hint"), OptionId::MenuControls,
               g_settings.vsh_show_controls),
    };
    /* Only what the skin in use draws: Apparition has no frame, and Sweety's
       pages are the console's own, with its own surfaces and font. One font
       file stands in for every weight while the custom font is on.
       shortcut: a Sweety whose pop-up failed is drawn as Akari yet hides
       these (SweetyNative::Wanted comes after this file), share it if that
       fallback matters. */
    const bool drawn_here = active_menu_theme() != kThemeSweety ||
                            !SystemDialogs::PopupMenuAvailable();
    std::erase_if(options, [drawn_here](const Option &option) {
      switch (option.id) {
      case OptionId::MenuBorderEnabled:
      case OptionId::MenuBorder:
        return !drawn_here || apparition_theme();
      case OptionId::MenuOpacity:
        return !drawn_here;
      case OptionId::MenuFontWeight:
        return !drawn_here || g_settings.custom_font_enabled;
      default:
        return false;
      }
    });
    break;
  }
  case Page::Colors: {
    const char *directions[] = {tr("vsh.gradient.left_to_right"),
                                tr("vsh.gradient.right_to_left")};
    options = {
        color_slider(tr("vsh.m.color1"), OptionId::ColorMenuLeft),
        toggle(tr("vsh.gradient.enable"), OptionId::MenuDualColor,
               g_settings.vsh_dual_color),
        color_slider(tr("vsh.m.color2"), OptionId::ColorMenuRight),
        choice(tr("vsh.gradient.direction"), OptionId::MenuGradientDirection,
               directions, g_settings.vsh_gradient_direction),
    };
    break;
  }
  case Page::ScalePosition:
    options = {
        slider_float(tr("vsh.scale.menu_scale"), OptionId::MenuScale,
                     g_settings.vsh_menu_scale, 0.60f, 1.50f, 0.05f, 2),
        slider_float(tr("vsh.set.menu_width"), OptionId::MenuWidth,
                     g_settings.vsh_menu_width, 300.0f, 750.0f, 3.0f, 0),
        toggle(tr("vsh.set.edit_position"), OptionId::MenuMove, s_moving),
        action(tr("vsh.scale.reset_position"),
               OptionId::MenuResetPosition),
    };
    break;
  default:
    break;
  }
}

/* The top page, one row per area with the menu's own look kept apart from the
   console's, and the small pages that are an area of their own. */
void top_level_pages(Page page, std::vector<Option> &options) {
  switch (page) {
  case Page::Main:
    options = {submenu(Page::ConsoleManagement), submenu(Page::Controller),
               submenu(Page::HomeScreen), submenu(Page::SystemCustomize),
               submenu(Page::OSD), submenu(Page::Keyboard),
               submenu(Page::Debug), submenu(Page::Customize),
               submenu(Page::Settings)};
    break;
  case Page::Keyboard:
    options = {
        /* the glyph key after Shift on the system keyboard (osk_glyphs) */
        toggle(tr("vsh.osk_glyph_key"), OptionId::OskGlyphKey,
               g_settings.osk_glyph_key),
        /* the keyboard's own emoji in every field (osk_glyphs) */
        toggle(tr("vsh.osk_emoji"), OptionId::OskEmoji, g_settings.osk_emoji),
        /* a USB mouse with a pointer in the interface (mouse_control) */
        toggle(tr("vsh.mouse_control"), OptionId::MouseControl,
               g_settings.mouse_control_enabled),
    };
    break;
  case Page::Debug:
    /* the system's own DevKit/TestKit overlays (devkit_overlay) */
    options = {
        toggle(tr("vsh.m.devkit_info"), OptionId::DevkitOverlayInfo,
               g_settings.devkit_overlay_info),
        toggle(tr("vsh.m.devkit_expired"), OptionId::DevkitOverlayExpired,
               g_settings.devkit_overlay_expired),
    };
    break;
  case Page::Settings:
    options = {
        /* every change to the console's look, on or off at once */
        toggle(tr("vsh.m.customization"), OptionId::CustomizationEnabled,
               g_settings.shell_customization_enabled),
        action(tr("vsh.power.refresh_home"), OptionId::RefreshHome),
        action(tr("vsh.m.restart_ui"), OptionId::RestartShell),
        /* both ask for a second press */
        action(tr("vsh.m.reset"), OptionId::ResetConfig),
        action(tr("vsh.m.unload"), OptionId::UnloadStack),
    };
    break;
  default:
    break;
  }
}

std::vector<Option> build_page(Page page) {
  std::vector<Option> options;
  top_level_pages(page, options);
  console_pages(page, options);
  controller_pages(page, options);
  home_pages(page, options);
  system_look_pages(page, options);
  overlay_pages(page, options);
  menu_look_pages(page, options);
  return options;
}

const std::vector<Option> &page_options(Page page,
                                        std::vector<Option> &scratch) {
  scratch = build_page(page);
  return scratch;
}
