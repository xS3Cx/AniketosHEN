/*
 * Anomaly visual theme for AniketosShell.
 *
 * The look of Tazhys' Anomaly PS3 VSH menu (itself built on Akari), from its
 * SPRX: everything in a 20-unit row of the PS3's 1280x720 space shown at 1.5x
 * (a 30 px row, 24 px text), a 400-unit (600 px) body with 5 units (7.5 px)
 * of inset, a 2-unit (3 px) purple frame round a 1.1-row header holding the
 * page's name, the credit line just above the frame and the controls line
 * just below it; opaque black inside, a solid purple bar on the selected
 * row. Opening slides it down from above the screen in a second, with the
 * slight overshoot of Out (Back); closing slides it back up.
 */

/* The legend line sits this far (4 units) from the frame, as the credits do. */
constexpr float kAnomalyLineGap = 6.0f;
/* where the controls line is when the menu is in place */
float s_anomaly_controls_y = 0.0f;

/* A font of its own size for the credit/legend line and the page's name,
   made once per size. */
MonoObject *anomaly_font(int size, int which) {
  static MonoObject *fonts[2] = {};
  static uint32_t handles[2] = {};
  static int sizes[2] = {};
  if (fonts[which] && sizes[which] == size)
    return fonts[which];
  if (handles[which] && mono_gchandle_free)
    mono_gchandle_free(handles[which]);
  handles[which] = 0;
  fonts[which] = create_pinned_font(size, 400, handles[which]);
  sizes[which] = fonts[which] ? size : 0;
  return fonts[which];
}

void update_anomaly_controls(float menu_x, float controls_y, float menu_width,
                             float height, int font_size) {
  update_controls();
  if (!IsVisible()) {
    hide_anomaly_extras();
    return;
  }
  s_anomaly_controls_y = controls_y;

  /* "[Up/Down] - Scroll | [Left/Right] - Increment | [X] - Select |
     [O] - Exit", centred under the frame */
  const bool show_controls = show_menu_controls();
  MonoObject *font = anomaly_font(font_size, 0);
  const float icon_height = std::round(height * 0.72f);
  const float icon_y = std::round((height - icon_height) * 0.5f);
  const float icon_width = std::round(icon_height * 1.25f);
  constexpr float kGap = 2.0f;
  char texts[4][96];
  if (s_moving) {
    std::snprintf(texts[0], sizeof(texts[0]), " - %s | ",
                  tr("vsh.ctrl.move_position"));
    std::snprintf(texts[1], sizeof(texts[1]), " - %s | ",
                  tr("vsh.ctrl.finish_move"));
    std::snprintf(texts[2], sizeof(texts[2]), " - %s",
                  tr("vsh.ctrl.cancel_move"));
    texts[3][0] = '\0';
  } else {
    std::snprintf(texts[0], sizeof(texts[0]), " - %s | ",
                  tr("vsh.anomaly.scroll"));
    std::snprintf(texts[1], sizeof(texts[1]), " - %s | ",
                  tr("vsh.anomaly.increment"));
    std::snprintf(texts[2], sizeof(texts[2]), " - %s | ",
                  tr("vsh.anomaly.select"));
    std::snprintf(texts[3], sizeof(texts[3]), " - %s",
                  tr("vsh.anomaly.exit"));
  }
  const AnomalyControlIcon icons_open[4] = {
      AnomalyControlDpad, AnomalyControlDpad2, AnomalyControlCross,
      AnomalyControlCircle};
  const AnomalyControlIcon icons_moving[3] = {
      AnomalyControlL3, AnomalyControlCross, AnomalyControlCircle};
  const int groups = s_moving ? 3 : 4;
  auto measured = [&](const char *text) {
    return std::ceil(approximate_text_width(text, static_cast<float>(font_size))) +
           2.0f;
  };
  float content_width = 0.0f;
  for (int i = 0; i < groups; ++i)
    content_width += icon_width + kGap + measured(texts[i]);

  const float controls_width = std::min(
      s_viewport_width, std::max(menu_width, content_width));
  const float controls_x = std::clamp(
      menu_x + (menu_width - controls_width) * 0.5f, 0.0f,
      std::max(0.0f, s_viewport_width - controls_width));
  float x = std::max(0.0f, (controls_width - content_width) * 0.5f);
  bool icon_used[kAnomalyControlIconCount]{};
  int next_label = 0;
  if (show_controls) {
    for (int i = 0; i < groups; ++i) {
      const AnomalyControlIcon which = s_moving ? icons_moving[i] : icons_open[i];
      image_geometry(s_widgets.anomaly_control_icons[which], x, icon_y,
                     icon_width, icon_height);
      icon_used[which] = true;
      x += icon_width + kGap;
      MonoObject *label = s_widgets.anomaly_control_labels[next_label++];
      const float width = measured(texts[i]);
      label_text(label, texts[i]);
      label_geometry(label, x, 0.0f, width, height);
      Set_Property(cls(label), label, "VerticalAlignment", 2);
      if (font)
        Set_Property_Invoke(cls(label), label, "Font", font);
      x += width;
    }
  }

  panel_geometry(s_widgets.anomaly_controls, controls_x, controls_y,
                 show_controls ? controls_width : 0.0f,
                 show_controls ? height : 0.0f);
  panel_hide(s_widgets.anomaly_controls);
  for (int i = 0; i < kAnomalyControlIconCount; ++i) {
    if (!show_controls || !icon_used[i])
      image_geometry(s_widgets.anomaly_control_icons[i], 0, 0, 0, 0, false);
  }
  for (int i = show_controls ? next_label : 0;
       i < kAnomalyControlLabelCount; ++i) {
    label_text(s_widgets.anomaly_control_labels[i], "");
    label_geometry(s_widgets.anomaly_control_labels[i], 0, 0, 0, 0);
  }
}

