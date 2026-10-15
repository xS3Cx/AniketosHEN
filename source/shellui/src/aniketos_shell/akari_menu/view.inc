/* The widgets that belong to the Home screen rather than to the menu: the
 * OSD and the open/close legend (the date label is made on demand). Under Home's root
 * they are UI3 widgets, on our own host UI2 ones. */
const AniketosUI::Pui::Runtime &home_runtime() {
  return s_clock_layer ? s_pui_home : s_pui;
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
  s_widgets.overlay = create_home_label(parent, kOverlayName, 0, 16);

  /* The "Press L1 + R3 to open" legend. */
  s_widgets.controls = AniketosUI::Pui::CreatePanel(runtime, parent, kControlsName);
  panel_hide(s_widgets.controls);
  s_widgets.control_backdrop =
      AniketosUI::Pui::CreatePanel(runtime, s_widgets.controls, "id_anivsh_control_backdrop");
  panel_hide(s_widgets.control_backdrop);
  for (int i = 0; i < kControlBorderGradientSlices; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "id_anivsh_control_top_%d", i);
    s_widgets.control_border_top[i] = AniketosUI::Pui::CreatePanel(runtime, s_widgets.controls, name);
    std::snprintf(name, sizeof(name), "id_anivsh_control_bottom_%d", i);
    s_widgets.control_border_bottom[i] =
        AniketosUI::Pui::CreatePanel(runtime, s_widgets.controls, name);
  }
  s_widgets.control_border_left =
      AniketosUI::Pui::CreatePanel(runtime, s_widgets.controls, "id_anivsh_control_left");
  s_widgets.control_border_right =
      AniketosUI::Pui::CreatePanel(runtime, s_widgets.controls, "id_anivsh_control_right");
  s_widgets.control_icon_l1 = AniketosUI::Pui::CreateSystemImage(
      runtime, s_widgets.controls, "id_anivsh_control_icon_l1", kKeyGuideL1);
  s_widgets.control_icon_r3 = AniketosUI::Pui::CreateSystemImage(
      runtime, s_widgets.controls, "id_anivsh_control_icon_r3", kKeyGuideR3);
  for (int i = 0; i < kControlLabelCount; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "id_anivsh_control_label_%d", i);
    s_widgets.control_labels[i] =
        create_home_label(s_widgets.controls, name, 0);
  }
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
    char name[64];
    std::snprintf(name, sizeof(name), "id_anivsh_theme_header_%d", i);
    s_widgets.theme_header_slices[i] =
        create_panel(s_widgets.content, name);
    std::snprintf(name, sizeof(name), "id_anivsh_theme_footer_%d", i);
    s_widgets.theme_footer_slices[i] =
        create_panel(s_widgets.content, name);
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

  for (size_t i = 0; i < kMaxVisibleOptions; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "id_anivsh_row_%zu", i);
    s_widgets.rows[i] = create_label(s_widgets.scroll_clip, name, 0);
    std::snprintf(name, sizeof(name), "id_anivsh_value_%zu", i);
    s_widgets.values[i] = create_label(s_widgets.scroll_clip, name, 2);
    std::snprintf(name, sizeof(name), "id_anivsh_selector_left_%zu", i);
    s_widgets.selector_left[i] = create_label(s_widgets.scroll_clip, name, 1);
    std::snprintf(name, sizeof(name), "id_anivsh_selector_right_%zu", i);
    s_widgets.selector_right[i] = create_label(s_widgets.scroll_clip, name, 1);
    std::snprintf(name, sizeof(name), "id_anivsh_toggle_%zu", i);
    s_widgets.toggles[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_toggle_inner_%zu", i);
    s_widgets.toggle_inners[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_toggle_check_%zu", i);
    s_widgets.toggle_checks[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_slider_track_%zu", i);
    s_widgets.slider_tracks[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_slider_inner_%zu", i);
    s_widgets.slider_inners[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_slider_core_%zu", i);
    s_widgets.slider_cores[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_slider_fill_%zu", i);
    s_widgets.slider_fills[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_slider_knob_%zu", i);
    s_widgets.slider_knobs[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_slider_knob_inner_%zu", i);
    s_widgets.slider_knob_inners[i] =
        create_panel(s_widgets.scroll_clip, name);
    /* made before the track so that it lies under it: a black edge */
    std::snprintf(name, sizeof(name), "id_anivsh_color_border_%zu", i);
    s_widgets.color_track_borders[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_color_track_%zu", i);
    s_widgets.color_tracks[i] = create_system_image(
        s_widgets.scroll_clip, name, ColorTrackPath(ColorTrack::Hues));
    std::snprintf(name, sizeof(name), "id_anivsh_color_top_%zu", i);
    s_widgets.color_track_tops[i] = create_system_image(
        s_widgets.scroll_clip, name, ColorTrackPath(ColorTrack::Fade));
    std::snprintf(name, sizeof(name), "id_anivsh_color_hint_pre_%zu", i);
    s_widgets.color_hint_pre[i] = create_label(s_widgets.scroll_clip, name, 2);
    std::snprintf(name, sizeof(name), "id_anivsh_color_hint_glyph_%zu", i);
    s_widgets.color_hint_glyphs[i] = create_system_image(
        s_widgets.scroll_clip, name, kKeyGuideSquare);
    std::snprintf(name, sizeof(name), "id_anivsh_color_hint_%zu", i);
    s_widgets.color_hints[i] = create_label(s_widgets.scroll_clip, name, 0);
    std::snprintf(name, sizeof(name), "id_anivsh_color_knob_%zu", i);
    s_widgets.color_knobs[i] = create_panel(s_widgets.scroll_clip, name);
    std::snprintf(name, sizeof(name), "id_anivsh_color_knob_inner_%zu", i);
    s_widgets.color_knob_inners[i] = create_panel(s_widgets.scroll_clip, name);
  }
  s_widgets.position_edit_icon = create_system_image(
      s_widgets.content, "id_anivsh_position_edit_l3", kKeyGuideL3);
  for (int i = 0; i < kGradientSlices; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "id_anivsh_highlight_%d", i);
    s_widgets.highlight_slices[i] = create_panel(s_widgets.highlight, name);
  }
  for (int i = 0; i < 2; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "id_anivsh_left_%d", i);
    s_widgets.left_borders[i] = create_panel(s_widgets.content, name);
    std::snprintf(name, sizeof(name), "id_anivsh_right_%d", i);
    s_widgets.right_borders[i] = create_panel(s_widgets.content, name);
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
  };
  for (int i = 0; i < kAnomalyControlIconCount; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "id_anivsh_anomaly_icon_%d", i);
    s_widgets.anomaly_control_icons[i] = create_system_image(
        s_widgets.anomaly_controls, name, anomaly_control_assets[i]);
  }
  for (int i = 0; i < kAnomalyControlLabelCount; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "id_anivsh_anomaly_label_%d", i);
    s_widgets.anomaly_control_labels[i] =
        create_label(s_widgets.anomaly_controls, name, 0);
  }
  create_home_widgets(s_clock_layer ? s_clock_layer : s_host);

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
  if (s_control_font) {
    for (MonoObject *label : s_widgets.control_labels)
      Set_Property_Invoke(cls(label), label, "Font", s_control_font);
    for (MonoObject *label : s_widgets.anomaly_control_labels)
      Set_Property_Invoke(cls(label), label, "Font", s_control_font);
  }
  s_attached = s_widgets.master && s_widgets.controls &&
               s_widgets.anomaly_controls && s_widgets.overlay;
  s_dirty = true;
  LOG_INFO("[ANIVSH:AKARI] widgets created attached=%d pinned=%zu generation=%lu",
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

void apply_font() {
  const int size = static_cast<int>(std::lround(menu_text_size()));
  replace_font(size);
  label_font(s_widgets.title);
  label_font(s_widgets.version);
  label_font(s_widgets.page_title);
  label_font(s_widgets.counter);
  label_font(s_widgets.overlay);
  if (s_control_font) {
    for (MonoObject *label : s_widgets.control_labels)
      Set_Property_Invoke(cls(label), label, "Font", s_control_font);
    for (MonoObject *label : s_widgets.anomaly_control_labels)
      Set_Property_Invoke(cls(label), label, "Font", s_control_font);
  }
  for (size_t i = 0; i < kMaxVisibleOptions; ++i) {
    label_font(s_widgets.rows[i]);
    label_font(s_widgets.values[i]);
    label_font(s_widgets.selector_left[i]);
    label_font(s_widgets.selector_right[i]);
  }
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
    panel_color(slice, active_menu_style().dual_color
                           ? mix(left_color(), right_color(), gradient_t(t), 1.0f)
                           : left_color());
  }
}

void hide_theme_accent_slices() {
  for (MonoObject *slice : s_widgets.theme_header_slices) {
    panel_geometry(slice, 0, 0, 0, 0);
    panel_hide(slice);
  }
  for (MonoObject *slice : s_widgets.theme_footer_slices) {
    panel_geometry(slice, 0, 0, 0, 0);
    panel_hide(slice);
  }
}

void update_auxiliary_frame(
    MonoObject *const *top, MonoObject *const *bottom, MonoObject *left,
    MonoObject *right, int segment_count, float width, float height,
    float border, bool visible) {
  visible = visible && border > 0.0f && width > 0.0f && height > 0.0f;
  for (int i = 0; i < segment_count; ++i) {
    if (!visible) {
      panel_geometry(top[i], 0, 0, 0, 0);
      panel_geometry(bottom[i], 0, 0, 0, 0);
      panel_hide(top[i]);
      panel_hide(bottom[i]);
      continue;
    }
    const float x0 = width * static_cast<float>(i) /
                     static_cast<float>(segment_count);
    const float x1 = width * static_cast<float>(i + 1) /
                     static_cast<float>(segment_count);
    const float t = (static_cast<float>(i) + 0.5f) /
                    static_cast<float>(segment_count);
    const Color color = active_menu_style().dual_color
        ? mix(left_color(), right_color(), gradient_t(t), 1.0f)
        : left_color();
    panel_geometry(top[i], x0, 0.0f, x1 - x0, border);
    panel_geometry(bottom[i], x0, height - border, x1 - x0, border);
    panel_color(top[i], color);
    panel_color(bottom[i], color);
  }
  if (!visible) {
    panel_geometry(left, 0, 0, 0, 0);
    panel_geometry(right, 0, 0, 0, 0);
    panel_hide(left);
    panel_hide(right);
    return;
  }
  // Let the horizontal segments own all four corners. This prevents the side
  // panels (created later in painter order) from covering the first/last
  // gradient samples and matches the menu frame's non-overlapping geometry.
  const float side_height = std::max(0.0f, height - border * 2.0f);
  panel_geometry(left, 0.0f, border, border, side_height);
  panel_geometry(right, width - border, border, border, side_height);
  panel_color(left, gradient_edge_color(/*right_edge=*/false));
  panel_color(right, gradient_edge_color(/*right_edge=*/true));
}

void update_controls() {
  if (!s_widgets.controls)
    return;
  if (s_auxiliary_color_only_update)
    return;
  const bool show_controls = show_menu_controls();
  // Native KeyGuide images use the same firmware UI assets as ShellUI hints.
  constexpr float kHeight = 28.0f;
  // Keep the compact open/close hint close to the physical bottom-left edge.
  constexpr float kOuterMargin = 4.0f;
  constexpr float kHorizontalPadding = 6.0f;
  constexpr float kItemGap = 0.0f;
  constexpr float kChordGap = 3.0f;
  constexpr float kGlyphTextGap = 5.0f;
  constexpr float kIconHeight = 16.0f;
  constexpr float kIconY = 6.0f;
  constexpr float kControlTextSize = 14.0f;
  struct TextWidthCache {
    MonoObject *label = nullptr;
    MonoObject *font = nullptr;
    uint64_t host_generation = 0;
    std::string text;
    float width = 0.0f;
  };
  static TextWidthCache width_cache[kControlLabelCount];
  float x = kHorizontalPadding;
  int next_label = 0;
  auto text_item = [&](const char *text) {
    if (next_label >= kControlLabelCount)
      return;
    const int label_index = next_label++;
    MonoObject *label = s_widgets.control_labels[label_index];
    TextWidthCache &cache = width_cache[label_index];
    const std::string value = text ? text : "";
    if (cache.label != label || cache.font != s_control_font ||
        cache.host_generation != s_host_generation ||
        cache.text != value) {
      label_text(label, value);
      if (s_control_font)
        Set_Property_Invoke(cls(label), label, "Font", s_control_font);
      Set_Property(cls(label), label, "MaxLineCount", 1);
      Set_Property(cls(label), label, "TextTrimming", 0);
      // A centred label exposes half of its safety width before/after the
      // painted text. In a compact icon chord that looked like an arbitrary
      // gap around "Press", "+" and the action. Anchor the text at the exact
      // cursor position; spacing between items is owned only by the constants
      // below.
      Set_Property(cls(label), label, "HorizontalAlignment", 0);
      // UI2.VerticalAlignment is Top=0, Bottom=1, Center=2 on 11.40.
      // Bottom was the reason the instruction text sat visibly below the
      // centre of its 34 px container.
      Set_Property(cls(label), label, "VerticalAlignment", 2);
      const float fallback =
          approximate_text_width(value.c_str(), kControlTextSize);
      cache.label = label;
      cache.font = s_control_font;
      cache.host_generation = s_host_generation;
      cache.text = value;
      // GetTextWidth is asynchronous in this injected UI tree and can report
      // the previous label's (much wider) layout immediately after a text
      // change. That stale width created the visible gap before L1. The
      // deterministic UTF-8-aware estimate keeps the glyph flush to "Press".
      cache.width = std::max(2.0f, std::ceil(fallback));
    }
    const float width = cache.width;
    label_geometry(label, x, 2.0f, width, kHeight);
    x += width;
  };
  auto icon_item = [&](MonoObject *icon, float width) {
    image_geometry(icon, x, kIconY, width, kIconHeight);
    x += width;
  };
  auto shortcut = [&](const char *action) {
    text_item(tr("vsh.ctrl.press"));
    x += kItemGap;
    icon_item(s_widgets.control_icon_l1, 27.0f);
    x += kChordGap;
    text_item("+");
    x += kChordGap;
    icon_item(s_widgets.control_icon_r3, 22.0f);
    x += kGlyphTextGap;
    text_item(action);
  };

  if (show_controls)
    shortcut(IsVisible() ? close_control_text() : open_control_text());
  else {
    image_geometry(s_widgets.control_icon_l1, 0.0f, 0.0f, 0.0f, 0.0f,
                   false);
    image_geometry(s_widgets.control_icon_r3, 0.0f, 0.0f, 0.0f, 0.0f,
                   false);
  }

  const float control_width =
      show_controls ? std::ceil(x + kHorizontalPadding) : 0.0f;
  panel_geometry(s_widgets.controls, kOuterMargin,
                 s_viewport_height - kOuterMargin - kHeight,
                 control_width,
                 show_controls ? kHeight : 0.0f);
  // The compact key guide is intentionally a free-standing glyph/text row,
  // matching the original Shell presentation in both foreground contexts.
  panel_hide(s_widgets.controls);
  panel_geometry(s_widgets.control_backdrop, 0.0f, 0.0f, 0.0f, 0.0f);
  panel_hide(s_widgets.control_backdrop);
  update_auxiliary_frame(
      s_widgets.control_border_top, s_widgets.control_border_bottom,
      s_widgets.control_border_left, s_widgets.control_border_right,
      kControlBorderGradientSlices, 0.0f, 0.0f, 0.0f, false);

  for (int i = next_label; i < kControlLabelCount; ++i) {
    label_text(s_widgets.control_labels[i], "");
    label_geometry(s_widgets.control_labels[i], 0, 0, 0, 0);
    width_cache[i] = {};
  }
}

void hide_game_shell_extras() {
  // PanelHide only clears a panel's own background on the supported UI2
  // runtime; it does not hide child labels/images. Clear every child as well
  // so the game-hosted mod menu cannot inherit ShellUI's OSD or its
  // "Press ... to open/close" legend from the HomeUI host.
  if (s_widgets.overlay) {
    label_text(s_widgets.overlay, "");
    label_geometry(s_widgets.overlay, 0.0f, 0.0f, 0.0f, 0.0f);
  }
  if (s_widgets.clock_date) {
    label_text(s_widgets.clock_date, "");
    label_geometry(s_widgets.clock_date, 0.0f, 0.0f, 0.0f, 0.0f);
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "Opacity", 0.0f);
  }

  panel_geometry(s_widgets.controls, 0.0f, 0.0f, 0.0f, 0.0f);
  panel_hide(s_widgets.controls);
  panel_geometry(s_widgets.control_backdrop, 0.0f, 0.0f, 0.0f, 0.0f);
  panel_hide(s_widgets.control_backdrop);
  for (MonoObject *panel : s_widgets.control_border_top) {
    panel_geometry(panel, 0.0f, 0.0f, 0.0f, 0.0f);
    panel_hide(panel);
  }
  for (MonoObject *panel : s_widgets.control_border_bottom) {
    panel_geometry(panel, 0.0f, 0.0f, 0.0f, 0.0f);
    panel_hide(panel);
  }
  panel_geometry(s_widgets.control_border_left, 0.0f, 0.0f, 0.0f, 0.0f);
  panel_hide(s_widgets.control_border_left);
  panel_geometry(s_widgets.control_border_right, 0.0f, 0.0f, 0.0f, 0.0f);
  panel_hide(s_widgets.control_border_right);
  for (MonoObject *label : s_widgets.control_labels) {
    label_text(label, "");
    label_geometry(label, 0.0f, 0.0f, 0.0f, 0.0f);
  }
  image_geometry(s_widgets.control_icon_l1, 0.0f, 0.0f, 0.0f, 0.0f, false);
  image_geometry(s_widgets.control_icon_r3, 0.0f, 0.0f, 0.0f, 0.0f, false);

  panel_geometry(s_widgets.anomaly_controls, 0.0f, 0.0f, 0.0f, 0.0f);
  panel_hide(s_widgets.anomaly_controls);
  for (MonoObject *image : s_widgets.anomaly_control_icons)
    image_geometry(image, 0.0f, 0.0f, 0.0f, 0.0f, false);
  for (MonoObject *label : s_widgets.anomaly_control_labels) {
    label_text(label, "");
    label_geometry(label, 0.0f, 0.0f, 0.0f, 0.0f);
  }
}

void hide_anomaly_extras() {
  panel_geometry(s_widgets.anomaly_controls, 0, 0, 0, 0);
  panel_hide(s_widgets.anomaly_controls);
  for (MonoObject *icon : s_widgets.anomaly_control_icons)
    image_geometry(icon, 0, 0, 0, 0, false);
  for (MonoObject *label : s_widgets.anomaly_control_labels) {
    label_text(label, "");
    label_geometry(label, 0, 0, 0, 0);
  }
  image_geometry(s_widgets.position_edit_icon, 0, 0, 0, 0, false);
}


float slider_progress(const Option &option) {
  if (option.type == OptionType::SliderInt) {
    const int value = std::atoi(option.value.c_str());
    return option.maximum_i == option.minimum_i
               ? 0.0f
               : static_cast<float>(value - option.minimum_i) /
                     static_cast<float>(option.maximum_i - option.minimum_i);
  }
  if (option.type == OptionType::SliderFloat) {
    const float value = std::strtof(option.value.c_str(), nullptr);
    return option.maximum_f == option.minimum_f
               ? 0.0f
               : (value - option.minimum_f) /
                     (option.maximum_f - option.minimum_f);
  }
  return 0.0f;
}

#include "option_controls.inc"
#include "anomaly_theme.inc"
#include "apparition_theme.inc"

void update_menu_widgets() {
  if (!s_widgets.master || s_stack.empty())
    return;
  if (active_menu_theme() != 5) {
    for (size_t i = 0; i < kMaxVisibleOptions; ++i)
      clear_selector_markers(i);
  }
  // Keep one coherent local animation timestamp for this structural frame.
  // The timestamp is page-owned; it must not be the absolute process clock.
  s_menu_color_frame_ms = active_menu_color_frame_ms();
  s_global_color_sample_ms = monotonic_millis();
  s_menu_color_frame_valid = true;
  image_geometry(s_widgets.header_platform_glyph, 0.0f, 0.0f, 0.0f, 0.0f,
                 false);
  if (active_menu_theme() == 2) {
    update_anomaly_menu_widgets();
    return;
  }
  // IDs 1, 3 and 4 belonged to styles that were removed; they fall back to
  // Akari.
  if (active_menu_theme() == 5) {
    update_apparition_menu_widgets();
    return;
  }
  PageState &state = s_stack.back();
  std::vector<Option> options_scratch;
  const std::vector<Option> &options =
      page_options(state.page, options_scratch);
  ensure_selection(state, options);
  hide_theme_accent_slices();
  hide_anomaly_extras();

  // The menu uses one pixel-snapped Akari layout calculation.
  const AniketosUI::AkariLayout layout = AniketosUI::CalculateAkariLayout(
      active_menu_style(), s_viewport_width,
      s_viewport_height, options.size(), kMaxVisibleOptions);
  const float width = layout.width;
  const float border = layout.border;
  const float outer_width = layout.outer_width;
  const float row = layout.row;
  const float padding = layout.padding;
  const float body_height = layout.body_height;
  const float header_y = layout.header_y;
  const float header_bottom = layout.header_bottom;
  const float body_y = layout.body_y;
  const float body_bottom = layout.body_bottom;
  const float footer_y = layout.footer_y;
  const float total_height = layout.total_height;
  const float x = layout.x;
  const float y = layout.y;
  set_master_geometry(x, y, outer_width, total_height);
  const Color background{0, 0, 0, active_menu_style().background_opacity};
  // These are the original PUI2 surfaces. Keep them as real header/body/
  // footer panels so their native painter order, clipping and theme colours
  // remain intact on every firmware.
  panel_geometry(s_widgets.header, 0.0f, header_y, outer_width, row);
  panel_geometry(s_widgets.body, border, body_y, width, body_height);
  panel_geometry(s_widgets.footer, border, footer_y, width, row);
  panel_color(s_widgets.header, background);
  panel_color(s_widgets.body, background);
  panel_color(s_widgets.footer, background);
  for (MonoObject *slice : s_widgets.theme_header_slices)
    panel_hide(slice);
  for (MonoObject *slice : s_widgets.theme_footer_slices)
    panel_hide(slice);

  // Header, body and footer lines all share the same pixel-snapped thickness.
  // The header remains free of vertical side bars.
  const float section_y[2] = {body_y, footer_y};
  const float section_h[2] = {body_height, row};
  for (int i = 0; i < 2; ++i) {
    panel_geometry(s_widgets.left_borders[i], 0, section_y[i], border,
                   section_h[i]);
    panel_geometry(s_widgets.right_borders[i], border + width, section_y[i],
                   border, section_h[i]);
    panel_color(s_widgets.left_borders[i],
                gradient_edge_color(/*right_edge=*/false));
    panel_color(s_widgets.right_borders[i],
                gradient_edge_color(/*right_edge=*/true));
  }
  horizontal_border_gradient(0, 0.0f, outer_width, border);
  horizontal_border_gradient(1, header_bottom, outer_width, border);
  horizontal_border_gradient(2, body_bottom, outer_width, border);
  horizontal_border_gradient(3, footer_y - border, outer_width, border);
  horizontal_border_gradient(4, footer_y + row, outer_width, border);

  apply_font();
  Set_Property(s_label_class, s_widgets.title, "HorizontalAlignment", 1);
  Set_Property(s_label_class, s_widgets.page_title, "HorizontalAlignment", 0);
  Set_Property(s_label_class, s_widgets.counter, "HorizontalAlignment", 2);
  Set_Property(s_label_class, s_widgets.title, "VerticalAlignment", 2);
  Set_Property(s_label_class, s_widgets.page_title, "VerticalAlignment", 2);
  Set_Property(s_label_class, s_widgets.counter, "VerticalAlignment", 2);
  label_text(s_widgets.version, "");
  set_color(s_label_class, s_widgets.title, "TextColor",
            Color{1.0f, 1.0f, 1.0f, 1.0f});
  label_text(s_widgets.title, kMenuBrandName);
  label_geometry(s_widgets.title, 0.0f, header_y, outer_width, row);
  label_text(s_widgets.page_title, "");
  label_geometry(s_widgets.page_title, 0.0f, 0.0f, 0.0f, 0.0f);
  char counter[32];
  std::snprintf(counter, sizeof(counter), "[%zu/%zu]",
                options.empty() ? 0 : state.selected + 1, options.size());
  label_text(s_widgets.counter, counter);
  label_geometry(s_widgets.counter, border + width * 0.7f, footer_y,
                 width * 0.3f, row);

  const float target_highlight =
      body_y + static_cast<float>(state.selected - state.scroll) * row;
  update_highlight_target(state, target_highlight);
  panel_geometry(s_widgets.highlight, border, s_highlight_y, width, row);
  for (int i = 0; i < kGradientSlices; ++i) {
    // Derive both edges independently. This removes the old +1 px overlap,
    // which doubled alpha at every seam and caused the visible banding.
    const float x0 = width * static_cast<float>(i) /
                     static_cast<float>(kGradientSlices);
    const float x1 = width * static_cast<float>(i + 1) /
                     static_cast<float>(kGradientSlices);
    const float t = (static_cast<float>(i) + 0.5f) /
                    static_cast<float>(kGradientSlices);
    panel_geometry(s_widgets.highlight_slices[i], x0, 0, x1 - x0, row);
    panel_color(s_widgets.highlight_slices[i],
                active_menu_style().dual_color
                    ? mix(left_color(), right_color(), gradient_t(t), 0.4f)
                    : right_color(0.4f));
  }

  const float text_x = border + padding;
  const float content_right = border + width - padding;
  const float control_right = border + width;
  const float base_value_width = width * 0.43f;
  const float column_gap = std::max(8.0f, row * 0.25f);
  for (size_t visible_index = 0; visible_index < kMaxVisibleOptions;
       ++visible_index) {
    const size_t option_index = state.scroll + visible_index;
    const bool exists = option_index < options.size();
    const float row_y = body_y + visible_index * row;
    if (!exists) {
      label_text(s_widgets.rows[visible_index], "");
      label_text(s_widgets.values[visible_index], "");
      Set_Property(s_label_class, s_widgets.rows[visible_index], "Opacity", 0.0f);
      Set_Property(s_label_class, s_widgets.values[visible_index], "Opacity", 0.0f);
      label_angle(s_widgets.rows[visible_index], 0.0f);
      label_angle(s_widgets.values[visible_index], 0.0f);
      clear_selector_markers(visible_index);
      hide_appendum_option_control(visible_index);
      hide_color_hint(visible_index);
      continue;
    }
    const Option &option = options[option_index];
    /* a row whose value is too long for the usual column (a console
       identifier) gives it most of the width; its title is short */
    const float value_width =
        option.wide_value ? width * 0.80f : base_value_width;
    const bool selected = option_index == state.selected;
    // Apparition deliberately fades unused pooled labels to zero. Restore the
    // complete native row state before another theme reuses the same widgets;
    // otherwise only the previously selected row remains visible after a
    // theme switch.
    Set_Property(s_label_class, s_widgets.rows[visible_index], "Opacity", 1.0f);
    Set_Property(s_label_class, s_widgets.values[visible_index], "Opacity", 1.0f);
    label_angle(s_widgets.rows[visible_index], 0.0f);
    label_angle(s_widgets.values[visible_index], 0.0f);
    const AppendumControlLayout control =
        update_appendum_option_control(visible_index, option, selected, row_y,
                                       row, control_right);
    const float value_x = content_right - value_width;
    const float title_right = control.toggle || control.slider ||
                                      control.color
                                  ? control.title_right
                              : option.type == OptionType::Action
                                  ? content_right
                                  : value_x - column_gap;
    const float title_width = std::max(0.0f, title_right - text_x);
    label_text(s_widgets.rows[visible_index], option.title);
    label_geometry(s_widgets.rows[visible_index], text_x, row_y, title_width,
                   row);
    std::string display_value;
    if (option.type == OptionType::Submenu)
      display_value = ">";
    else if (option.type == OptionType::Strings)
      display_value = option.value;
    else if (option.type == OptionType::Value ||
             option.type == OptionType::ThemeChoice)
      display_value = option.value;
    else if (control.slider)
      display_value = option.value;
    const bool selector = option.type == OptionType::Strings ||
                          option.type == OptionType::ThemeChoice;
    if (selector)
      display_value = selector_display_value(option);
    label_text(s_widgets.values[visible_index], display_value);
    const SelectorMarkerLayout selector_layout = update_selector_markers(
        visible_index, selector, option.string_index, option.string_count,
        value_x, row_y, value_width, row,
        selector_painted_text_width(display_value.c_str(), row),
        selected ? Color{0.0f, 0.0f, 0.0f, 1.0f}
                 : Color{1.0f, 1.0f, 1.0f, 1.0f},
        selected ? Color{0.25f, 0.25f, 0.25f, 0.65f}
                 : Color{0.35f, 0.35f, 0.35f, 0.65f});
    if (control.slider) {
      value_label_geometry(s_widgets.values[visible_index], control.value_x,
                           row_y, control.value_width, row,
                           false);
    } else if (selector_layout.visible) {
      selector_value_label_geometry(s_widgets.values[visible_index],
                                    selector_layout.value_x, row_y,
                                    selector_layout.value_width, row);
    } else {
      value_label_geometry(s_widgets.values[visible_index], value_x, row_y,
                           value_width, row,
                           false);
    }
  }

  update_controls();
  s_dirty = false;
}

MonoObject *home_layer() { return s_clock_layer ? s_clock_layer : s_host; }

/* Keep the Home widgets under the root of whatever scene shows the clock.
 * Hooked in there they are not imitating Home's fade and zoom: they are in
 * it. Our own overlay host is only the fallback for when no clock is known
 * or the scene will not take them. */
void sync_clock_layer() {
  if (!s_widgets.master)
    return;
  static MonoObject *refused_root = nullptr;
  MonoObject *root = s_pui_home.label && s_pui_home.panel
                         ? ShellClock::GetHomeRoot()
                         : nullptr;
  if (root == refused_root)
    root = nullptr;
  const bool lost =
      s_clock_layer && ShellClock::IsWidgetDisposed(s_clock_layer);
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
    ShellClock::RemoveStaleHomeChildren(root);
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
  if (!Profile::g_enabled || !s_widgets.overlay)
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
             ShellClock::DebugPropText(widget, name);
  }
  now += " | legend rdy=" +
         ShellClock::DebugPropText(s_widgets.control_labels[0],
                                   "IsReadyToRenderInternal") +
         " icon=" +
         ShellClock::DebugPropText(s_widgets.control_icon_l1,
                                   "IsReadyToRenderInternal") +
         " fop=" +
         ShellClock::DebugPropText(s_widgets.control_labels[0], "FinalOpacity") +
         " y=" + ShellClock::DebugPropText(s_widgets.controls, "Y");
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
    label_text(s_widgets.overlay, "");
    label_geometry(s_widgets.overlay, 0.0f, 0.0f, 0.0f, 0.0f);
    return;
  }

  const auto temperature = [](int celsius) {
    switch (g_settings.overlay_temp_unit) {
    case 1: return number(celsius * 9.0f / 5.0f + 32.0f, 0) + " F";
    case 2: return number(celsius + 273.15f, 0) + " K";
    default:
      return std::to_string(celsius) +
             AniketosShell::TextUnits::kCelsiusSuffix;
    }
  };
  const auto memory = [](int megabytes) {
    switch (g_settings.overlay_memory_unit) {
    case 0: return std::to_string(megabytes * 1024) + " KB";
    case 2: return number(megabytes / 1024.0f, 2) + " GB";
    default: return std::to_string(megabytes) + " MB";
    }
  };

  std::vector<std::string> lines;
  if (g_settings.home_osd_fps)
    lines.push_back(std::string(tr("vsh.hud.fps")) + ": " +
                    (telemetry.shell_fps_valid
                         ? number(telemetry.shell_fps,
                                  g_settings.overlay_fps_precision)
                         : std::string("--")));
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
      temperatures += std::string(tr("vsh.info.cpu_temp")) + ": " +
                      (telemetry.cpu_temperature_valid
                           ? temperature(telemetry.cpu_temperature_c)
                           : std::string("--"));
    if (g_settings.home_osd_cpu && g_settings.home_osd_gpu)
      temperatures += " / ";
    if (g_settings.home_osd_gpu)
      temperatures += std::string(tr("vsh.info.soc_temp")) + ": " +
                      (telemetry.soc_temperature_valid
                           ? temperature(telemetry.soc_temperature_c)
                           : std::string("--"));
    lines.push_back(std::move(temperatures));
  }
  if (g_settings.home_osd_fan)
    lines.push_back(std::string(tr("vsh.info.fan_duty")) + ": " +
                    (telemetry.fan_duty_valid
                         ? number(telemetry.fan_duty_percent, 1) + "%"
                         : std::string("--")));
  if (g_settings.home_osd_ram && telemetry.memory_valid) {
    std::string ram = std::string(tr("vsh.info.system_memory")) + ": " +
                      number(telemetry.memory_used_percent, 1) + "% " +
                      memory(telemetry.memory_used_mb) + " / " +
                      memory(telemetry.memory_total_mb);
    lines.push_back(std::move(ram));
  }
  if (g_settings.overlay_free_ram && telemetry.memory_valid)
    lines.push_back(std::string(tr("vsh.info.free_memory")) + ": " +
                    memory(telemetry.memory_free_mb));
  if (g_settings.overlay_storage && telemetry.storage_valid)
    lines.push_back(std::string(tr("vsh.info.storage")) + ": " +
                    storage_amount(telemetry.storage_free_bytes,
                                   telemetry.storage_total_bytes));
  if (g_settings.overlay_firmware && !telemetry.firmware.empty())
    lines.push_back(std::string(tr("vsh.info.firmware")) + ": " +
                    telemetry.firmware);
  if (g_settings.overlay_uptime && telemetry.uptime_valid)
    lines.push_back(std::string(tr("vsh.info.uptime")) + ": " +
                    uptime_text(telemetry.uptime_seconds));
  if (g_settings.home_osd_ip && telemetry.local_ip_valid)
    lines.push_back(std::string(tr("vsh.info.local_ip")) + ": " +
                    telemetry.local_ip);

  if (lines.empty()) {
    label_text(s_widgets.overlay, "");
    label_geometry(s_widgets.overlay, 0.0f, 0.0f, 0.0f, 0.0f);
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
  Set_Property(cls(s_widgets.overlay), s_widgets.overlay, "MaxLineCount",
               static_cast<int>(lines.size()));
  Set_Property(cls(s_widgets.overlay), s_widgets.overlay, "HorizontalAlignment",
               g_settings.overlay_horizontal);
  Set_Property(cls(s_widgets.overlay), s_widgets.overlay, "VerticalAlignment", 0);
  Set_Property(cls(s_widgets.overlay), s_widgets.overlay, "FitHeightToText", false);
  s_overlay_applied_opacity = kOverlayOpacity * s_overlay_fade;
  Set_Property(cls(s_widgets.overlay), s_widgets.overlay, "Opacity",
               s_overlay_applied_opacity);
  set_color(cls(s_widgets.overlay), s_widgets.overlay, "TextColor",
            {0.92f, 0.92f, 0.92f, 1.0f});
  label_geometry(s_widgets.overlay, x, std::max(0.0f, y), width, box_height);
}

void update_clock_date() {
  ShellClock::DateOverlayState state{};
  ShellClock::GetDateOverlayState(&state);
  /* Same frame-by-frame mirror of the clock's opacity as the OSD. */
  float clock_opacity = 0.0f;
  ShellClock::GetNativeClockOpacity(&clock_opacity);
  if (s_clock_layer) /* part of Home's scene: PUI fades it with the clock */
    clock_opacity = 1.0f;
  const bool visible = state.wanted && clock_opacity > 0.0f;
  /* What was last written to the date label. This function runs every frame
     for as long as the host exists, and nearly all of what it sets only
     changes with the minute, a setting or the clock position; setting Font or
     geometry again re-runs the label's layout even with identical values. */
  struct AppliedDate {
    MonoObject *widget = nullptr;
    MonoObject *font = nullptr;
    float x = 0, y = 0, width = 0, height = 0, opacity = -1.0f;
  };
  static AppliedDate applied;
  if (applied.widget != s_widgets.clock_date) {
    applied = {};
    applied.widget = s_widgets.clock_date;
  }

  static float shown_opacity = 0.0f;
  shown_opacity = visible ? clock_opacity : 0.0f;
  if (!visible) {
    if (s_widgets.clock_date &&
        std::fabs(applied.opacity - shown_opacity) > 0.001f) {
      Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "Opacity",
                   shown_opacity);
      applied.opacity = shown_opacity;
      if (shown_opacity == 0.0f)
        label_text(s_widgets.clock_date, "");
    }
    return;
  }

  if (!s_widgets.clock_date) {
    if (!s_clock_layer)
      remove_named(s_host, kClockDateName);
    s_widgets.clock_date = create_home_label(home_layer(), kClockDateName, 1);
    if (!s_widgets.clock_date)
      return;
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "VerticalAlignment", 0);
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "MaxLineCount", 1);
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "NumberOfLines", 1);
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "TextTrimming", 0);
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "FitWidthToText", false);
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "FitHeightToText", false);
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date,
                 "EnableThemedColor", false);
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date,
                 "EnableThemedShadowColor", false);
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date,
                 "EnableThemedTextShadow", true);
  }

  if (!s_clock_date_font || !s_clock_date_font_handle ||
      s_clock_date_font_size != state.font_size) {
    if (s_clock_date_font_handle && mono_gchandle_free)
      mono_gchandle_free(s_clock_date_font_handle);
    s_clock_date_font_handle = 0;
    s_clock_date_font =
        create_pinned_font(state.font_size, 400, s_clock_date_font_handle);
    s_clock_date_font_size = s_clock_date_font ? state.font_size : 0;
  }
  if (applied.widget != s_widgets.clock_date) { /* created just above */
    applied = {};
    applied.widget = s_widgets.clock_date;
  }
  if (s_clock_date_font && applied.font != s_clock_date_font) {
    Set_Property_Invoke(cls(s_widgets.clock_date), s_widgets.clock_date, "Font",
                        s_clock_date_font);
    applied.font = s_clock_date_font;
  }

  const float scale_x = s_viewport_width / kReferenceWidth;
  const float scale_y = s_viewport_height / kReferenceHeight;
  const float width = std::clamp(
      approximate_text_width(state.text.c_str(),
                             static_cast<float>(state.font_size)) +
          44.0f * scale_x,
      180.0f * scale_x, 720.0f * scale_x);
  /* Sits right under the digits, centred on them whatever its length. */
  const float time_width = std::max(state.time_width * scale_x, 52.0f * scale_x);
  const bool time_located =
      state.has_global_time_geometry && state.time_center_x > 0.0f &&
      state.time_center_x < home_layout_width();
  const float time_right =
      time_located ? state.time_center_x + time_width * 0.5f
                   : home_layout_width() - 84.0f * scale_x;
  const int alignment = 1;
  const float rest_x = std::clamp(
      time_right - time_width * 0.5f - width * 0.5f, 0.0f,
      std::max(0.0f, home_layout_width() - width));
  const float height = std::max(30.0f * scale_y,
                                state.font_size * 1.75f * scale_y);
  const bool valid_time_geometry =
      state.has_global_time_geometry && state.time_text_bottom_y > 0.0f &&
      state.time_text_bottom_y < s_viewport_height;
  const float time_bottom =
      valid_time_geometry ? state.time_text_bottom_y : 80.0f * scale_y;
  /* "Date spacing" is the gap between the digits and the date; it can be
     negative to tuck the date in, but never far enough to overlap. */
  const float date_gap =
      std::max(0.0f, 6.0f + static_cast<float>(state.date_spacing));
  const float requested_y = time_bottom + date_gap * scale_y;
  const float rest_y = std::clamp(
      requested_y, 0.0f, std::max(0.0f, s_viewport_height - height));
  const float x = rest_x;
  const float y = rest_y;

  label_text(s_widgets.clock_date, state.text);
  static int applied_alignment = -1;
  static MonoObject *aligned_widget = nullptr;
  if (applied_alignment != alignment || aligned_widget != s_widgets.clock_date) {
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "HorizontalAlignment",
                 alignment);
    applied_alignment = alignment;
    aligned_widget = s_widgets.clock_date;
  }
  set_color(cls(s_widgets.clock_date), s_widgets.clock_date, "TextColor",
            {state.r / 255.0f, state.g / 255.0f, state.b / 255.0f, 1.0f});
  const float opacity = shown_opacity;
  if (std::fabs(applied.opacity - opacity) > 0.001f) {
    Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "Opacity", opacity);
    applied.opacity = opacity;
  }
  if (std::fabs(applied.x - x) > 0.01f || std::fabs(applied.y - y) > 0.01f ||
      std::fabs(applied.width - width) > 0.01f ||
      std::fabs(applied.height - height) > 0.01f) {
    label_geometry(s_widgets.clock_date, x, y, width, height);
    applied.x = x;
    applied.y = y;
    applied.width = width;
    applied.height = height;
  }
}

