/* Editing anything the global theme sets ends the global theme. */
void leave_global_theme(OptionId id) {
  if (g_settings.vsh_global_theme_enabled && is_global_theme_owned_option(id)) {
    g_settings.vsh_global_theme_enabled = false;
    notify("notify.vsh.global_theme.off");
  }
}

/* Writes the colour at a gradient-slider position to whatever the slider
   edits, and shows it there at once where that is not left to the common
   handling in change_option(). */
void write_slider_color(OptionId id, int position) {
  const AniketosTheme::RGB rgb =
      color_with(color_of(id), color_mode(id), position);
  const int r = theme_channel(rgb.r), g = theme_channel(rgb.g), b = theme_channel(rgb.b);
  const auto put = [&](int &out_r, int &out_g, int &out_b) {
    out_r = r;
    out_g = g;
    out_b = b;
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
  case OptionId::ColorController2: {
    /* colour 2 is the second colour of the pulse; shown while it moves */
    const bool second = id == OptionId::ColorController2;
    controller_set_custom_channel(second, 0, static_cast<uint8_t>(r));
    controller_set_custom_channel(second, 1, static_cast<uint8_t>(g));
    controller_set_custom_channel(second, 2, static_cast<uint8_t>(b));
    controller_stop_animation();
    (void)controller_apply_custom_color(second);
    break;
  }
  case OptionId::ColorBackground:
    put(g_settings.shell_background_base_r, g_settings.shell_background_base_g,
        g_settings.shell_background_base_b);
    break;
  case OptionId::ColorGlow:
    put(g_settings.shell_background_glow_r, g_settings.shell_background_glow_g,
        g_settings.shell_background_glow_b);
    break;
  case OptionId::ColorOutline:
    put(g_settings.shell_tiles_outline_r, g_settings.shell_tiles_outline_g,
        g_settings.shell_tiles_outline_b);
    break;
  case OptionId::ColorPanels:
    put(g_settings.shell_ui_panels_r, g_settings.shell_ui_panels_g,
        g_settings.shell_ui_panels_b);
    break;
  case OptionId::ColorClockTime:
    put(g_settings.shell_clock_time_r, g_settings.shell_clock_time_g,
        g_settings.shell_clock_time_b);
    break;
  case OptionId::ColorClockDate:
    put(g_settings.shell_clock_date_r, g_settings.shell_clock_date_g,
        g_settings.shell_clock_date_b);
    break;
  case OptionId::ColorIcon:
    put(g_settings.system_icon_color_r, g_settings.system_icon_color_g,
        g_settings.system_icon_color_b);
    break;
  case OptionId::ColorLogo:
    put(g_settings.logo_color_r, g_settings.logo_color_g,
        g_settings.logo_color_b);
    /* each move puts this off: the picture is made once, when it stops. Only
       Home's script holds the icon's path; the Control Center button takes
       the new picture by itself (control_center_entry). */
    if (g_settings.logo_color_enabled) {
      HoldAniketosIcon();
      shellui_request_home_reload_after(1500);
    }
    break;
  default:
    break;
  }
}

/* The file `direction` steps from `index`, stopping at either end. Null when
   the list is empty or that file is `current` already. */
const std::string *step_file(const std::vector<std::string> &files, int index,
                             int direction, const std::string &current) {
  if (files.empty())
    return nullptr;
  const int next =
      std::clamp(index + direction, 0, static_cast<int>(files.size()) - 1);
  const std::string &file = files[static_cast<size_t>(next)];
  return file == current ? nullptr : &file;
}

bool step_home_image(int direction) {
  const std::string *chosen = step_file(HomeVideo::Images(), home_image_index(),
                                        direction, g_settings.home_image_path);
  if (!chosen)
    return false;
  g_settings.home_image_path = *chosen;
  HomeVideo::RestartImage();
  return true;
}

