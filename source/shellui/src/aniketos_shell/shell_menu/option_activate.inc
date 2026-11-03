/* Shows the system's pop-up asking to show data that singles this console
   out. The menu steps aside for it and comes back when it is answered
   (Tick, by s_confirming). With no dialog the menu just stays. */
void ask_reveal(OptionId asking, const char *title, const char *message) {
  SystemDialogs::AlertOptions dialog;
  dialog.style = SystemDialogs::AlertStyle::Popup;
  dialog.title = tr(title);
  dialog.message = tr(message);
  dialog.positive = tr("vsh.info.reveal.show");
  dialog.negative = tr("vsh.info.reveal.cancel");
  std::string failure;
  Hide();
  if (SystemDialogs::ShowAlert(dialog, &failure)) {
    s_confirming = asking;
    return;
  }
  LOG_WARN("[ANIVSH] reveal dialog: %s", failure.c_str());
  Show();
}

/* The first press of an action that cannot be taken back only asks for a
   second one. */
bool confirmed(const Option &option) {
  if (s_armed_option == option.id) {
    s_armed_option = OptionId::None;
    return true;
  }
  s_armed_option = option.id;
  notify("notify.vsh.confirm_again", option.title.c_str());
  return false;
}

/* Back to the defaults: every [shell_menu] setting and the overlay settings
   the menu edits. */
void reset_vsh_settings() {
  const onion::Settings defaults{};
  const int buzzer_volume = g_settings.vsh_buzzer_volume;
#define ONION_SHELL_SETTING_BOOL(name, def) g_settings.name = defaults.name;
#define ONION_SHELL_SETTING_INT(name, def) g_settings.name = defaults.name;
#define ONION_SHELL_SETTING_FLOAT(name, def) g_settings.name = defaults.name;
#define ONION_SHELL_SETTING_STRING(name, def) g_settings.name = defaults.name;
#include <onion/shell_menu_settings.def>
#undef ONION_SHELL_SETTING_BOOL
#undef ONION_SHELL_SETTING_INT
#undef ONION_SHELL_SETTING_FLOAT
#undef ONION_SHELL_SETTING_STRING
  g_settings.overlay_background = defaults.overlay_background;
  g_settings.all_cpu_usage = defaults.all_cpu_usage;
  g_settings.overlay_enabled = defaults.overlay_enabled;
  g_settings.overlay_ram = defaults.overlay_ram;
  g_settings.overlay_cpu = defaults.overlay_cpu;
  g_settings.overlay_gpu = defaults.overlay_gpu;
  g_settings.overlay_ip = defaults.overlay_ip;
  g_settings.overlay_fps = defaults.overlay_fps;
  g_settings.overlay_fan = defaults.overlay_fan;
  g_settings.overlay_pos = defaults.overlay_pos;
  /* only when the menu had moved it: the console's own setting is the user's */
  if (g_settings.vsh_buzzer_volume != buzzer_volume)
    (void)Buzzer::SetVolume(
        static_cast<Buzzer::Volume>(g_settings.vsh_buzzer_volume));
  controller_stop_animation();
  PowerIndicator::RestoreNormal();
  reset_highlight_for_theme_change();
  reapply_customization();
  AniketosTheme::IconColor::RequestApply();
  Telemetry::SetRefreshSeconds(g_settings.overlay_refresh_seconds);
}

/* The after_*_toggle families take effect a switch just flipped; each acts on
   its own ids only. activate() saves and redraws after them. */

