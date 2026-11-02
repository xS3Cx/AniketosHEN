/* The widgets that belong to the Home screen rather than to the menu: the
 * OSD and the open/close legend (the date label is made on demand). Under Home's root
 * they are UI3 widgets, on our own host UI2 ones. */
const AniketosUI::Pui::Runtime &home_runtime() {
  return s_clock_layer ? s_pui_home : s_pui;
}

MonoObject *home_layer() { return s_clock_layer ? s_clock_layer : s_host; }

/* The name of a pooled widget. The create call it is passed to copies it into
   a managed string, so one buffer serves every slot. */
const char *slot_name(const char *kind, size_t slot) {
  static char name[64];
  std::snprintf(name, sizeof(name), "id_anivsh_%s_%zu", kind, slot);
  return name;
}

MonoObject *create_home_label(MonoObject *parent, const char *name,
                              int horizontal, int lines = 1) {
  AniketosUI::Pui::Runtime runtime = home_runtime();
  runtime.font = s_font;
  MonoObject *label =
      AniketosUI::Pui::CreateLabel(runtime, parent, name, horizontal, lines);
  /* A UI3 label renders its text asynchronously and can get stuck while the
     scene is covered (that is what made the time disappear). */
  if (label && s_clock_layer)
    Set_Property(cls(label), label, "RenderSync", true);
  return label;
}

void create_home_widgets(MonoObject *parent) {
  const AniketosUI::Pui::Runtime &runtime = home_runtime();
  const auto control_panel = [&runtime](const char *name) {
    return AniketosUI::Pui::CreatePanel(runtime, s_widgets.controls, name);
  };
  s_widgets.overlay = create_home_label(parent, kOverlayName, 0, 16);

  /* The "Press L1 + R3 to open" legend. */
  s_widgets.controls = AniketosUI::Pui::CreatePanel(runtime, parent, kControlsName);
  panel_hide(s_widgets.controls);
  s_widgets.control_backdrop = control_panel("id_anivsh_control_backdrop");
  panel_hide(s_widgets.control_backdrop);
  for (int i = 0; i < kControlBorderGradientSlices; ++i) {
    s_widgets.control_border_top[i] = control_panel(slot_name("control_top", i));
    s_widgets.control_border_bottom[i] =
        control_panel(slot_name("control_bottom", i));
  }
  s_widgets.control_border_left = control_panel("id_anivsh_control_left");
  s_widgets.control_border_right = control_panel("id_anivsh_control_right");
  s_widgets.control_icon_l1 = AniketosUI::Pui::CreateSystemImage(
      runtime, s_widgets.controls, "id_anivsh_control_icon_l1", kKeyGuideL1);
  s_widgets.control_icon_r3 = AniketosUI::Pui::CreateSystemImage(
      runtime, s_widgets.controls, "id_anivsh_control_icon_r3", kKeyGuideR3);
  for (int i = 0; i < kControlLabelCount; ++i)
    s_widgets.control_labels[i] =
        create_home_label(s_widgets.controls, slot_name("control_label", i), 0);
}

void apply_control_font() {
  if (!s_control_font)
    return;
  for (MonoObject *label : s_widgets.control_labels)
    Set_Property_Invoke(cls(label), label, "Font", s_control_font);
  for (MonoObject *label : s_widgets.anomaly_control_labels)
    Set_Property_Invoke(cls(label), label, "Font", s_control_font);
}