bool page_transition_active() {
  return s_page_transition != PageTransitionState::Idle;
}

// Geometry is shared by UI2.Panel and UI2.Label, but the managed property is
// declared on the concrete class (or one of its parents). Reading a label
// through UI2.Panel returns zero on firmwares that do not expose the inherited
// property through that unrelated class. That zero rectangle was the source
// of the disappearing footer and the detached transition text.
MonoClass *resolve_page_transition_property_class(MonoObject *object,
                                                   const char *property) {
  if (!object || !property || !mono_object_get_class ||
      !mono_class_get_property_from_name || !mono_class_get_parent)
    return nullptr;
  for (MonoClass *klass = mono_object_get_class(object); klass;
       klass = mono_class_get_parent(klass)) {
    if (mono_class_get_property_from_name(klass, property))
      return klass;
  }
  return nullptr;
}

/* A page change reads the geometry of ~260 pooled widgets in one frame. They
 * are instances of three classes, so resolve the declaring class once per
 * class instead of walking the hierarchy by name for every widget. */
MonoClass *page_transition_property_class(MonoObject *object,
                                           const char *property) {
  struct Entry {
    MonoClass *object_class;
    const char *property;
    MonoClass *declaring_class;
  };
  static Entry cache[8];
  MonoClass *object_class =
      object && mono_object_get_class ? mono_object_get_class(object) : nullptr;
  if (!object_class || !property)
    return nullptr;
  for (Entry &entry : cache) {
    if (entry.object_class == object_class && entry.property == property)
      return entry.declaring_class;
    if (!entry.object_class) {
      MonoClass *declaring =
          resolve_page_transition_property_class(object, property);
      if (declaring)
        entry = {object_class, property, declaring};
      return declaring;
    }
  }
  return resolve_page_transition_property_class(object, property);
}