void after_home_toggle(OptionId id, bool on) {
  switch (id) {
  case OptionId::ShellTilesCircular:
    shellui_request_home_reload();
    notify(on ? "notify.vsh.tiles.circular" : "notify.vsh.tiles.stock");
    break;
  case OptionId::HomeBookmark:
    /* Home takes the tab in, or leaves it out, as it starts */
    shellui_request_home_reload();
    LOG_INFO("[BOOKMARK] enabled=%d", on ? 1 : 0);
    break;
  case OptionId::HomeHideApps:
    shellui_request_home_reload();
    LOG_INFO("[HIDDEN] filter enabled=%d", on ? 1 : 0);
    break;
  case OptionId::HomeEmulatorsTab:
    /* the same for the Emulators tab, whose titles go back to Games */
    shellui_request_home_reload();
    LOG_INFO("[HOMETABS] emulators tab enabled=%d", on ? 1 : 0);
    break;
  case OptionId::HomeTabIcons:
    /* the captions are made as Home starts */
    shellui_request_home_reload();
    LOG_INFO("[HOMETABS] tab icons=%d", on ? 1 : 0);
    break;
  default:
    break;
  }
}

void after_input_toggle(OptionId id, bool on) {
  switch (id) {
  case OptionId::OskGlyphKey:
    /* the keyboard takes it in when it is next laid out */
    LOG_INFO("[OSK] glyph key enabled=%d", on ? 1 : 0);
    break;
  case OptionId::OskEmoji:
    /* taken in when a keyboard next opens */
    LOG_INFO("[OSK] emoji enabled=%d", on ? 1 : 0);
    break;
  case OptionId::MouseControl:
    /* MouseControl::Tick follows the setting */
    LOG_INFO("[MOUSE] enabled=%d", on ? 1 : 0);
    break;
  default:
    break;
  }
}

void after_look_toggle(OptionId id, bool on) {
  if (is_shell_clock_option(id))
    ShellClock::RequestRefresh();
  switch (id) {
  case OptionId::CustomizationEnabled:
    reapply_customization();
    LOG_INFO("[ANIVSH:CUSTOMIZATION] enabled=%d", on ? 1 : 0);
    break;
  case OptionId::GlobalThemeEnabled:
    if (on)
      apply_global_theme(g_settings.vsh_left_r, g_settings.vsh_left_g,
                         g_settings.vsh_left_b);
    else
      LOG_INFO("[ANIVSH:GLOBAL-THEME] disabled; keeping applied colors");
    break;
  case OptionId::LogoColorEnabled:
    /* Home's top-bar icon: the byte patch's own picture or the script's */
    shellui_request_home_reload_after(1500);
    break;
  case OptionId::ShellBackgroundEnabled:
    if (on) {
      if (g_settings.shell_background_preset ==
          static_cast<int>(AniketosTheme::Preset::Original))
        g_settings.shell_background_preset =
            static_cast<int>(AniketosTheme::Preset::Custom);
      AniketosTheme::RequestApply();
    } else {
      /* The main switch owns the entire custom background.  Leaving image,
       * force-home or particles enabled made the old wallpaper survive an
       * apparent disable. */
      AniketosTheme::ApplyPresetToSettings(g_settings,
                                           AniketosTheme::Preset::Original);
      AniketosTheme::NativeShellBackground::DisableImageOverride();
      AniketosTheme::RequestRestore();
    }
    break;
  case OptionId::ShellBackgroundForceHome:
    AniketosTheme::RequestApply();
    break;
  default:
    break;
  }
}

void after_media_toggle(OptionId id, bool on) {
  switch (id) {
  case OptionId::HomeImageEnabled:
    HomeVideo::RestartImage();
    if (on && HomeVideo::Images(true).empty())
      notify("notify.vsh.home_image.none");
    break;
  case OptionId::HomeVideoEnabled:
    /* An explicit switch-on also retries after a failure or a crash. */
    HomeVideo::Restart();
    if (on && HomeVideo::Videos(true).empty())
      notify("notify.vsh.home_video.none");
    break;
  case OptionId::CustomFontEnabled:
    if (on && AniketosTheme::CustomFont::Files(true).empty())
      notify("notify.vsh.font.none");
    break;
  case OptionId::AccountAvatarEnabled: {
    const std::vector<std::string> &files = AccountAvatar::Files(true);
    if (on) {
      /* The first file is the one shown until another is chosen. */
      if (g_settings.account_avatar_path.empty() && !files.empty())
        g_settings.account_avatar_path = files.front();
      g_settings.account_avatar_user = AccountAvatar::SignedInUser();
      if (files.empty())
        notify("notify.vsh.account_avatar.none");
    }
    AccountAvatar::Refresh();
    break;
  }
  case OptionId::DevkitOverlayInfo:
  case OptionId::DevkitOverlayExpired:
    DevkitOverlay::Refresh();
    break;
  default:
    break;
  }
}

