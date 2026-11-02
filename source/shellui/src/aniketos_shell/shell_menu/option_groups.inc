bool is_shell_background_option(OptionId id) {
  switch (id) {
  case OptionId::ShellBackgroundEnabled:
  case OptionId::ShellBackgroundForceHome:
  case OptionId::ShellBackgroundAnimation:
  case OptionId::ShellBackgroundAnimationSpeed:
  case OptionId::ShellBackgroundPs3Waves:
  case OptionId::ShellBackgroundParticles:
  case OptionId::ColorBackground:
  case OptionId::ColorGlow:
  case OptionId::ShellGlowIntensity:
    return true;
  default:
    return false;
  }
}

bool is_shell_tile_option(OptionId id) {
  switch (id) {
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

bool is_icon_color_option(OptionId id) {
  switch (id) {
  case OptionId::IconColorEnabled:
  case OptionId::IconColorAnimation:
  case OptionId::IconColorAnimationSpeed:
  case OptionId::ColorIcon:
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
  case OptionId::PerformanceOverlayFps:
  case OptionId::PerformanceOverlayFan:
  case OptionId::PerformanceOverlayPosition:
    return true;
  default:
    return false;
  }
}