PageTransitionRect read_page_transition_rect(MonoObject *object) {
  MonoClass *klass = page_transition_property_class(object, "X");
  if (!klass)
    return {};
  return {
      Get_Property<float>(klass, object, "X"),
      Get_Property<float>(klass, object, "Y"),
      Get_Property<float>(klass, object, "Width"),
      Get_Property<float>(klass, object, "Height")};
}

PageTransitionRect lerp_page_transition_rect(const PageTransitionRect &from,
                                             const PageTransitionRect &to,
                                             float progress) {
  const float t = std::clamp(progress, 0.0f, 1.0f);
  return {from.x + (to.x - from.x) * t,
          from.y + (to.y - from.y) * t,
          from.width + (to.width - from.width) * t,
          from.height + (to.height - from.height) * t};
}

void capture_page_transition_target_geometry() {
  if (!s_page_transition_start_geometry.valid)
    return;
  s_page_transition_target_geometry.valid = true;
  s_page_transition_target_geometry.master =
      read_page_transition_rect(s_widgets.master);
  s_page_transition_target_geometry.content =
      read_page_transition_rect(s_widgets.content);
  s_page_transition_target_geometry.header =
      read_page_transition_rect(s_widgets.header);
  s_page_transition_target_geometry.body =
      read_page_transition_rect(s_widgets.body);
  s_page_transition_target_geometry.scroll_clip =
      read_page_transition_rect(s_widgets.scroll_clip);
  s_page_transition_target_geometry.footer =
      read_page_transition_rect(s_widgets.footer);
  s_page_transition_target_geometry.page_title =
      read_page_transition_rect(s_widgets.page_title);
  s_page_transition_target_geometry.counter =
      read_page_transition_rect(s_widgets.counter);
  s_page_transition_target_geometry.version =
      read_page_transition_rect(s_widgets.version);
  for (int i = 0; i < kFrameGradientSlices; ++i) {
    s_page_transition_target_geometry.theme_header_slice[i] =
        read_page_transition_rect(s_widgets.theme_header_slices[i]);
    s_page_transition_target_geometry.theme_footer_slice[i] =
        read_page_transition_rect(s_widgets.theme_footer_slices[i]);
    s_page_transition_target_geometry.body_bottom_border[i] =
        read_page_transition_rect(s_widgets.horizontal_borders[2][i]);
    s_page_transition_target_geometry.footer_top_border[i] =
        read_page_transition_rect(s_widgets.horizontal_borders[3][i]);
    s_page_transition_target_geometry.footer_bottom_border[i] =
        read_page_transition_rect(s_widgets.horizontal_borders[4][i]);
  }
}