void flip_toggle(OptionId id) {
  bool &target = toggle_target(id);
  target = !target;
  if (id == OptionId::IconColorEnabled)
    AniketosTheme::IconColor::RequestApply();
  const bool on = target;
  /* the game bar shows the average or every core, not both */
  if (on && id == OptionId::PerformanceOverlayCpu)
    g_settings.all_cpu_usage = false;
  if (on && id == OptionId::PerformanceOverlayAllCpuUsage)
    g_settings.overlay_cpu = false;
  if (id == OptionId::FanDynamic) {
    g_settings.fan_threshold = std::clamp(g_settings.fan_threshold,
                                          onion::kFanThresholdMinCelsius,
                                          onion::kFanThresholdMaxCelsius);
    if (!AniketosFan::SetEnabled(on, g_settings.fan_threshold))
      notify("notify.fan.adjust_failed");
  }
  after_home_toggle(id, on);
  after_input_toggle(id, on);
  after_look_toggle(id, on);
  after_media_toggle(id, on);
  leave_global_theme(id);
}

/* The run_*_action families each act on their own ids only. */

void run_settings_action(const Option &option) {
  switch (option.id) {
  case OptionId::ShellRestoreOriginal:
    AniketosTheme::ApplyPresetToSettings(g_settings,
                                         AniketosTheme::Preset::Original);
    AniketosTheme::RequestRestore();
    (void)flush_save();
    notify("notify.vsh.background.restored");
    break;
  case OptionId::ResetConfig:
    if (!confirmed(option))
      break;
    reset_vsh_settings();
    s_overlay_dirty = true;
    (void)flush_save();
    notify("notify.vsh.config.reset");
    break;
  case OptionId::MenuResetPosition:
    s_moving = false;
    s_last_move_tick_ms = 0;
    g_settings.vsh_menu_x = s_viewport_valid ? s_viewport_width * 0.5f
                                             : kReferenceWidth * 0.5f;
    g_settings.vsh_menu_y = s_viewport_valid ? s_viewport_height * 0.5f
                                             : kReferenceHeight * 0.5f;
    (void)flush_save();
    break;
  default:
    break;
  }
}

void run_file_scan_action(OptionId id) {
  switch (id) {
  case OptionId::HomeImageRefresh:
    notify("notify.vsh.background.found_count",
           HomeVideo::Images(true).size());
    break;
  case OptionId::AccountAvatarRefresh:
    notify("notify.vsh.account_avatar.found_count",
           AccountAvatar::Files(true).size());
    if (AccountAvatar::Unsupported())
      notify("notify.vsh.account_avatar.unsupported",
             AccountAvatar::Unsupported());
    break;
  case OptionId::HomeVideoRefresh:
    notify("notify.vsh.home_video.found_count",
           HomeVideo::Videos(true).size());
    break;
  default:
    break;
  }
}

void run_buzzer_action(OptionId id) {
  switch (id) {
  case OptionId::BuzzerShort:
    (void)Buzzer::PlayTest(Buzzer::Pattern::Short);
    break;
  case OptionId::BuzzerError:
    (void)Buzzer::PlayTest(Buzzer::Pattern::Error);
    break;
  case OptionId::BuzzerLong:
    (void)Buzzer::PlayTest(Buzzer::Pattern::Long);
    break;
  default:
    break;
  }
}

