/* Keep the Home widgets under the root of whatever scene shows the clock.
 * Hooked in there they are not imitating Home's fade and zoom: they are in
 * it. Our own overlay host is only the fallback for when no clock is known
 * or the scene will not take them. */
void sync_clock_layer() {
  if (!s_widgets.master)
    return;
  static MonoObject *refused_root = nullptr;
  MonoObject *root = s_pui_home.label && s_pui_home.panel
                         ? AniketosUI::HomeScene::Root()
                         : nullptr;
  if (root == refused_root)
    root = nullptr;
  const bool lost =
      s_clock_layer && AniketosUI::HomeScene::IsDisposed(s_clock_layer);
  if (root == s_clock_layer && !lost)
    return;
  if (s_clock_layer) {
    if (!lost)
      for (MonoObject *widget : {s_widgets.overlay, s_widgets.clock_date,
                                 s_widgets.controls})
        AniketosUI::Pui::Remove(s_pui, widget);
  } else {
    remove_named(s_host, kControlsName);
    remove_named(s_host, kOverlayName);
    remove_named(s_host, kClockDateName);
  }
  s_clock_layer = root;
  if (root)
    AniketosUI::HomeScene::RemoveStaleChildren(root);
  s_widgets.clock_date = nullptr;
  create_home_widgets(home_layer());
  if (root && (!s_widgets.overlay || !s_widgets.controls ||
               !s_widgets.control_icon_l1)) {
    for (MonoObject *widget : {s_widgets.overlay, s_widgets.controls})
      AniketosUI::Pui::Remove(s_pui, widget);
    /* The scene would not take them: back to our own host for good. */
    refused_root = root;
    s_clock_layer = nullptr;
    create_home_widgets(s_host);
  }
  label_font(s_widgets.overlay);
  s_overlay_dirty = true;
  s_dirty = true; /* lay the legend out again */
  LOG_INFO("[SHELLMENU] home widgets %s root=%p",
           s_clock_layer ? "in Home scene" : "on overlay host",
           static_cast<void *>(root));
}

void debug_home_layer() {
  if (!Profile::enabled || !s_widgets.overlay)
    return;
  static int countdown = 0;
  static std::string last;
  if (--countdown > 0)
    return;
  countdown = 12;
  std::string now = std::string("layer=") + (s_clock_layer ? "1" : "0");
  for (MonoObject *widget : {s_widgets.overlay, s_widgets.clock_date}) {
    now += widget == s_widgets.overlay ? " | osd" : " | date";
    for (const char *name : {"FinalOpacity", "IsReadyToRenderInternal",
                             "X", "Y", "Width"})
      now += std::string(" ") + name + "=" +
             AniketosUI::HomeScene::PropertyText(widget, name);
  }
  now += " | legend rdy=" +
         AniketosUI::HomeScene::PropertyText(s_widgets.control_labels[0],
                                   "IsReadyToRenderInternal") +
         " icon=" +
         AniketosUI::HomeScene::PropertyText(s_widgets.control_icon_l1,
                                   "IsReadyToRenderInternal") +
         " fop=" +
         AniketosUI::HomeScene::PropertyText(s_widgets.control_labels[0], "FinalOpacity") +
         " y=" + AniketosUI::HomeScene::PropertyText(s_widgets.controls, "Y");
  if (now != last) {
    last = now;
    LOG_INFO("[SHELLMENU:PROFILE] osd %s", now.c_str());
  }
}

/* The Home OSD belongs to the Home screen in the same way the clock's date
 * does, so it fades with the system clock instead of staying painted over
 * Settings, the Control Center or anything else that covers Home. */
constexpr float kOverlayOpacity = 0.88f;
float s_overlay_fade = 1.0f;
float s_overlay_applied_opacity = -1.0f;

