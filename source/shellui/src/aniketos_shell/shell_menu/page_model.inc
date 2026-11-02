const char *tr(const char *key) { return toolbox_i18n::tr(key); }

std::string with_name(const char *key, const std::string &name) {
  const std::string text = tr(key);
  const size_t at = text.find("%s");
  return at == std::string::npos
             ? text
             : text.substr(0, at) + name + text.substr(at + 2);
}

const char *menu_header_text() { return kMenuBrandName; }

const char *menu_footer_text() { return "https://goodfeelings.cc"; }

const char *page_title(Page page) {
  switch (page) {
  case Page::Main: return tr("vsh.m.main");
  case Page::ConsoleManagement: return tr("vsh.m.console");
  case Page::Information: return tr("vsh.m.info");
  case Page::HiddenApps: return tr("vsh.m.hidden_apps");
  case Page::Power: return tr("vsh.m.power");
  case Page::Fan: return tr("vsh.m.fan");
  case Page::PowerIndicator: return tr("vsh.m.power_light");
  case Page::Buzzer: return tr("vsh.m.buzzer");
  case Page::Controller: return tr("vsh.m.controller");
  case Page::ControllerAnimations: return tr("vsh.m.lightbar_animations");
  case Page::ControllerVibration: return tr("vsh.m.vibration_test");
  case Page::ControllerLeds: return tr("vsh.m.player_lights");
  case Page::HomeScreen: return tr("vsh.m.home");
  case Page::HomeVideo: return tr("vsh.m.home_bg");
  case Page::HomeBgVideo: return tr("vsh.m.home_bg_video");
  case Page::HomeBgImage: return tr("vsh.m.home_bg_picture");
  case Page::HomeBgEffects: return tr("vsh.m.home_bg_effects");
  case Page::HomeParticles: return tr("vsh.m.home_particles");
  case Page::ShellClock: return tr("vsh.m.clock");
  case Page::HomeTabs: return tr("vsh.m.tabs");
  case Page::ShellTiles: return tr("vsh.m.tiles");
  case Page::HomeProfile: return tr("vsh.m.profile");
  case Page::SystemCustomize: return tr("vsh.m.system_look");
  case Page::GlobalCustomization: return tr("vsh.m.global_theme");
  case Page::ShellBackground: return tr("vsh.m.system_bg");
  case Page::ShellOutline: return tr("vsh.m.outlines");
  case Page::ShellUIPanels: return tr("vsh.m.panels");
  case Page::OSD: return tr("vsh.m.overlays");
  case Page::HomeScreenOSD: return tr("vsh.m.osd_home");
  case Page::HomeScreenOSDSettings: return tr("vsh.m.osd_home_settings");
  case Page::PerformanceOSD: return tr("vsh.m.osd_game");
  case Page::Customize: return tr("vsh.m.menu_look");
  case Page::Colors: return tr("vsh.m.menu_colors");
  case Page::ScalePosition: return tr("vsh.m.size_position");
  case Page::Keyboard: return tr("vsh.m.keyboard");
  case Page::Debug: return tr("vsh.m.debug");
  case Page::Settings: return tr("vsh.m.hen");
  }
  return "Aniketos";
}

/* A colour channel of 0..1 as a 0..255 slider value. */
int theme_channel(float value) {
  return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) *
                                      255.0f));
}

const char *outline_animation_name(AniketosTheme::AnimationMode animation) {
  switch (animation) {
  case AniketosTheme::AnimationMode::Static:
    return tr("vsh.animation.static");
  case AniketosTheme::AnimationMode::Rainbow:
    return tr("controller.animation.rainbow");
  case AniketosTheme::AnimationMode::Count: break;
  }
  return tr("vsh.animation.static");
}

std::string number(float value, int precision = 1) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
  return buffer;
}

std::string masked_serial(const std::string &serial) {
  return std::string(std::max<size_t>(serial.size(), 8), '*');
}

/* "r229177/releases/13.42 Jun 26 2026 02:17:03" is wider than a row: the
   revision and the date say it all ("r229177, Jun 26 2026"). */
std::string short_kernel_build(const std::string &full) {
  const size_t slash = full.find('/');
  const size_t space = full.find(' ');
  if (slash == std::string::npos || space == std::string::npos ||
      slash > space)
    return full.substr(0, 24);
  std::string date = full.substr(space + 1);
  const size_t clock = date.rfind(' '); /* drop the time of day */
  if (clock != std::string::npos && date.find(':', clock) != std::string::npos)
    date.erase(clock);
  return full.substr(0, slash) + ", " + date;
}

// "part / total" of the user partition in decimal gigabytes, the unit the
// system's own storage screen uses.
std::string storage_amount(uint64_t part_bytes, uint64_t total_bytes) {
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), "%.1f / %.1f GB", part_bytes / 1e9,
                total_bytes / 1e9);
  return buffer;
}