bool step_account_avatar(int direction) {
  const std::string *chosen =
      step_file(AccountAvatar::Files(), account_avatar_index(), direction,
                g_settings.account_avatar_path);
  if (!chosen)
    return false;
  g_settings.account_avatar_path = *chosen;
  g_settings.account_avatar_user = AccountAvatar::SignedInUser();
  AccountAvatar::Refresh();
  return true;
}

bool step_custom_font(int direction) {
  /* fonts copied over FTP show up without a refresh action */
  const std::vector<std::string> &files =
      AniketosTheme::CustomFont::Files(true);
  const std::string *chosen = step_file(files, custom_font_index(), direction,
                                        g_settings.custom_font_path);
  if (!chosen)
    return false;
  g_settings.custom_font_path = *chosen;
  return true;
}

bool step_home_video(int direction) {
  const std::string *chosen = step_file(HomeVideo::Videos(), home_video_index(),
                                        direction, g_settings.home_video_path);
  if (!chosen)
    return false;
  g_settings.home_video_path = *chosen;
  HomeVideo::Restart();
  LOG_INFO("[ANIVSH:HOME-VIDEO] selected '%s' enabled=%d",
           g_settings.home_video_path.c_str(),
           g_settings.home_video_enabled ? 1 : 0);
  return true;
}

/* Options that do all their work here, without the common handling of
   change_option(). False for any other option. */
bool change_standalone_option(const Option &option, int direction) {
  bool changed = true;
  switch (option.id) {
  case OptionId::MenuThemeSelector:
    g_settings.vsh_menu_theme =
        cycle_theme(g_settings.vsh_menu_theme, direction);
    reset_highlight_for_theme_change();
    LOG_INFO("[ANIVSH:THEME] selected=%s visual_only=1",
             theme_display_name(g_settings.vsh_menu_theme));
    break;
  case OptionId::MenuAppearanceSelector:
    g_settings.vsh_menu_appearance =
        std::clamp(g_settings.vsh_menu_appearance, 0, 1) == 0 ? 1 : 0;
    LOG_INFO("[ANIVSH:APPEARANCE] selected=%s",
             g_settings.vsh_menu_appearance == 1 ? "light" : "dark");
    break;
  case OptionId::GlobalThemeAnimation:
    apply_global_animation(g_settings.vsh_color_animation + direction,
                           g_settings.vsh_color_animation_speed);
    break;
  case OptionId::GlobalThemeAnimationSpeed:
    apply_global_animation(g_settings.vsh_color_animation,
                           g_settings.vsh_color_animation_speed +
                               direction * option.step_i);
    break;
  case OptionId::PerformanceOverlayPosition:
    g_settings.overlay_pos =
        std::clamp(selector_index_for(option.id) + direction, 0, 1) == 1 ? 2
                                                                         : 0;
    s_overlay_dirty = true;
    break;
  case OptionId::HomeImageFile:
    changed = step_home_image(direction);
    break;
  case OptionId::AccountAvatarFile:
    changed = step_account_avatar(direction);
    break;
  case OptionId::CustomFontFile:
    changed = step_custom_font(direction);
    break;
  case OptionId::HomeVideoFile:
    changed = step_home_video(direction);
    break;
  default:
    return false;
  }
  if (changed) {
    request_save();
    s_dirty = true;
  }
  return true;
}

void change_int_slider(const Option &option, int direction) {
  int &target = int_target(option.id);
  const int previous = target;
  target = std::clamp(target + direction * option.step_i, option.minimum_i,
                      option.maximum_i);
  switch (option.id) {
  case OptionId::FanThreshold:
    if (g_settings.enable_fan_speed &&
        !AniketosFan::SetTarget(g_settings.fan_threshold))
      notify("notify.fan.adjust_failed");
    break;
  case OptionId::ShellTilesCornerRadius:
    if (target != previous || !g_settings.shell_tiles_circular) {
      /* Choosing a rounding turns the rounded tiles on. Home takes the
         shape as it loads; the reload waits for the slider to rest, so
         holding a direction reloads it once, not at every step, and not
         at all against either end. */
      g_settings.shell_tiles_circular = true;
      shellui_request_home_reload_after(400);
      LOG_INFO("[HOME-TILES] rounding=%d%%",
               g_settings.shell_tiles_corner_radius);
    }
    break;
  case OptionId::HomeVideoVolume:
    /* Turning the volume up turns the video's sound on. */
    if (target > previous)
      g_settings.home_video_sound = true;
    break;
  default:
    break;
  }
}