void create_widgets() {
  if (!s_host || s_widgets.master)
    return;
  remove_named(s_host, kMenuName);
  remove_named(s_host, kOverlayName);
  remove_named(s_host, kControlsName);
  remove_named(s_host, kAnomalyControlsName);

  replace_font(static_cast<int>(std::lround(menu_text_size())));

  s_widgets.master = create_panel(s_host, kMenuName);
  Set_Property(s_widget_class, s_widgets.master, "ClipChildren", true);
  s_widgets.content =
      create_panel(s_widgets.master, "id_anivsh_content");
  s_widgets.header = create_panel(s_widgets.content, "id_anivsh_header");
  s_widgets.body = create_panel(s_widgets.content, "id_anivsh_body");
  s_widgets.footer = create_panel(s_widgets.content, "id_anivsh_footer");
  s_widgets.scroll_clip =
      create_panel(s_widgets.content, "id_anivsh_scroll_clip");
  Set_Property(s_widget_class, s_widgets.scroll_clip, "ClipChildren", true);
  s_widgets.highlight =
      create_panel(s_widgets.scroll_clip, "id_anivsh_highlight");
  // Theme accent planes are inserted before every label so a style can use
  // a real two-colour header/footer without covering text in PUI's painter
  // order. Other themes collapse these planes to zero-sized rectangles.
  for (int i = 0; i < kFrameGradientSlices; ++i) {
    s_widgets.theme_header_slices[i] =
        create_panel(s_widgets.content, slot_name("theme_header", i));
    s_widgets.theme_footer_slices[i] =
        create_panel(s_widgets.content, slot_name("theme_footer", i));
  }
  // Container panels only position/fade their children. Do not submit their
  // fully transparent backgrounds as extra PUI draw calls.
  panel_hide(s_widgets.master);
  panel_hide(s_widgets.content);
  panel_hide(s_widgets.scroll_clip);
  panel_hide(s_widgets.highlight);
  s_widgets.title = create_label(s_widgets.content, "id_anivsh_title", 1);
  s_widgets.header_platform_glyph = create_system_image(
      s_widgets.content, "id_anivsh_header_platform_ps5", kIconTagPS5);
  s_widgets.version = create_label(s_widgets.content, "id_anivsh_version", 0);
  s_widgets.page_title = create_label(s_widgets.content, "id_anivsh_page", 0);
  s_widgets.counter = create_label(s_widgets.content, "id_anivsh_counter", 2);
  /* Header and footer text appear with their bars: PUI otherwise renders a
     label's text on its text thread and shows it frames later. Two short
     labels cost little on the UI thread. */
  for (MonoObject *label : {s_widgets.title, s_widgets.page_title})
    Set_Property(cls(label), label, "RenderSync", true);

  MonoObject *const clip = s_widgets.scroll_clip;
  for (size_t i = 0; i < kMaxVisibleOptions; ++i) {
    s_widgets.rows[i] = create_label(clip, slot_name("row", i), 0);
    s_widgets.values[i] = create_label(clip, slot_name("value", i), 2);
    s_widgets.selector_left[i] =
        create_label(clip, slot_name("selector_left", i), 1);
    s_widgets.selector_right[i] =
        create_label(clip, slot_name("selector_right", i), 1);
    s_widgets.toggles[i] = create_panel(clip, slot_name("toggle", i));
    s_widgets.toggle_inners[i] = create_panel(clip, slot_name("toggle_inner", i));
    s_widgets.toggle_checks[i] = create_panel(clip, slot_name("toggle_check", i));
    s_widgets.slider_tracks[i] = create_panel(clip, slot_name("slider_track", i));
    s_widgets.slider_inners[i] = create_panel(clip, slot_name("slider_inner", i));
    s_widgets.slider_cores[i] = create_panel(clip, slot_name("slider_core", i));
    s_widgets.slider_fills[i] = create_panel(clip, slot_name("slider_fill", i));
    s_widgets.slider_knobs[i] = create_panel(clip, slot_name("slider_knob", i));
    s_widgets.slider_knob_inners[i] =
        create_panel(clip, slot_name("slider_knob_inner", i));
    /* made before the track so that it lies under it: a black edge */
    s_widgets.color_track_borders[i] =
        create_panel(clip, slot_name("color_border", i));
    s_widgets.color_tracks[i] = create_system_image(
        clip, slot_name("color_track", i), ColorTrackPath(ColorTrack::Hues));
    s_widgets.color_track_tops[i] = create_system_image(
        clip, slot_name("color_top", i), ColorTrackPath(ColorTrack::Fade));
    s_widgets.color_hint_pre[i] =
        create_label(clip, slot_name("color_hint_pre", i), 2);
    s_widgets.color_hint_glyphs[i] = create_system_image(
        clip, slot_name("color_hint_glyph", i), kKeyGuideSquare);
    s_widgets.color_hints[i] = create_label(clip, slot_name("color_hint", i), 0);
    s_widgets.color_knobs[i] = create_panel(clip, slot_name("color_knob", i));
    s_widgets.color_knob_inners[i] =
        create_panel(clip, slot_name("color_knob_inner", i));
  }
  s_widgets.position_edit_icon = create_system_image(
      s_widgets.content, "id_anivsh_position_edit_l3", kKeyGuideL3);
  for (int i = 0; i < kGradientSlices; ++i)
    s_widgets.highlight_slices[i] =
        create_panel(s_widgets.highlight, slot_name("highlight", i));
  for (int i = 0; i < 2; ++i) {
    s_widgets.left_borders[i] =
        create_panel(s_widgets.content, slot_name("left", i));
    s_widgets.right_borders[i] =
        create_panel(s_widgets.content, slot_name("right", i));
  }
  for (int line = 0; line < kBorderLines; ++line) {
    for (int segment = 0; segment < kFrameGradientSlices; ++segment) {
      char name[64];
      std::snprintf(name, sizeof(name), "id_anivsh_border_%d_%d", line,
                    segment);
      s_widgets.horizontal_borders[line][segment] =
          create_panel(s_widgets.content, name);
    }
  }
  // Anomaly has a second instruction row directly under its menu. Keep it
  // independent from the bottom-left open/close legend so both can remain
  // visible at the same time.
  s_widgets.anomaly_controls =
      create_panel(s_host, kAnomalyControlsName);
  panel_hide(s_widgets.anomaly_controls);
  const char *anomaly_control_assets[kAnomalyControlIconCount] = {
      kKeyGuideDpad, kKeyGuideL3, kKeyGuideCross, kKeyGuideCircle,
      kKeyGuideDpad,
  };
  for (int i = 0; i < kAnomalyControlIconCount; ++i)
    s_widgets.anomaly_control_icons[i] = create_system_image(
        s_widgets.anomaly_controls, slot_name("anomaly_icon", i),
        anomaly_control_assets[i]);
  for (int i = 0; i < kAnomalyControlLabelCount; ++i)
    s_widgets.anomaly_control_labels[i] = create_label(
        s_widgets.anomaly_controls, slot_name("anomaly_label", i), 0);
  create_home_widgets(home_layer());

  s_control_font = create_pinned_font(14, 400, s_control_font_handle);
  /* The hint before a selected colour slider is laid out like a title:
     the full row height, centred, one line. */
  for (size_t i = 0; i < kMaxVisibleOptions; ++i)
    for (MonoObject *label :
         {s_widgets.color_hint_pre[i], s_widgets.color_hints[i]}) {
      Set_Property(cls(label), label, "MaxLineCount", 1);
      Set_Property(cls(label), label, "FitHeightToText", false);
      Set_Property(cls(label), label, "FitWidthToText", false);
      Set_Property(cls(label), label, "VerticalAlignment", 2);
    }
  // Anomaly's renderer uses a 20 px option font, a 1.9x page title and a
  // 1.1x footer at 1280x720. The PS5 viewport is the exact 1.5x mapping.
  s_anomaly_title_font =
      create_pinned_font(57, 400, s_anomaly_title_font_handle);
  s_anomaly_footer_font =
      create_pinned_font(33, 400, s_anomaly_footer_font_handle);
  apply_control_font();
  s_attached = s_widgets.master && s_widgets.controls &&
               s_widgets.anomaly_controls && s_widgets.overlay;
  s_dirty = true;
  LOG_INFO("[ANIVSH:MENU] widgets created attached=%d pinned=%zu generation=%lu",
           s_attached ? 1 : 0, s_widget_handles.size(),
           static_cast<unsigned long>(s_host_generation));
}