// Compact uptime, e.g. "3d 04h 17m" or "12m 05s" for short sessions.
std::string uptime_text(uint64_t seconds) {
  const uint64_t days = seconds / 86400u;
  const uint64_t hours = (seconds % 86400u) / 3600u;
  const uint64_t minutes = (seconds % 3600u) / 60u;
  const uint64_t secs = seconds % 60u;
  char buffer[64];
  if (days > 0)
    std::snprintf(buffer, sizeof(buffer), "%llu%s %02llu%s %02llu%s",
                  static_cast<unsigned long long>(days),
                  tr("vsh.unit.day_short"),
                  static_cast<unsigned long long>(hours),
                  tr("vsh.unit.hour_short"),
                  static_cast<unsigned long long>(minutes),
                  tr("vsh.unit.minute_short"));
  else if (hours > 0)
    std::snprintf(buffer, sizeof(buffer), "%llu%s %02llu%s %02llu%s",
                  static_cast<unsigned long long>(hours),
                  tr("vsh.unit.hour_short"),
                  static_cast<unsigned long long>(minutes),
                  tr("vsh.unit.minute_short"),
                  static_cast<unsigned long long>(secs),
                  tr("vsh.unit.second_short"));
  else
    std::snprintf(buffer, sizeof(buffer), "%llu%s %02llu%s",
                  static_cast<unsigned long long>(minutes),
                  tr("vsh.unit.minute_short"),
                  static_cast<unsigned long long>(secs),
                  tr("vsh.unit.second_short"));
  return buffer;
}

std::string easing_display_name(int type, bool english_only = false) {
  if (type <= 0)
    return english_only ? "Linear" : tr("vsh.easing.linear");
  constexpr const char *families[] = {
      "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back"};
  const int index = std::clamp(type - 1, 0, 23);
  const char *direction = english_only
                              ? (index % 3 == 0   ? "In"
                                 : index % 3 == 1 ? "Out"
                                                  : "In-Out")
                              : (index % 3 == 0   ? tr("vsh.easing.in")
                                 : index % 3 == 1 ? tr("vsh.easing.out")
                                                  : tr("vsh.easing.in_out"));
  return std::string(direction) + " (" + families[index / 3] + ")";
}

Option submenu(const char *title, Page target) {
  Option option{};
  option.title = title;
  option.type = OptionType::Submenu;
  option.target = target;
  return option;
}

Option toggle(const char *title, OptionId id, bool value) {
  Option option{};
  option.title = title;
  option.type = OptionType::Toggle;
  option.id = id;
  option.value = value ? "1" : "0";
  return option;
}

Option value(const char *title, std::string text) {
  Option option{};
  option.title = title;
  option.type = OptionType::Value;
  option.value = std::move(text);
  return option;
}

Option action(const char *title, OptionId id) {
  Option option{};
  option.title = title;
  option.type = OptionType::Action;
  option.id = id;
  return option;
}

const char *theme_display_name(int theme) {
  switch (theme) {
  case 2: return "Anomaly";
  case 5: return "Apparition";
  case kThemeSweety: return "Sweety";
  default: return "Akari";
  }
}

// Persisted theme ids 1, 3 and 4 belonged to styles that were removed. They
// stay reserved so an old config cannot silently turn into a different one.
constexpr int kThemeChoices[] = {0, 2, 5, kThemeSweety};
constexpr int kThemeChoiceCount = static_cast<int>(std::size(kThemeChoices));

int cycle_theme(int current, int direction) {
  const int *themes = kThemeChoices;
  int index = 0;
  for (int i = 0; i < kThemeChoiceCount; ++i) {
    if (themes[i] == current) {
      index = i;
      break;
    }
  }
  index = (index + (direction < 0 ? -1 : 1)) % kThemeChoiceCount;
  if (index < 0)
    index += kThemeChoiceCount;
  return themes[index];
}


Option slider_int(const char *title, OptionId id, int current, int minimum,
                  int maximum, int step) {
  Option option{};
  option.title = title;
  option.type = OptionType::SliderInt;
  option.id = id;
  option.value = std::to_string(current);
  option.minimum_i = minimum;
  option.maximum_i = maximum;
  option.step_i = step;
  return option;
}

Option slider_float(const char *title, OptionId id, float current,
                    float minimum, float maximum, float step,
                    int precision = 0) {
  Option option{};
  option.title = title;
  option.type = OptionType::SliderFloat;
  option.id = id;
  option.value = number(current, precision);
  option.minimum_f = minimum;
  option.maximum_f = maximum;
  option.step_f = step;
  return option;
}

Option strings(const char *title, OptionId id, const char *current, int count) {
  Option option{};
  option.title = title;
  option.type = OptionType::Strings;
  option.id = id;
  option.value = current;
  option.string_count = count;
  option.string_index = selector_index_for(id);
  return option;
}

/* The colour gradient slider. Its positions are hues five degrees apart
   (0..71) and white at the end (kColorWhite); a colour is shown at the
   position nearest to it. */
constexpr int kColorWhite = 72;

AniketosTheme::RGB rgb_of(int r, int g, int b) {
  return {r / 255.0f, g / 255.0f, b / 255.0f};
}

/* The colour a slider edits, as it is now. */
AniketosTheme::RGB color_of(OptionId id) {
  switch (id) {
  case OptionId::ColorGlobal:
  case OptionId::ColorMenuLeft:
    return {g_settings.vsh_left_r, g_settings.vsh_left_g, g_settings.vsh_left_b};
  case OptionId::ColorMenuRight:
    return {g_settings.vsh_right_r, g_settings.vsh_right_g,
            g_settings.vsh_right_b};
  case OptionId::ColorController:
    return rgb_of(controller_get_custom_channel(false, 0),
                  controller_get_custom_channel(false, 1),
                  controller_get_custom_channel(false, 2));
  case OptionId::ColorController2:
    return rgb_of(controller_get_custom_channel(true, 0),
                  controller_get_custom_channel(true, 1),
                  controller_get_custom_channel(true, 2));
  case OptionId::ColorBackground:
    return rgb_of(g_settings.shell_background_base_r,
                  g_settings.shell_background_base_g,
                  g_settings.shell_background_base_b);
  case OptionId::ColorGlow:
    return rgb_of(g_settings.shell_background_glow_r,
                  g_settings.shell_background_glow_g,
                  g_settings.shell_background_glow_b);
  case OptionId::ColorOutline:
    return rgb_of(g_settings.shell_tiles_outline_r,
                  g_settings.shell_tiles_outline_g,
                  g_settings.shell_tiles_outline_b);
  case OptionId::ColorPanels:
    return rgb_of(g_settings.shell_ui_panels_r, g_settings.shell_ui_panels_g,
                  g_settings.shell_ui_panels_b);
  case OptionId::ColorClockTime:
    return rgb_of(g_settings.shell_clock_time_r, g_settings.shell_clock_time_g,
                  g_settings.shell_clock_time_b);
  case OptionId::ColorClockDate:
    return rgb_of(g_settings.shell_clock_date_r, g_settings.shell_clock_date_g,
                  g_settings.shell_clock_date_b);
  default:
    return {1.0f, 1.0f, 1.0f};
  }
}