void update_anomaly_menu_widgets() {
  PageState &state = s_stack.back();
  std::vector<Option> options_scratch;
  const std::vector<Option> &options =
      page_options(state.page, options_scratch);
  ensure_selection(state, options);
  hide_theme_accent_slices();

  /* The settings' defaults (text 18, width 450, border 4) come out as
     Anomaly's 20-unit text, 400-unit width and 2-unit border. */
  const float row = std::round(menu_text_size() * 5.0f / 3.0f);
  const int font = static_cast<int>(std::lround(row * 0.8f));
  const float width = std::round(menu_width() * 4.0f / 3.0f);
  const float border = active_menu_style().border_enabled
                           ? std::max(1.0f, std::round(menu_border() * 0.75f))
                           : 0.0f;
  const float outer_width = width + border * 2.0f;
  const float inset = row * 0.25f;
  const float line_height = std::round(row * 1.1f);
  const int line_font = static_cast<int>(std::lround(line_height * 0.8f));
  const int title_font = static_cast<int>(std::lround(row * 1.2f * 0.8f));
  const float opacity = anomaly_own_palette()
                            ? 1.0f
                            : active_menu_style().background_opacity;
  const Color black{0.0f, 0.0f, 0.0f, opacity};
  const Color white{1.0f, 1.0f, 1.0f, 1.0f};

  const size_t visible = std::min(kMaxVisibleOptions, options.size());
  const float rows_height = std::max(row, row * static_cast<float>(visible));
  const float body_height = rows_height + inset * 2.0f;
  /* the credit line, the frame's top line, the header, the body */
  const float frame_y = line_height + kAnomalyLineGap;
  const float header_y = frame_y + border;
  const float body_y = header_y + line_height;
  const float rows_y = body_y + inset;
  const float frame_bottom = body_y + body_height + border;
  const float controls_offset = frame_bottom + kAnomalyLineGap;
  const float layout_height = controls_offset + line_height;
  const float x = std::clamp(active_menu_x() - outer_width * 0.5f, 0.0f,
                             std::max(0.0f, s_viewport_width - outer_width));
  const float y = std::clamp(active_menu_y() - (body_y + body_height * 0.5f),
                             0.0f,
                             std::max(0.0f, s_viewport_height - layout_height));

  set_master_geometry(x, y, outer_width, frame_bottom);

  panel_geometry(s_widgets.header, border, header_y, width, line_height);
  panel_geometry(s_widgets.body, border, body_y, width, body_height);
  panel_geometry(s_widgets.footer, 0.0f, 0.0f, 0.0f, 0.0f);
  panel_color(s_widgets.header, black);
  panel_color(s_widgets.body, black);
  panel_hide(s_widgets.footer);

  /* the frame: a top line over the header, a bottom line under the body and
     sides along both, with no line between header and body */
  for (int line = 0; line < kBorderLines; ++line) {
    for (int segment = 0; segment < kFrameGradientSlices; ++segment) {
      MonoObject *part = s_widgets.horizontal_borders[line][segment];
      if (line >= 2 || border <= 0.0f) {
        panel_geometry(part, 0, 0, 0, 0);
        panel_hide(part);
        continue;
      }
      const float line_y = line == 0 ? frame_y : body_y + body_height;
      const float x0 = outer_width * static_cast<float>(segment) /
                       kFrameGradientSlices;
      const float x1 = outer_width * static_cast<float>(segment + 1) /
                       kFrameGradientSlices;
      const float t = (static_cast<float>(segment) + 0.5f) /
                      kFrameGradientSlices;
      panel_geometry(part, x0, line_y, x1 - x0, border);
      panel_color(part, menu_dual_color()
                            ? mix(left_color(), right_color(), gradient_t(t), 1.0f)
                            : right_color());
    }
  }
  panel_geometry(s_widgets.left_borders[0], 0, header_y, border,
                 line_height + body_height);
  panel_geometry(s_widgets.right_borders[0], width + border, header_y, border,
                 line_height + body_height);
  panel_color(s_widgets.left_borders[0],
              gradient_edge_color(/*right_edge=*/false));
  panel_color(s_widgets.right_borders[0],
              gradient_edge_color(/*right_edge=*/true));
  panel_geometry(s_widgets.left_borders[1], 0, 0, 0, 0);
  panel_geometry(s_widgets.right_borders[1], 0, 0, 0, 0);
  panel_hide(s_widgets.left_borders[1]);
  panel_hide(s_widgets.right_borders[1]);

  replace_font(font);
  label_font(s_widgets.version);
  label_font(s_widgets.counter);
  for (size_t i = 0; i < kMaxVisibleOptions; ++i) {
    label_font(s_widgets.rows[i]);
    label_font(s_widgets.values[i]);
    label_font(s_widgets.selector_left[i]);
    label_font(s_widgets.selector_right[i]);
  }

  /* above the frame: who made the look; in the header: the page's name */
  Set_Property(s_label_class, s_widgets.title, "HorizontalAlignment", 1);
  Set_Property(s_label_class, s_widgets.page_title, "HorizontalAlignment", 1);
  if (MonoObject *credit_font = anomaly_font(line_font, 0))
    Set_Property_Invoke(s_label_class, s_widgets.title, "Font", credit_font);
  if (MonoObject *header_font = anomaly_font(title_font, 1))
    Set_Property_Invoke(s_label_class, s_widgets.page_title, "Font",
                        header_font);
  set_color(s_label_class, s_widgets.title, "TextColor", white);
  set_color(s_label_class, s_widgets.page_title, "TextColor", white);
  Set_Property(s_label_class, s_widgets.title, "Opacity", 1.0f);
  Set_Property(s_label_class, s_widgets.page_title, "Opacity", 1.0f);
  label_text(s_widgets.title, "Anomaly - Developed by AlexZero");
  label_geometry(s_widgets.title, 0.0f, 0.0f, outer_width, line_height);
  label_text(s_widgets.page_title, page_title(state.page));
  label_geometry(s_widgets.page_title, border, header_y, width, line_height);
  label_text(s_widgets.version, "");
  label_geometry(s_widgets.version, 0, 0, 0, 0);
  label_text(s_widgets.counter, "");
  label_geometry(s_widgets.counter, 0, 0, 0, 0);
  image_geometry(s_widgets.position_edit_icon, 0, 0, 0, 0, false);

  /* the selected row: a solid bar */
  const float target_highlight =
      rows_y + static_cast<float>(state.selected - state.scroll) * row;
  update_highlight_target(state, target_highlight);
  panel_geometry(s_widgets.highlight, border, s_highlight_y, width, row);
  for (int i = 0; i < kGradientSlices; ++i) {
    const float x0 = width * static_cast<float>(i) / kGradientSlices;
    const float x1 = width * static_cast<float>(i + 1) / kGradientSlices;
    const float t = (static_cast<float>(i) + 0.5f) / kGradientSlices;
    panel_geometry(s_widgets.highlight_slices[i], x0, 0, x1 - x0, row);
    panel_color(s_widgets.highlight_slices[i],
                menu_dual_color()
                    ? mix(left_color(), right_color(), gradient_t(t), 1.0f)
                    : right_color(1.0f));
  }

  const float base_value_width = width * 0.42f;
  const float text_x = border + inset;
  const float content_right = border + width - inset;
  for (size_t visible_index = 0; visible_index < kMaxVisibleOptions;
       ++visible_index) {
    const size_t option_index = state.scroll + visible_index;
    const bool exists = option_index < options.size();
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
    Set_Property(s_label_class, s_widgets.rows[visible_index], "Opacity", 1.0f);
    Set_Property(s_label_class, s_widgets.values[visible_index], "Opacity", 1.0f);
    label_angle(s_widgets.rows[visible_index], 0.0f);
    label_angle(s_widgets.values[visible_index], 0.0f);
    const float row_y = rows_y + visible_index * row;
    const bool selected = option_index == state.selected;
    std::string display_value;
    if (option.type == OptionType::Submenu)
      display_value = ">";
    else if (option.type == OptionType::SliderInt ||
             option.type == OptionType::SliderFloat ||
             option.type == OptionType::Value)
      display_value = option.value;
    const bool selector = option.type == OptionType::Strings ||
                          option.type == OptionType::ThemeChoice;
    if (selector) {
      /* the plain choice; "< choice >" on the row the cursor is on, without
         the arrow that has nothing beyond it */
      const bool first = option.string_index <= 0;
      const bool last = option.string_index + 1 >= option.string_count;
      display_value = !selected
                          ? option.value
                          : std::string(first ? "  " : "< ") + option.value +
                                (last ? "  " : " >");
    }

    Set_Property(s_label_class, s_widgets.rows[visible_index],
                 "HorizontalAlignment", 0);
    Set_Property(s_label_class, s_widgets.values[visible_index],
                 "HorizontalAlignment", 2);
    set_color(s_label_class, s_widgets.rows[visible_index], "TextColor",
              white);
    set_color(s_label_class, s_widgets.values[visible_index], "TextColor",
              white);
    const AppendumControlLayout control =
        update_appendum_option_control(visible_index, option, selected, row_y,
                                       row, border + width);
    clear_selector_markers(visible_index);
    label_text(s_widgets.rows[visible_index], option.title);
    const float title_right = control.toggle || control.slider ||
                                      control.color
                                  ? control.title_right
                              : option.type == OptionType::Action
                                  ? content_right
                                  : content_right - value_width - inset;
    label_geometry(s_widgets.rows[visible_index], text_x, row_y,
                   std::max(0.0f, title_right - text_x), row);
    label_text(s_widgets.values[visible_index], display_value);
    if (control.slider)
      value_label_geometry(s_widgets.values[visible_index], control.value_x,
                           row_y, control.value_width, row, false);
    else
      value_label_geometry(s_widgets.values[visible_index],
                           content_right - value_width, row_y, value_width,
                           row, false);
  }

  update_anomaly_controls(x, y + controls_offset, outer_width, line_height,
                          line_font);
  s_dirty = false;
}