void update_overlay_fade() {
  if (!s_widgets.overlay)
    return;
  static MonoObject *faded_widget = nullptr;
  if (faded_widget != s_widgets.overlay) {
    faded_widget = s_widgets.overlay;
    s_overlay_applied_opacity = -1.0f;
  }
  /* Home is a React Native tree, so nothing of ours can be parented into it
     (a panel hung under the clock's holder is simply never drawn). The fade
     itself happens above Home's root widget and shows up as the FinalOpacity
     of everything below it; mirror the clock label's value frame by frame,
     unsmoothed, which gives the same curve as the time. */
  float wanted = 1.0f;
  if (!s_clock_layer) /* in Home's scene PUI does it; otherwise mirror */
    ShellClock::GetNativeClockOpacity(&wanted);
  s_overlay_fade = wanted;
  debug_home_layer();
  const float opacity = kOverlayOpacity * s_overlay_fade;
  if (std::fabs(opacity - s_overlay_applied_opacity) <= 0.001f)
    return;
  Set_Property(cls(s_widgets.overlay), s_widgets.overlay, "Opacity", opacity);
  s_overlay_applied_opacity = opacity;
}

void update_overlay() {
  if (!s_widgets.overlay)
    return;

  const Telemetry::Snapshot telemetry = Telemetry::GetSnapshot();
  s_last_overlay_sample = telemetry.sampled_at_ms;
  if (!g_settings.home_osd_enabled) {
    clear_label(s_widgets.overlay);
    return;
  }

  const auto temperature = [](int celsius) {
    switch (g_settings.overlay_temp_unit) {
    case 1: return number(celsius * 9.0f / 5.0f + 32.0f, 0) + " F";
    case 2: return number(celsius + 273.15f, 0) + " K";
    default:
      return std::to_string(celsius) + kCelsiusSuffix;
    }
  };
  const auto memory = [](int megabytes) {
    switch (g_settings.overlay_memory_unit) {
    case 0: return std::to_string(megabytes * 1024) + " KB";
    case 2: return number(megabytes / 1024.0f, 2) + " GB";
    default: return std::to_string(megabytes) + " MB";
    }
  };

  const auto entry = [](const char *key, const std::string &value) {
    return std::string(tr(key)) + ": " + value;
  };
  const std::string unknown = "--";
  std::vector<std::string> lines;
  if (g_settings.home_osd_fps)
    lines.push_back(entry("vsh.hud.fps",
                          telemetry.shell_fps_valid
                              ? number(telemetry.shell_fps,
                                       g_settings.overlay_fps_precision)
                              : unknown));
  if (g_settings.overlay_resolution && s_viewport_valid) {
    char viewport[64];
    std::snprintf(viewport, sizeof(viewport), "%s: %.0fx%.0f",
                  tr("vsh.hud.viewport"), s_viewport_width,
                  s_viewport_height);
    lines.emplace_back(viewport);
  }
  if (g_settings.home_osd_cpu || g_settings.home_osd_gpu) {
    std::string temperatures;
    if (g_settings.home_osd_cpu)
      temperatures = entry("vsh.info.cpu_temp",
                           telemetry.cpu_temperature_valid
                               ? temperature(telemetry.cpu_temperature_c)
                               : unknown);
    if (g_settings.home_osd_cpu && g_settings.home_osd_gpu)
      temperatures += " / ";
    if (g_settings.home_osd_gpu)
      temperatures += entry("vsh.info.soc_temp",
                            telemetry.soc_temperature_valid
                                ? temperature(telemetry.soc_temperature_c)
                                : unknown);
    lines.push_back(std::move(temperatures));
  }
  if (g_settings.home_osd_fan)
    lines.push_back(entry("vsh.info.fan_duty",
                          telemetry.fan_duty_valid
                              ? number(telemetry.fan_duty_percent, 1) + "%"
                              : unknown));
  if (g_settings.home_osd_ram && telemetry.memory_valid)
    lines.push_back(entry("vsh.info.system_memory",
                          number(telemetry.memory_used_percent, 1) + "% " +
                              memory(telemetry.memory_used_mb) + " / " +
                              memory(telemetry.memory_total_mb)));
  if (g_settings.overlay_free_ram && telemetry.memory_valid)
    lines.push_back(
        entry("vsh.info.free_memory", memory(telemetry.memory_free_mb)));
  if (g_settings.overlay_storage && telemetry.storage_valid)
    lines.push_back(entry("vsh.info.storage",
                          storage_amount(telemetry.storage_free_bytes,
                                         telemetry.storage_total_bytes)));
  if (g_settings.overlay_firmware && !telemetry.firmware.empty())
    lines.push_back(entry("vsh.info.firmware", telemetry.firmware));
  if (g_settings.overlay_uptime && telemetry.uptime_valid)
    lines.push_back(
        entry("vsh.info.uptime", uptime_text(telemetry.uptime_seconds)));
  if (g_settings.home_osd_ip && telemetry.local_ip_valid)
    lines.push_back(entry("vsh.info.local_ip", telemetry.local_ip));

  if (lines.empty()) {
    clear_label(s_widgets.overlay);
    return;
  }

  std::string text;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i)
      text += '\n';
    text += lines[i];
  }
  const float scale_x = s_viewport_width / kReferenceWidth;
  const float scale_y = s_viewport_height / kReferenceHeight;
  const int font_size = std::clamp(
      static_cast<int>(std::lround(g_settings.overlay_text_size * scale_y)),
      12, 72);
  if (!s_overlay_font || s_overlay_font_size != font_size) {
    if (s_overlay_font_handle && mono_gchandle_free)
      mono_gchandle_free(s_overlay_font_handle);
    s_overlay_font_handle = 0;
    s_overlay_font =
        create_pinned_font(font_size, 300, s_overlay_font_handle);
    s_overlay_font_size = s_overlay_font ? font_size : 0;
  }
  if (s_overlay_font)
    Set_Property_Invoke(cls(s_widgets.overlay), s_widgets.overlay, "Font",
                        s_overlay_font);

  const float margin_x = 40.0f * scale_x;
  const float margin_y = 36.0f * scale_y;
  const float line_height = std::ceil(font_size * 1.40f);
  const float width = std::min(home_layout_width() - margin_x * 2.0f,
                               940.0f * scale_x);
  const float height = line_height * static_cast<float>(lines.size());
  /* `height` is the painted text block and decides where it sits. The label
     itself is made taller: PUI's real line advance is larger than 1.40 em, so
     a box of exactly `height` clipped the last line - with every line shown,
     switching one more on revealed the previously hidden one instead. */
  const float box_height =
      std::ceil(font_size * 1.75f) * static_cast<float>(lines.size()) +
      line_height;
  const float x = g_settings.overlay_horizontal == 0
                      ? margin_x
                      : g_settings.overlay_horizontal == 1
                            ? (home_layout_width() - width) * 0.5f
                            : home_layout_width() - margin_x - width;
  const float y = g_settings.overlay_vertical == 0
                      ? margin_y
                      : g_settings.overlay_vertical == 1
                            ? (s_viewport_height - height) * 0.5f
                            : s_viewport_height - margin_y - height;
  label_text(s_widgets.overlay, text);
  MonoClass *klass = cls(s_widgets.overlay);
  Set_Property(klass, s_widgets.overlay, "MaxLineCount",
               static_cast<int>(lines.size()));
  Set_Property(klass, s_widgets.overlay, "HorizontalAlignment",
               g_settings.overlay_horizontal);
  Set_Property(klass, s_widgets.overlay, "VerticalAlignment", 0);
  Set_Property(klass, s_widgets.overlay, "FitHeightToText", false);
  s_overlay_applied_opacity = kOverlayOpacity * s_overlay_fade;
  Set_Property(klass, s_widgets.overlay, "Opacity", s_overlay_applied_opacity);
  set_color(klass, s_widgets.overlay, "TextColor", {0.92f, 0.92f, 0.92f, 1.0f});
  label_geometry(s_widgets.overlay, x, std::max(0.0f, y), width, box_height);
}