/* Which part of each colour its slider moves now; not saved. */
ColorMode s_color_modes[static_cast<int>(OptionId::ColorMenuRight) -
                        static_cast<int>(OptionId::ColorGlobal) + 1]{};

ColorMode &color_mode(OptionId id) {
  const int index = std::clamp(
      static_cast<int>(id) - static_cast<int>(OptionId::ColorGlobal), 0,
      static_cast<int>(std::size(s_color_modes)) - 1);
  return s_color_modes[index];
}

int color_position(const AniketosTheme::RGB &rgb, ColorMode mode) {
  const AniketosTheme::HSV hsv = AniketosTheme::RGBToHSV(rgb);
  switch (mode) {
  case ColorMode::Brightness:
    return static_cast<int>(std::lround(hsv.v * kColorLevels));
  case ColorMode::Saturation:
    return static_cast<int>(std::lround(hsv.s * kColorLevels));
  default:
    if (hsv.s < 0.15f)
      return kColorWhite;
    return static_cast<int>(std::lround(hsv.h / 5.0f)) % kColorWhite;
  }
}

/* The colour a slider position makes of the current one. Moving the hue
   keeps its brightness and saturation (a grey takes full saturation, so
   the hue shows); the white end of the hue track takes the saturation out. */
AniketosTheme::RGB color_with(const AniketosTheme::RGB &current,
                              ColorMode mode, int position) {
  AniketosTheme::HSV hsv = AniketosTheme::RGBToHSV(current);
  switch (mode) {
  case ColorMode::Brightness:
    hsv.v = static_cast<float>(position) / kColorLevels;
    break;
  case ColorMode::Saturation:
    hsv.s = static_cast<float>(position) / kColorLevels;
    break;
  default:
    if (position >= kColorWhite) {
      hsv.s = 0.0f;
    } else {
      hsv.h = static_cast<float>(position * 5);
      if (hsv.s < 0.15f)
        hsv.s = 1.0f;
    }
    break;
  }
  return AniketosTheme::HSVToRGB(hsv);
}

Option color_slider(const char *title, OptionId id) {
  Option option{};
  option.title = title;
  option.type = OptionType::ColorSlider;
  option.id = id;
  const ColorMode mode = color_mode(id);
  option.value = std::to_string(color_position(color_of(id), mode));
  option.minimum_i = 0;
  option.maximum_i = mode == ColorMode::Hue ? kColorWhite : kColorLevels;
  option.step_i = 1;
  /* the mode, shown as "[n/3]" */
  option.string_index = static_cast<int>(mode);
  option.string_count = static_cast<int>(ColorMode::Count);
  return option;
}