void vibrate(VibrationPattern pattern) {
  if (!controller_vibrate(pattern))
    notify("notify.controller.vibration_failed");
}

void run_controller_action(OptionId id) {
  switch (id) {
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
  case OptionId::VibrationShort:
    vibrate(VibrationPattern::Short);
    break;
  case OptionId::VibrationLong:
    vibrate(VibrationPattern::Long);
    break;
  case OptionId::VibrationHeartbeat:
    vibrate(VibrationPattern::Heartbeat);
    break;
  case OptionId::VibrationLight:
    vibrate(VibrationPattern::Light);
    break;
  case OptionId::VibrationHeavy:
    vibrate(VibrationPattern::Heavy);
    break;
  case OptionId::VibrationWave:
    vibrate(VibrationPattern::Wave);
    break;
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
  default:
    break;
  }
}

void run_home_action(const Option &option) {
  switch (option.id) {
  case OptionId::HomeBookmarkRename:
    /* the system's prompt, on Home once this menu is gone */
    Hide();
    HomeBookmark::RequestRename();
    break;
  case OptionId::HiddenAppShow:
    HiddenApps::Show(option.external_key);
    break;
  case OptionId::HiddenAppsShowAll:
    HiddenApps::ShowAll();
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
  default:
    break;
  }
}

void run_power_action(const Option &option) {
  switch (option.id) {
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
    if (!confirmed(option))
      break;
    (void)flush_save();
    Hide();
    if (sceSystemServiceRequestReboot() != 0)
      notify("notify.vsh.power.failed");
    break;
  case OptionId::PowerOff:
    if (!confirmed(option))
      break;
    (void)flush_save();
    Hide();
    if (sceSystemServiceRequestPowerOff() != 0)
      notify("notify.vsh.power.failed");
    break;
  case OptionId::UnloadStack:
    if (!confirmed(option))
      break;
    LOG_INFO("[ANIVSH:MENU] confirmed stack shutdown");
    (void)flush_save();
    if (AniketosRuntime::service_available(false, "stack-shutdown"))
      IPC_Client::getInstance(false).ShutdownStack();
    break;
  default:
    break;
  }
}

void activate(const Option &option) {
  switch (option.type) {
  case OptionType::Submenu:
    begin_page_transition(PageTransitionAction::Push, option.target);
    break;
  case OptionType::Toggle:
    /* The DevKit info panel stays on screen with the IP and MAC addresses,
       host name and serial number: switched on only once that is said. */
    if (option.id == OptionId::DevkitOverlayInfo &&
        !g_settings.devkit_overlay_info) {
      ask_reveal(option.id, "vsh.devkit.reveal.title",
                 "vsh.devkit.reveal.message");
      return;
    }
    flip_toggle(option.id);
    break;
  case OptionType::Strings:
  case OptionType::SliderInt:
  case OptionType::SliderFloat:
  case OptionType::ColorSlider:
    change_option(option, 1);
    break;
  case OptionType::Action:
    if (option.id == OptionId::RevealSerialNumber) {
      /* Asked every time: the serial number and the two identifiers single
         this console out, and are not to end up in a screenshot or a
         stream by accident. */
      if (!s_show_serial) {
        ask_reveal(option.id, "vsh.info.reveal.title",
                   "vsh.info.reveal.message");
        return;
      }
      s_show_serial = false;
    }
    run_settings_action(option);
    run_file_scan_action(option.id);
    run_buzzer_action(option.id);
    run_controller_action(option.id);
    run_home_action(option);
    run_power_action(option);
    break;
  default:
    break;
  }
  if (is_overlay_option(option.id))
    s_overlay_dirty = true;
  // Actions and toggles can change settings too (including global theme and
  // native Shell customisation), so persist their final state consistently.
  if (option.type != OptionType::Submenu)
    request_save();
  s_dirty = true;
  LOG_INFO("[ANIVSH:MENU] select page=%s option=%s",
           page_title(s_stack.back().page), option.title.c_str());
}