void set_master_geometry(float x, float y, float width, float height) {
  s_menu_target_x = x;
  s_menu_target_y = y;
  s_menu_target_width = std::max(1.0f, width);
  s_menu_target_height = std::max(1.0f, height);
  panel_geometry(s_widgets.master, x, y, width, height);
  panel_geometry(s_widgets.content, 0.0f, 0.0f, width, height);
  // Most themes use their existing absolute coordinates.  Apparition narrows
  // this viewport to its expanding list in its dedicated renderer.
  panel_geometry(s_widgets.scroll_clip, 0.0f, 0.0f, width, height);
}

void apply_font(float font_size = 0.0f) {
  const int size = static_cast<int>(
      std::lround(font_size > 0.0f ? font_size : menu_text_size()));
  replace_font(size);
  label_font(s_widgets.title);
  label_font(s_widgets.version);
  label_font(s_widgets.page_title);
  label_font(s_widgets.counter);
  label_font(s_widgets.overlay);
  apply_control_font();
  for (size_t i = 0; i < kMaxVisibleOptions; ++i)
    option_row_fonts(i);
}

void horizontal_border_gradient(int line, float y, float width, float border,
                                float edge_radius = 0.0f) {
  if (line < 0 || line >= kBorderLines)
    return;
  const float inset = std::clamp(edge_radius, 0.0f, width * 0.5f);
  const float drawable_width = std::max(0.0f, width - inset * 2.0f);
  for (int i = 0; i < kFrameGradientSlices; ++i) {
    const float x0 = inset + drawable_width * static_cast<float>(i) /
                     static_cast<float>(kFrameGradientSlices);
    const float x1 = inset + drawable_width * static_cast<float>(i + 1) /
                     static_cast<float>(kFrameGradientSlices);
    const float t = (static_cast<float>(i) + 0.5f) /
                    static_cast<float>(kFrameGradientSlices);
    MonoObject *slice = s_widgets.horizontal_borders[line][i];
    panel_geometry(slice, x0, y, x1 - x0, border);
    panel_color(slice, menu_dual_color()
                           ? mix(left_color(), right_color(), gradient_t(t), 1.0f)
                           : akari_theme() ? right_color() : left_color());
  }
}

void hide_theme_accent_slices() {
  for (MonoObject *slice : s_widgets.theme_header_slices)
    collapse_panel(slice);
  for (MonoObject *slice : s_widgets.theme_footer_slices)
    collapse_panel(slice);
}