void apply_page_transition_rect(MonoObject *object,
                                const PageTransitionRect &from,
                                const PageTransitionRect &to,
                                float progress) {
  if (!object)
    return;
  const PageTransitionRect current =
      lerp_page_transition_rect(from, to, progress);
  panel_geometry(object, current.x, current.y, current.width,
                 current.height);
}

void apply_page_transition_vertical(MonoObject *object, float y,
                                    float height) {
  if (!object || !s_panel_class)
    return;
  AniketosUI::Pui::PanelYHeight(s_pui, object, y, height);
}

void begin_page_transition(PageTransitionAction action,
                           Page target = Page::Main) {
  if (page_transition_active() || s_animation != AnimationState::Visible)
    return;
  if (action == PageTransitionAction::Pop && s_stack.size() <= 1)
    return;
  const uint64_t profile_t0 = Profile::g_enabled ? Profile::NowNs() : 0;

  // Reconcile the parent page before copying it. This makes the transition
  // path authoritative even if activation came from a redraw rather than the
  // normal input tick.
  std::vector<Option> outgoing_options = build_page(s_stack.back().page);
  ensure_selection(s_stack.back(), outgoing_options);
  const PageState outgoing = s_stack.back();
  const char *outgoing_item =
      outgoing.selected < outgoing_options.size()
          ? outgoing_options[outgoing.selected].title.c_str()
          : "<none>";
  const char *action_name = action == PageTransitionAction::Push ? "push"
                           : action == PageTransitionAction::Pop ? "pop"
                                                                  : "none";
  LOG_INFO("[ANIVSH:STATE] transition_begin action=%s from=%s "
           "selected=%zu/%zu selected_id=%d item=%s scroll=%zu theme=%d",
           action_name, page_title(outgoing.page), outgoing.selected,
           outgoing_options.size(), static_cast<int>(outgoing.selected_id),
           outgoing_item, outgoing.scroll, active_menu_theme());
  ++s_page_focus_epoch;
  // The expanding UIScrollView-style transition is an Apparition-specific
  // composition. The other themes historically switch pages immediately and
  // must keep their own native layout/paint timing unchanged.
  if (active_menu_theme() != 5) {
    if (action == PageTransitionAction::Push) {
      PageState next{};
      next.page = target;
      s_stack.push_back(next);
    } else if (action == PageTransitionAction::Pop) {
      s_stack.pop_back();
    }
    if (!s_stack.empty())
      reset_page_selection(s_stack.back());
    s_page_transition = PageTransitionState::Idle;
    s_page_transition_started_ms = 0;
    s_page_transition_start_geometry = {};
    s_page_transition_target_geometry = {};
    s_page_transition_text_revealed = true;
    update_menu_widgets();
    snap_highlight_to_target();
    s_highlight_page_epoch = s_page_focus_epoch;
    const PageState &entered = s_stack.back();
    const std::vector<Option> entered_options = build_page(entered.page);
    const char *entered_item =
        entered.selected < entered_options.size()
            ? entered_options[entered.selected].title.c_str()
            : "<none>";
    LOG_INFO("[ANIVSH:STATE] transition_commit page=%s selected=%zu/%zu "
             "selected_id=%d item=%s scroll=%zu focus_epoch=%lu "
             "highlight=%.2f",
             page_title(entered.page), entered.selected, entered_options.size(),
             static_cast<int>(entered.selected_id), entered_item, entered.scroll,
             static_cast<unsigned long>(s_page_focus_epoch), s_highlight_to);
    s_dirty = true;
    return;
  }
  // Capture the real PUI2 geometry before the destination page is rendered.
  // Apparition keeps the menu origin stable and animates the outer frame,
  // scroll viewport and footer together; retaining these rectangles gives the
  // native ShellUI renderer the same hand-off point without guessing a
  // header-relative collapse height.
  s_page_transition_start_geometry.valid =
      s_widgets.master && s_widgets.content && s_widgets.header &&
      s_widgets.body && s_widgets.scroll_clip && s_widgets.footer;
  s_page_transition_target_geometry = {};
  if (s_page_transition_start_geometry.valid) {
    s_page_transition_start_geometry.master =
        read_page_transition_rect(s_widgets.master);
    s_page_transition_start_geometry.content =
        read_page_transition_rect(s_widgets.content);
    s_page_transition_start_geometry.header =
        read_page_transition_rect(s_widgets.header);
    s_page_transition_start_geometry.body =
        read_page_transition_rect(s_widgets.body);
    s_page_transition_start_geometry.scroll_clip =
        read_page_transition_rect(s_widgets.scroll_clip);
    s_page_transition_start_geometry.footer =
        read_page_transition_rect(s_widgets.footer);
    s_page_transition_start_geometry.page_title =
        read_page_transition_rect(s_widgets.page_title);
    s_page_transition_start_geometry.counter =
        read_page_transition_rect(s_widgets.counter);
    s_page_transition_start_geometry.version =
        read_page_transition_rect(s_widgets.version);
    for (int i = 0; i < kFrameGradientSlices; ++i) {
      s_page_transition_start_geometry.theme_header_slice[i] =
          read_page_transition_rect(s_widgets.theme_header_slices[i]);
      s_page_transition_start_geometry.theme_footer_slice[i] =
          read_page_transition_rect(s_widgets.theme_footer_slices[i]);
      s_page_transition_start_geometry.body_bottom_border[i] =
          read_page_transition_rect(s_widgets.horizontal_borders[2][i]);
      s_page_transition_start_geometry.footer_top_border[i] =
          read_page_transition_rect(s_widgets.horizontal_borders[3][i]);
      s_page_transition_start_geometry.footer_bottom_border[i] =
          read_page_transition_rect(s_widgets.horizontal_borders[4][i]);
    }
    s_page_transition_start_height =
        std::max(1.0f, s_page_transition_start_geometry.master.height);
  } else {
    s_page_transition_start_height = std::max(1.0f, s_menu_target_height);
  }
  const uint64_t profile_t1 = profile_t0 ? Profile::NowNs() : 0;
  if (action == PageTransitionAction::Push) {
    PageState next{};
    next.page = target;
    s_stack.push_back(next);
  } else if (action == PageTransitionAction::Pop) {
    s_stack.pop_back();
  }
  if (!s_stack.empty())
    reset_page_selection(s_stack.back());
  // A page switch must be a single UI hand-off. Build the destination now,
  // while the old rectangles are still retained above, so no render can show
  // the old value pool with the new row labels (or vice versa). This is also
  // important on ShellUI builds where managed property setters are observed
  // in the same frame rather than deferred to the next tick.
  s_highlight_started_ms = 0;
  s_highlight_from = 0.0f;
  s_highlight_to = 0.0f;
  s_highlight_y = 0.0f;
  s_highlight_page = Page::Main;
  s_highlight_selection_key = 0;
  s_highlight_scroll = 0;
  s_highlight_initialized = false;
  update_menu_widgets();
  const uint64_t profile_t2 = profile_t0 ? Profile::NowNs() : 0;
  // A page hand-off selects the destination page's first valid row. The
  // structural layout above computes that exact destination, so do not replay
  // the normal Up/Down cursor animation from y=0 while the viewport expands;
  // that made the highlight visibly land on a different switch on exit.
  if (active_menu_theme() == 5) {
    snap_highlight_to_target();
    s_apparition_force_color_refresh = true;
    s_color_dirty = true;
  }
  s_highlight_page_epoch = s_page_focus_epoch;
  const PageState &entered = s_stack.back();
  const std::vector<Option> entered_options = build_page(entered.page);
  const char *entered_item =
      entered.selected < entered_options.size()
          ? entered_options[entered.selected].title.c_str()
          : "<none>";
  LOG_INFO("[ANIVSH:STATE] transition_commit page=%s selected=%zu/%zu "
           "selected_id=%d item=%s scroll=%zu focus_epoch=%lu "
           "highlight=%.2f color_frame=%lu",
           page_title(entered.page), entered.selected, entered_options.size(),
           static_cast<int>(entered.selected_id), entered_item, entered.scroll,
           static_cast<unsigned long>(s_page_focus_epoch), s_highlight_to,
           static_cast<unsigned long>(s_menu_color_frame_ms));
  if (s_page_transition_start_geometry.valid)
    capture_page_transition_target_geometry();
  if (profile_t0)
    LOG_INFO("[SHELLMENU:PROFILE] page change: read_start=%llu us "
             "relayout=%llu us commit+read_target=%llu us",
             static_cast<unsigned long long>((profile_t1 - profile_t0) / 1000),
             static_cast<unsigned long long>((profile_t2 - profile_t1) / 1000),
             static_cast<unsigned long long>((Profile::NowNs() - profile_t2) /
                                             1000));

  // Keep the current height as the visual hand-off point. The destination is
  // committed immediately while the outer clip animates from the old page
  // height to the new capped height.
  s_page_transition = PageTransitionState::Expanding;
  s_page_transition_started_ms = monotonic_millis();
  // The destination labels were committed above and remain live throughout
  // the clip expansion.  Do not schedule a second complete PUI2 relayout on
  // the next tick; that duplicate pooled-label rewrite was another source of
  // intermittent disappearing text.
  s_page_transition_text_revealed = true;
  s_dirty = false;
}

