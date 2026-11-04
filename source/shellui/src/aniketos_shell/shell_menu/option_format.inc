const char *tr(const char *key) { return toolbox_i18n::tr(key); }

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
  case Page::HomeTabDetail: return tr("vsh.m.tab_detail");
  case Page::ShellTiles: return tr("vsh.m.tiles");
  case Page::HomeProfile: return tr("vsh.m.profile");
  case Page::SystemCustomize: return tr("vsh.m.system_look");
  case Page::IconColor: return tr("vsh.m.icon_color");
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
  return kMenuBrandName;
}

/* A colour channel of 0..1 as a 0..255 slider value. */
int theme_channel(float value) {
  return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) *
                                      255.0f));
}

/* A saved animation setting by name, clamped as the theme config clamps it. */
const char *animation_mode_name(int setting) {
  const auto mode = static_cast<AniketosTheme::AnimationMode>(
      std::clamp(setting, 0, AniketosTheme::AnimationModeCount() - 1));
  return mode == AniketosTheme::AnimationMode::Rainbow
             ? tr("controller.animation.rainbow")
             : tr("vsh.animation.static");
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
  const unsigned long long days = seconds / 86400u;
  const unsigned long long hours = (seconds % 86400u) / 3600u;
  const unsigned long long minutes = (seconds % 3600u) / 60u;
  const unsigned long long secs = seconds % 60u;
  const char *hour_unit = tr("vsh.unit.hour_short");
  const char *minute_unit = tr("vsh.unit.minute_short");
  const char *second_unit = tr("vsh.unit.second_short");
  char buffer[64];
  if (days > 0)
    std::snprintf(buffer, sizeof(buffer), "%llu%s %02llu%s %02llu%s", days,
                  tr("vsh.unit.day_short"), hours, hour_unit, minutes,
                  minute_unit);
  else if (hours > 0)
    std::snprintf(buffer, sizeof(buffer), "%llu%s %02llu%s %02llu%s", hours,
                  hour_unit, minutes, minute_unit, secs, second_unit);
  else
    std::snprintf(buffer, sizeof(buffer), "%llu%s %02llu%s", minutes,
                  minute_unit, secs, second_unit);
  return buffer;
}

std::string easing_display_name(int type) {
  if (type <= 0)
    return tr("vsh.easing.linear");
  constexpr const char *families[] = {
      "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back"};
  const int index = std::clamp(type - 1, 0, 23);
  const char *direction = index % 3 == 0   ? tr("vsh.easing.in")
                          : index % 3 == 1 ? tr("vsh.easing.out")
                                           : tr("vsh.easing.in_out");
  return std::string(direction) + " (" + families[index / 3] + ")";
}