/* Opening: down from above the screen in a second with Out (Back)'s
   overshoot, the menu fading in over its first 300 ms; closing: back up.
   Returns false when another look owns the animation. */
constexpr float kAnomalySlideMs = 1000.0f;

void update_anomaly_open_close(float elapsed_ms, bool opening) {
  const float t = std::clamp(elapsed_ms / kAnomalySlideMs, 0.0f, 1.0f);
  const float eased = Easing::Apply(23 /* Out (Back) */, t);
  const float travel = s_menu_target_y + s_menu_target_height + 80.0f;
  const float offset = opening ? -travel * (1.0f - eased) : -travel * eased;
  const float opacity =
      opening ? std::clamp(elapsed_ms / 300.0f, 0.0f, 1.0f)
              : 1.0f - std::clamp((elapsed_ms - 200.0f) / 300.0f, 0.0f, 1.0f);
  panel_geometry(s_widgets.master, s_menu_target_x, s_menu_target_y + offset,
                 s_menu_target_width, s_menu_target_height);
  panel_geometry(s_widgets.content, 0.0f, 0.0f, s_menu_target_width,
                 s_menu_target_height);
  Set_Property(s_panel_class, s_widgets.master, "Opacity", opacity);
  if (s_widgets.anomaly_controls) {
    AniketosUI::Pui::PanelY(s_pui, s_widgets.anomaly_controls,
                            s_anomaly_controls_y + offset);
    Set_Property(s_panel_class, s_widgets.anomaly_controls, "Opacity",
                 opacity);
  }
}