float update_page_transition(uint64_t now) {
  if (s_page_transition == PageTransitionState::Idle)
    return 1.0f;

  const float t = std::min(
      1.0f, static_cast<float>(now - s_page_transition_started_ms) /
                kPageTransitionDurationMs);
  // Page resizing must stay monotonic even when the user selected a Back or
  // other overshooting menu easing for open/close animations. UIKit's submenu
  // resize is a calm in-out curve, so keep that curve local to page changes.
  const float reveal = Easing::Apply(/*In-out Sine=*/3, t);
  if (t >= 1.0f) {
    s_page_transition_text_revealed = true;
    s_page_transition = PageTransitionState::Idle;
    return 1.0f;
  }
  return reveal;
}

void apply_page_transition_reveal(float reveal) {
  if (!s_widgets.master)
    return;

  const float progress = std::clamp(reveal, 0.0f, 1.0f);
  if (s_page_transition_start_geometry.valid) {
    // The renderer has already laid out the destination page. Interpolating
    // the structural PUI2 rectangles reproduces UIScrollView's behaviour:
    // content remains a full list, while its viewport and the footer move to
    // the new capped height in one smooth motion.
    apply_page_transition_rect(s_widgets.master,
                               s_page_transition_start_geometry.master,
                               s_page_transition_target_geometry.master,
                               progress);
    apply_page_transition_rect(s_widgets.content,
                               s_page_transition_start_geometry.content,
                               s_page_transition_target_geometry.content,
                               progress);
    apply_page_transition_rect(s_widgets.header,
                               s_page_transition_start_geometry.header,
                               s_page_transition_target_geometry.header,
                               progress);
    const PageTransitionRect body_current = lerp_page_transition_rect(
        s_page_transition_start_geometry.body,
        s_page_transition_target_geometry.body, progress);
    panel_geometry(s_widgets.body, body_current.x, body_current.y,
                   body_current.width, body_current.height);
    apply_page_transition_rect(s_widgets.scroll_clip,
                               s_page_transition_start_geometry.scroll_clip,
                               s_page_transition_target_geometry.scroll_clip,
                               progress);
    const PageTransitionRect footer_current = lerp_page_transition_rect(
        s_page_transition_start_geometry.footer,
        s_page_transition_target_geometry.footer, progress);
    panel_geometry(s_widgets.footer, footer_current.x, footer_current.y,
                   footer_current.width, footer_current.height);
    const PageTransitionRect page_title_current = lerp_page_transition_rect(
        s_page_transition_start_geometry.page_title,
        s_page_transition_target_geometry.page_title, progress);
    label_geometry(s_widgets.page_title, page_title_current.x,
                   page_title_current.y, page_title_current.width,
                   page_title_current.height);
    for (int i = 0; i < kFrameGradientSlices; ++i) {
      const PageTransitionRect header_slice = lerp_page_transition_rect(
          s_page_transition_start_geometry.theme_header_slice[i],
          s_page_transition_target_geometry.theme_header_slice[i], progress);
      const PageTransitionRect footer_slice = lerp_page_transition_rect(
          s_page_transition_start_geometry.theme_footer_slice[i],
          s_page_transition_target_geometry.theme_footer_slice[i], progress);
      // Apparition's dedicated slices are the only visible separators. Their
      // X/Width are fixed for a page transition; updating only Y/Height avoids
      // hundreds of redundant managed writes and keeps input responsive.
      apply_page_transition_vertical(
          s_widgets.theme_header_slices[i],
          body_current.y - header_slice.height, header_slice.height);
      apply_page_transition_vertical(
          s_widgets.theme_footer_slices[i],
          footer_current.y - footer_slice.height, footer_slice.height);
    }
    return;
  }

  // Safe fallback if a firmware does not expose the geometry properties.
  // Keep the old fixed-origin height interpolation rather than disabling the
  // page transition entirely.
  const float width = std::max(1.0f, s_menu_target_width);
  const float height = std::max(1.0f, s_menu_target_height);
  const float start_height = std::max(1.0f, s_page_transition_start_height);
  const float visible_height = start_height +
      (height - start_height) * progress;
  panel_geometry(s_widgets.master, s_menu_target_x, s_menu_target_y, width,
                 visible_height);
  panel_geometry(s_widgets.content, 0.0f, 0.0f, width, height);
}