void change_list_option(const Option &option, int direction) {
  int &target = int_target(option.id);
  target = std::clamp(target + direction, 0, option.string_count - 1);
  switch (option.id) {
  case OptionId::PlayerLeds:
    if (!controller_set_player_leds(target + 1))
      notify("notify.controller.leds_failed");
    break;
  case OptionId::LightBrightness:
    if (!controller_set_light_brightness(target))
      notify("notify.controller.leds_failed");
    break;
  case OptionId::BuzzerVolume:
    (void)Buzzer::SetVolumeAndConfirm(static_cast<Buzzer::Volume>(target));
    break;
  case OptionId::PowerIndicatorColor:
    (void)PowerIndicator::SetColor(static_cast<PowerIndicator::Color>(target));
    g_settings.vsh_power_indicator_animation = 0;
    break;
  case OptionId::HomePresenceDot:
    shellui_request_presence_reload(1500, true);
    break;
  case OptionId::HenButtonIcon:
    /* Home takes its top bar as it loads (homeui_top_nav_patch) */
    shellui_request_home_reload_after(1500);
    break;
  default:
    break;
  }
}

/* What a stepped option shares with the others of its group. */
void finish_change(OptionId id) {
  if (is_shell_background_option(id)) {
    if (id != OptionId::ShellBackgroundAnimation &&
        id != OptionId::ShellBackgroundAnimationSpeed &&
        id != OptionId::ShellBackgroundParticles) {
      g_settings.shell_background_enabled = true;
      g_settings.shell_background_preset =
          static_cast<int>(AniketosTheme::Preset::Custom);
    }
    if (g_settings.shell_background_enabled)
      AniketosTheme::RequestApply();
    else
      AniketosTheme::RequestRestore();
  }
  if (is_shell_tile_option(id)) {
    g_settings.shell_tiles_outline_enabled = true;
    g_settings.shell_tiles_outline_sync_background = false;
    g_settings.shell_tiles_outline_preset =
        static_cast<int>(AniketosTheme::OutlinePreset::Custom);
  }
  if (is_shell_clock_option(id))
    ShellClock::RequestRefresh();
  if (is_shell_ui_panel_option(id))
    g_settings.shell_ui_panels_enabled = true;
  if (is_icon_color_option(id))
    AniketosTheme::IconColor::RequestApply();
  leave_global_theme(id);
  Telemetry::SetRefreshSeconds(g_settings.overlay_refresh_seconds);
  if (is_overlay_option(id))
    s_overlay_dirty = true;
  // All mutable menu options persist immediately.  HEN unload/injection must
  // never discard a setting simply because the user did not visit Save Config.
  request_save();
  s_dirty = true;
}

/* One step of a slider, a list or a colour. Switches and actions never come
   here: they go through activate(). */
void change_option(const Option &option, int direction) {
  if (change_standalone_option(option, direction))
    return;
  switch (option.type) {
  case OptionType::ColorSlider:
    write_slider_color(option.id,
                       std::clamp(std::atoi(option.value.c_str()) + direction,
                                  0, option.maximum_i));
    break;
  case OptionType::SliderInt:
    change_int_slider(option, direction);
    break;
  case OptionType::SliderFloat: {
    float &target = float_target(option.id);
    target = std::clamp(target + direction * option.step_f, option.minimum_f,
                        option.maximum_f);
    break;
  }
  case OptionType::Strings:
    change_list_option(option, direction);
    break;
  default:
    break;
  }
  finish_change(option.id);
}