std::vector<Option> build_page(Page page) {
  const Telemetry::Snapshot telemetry = Telemetry::GetSnapshot();
  std::vector<Option> options;
  const auto temperature_rows = [&]() {
    if (telemetry.cpu_temperature_valid)
      options.push_back(value(tr("vsh.info.cpu_temp"),
                              std::to_string(telemetry.cpu_temperature_c) +
                                  AniketosShell::TextUnits::kCelsiusSuffix));
    if (telemetry.soc_temperature_valid)
      options.push_back(value(tr("vsh.info.soc_temp"),
                              std::to_string(telemetry.soc_temperature_c) +
                                  AniketosShell::TextUnits::kCelsiusSuffix));
    if (telemetry.fan_duty_valid)
      options.push_back(value(tr("vsh.m.fan_speed"),
                              number(telemetry.fan_duty_percent, 1) + "%"));
  };
  switch (page) {
  /* ---- the top: one row per area, the menu's own look kept apart from the
     console's ---- */
  case Page::Main:
    options = {submenu(tr("vsh.m.console"), Page::ConsoleManagement),
               submenu(tr("vsh.m.controller"), Page::Controller),
               submenu(tr("vsh.m.home"), Page::HomeScreen),
               submenu(tr("vsh.m.system_look"), Page::SystemCustomize),
               submenu(tr("vsh.m.overlays"), Page::OSD),
               submenu(tr("vsh.m.keyboard"), Page::Keyboard),
               submenu(tr("vsh.m.debug"), Page::Debug),
               submenu(tr("vsh.m.menu_look"), Page::Customize),
               submenu(tr("vsh.m.hen"), Page::Settings)};
    break;

  /* ---- Console ---- */
  case Page::ConsoleManagement:
    options = {submenu(tr("vsh.m.info"), Page::Information),
               submenu(tr("vsh.m.hidden_apps"), Page::HiddenApps),
               submenu(tr("vsh.m.power"), Page::Power),
               submenu(tr("vsh.m.fan"), Page::Fan),
               submenu(tr("vsh.m.power_light"), Page::PowerIndicator),
               submenu(tr("vsh.m.buzzer"), Page::Buzzer)};
    break;
  case Page::HiddenApps: {
    const std::vector<HomeHidden::Entry> hidden = HomeHidden::List();
    options.push_back(toggle(tr("vsh.hidden.enable"), OptionId::HomeHideApps,
                             g_settings.home_hide_apps));
    for (const HomeHidden::Entry &entry : hidden) {
      Option row = action(tr("vsh.hidden.show_fmt"), OptionId::HiddenAppShow);
      row.title = with_name("vsh.hidden.show_fmt", entry.name);
      row.external_key = entry.title_id;
      options.push_back(row);
    }
    if (hidden.size() > 1)
      options.push_back(
          action(tr("vsh.hidden.show_all"), OptionId::HiddenAppsShowAll));
    if (hidden.empty())
      options.push_back(value(tr("vsh.hidden.none"), ""));
    options.push_back(value(tr("vsh.hidden.hint"), ""));
    break;
  }
  case Page::Information:
    /* who the console is, its software, its hardware, then memory, disk and
       network; the button that shows the hidden numbers comes last */
    if (telemetry.system_name_valid)
      options.push_back(value(tr("vsh.info.system_name"),
                              telemetry.system_name));
    if (telemetry.model_valid)
      options.push_back(value(tr("vsh.info.model"), telemetry.model));
    if (telemetry.serial_valid)
      options.push_back(value(
          tr("vsh.info.serial"),
          s_show_serial ? telemetry.serial : masked_serial(telemetry.serial)));
    {
      /* 32 hex digits are wider than the usual value column, so these two
         rows ask for a wide one: name on the left, all the digits on the
         right, as the serial number is. */
      const auto identifier_row = [&](const char *name, const std::string &hex,
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
      identifier_row(tr("vsh.info.idps"), telemetry.idps, 4);
      identifier_row(tr("vsh.info.open_psid"), telemetry.open_psid, 0);
    }
    options.push_back(value(tr("vsh.info.firmware"),
                            telemetry.firmware.empty()
                                ? tr("vsh.info.unavailable")
                                : telemetry.firmware));
    if (!telemetry.kernel_build.empty())
      options.push_back(value(tr("vsh.info.kernel"),
                              short_kernel_build(telemetry.kernel_build)));
    if (!telemetry.soc.empty())
      options.push_back(value(tr("vsh.info.soc"), telemetry.soc));
    if (telemetry.cpu_threads > 0)
      options.push_back(value(tr("vsh.info.cpu_threads"),
                              std::to_string(telemetry.cpu_threads)));
    if (telemetry.cpu_frequency_valid)
      options.push_back(value(
          tr("vsh.info.cpu_frequency"),
          std::to_string(telemetry.cpu_frequency_hz / 1000000u) + " MHz"));
    if (telemetry.base_clock_hz > 0)
      options.push_back(value(
          tr("vsh.info.base_clock"),
          number(static_cast<float>(telemetry.base_clock_hz) / 1e6f, 1) +
              " MHz"));
    options.push_back(value(tr("vsh.info.disc_drive"),
                            telemetry.disc_drive ? tr("vsh.info.detected")
                                                 : tr("vsh.info.not_detected")));
    if (telemetry.memory_valid)
      options.push_back(value(tr("vsh.info.system_memory"),
                              std::to_string(telemetry.memory_used_mb) + " / " +
                                  std::to_string(telemetry.memory_total_mb) + " MB"));
    if (telemetry.memory_valid)
      options.push_back(value(tr("vsh.info.free_memory"),
                              std::to_string(telemetry.memory_free_mb) + " MB"));
    if (telemetry.swap_bytes > 0)
      options.push_back(value(
          tr("vsh.info.swap"),
          number(static_cast<float>(telemetry.swap_bytes) /
                     (1024.0f * 1024.0f * 1024.0f),
                 1) + " GB"));
    if (telemetry.storage_valid)
      options.push_back(value(tr("vsh.info.storage"),
                              /* what is left, of how much */
                              storage_amount(telemetry.storage_free_bytes,
                                             telemetry.storage_total_bytes) + " (" +
                                  number(100.0f - telemetry.storage_used_percent, 1) + "%)"));
    if (telemetry.uptime_valid)
      options.push_back(value(tr("vsh.info.uptime"),
                              uptime_text(telemetry.uptime_seconds)));
    if (telemetry.local_ip_valid)
      options.push_back(value(tr("vsh.info.local_ip"), telemetry.local_ip));
    if (telemetry.serial_valid)
      options.push_back(action(s_show_serial ? tr("vsh.info.hide_serial")
                                             : tr("vsh.info.show_serial"),
                               OptionId::RevealSerialNumber));
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
    temperature_rows();
    break;
  case Page::PowerIndicator: {
    const char *colors[] = {tr("vsh.indicator.white"),
                            tr("vsh.indicator.blue"),
                            tr("vsh.indicator.orange")};
    options = {
        strings(tr("vsh.indicator.color"), OptionId::PowerIndicatorColor,
                colors[std::clamp(g_settings.vsh_power_indicator_color, 0, 2)],
                3),
    };
    break;
  }
  case Page::Buzzer: {
    const char *volumes[] = {tr("vsh.buzzer.volume_high"),
                             tr("vsh.buzzer.volume_medium"),
                             tr("vsh.buzzer.volume_low")};
    options = {
        strings(tr("vsh.buzzer.volume"), OptionId::BuzzerVolume,
                volumes[std::clamp(g_settings.vsh_buzzer_volume, 0, 2)], 3),
        action(tr("vsh.debug.buzzer.short"), OptionId::BuzzerShort),
        action(tr("vsh.debug.buzzer.long"), OptionId::BuzzerLong),
        action(tr("vsh.debug.buzzer.error"), OptionId::BuzzerError),
    };
    break;
  }

  /* ---- Controller ---- */
  case Page::Controller: {
    /* The light bar takes the colour as the sliders move; colour 2 is the
       pulse animation's second colour (controller_tools). */
    const char *levels[] = {tr("controller.leds.bright"),
                            tr("controller.leds.medium"),
                            tr("controller.leds.dim")};
    options = {
        color_slider(tr("controller.lightbar"), OptionId::ColorController),
        color_slider(tr("vsh.m.lightbar_color2"), OptionId::ColorController2),
        submenu(tr("vsh.m.lightbar_animations"), Page::ControllerAnimations),
        /* the light bar and the player lights share one brightness */
        strings(tr("controller.leds.brightness"), OptionId::LightBrightness,
                levels[std::clamp(s_light_brightness, 0, 2)], 3),
        submenu(tr("vsh.m.player_lights"), Page::ControllerLeds),
        submenu(tr("vsh.m.vibration_test"), Page::ControllerVibration),
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
        action(tr("controller.animation.spectrum"), OptionId::ControllerSpectrum),
        action(tr("controller.animation.neon"), OptionId::ControllerNeonWave),
    };
    break;
  case Page::ControllerLeds: {
    const char *players[] = {"1", "2", "3", "4"};
    options = {
        strings(tr("vsh.m.player_number"), OptionId::PlayerLeds,
                players[std::clamp(s_player_leds, 0, 3)], 4),
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

  /* ---- Home screen ---- */
  case Page::HomeScreen:
    options = {submenu(tr("vsh.m.home_bg"), Page::HomeVideo),
               submenu(tr("vsh.m.clock"), Page::ShellClock),
               submenu(tr("vsh.m.tabs"), Page::HomeTabs),
               submenu(tr("vsh.m.tiles"), Page::ShellTiles),
               submenu(tr("vsh.m.profile"), Page::HomeProfile)};
    break;
  case Page::HomeVideo:
    /* a video, else a picture, with effects and particles over it */
    options = {submenu(tr("vsh.m.home_bg_video"), Page::HomeBgVideo),
               submenu(tr("vsh.m.home_bg_picture"), Page::HomeBgImage),
               submenu(tr("vsh.m.home_bg_effects"), Page::HomeBgEffects),
               submenu(tr("vsh.m.home_particles"), Page::HomeParticles),
               /* the video or picture behind the system apps as well */
               toggle(tr("vsh.home_video.global"),
                      OptionId::HomeBackgroundGlobal,
                      g_settings.home_background_global)};
    break;
  case Page::HomeBgVideo: {
    const std::vector<std::string> &videos = HomeVideo::Videos();
    options = {toggle(tr("vsh.home_video.enable"), OptionId::HomeVideoEnabled,
                      g_settings.home_video_enabled)};
    if (videos.empty()) {
      options.push_back(
          value(tr("vsh.home_video.file"), tr("vsh.home_video.no_videos")));
    } else {
      const int index = std::clamp(home_video_index(), 0,
                                   static_cast<int>(videos.size()) - 1);
      options.push_back(strings(
          tr("vsh.home_video.file"), OptionId::HomeVideoFile,
          HomeVideo::FileName(videos[static_cast<size_t>(index)]).c_str(),
          static_cast<int>(videos.size())));
    }
    options.push_back(toggle(tr("vsh.home_video.sound"),
                             OptionId::HomeVideoSound,
                             g_settings.home_video_sound));
    options.push_back(slider_int(tr("vsh.home_video.volume"),
                                 OptionId::HomeVideoVolume,
                                 g_settings.home_video_volume, 0, 100, 5));
    options.push_back(
        action(tr("vsh.home_video.refresh"), OptionId::HomeVideoRefresh));
    /* where to put files, as seen over FTP or on a USB drive */
    options.push_back(value(tr("vsh.home_video.folder"),
                            ONION_DIST_DATA_ROOT "/ShellUI/videos"));
    options.push_back(value(tr("vsh.home_video.usb_folder"),
                            "USB: /" ONION_DIST_DIR_NAME "/videos"));
    break;
  }
  case Page::HomeBgImage: {
    /* the still image, shown when no video is */
    const std::vector<std::string> &images = HomeVideo::Images();
    options = {toggle(tr("vsh.home_image.enable"), OptionId::HomeImageEnabled,
                      g_settings.home_image_enabled)};
    if (images.empty()) {
      options.push_back(
          value(tr("vsh.home_image.file"), tr("vsh.bg.no_images")));
    } else {
      const int index = std::clamp(home_image_index(), 0,
                                   static_cast<int>(images.size()) - 1);
      options.push_back(strings(
          tr("vsh.home_image.file"), OptionId::HomeImageFile,
          HomeVideo::FileName(images[static_cast<size_t>(index)]).c_str(),
          static_cast<int>(images.size())));
    }
    options.push_back(
        action(tr("vsh.bg.refresh_list"), OptionId::HomeImageRefresh));
    options.push_back(value(tr("vsh.home_image.folder"),
                            ONION_DIST_DATA_ROOT "/ShellUI/background"));
    break;
  }
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
    /* PlayStation shapes floating over the wallpaper or video */
    options = {toggle(tr("vsh.home_particles.enable"),
                      OptionId::HomeParticlesEnabled,
                      g_settings.home_particles_enabled),
               slider_int(tr("vsh.home_particles.count"),
                          OptionId::HomeParticlesCount,
                          g_settings.home_particles_count, 20, 300, 10),
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
    const auto pick = [](int index, int count) {
      return std::clamp(index, 0, count - 1);
    };
    /* the time, then the date */
    options = {
        toggle(tr("vsh.clock.enable"), OptionId::ShellClockEnabled,
               g_settings.shell_clock_enabled),
        strings(tr("vsh.clock.hour_format"), OptionId::ShellClockHourFormat,
                hour_formats[pick(g_settings.shell_clock_hour_format, 3)], 3),
        toggle(tr("vsh.clock.seconds"), OptionId::ShellClockSeconds,
               g_settings.shell_clock_show_seconds),
        slider_int(tr("vsh.clock.time_font_size"),
                   OptionId::ShellClockTimeFontSize,
                   g_settings.shell_clock_time_font_size, 24, 56, 1),
        color_slider(tr("vsh.clock.time_color"), OptionId::ColorClockTime),
        toggle(tr("vsh.clock.date"), OptionId::ShellClockDate,
               g_settings.shell_clock_show_date),
        strings(tr("vsh.clock.date_order"), OptionId::ShellClockDateOrder,
                date_orders[pick(g_settings.shell_clock_date_order, 4)], 4),
        strings(tr("vsh.clock.month_style"), OptionId::ShellClockMonthStyle,
                month_styles[pick(g_settings.shell_clock_month_style, 3)], 3),
        strings(tr("vsh.clock.weekday_style"),
                OptionId::ShellClockWeekdayStyle,
                weekday_styles[pick(g_settings.shell_clock_weekday_style, 3)],
                3),
        toggle(tr("vsh.clock.year"), OptionId::ShellClockYear,
               g_settings.shell_clock_show_year),
        strings(tr("vsh.clock.date_separator"),
                OptionId::ShellClockDateSeparator,
                separators[pick(g_settings.shell_clock_date_separator, 6)], 6),
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
    /* the user's picture as a picture or animated GIF/WebP of theirs */
    const std::vector<std::string> &files = AccountAvatar::Files();
    options = {toggle(tr("vsh.account_avatar.enable"),
                      OptionId::AccountAvatarEnabled,
                      g_settings.account_avatar_enabled)};
    if (files.empty()) {
      options.push_back(value(tr("vsh.account_avatar.file"),
                              tr("vsh.account_avatar.no_files")));
    } else {
      const int index = std::clamp(account_avatar_index(), 0,
                                   static_cast<int>(files.size()) - 1);
      options.push_back(strings(
          tr("vsh.account_avatar.file"), OptionId::AccountAvatarFile,
          AccountAvatar::FileName(files[static_cast<size_t>(index)]).c_str(),
          static_cast<int>(files.size())));
    }
    options.push_back(action(tr("vsh.account_avatar.refresh"),
                             OptionId::AccountAvatarRefresh));
    options.push_back(value(tr("vsh.account_avatar.folder"),
                            ONION_DIST_DATA_ROOT "/ShellUI/avatars"));
    /* the dot on the user's avatar, as if signed in (home_js) */
    const char *dots[] = {tr("vsh.home_presence.system"),
                          tr("vsh.home_presence.online"),
                          tr("vsh.home_presence.busy"),
                          tr("vsh.home_presence.offline")};
    options.push_back(strings(tr("vsh.home_presence.dot"),
                              OptionId::HomePresenceDot,
                              dots[std::clamp(g_settings.home_presence_dot, 0, 3)],
                              4));
    break;
  }

  /* ---- System look: the console's own interface ---- */
  case Page::SystemCustomize: {
    /* the look of the system's own on/off switches (toggle_styles.cpp) */
    const int count = AniketosTheme::ToggleStyles::Count();
    const int style = std::clamp(g_settings.shell_toggle_style, 0, count - 1);
    const char *name = AniketosTheme::ToggleStyles::Name(style);
    options = {submenu(tr("vsh.m.global_theme"), Page::GlobalCustomization),
               submenu(tr("vsh.m.system_bg"), Page::ShellBackground),
               submenu(tr("vsh.m.outlines"), Page::ShellOutline),
               submenu(tr("vsh.m.panels"), Page::ShellUIPanels),
               strings(tr("vsh.toggles.style"), OptionId::ShellToggleStyle,
                       name ? name : tr("vsh.toggles.system"), count),
               toggle(tr("vsh.font.enable"), OptionId::CustomFontEnabled,
                      g_settings.custom_font_enabled)};
    const std::vector<std::string> &fonts = AniketosTheme::CustomFont::Files();
    if (fonts.empty()) {
      options.push_back(value(tr("vsh.font.file"), tr("vsh.font.none")));
    } else {
      const int index = std::clamp(custom_font_index(), 0,
                                   static_cast<int>(fonts.size()) - 1);
      options.push_back(strings(
          tr("vsh.font.file"), OptionId::CustomFontFile,
          AniketosTheme::CustomFont::FileName(fonts[static_cast<size_t>(index)])
              .c_str(),
          static_cast<int>(fonts.size())));
    }
    options.push_back(
        value(tr("vsh.font.folder"), ONION_DIST_DATA_ROOT "/fonts"));
    break;
  }
  case Page::GlobalCustomization: {
    const char *animations[] = {
        tr("vsh.animation.static"), tr("vsh.animation.rainbow_solid"),
        tr("vsh.animation.rainbow_gradient"), tr("vsh.animation.pulse"),
        tr("vsh.animation.ocean")};
    options = {
        toggle(tr("vsh.global_theme.enable"), OptionId::GlobalThemeEnabled,
               g_settings.vsh_global_theme_enabled),
        color_slider(tr("vsh.global_theme.color"), OptionId::ColorGlobal),
        strings(tr("vsh.common.animation"), OptionId::GlobalThemeAnimation,
                animations[std::clamp(g_settings.vsh_color_animation, 0, 4)],
                5),
        slider_int(tr("vsh.common.animation_speed"),
                   OptionId::GlobalThemeAnimationSpeed,
                   g_settings.vsh_color_animation_speed, 25, 400, 25),
    };
    break;
  }
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
                AniketosTheme::AnimationModeName(config.animation),
                AniketosTheme::AnimationModeCount()),
        slider_int(tr("vsh.common.animation_speed"),
                   OptionId::ShellBackgroundAnimationSpeed,
                   g_settings.shell_background_animation_speed,
                   25, 400, 25),
        strings(tr("vsh.bg.native_particles"),
                OptionId::ShellBackgroundParticles,
                AniketosTheme::NativeShellBackground::ParticleModeName(
                    config.particles),
                AniketosTheme::NativeShellBackground::ParticleModeCount()),
        action(tr("vsh.bg.restore_original"), OptionId::ShellRestoreOriginal),
    };
    break;
  }
  case Page::ShellOutline: {
    const auto animation = static_cast<AniketosTheme::AnimationMode>(
        std::clamp(g_settings.shell_tiles_outline_animation, 0,
                   AniketosTheme::AnimationModeCount() - 1));
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
    const std::vector<Option> custom_options = {
        color_slider(tr("vsh.col.outline.color"), OptionId::ColorOutline),
        strings(tr("vsh.tiles.outline_animation"),
                OptionId::ShellTilesOutlineAnimation,
                outline_animation_name(animation),
                AniketosTheme::AnimationModeCount()),
        slider_int(tr("vsh.common.animation_speed"),
                   OptionId::ShellTilesOutlineAnimationSpeed,
                   g_settings.shell_tiles_outline_animation_speed,
                   25, 400, 25),
    };
    options.insert(options.end(), custom_options.begin(),
                   custom_options.end());
    break;
  }
  case Page::ShellUIPanels: {
    const auto animation = static_cast<AniketosTheme::AnimationMode>(
        std::clamp(g_settings.shell_ui_panels_animation, 0,
                   AniketosTheme::AnimationModeCount() - 1));
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
                AniketosTheme::AnimationModeName(animation),
                AniketosTheme::AnimationModeCount()),
        slider_int(tr("vsh.common.animation_speed"),
                   OptionId::ShellUIPanelsAnimationSpeed,
                   g_settings.shell_ui_panels_animation_speed,
                   25, 400, 25),
    };
    break;
  }

  /* ---- Overlays ---- */
  case Page::OSD:
    options = {submenu(tr("vsh.m.osd_home"), Page::HomeScreenOSD),
               submenu(tr("vsh.m.osd_game"), Page::PerformanceOSD)};
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
        submenu(tr("vsh.m.osd_home_settings"), Page::HomeScreenOSDSettings),
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
    const auto pick = [](int index) { return std::clamp(index, 0, 2); };
    options = {
        strings(tr("vsh.ovs.horizontal"), OptionId::OverlayHorizontal,
                horizontal[pick(g_settings.overlay_horizontal)], 3),
        strings(tr("vsh.ovs.vertical"), OptionId::OverlayVertical,
                vertical[pick(g_settings.overlay_vertical)], 3),
        slider_float(tr("vsh.common.text_size"), OptionId::OverlayTextSize,
                     g_settings.overlay_text_size, 12.0f, 48.0f, 1.0f, 0),
        strings(tr("vsh.ovs.temp_type"), OptionId::OverlayTempUnit,
                temperature[pick(g_settings.overlay_temp_unit)], 3),
        strings(tr("vsh.ovs.ram_type"), OptionId::OverlayMemoryUnit,
                memory[pick(g_settings.overlay_memory_unit)], 3),
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
        strings(tr("vsh.game_overlay.position"),
                OptionId::PerformanceOverlayPosition,
                positions[g_settings.overlay_pos == 2 ? 1 : 0], 2),
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

  /* ---- Keyboard ---- */
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

  /* ---- Debug: the system's own DevKit/TestKit overlays (devkit_overlay) */
  case Page::Debug:
    options = {
        toggle(tr("vsh.m.devkit_info"), OptionId::DevkitOverlayInfo,
               g_settings.devkit_overlay_info),
        toggle(tr("vsh.m.devkit_expired"), OptionId::DevkitOverlayExpired,
               g_settings.devkit_overlay_expired),
    };
    break;

  /* ---- Shell Menu look: this menu only ---- */
  case Page::Customize: {
    const char *modes[] = {
        tr("vsh.animation.static"), tr("vsh.animation.rainbow_solid"),
        tr("vsh.animation.rainbow_gradient"), tr("vsh.animation.pulse"),
        tr("vsh.animation.ocean")};
    const char *weights[] = {tr("vsh.clock.font_light"),
                             tr("vsh.clock.font_regular"),
                             tr("vsh.clock.font_bold")};
    const std::string easing = easing_display_name(g_settings.vsh_animation);
    options = {
        strings(tr("vsh.theme.style"), OptionId::MenuThemeSelector,
                theme_display_name(g_settings.vsh_menu_theme),
                kThemeChoiceCount),
        strings(tr("vsh.appearance.mode"), OptionId::MenuAppearanceSelector,
                tr(g_settings.vsh_menu_appearance == 1
                       ? "vsh.appearance.light"
                       : "vsh.appearance.dark"),
                2),
        submenu(tr("vsh.m.menu_colors"), Page::Colors),
        strings(tr("vsh.animation.color_mode"), OptionId::MenuColorAnimation,
                modes[std::clamp(g_settings.vsh_color_animation, 0, 4)], 5),
        slider_int(tr("vsh.common.animation_speed"),
                   OptionId::MenuColorAnimationSpeed,
                   g_settings.vsh_color_animation_speed, 25, 400, 25),
        strings(tr("vsh.m.motion"), OptionId::MenuAnimation, easing.c_str(),
                25),
        toggle(tr("vsh.border.enable"), OptionId::MenuBorderEnabled,
               g_settings.vsh_border_enabled),
        slider_float(tr("vsh.set.border_thickness"), OptionId::MenuBorder,
                     g_settings.vsh_border, 1.0f, 15.0f, 1.0f, 0),
        slider_float(tr("vsh.set.bg_opacity"), OptionId::MenuOpacity,
                     g_settings.vsh_background_opacity, 0, 1, 0.05f, 2),
        slider_float(tr("vsh.common.text_size"), OptionId::MenuTextSize,
                     g_settings.vsh_text_size, 12.0f, 48.0f, 1.0f, 0),
        strings(tr("vsh.text.font_weight"), OptionId::MenuFontWeight,
                weights[std::clamp(g_settings.vsh_font_weight, 0, 2)], 3),
        submenu(tr("vsh.m.size_position"), Page::ScalePosition),
        toggle(tr("vsh.m.shortcut_hint"), OptionId::MenuControls,
               g_settings.vsh_show_controls),
    };
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
        strings(tr("vsh.gradient.direction"),
                OptionId::MenuGradientDirection,
                directions[std::clamp(g_settings.vsh_gradient_direction, 0, 1)],
                2),
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

  /* ---- AniketosHEN ---- */
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
  }
  return options;
}

const std::vector<Option> &page_options(Page page,
                                       std::vector<Option> &scratch) {
  scratch = build_page(page);
  return scratch;
}

uint32_t option_selection_key(const Option &option) {
  if (option.id != OptionId::None)
    return 0x40000000u | static_cast<uint32_t>(option.id);

  // Submenus and informational rows do not have an action id. Hash their
  // immutable description instead of using their changing vector index. The
  // key is only compared within one PageState, so a compact FNV-1a key is
  // sufficient and remains stable across telemetry/settings rebuilds.
  uint32_t hash = 2166136261u;
  const auto add = [&hash](uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
  };
  add(static_cast<uint32_t>(option.type));
  add(static_cast<uint32_t>(option.target));
  for (unsigned char character : option.title) {
    add(character);
  }
  // A control's value is mutable state, not its identity. Including it here
  // made every toggle/selector edit look like a removed row, so
  // ensure_selection() fell back to the first option after each state write.
  // The same holds for a row that only shows a reading (temperature, clock
  // speed, uptime, free memory): the reading changes while the row is looked
  // at, and with it in the key the selection jumped back to the top of the
  // page on every refresh.
  if (option.type != OptionType::Value) {
    add(0xffu);
    for (unsigned char character : option.value) {
      add(character);
    }
  }
  add(static_cast<uint32_t>(option.payload));
  add(static_cast<uint32_t>(option.payload >> 32u));
  for (unsigned char character : option.external_key)
    add(character);
  return hash ? hash : 1u;
}

void ensure_selection(PageState &state, const std::vector<Option> &options) {
  const size_t count = options.size();
  if (!count) {
    state.selected = 0;
    state.scroll = 0;
    state.selected_id = OptionId::None;
    state.selected_key = 0;
    state.selection_initialized = true;
    return;
  }

  // Reconcile an already visited page by option identity.  A changed option
  // list must not make the old numeric index focus a different switch.
  if (state.selection_initialized && state.selected_key != 0) {
    // Rows may share a key (the same action offered twice, two readings
    // with one name): of those, the one nearest to where the selection was
    // is the one meant, not the first on the page.
    bool restored = false;
    size_t nearest = 0, nearest_distance = 0;
    for (size_t i = 0; i < count; ++i) {
      if (option_selection_key(options[i]) != state.selected_key)
        continue;
      const size_t distance =
          i > state.selected ? i - state.selected : state.selected - i;
      if (!restored || distance < nearest_distance) {
        nearest = i;
        nearest_distance = distance;
        restored = true;
      }
    }
    if (restored)
      state.selected = nearest;
    if (!restored) {
      state.selected = 0;
      state.scroll = 0;
    }
  }
  if (state.selected >= count)
    state.selected = count - 1;
  if (state.selected < state.scroll)
    state.scroll = state.selected;
  if (state.selected >= state.scroll + kMaxVisibleOptions)
    state.scroll = state.selected - kMaxVisibleOptions + 1;
  state.selected_id = options[state.selected].id;
  state.selected_key = option_selection_key(options[state.selected]);
  state.selection_initialized = true;
}

void commit_selection(PageState &state, const std::vector<Option> &options,
                      size_t selected) {
  if (options.empty()) {
    state.selected = 0;
    state.scroll = 0;
    state.selected_id = OptionId::None;
    state.selected_key = 0;
    state.selection_initialized = true;
    return;
  }
  state.selected = std::min(selected, options.size() - 1);
  state.selected_id = options[state.selected].id;
  state.selected_key = option_selection_key(options[state.selected]);
  state.selection_initialized = true;
  if (state.selected < state.scroll)
    state.scroll = state.selected;
  if (state.selected >= state.scroll + kMaxVisibleOptions)
    state.scroll = state.selected - kMaxVisibleOptions + 1;
}