void update_animations() {
  if (!s_widgets.master)
    return;
  const uint64_t now = monotonic_millis();
  const bool apparition_open_close =
      active_menu_theme() == 5 &&
      (s_animation == AnimationState::Opening ||
       s_animation == AnimationState::Closing);
  float menu_reveal = 1.0f;
  if (s_animation == AnimationState::Opening) {
    const float duration = active_menu_theme() == 5
                               ? kApparitionOpenDurationMs
                               : kOpenDurationMs;
    const float t = std::min(1.0f, (now - s_animation_started_ms) /
                                      duration);
    menu_reveal = Easing::Apply(active_menu_animation(), t);
    if (t >= 1.0f)
      s_animation = AnimationState::Visible;
  } else if (s_animation == AnimationState::Closing) {
    const float duration = active_menu_theme() == 5
                               ? kApparitionCloseDurationMs
                               : kCloseDurationMs;
    const float t = std::min(1.0f, (now - s_animation_started_ms) /
                                      duration);
    menu_reveal = 1.0f - Easing::Apply(active_menu_animation(), t);
    if (t >= 1.0f) {
      s_animation = AnimationState::Hidden;
      menu_reveal = 0.0f;
    }
  } else if (s_animation == AnimationState::Hidden) {
    menu_reveal = 0.0f;
  }
  menu_reveal = std::clamp(menu_reveal, 0.0f, 1.0f);
  const bool page_transition_was_active = page_transition_active();
  const float page_reveal = update_page_transition(now);
  if (apparition_open_close) {
    update_apparition_open_close_animation(menu_reveal);
  } else {
  // Non-Apparition themes keep a fixed geometry. The old centre-growing clip
  // changed the master width/height during every open and page repaint, which
  // made the menu overlay visibly jump and scale. Apparition owns its own
  // dedicated reveal animation above; the other themes only fade.
  panel_geometry(s_widgets.master, s_menu_target_x, s_menu_target_y,
                 s_menu_target_width, s_menu_target_height);
  panel_geometry(s_widgets.content, 0.0f, 0.0f,
                 s_menu_target_width, s_menu_target_height);
  const float opacity = menu_reveal;
  Set_Property(s_panel_class, s_widgets.master, "Opacity",
               opacity);
  if (s_widgets.anomaly_controls)
    Set_Property(s_panel_class, s_widgets.anomaly_controls, "Opacity",
                 opacity);
  }

  if (page_transition_was_active || page_transition_active() ||
      page_reveal < 1.0f)
    apply_page_transition_reveal(page_reveal);

  if (s_highlight_started_ms && s_widgets.highlight) {
    bool arrived = false;
    const float elapsed_ms = static_cast<float>(now - s_highlight_started_ms);
    if (s_highlight_follow) {
      /* A little longer than the repeat period: the next step always arrives
         while the cursor is still moving, so it never stops between rows. */
      const float t = std::min(
          1.0f, elapsed_ms / (static_cast<float>(Input::kListRepeatMs) * 1.2f));
      s_highlight_y = s_highlight_from +
                      (s_highlight_to - s_highlight_from) * t;
      arrived = t >= 1.0f;
    } else {
      const float t = std::min(
          1.0f, elapsed_ms /
                    (active_menu_theme() == 5 ? 150.0f : kHighlightDurationMs));
      /* Apparition uses a fixed ease-out; the other skins keep the easing the
         user picked on the Animations page. */
      const float eased = active_menu_theme() == 5
                              ? 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t)
                              : Easing::Apply(active_menu_animation(), t);
      s_highlight_y = s_highlight_from +
                      (s_highlight_to - s_highlight_from) * eased;
      arrived = t >= 1.0f;
    }
    if (arrived) {
      s_highlight_y = s_highlight_to;
      s_highlight_started_ms = 0;
      s_highlight_follow = false;
    }
    AniketosUI::Pui::PanelY(s_pui, s_widgets.highlight, s_highlight_y);
    if (Profile::g_enabled)
      LOG_INFO("[SHELLMENU:PROFILE] cursor t=%llu y=%.1f to=%.1f hold=%d",
               static_cast<unsigned long long>(now), s_highlight_y,
               s_highlight_to, s_highlight_follow ? 1 : 0);
    /* The title/value accent follows the cursor; repaint colours while it
       travels and once more when it lands. */
    if (active_menu_theme() == 5)
      s_color_dirty = true;
  }
}
