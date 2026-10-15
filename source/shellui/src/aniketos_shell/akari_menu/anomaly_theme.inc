/*
 * Anomaly visual theme for AniketosShell.
 *
 * This is a presentation-only reconstruction from the user-supplied SPRX.
 * The palette and composition follow the reference, while sizing follows the
 * same user-configurable width, row and border scale as Akari.
 */

void update_anomaly_controls(float menu_x, float controls_y,
                             float menu_width, float row) {
  update_controls();
  if (!IsVisible()) {
    hide_anomaly_extras();
    return;
  }

  const bool show_controls = show_menu_controls();
  const float height = std::max(24.0f, row);
  const float icon_height = std::min(20.0f, height * 0.72f);
  const float icon_y = std::round((height - icon_height) * 0.5f);
  constexpr float kGap = 3.0f;
  constexpr float kGroupGap = 6.0f;
  constexpr float kFontSize = 16.0f;
  const char *scroll_adjust =
      s_moving ? tr("vsh.ctrl.move_position") : tr("vsh.ctrl.scroll_adjust");
  const char *select =
      s_moving ? tr("vsh.ctrl.finish_move") : tr("vsh.ctrl.select");
  const char *back_exit =
      s_moving ? tr("vsh.ctrl.cancel_move") : tr("vsh.ctrl.back_exit");

  float content_width = 0.0f;
  auto measured = [&](const char *text) {
    return std::ceil(approximate_text_width(text, kFontSize)) + 2.0f;
  };
  content_width +=
      (s_moving ? 24.0f : 31.0f) + kGap + measured(scroll_adjust) + kGroupGap;
  content_width += 24.0f + kGap + measured(select) + kGroupGap;
  content_width += 24.0f + kGap + measured(back_exit);

  // Some translations are wider than the menu itself. Let the instruction
  // row grow symmetrically (as in the supplied reference) and clamp the
  // complete row to the viewport instead of clipping its last action.
  const float controls_width = std::min(
      s_viewport_width, std::max(menu_width, content_width));
  const float controls_x = std::clamp(
      menu_x + (menu_width - controls_width) * 0.5f, 0.0f,
      std::max(0.0f, s_viewport_width - controls_width));
  float x = std::max(0.0f, (controls_width - content_width) * 0.5f);
  int next_label = 0;
  bool icon_used[kAnomalyControlIconCount]{};
  auto icon = [&](AnomalyControlIcon which, float width) {
    image_geometry(s_widgets.anomaly_control_icons[which], x, icon_y, width,
                   icon_height);
    icon_used[which] = true;
    x += width + kGap;
  };
  auto text_item = [&](const char *text) {
    MonoObject *label = s_widgets.anomaly_control_labels[next_label++];
    const float width = measured(text);
    label_text(label, text);
    label_geometry(label, x, 0.0f, width, height);
    Set_Property(cls(label), label, "VerticalAlignment", 2);
    if (s_control_font)
      Set_Property_Invoke(cls(label), label, "Font", s_control_font);
    x += width;
  };
  auto next_group = [&]() { x += kGroupGap; };

  if (show_controls) {
    icon(s_moving ? AnomalyControlL3 : AnomalyControlDpad,
         s_moving ? 24.0f : 31.0f);
    text_item(scroll_adjust);
    next_group();
    icon(AnomalyControlCross, 24.0f);
    text_item(select);
    next_group();
    icon(AnomalyControlCircle, 24.0f);
    text_item(back_exit);
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

  const float width = menu_width();
  const float row = menu_text_size();
  const float border = menu_border();
  const float outer_width = width + border * 2.0f;
  const float text_inset = row * 0.35f;
  const float section_gap = border * 2.0f;
  const float controls_height = std::max(24.0f, row);
  const Color black{0.0f, 0.0f, 0.0f,
                    active_menu_style().background_opacity};
  const Color white{1.0f, 1.0f, 1.0f, 1.0f};

  const size_t visible = std::min(kMaxVisibleOptions, options.size());
  const float body_height = std::max(row, row * static_cast<float>(visible));
  const float branding_y = std::round(row * 0.12f);
  // The branding row is the only header. The previous extra header plane plus
  // section gap left an empty strip above the first option/switch.
  const float frame_y = row + section_gap;
  const float body_y = frame_y + border;
  const float frame_bottom = body_y + body_height;
  const float total_height = frame_bottom + border;
  const float controls_y_offset =
      total_height + section_gap + std::round(row * 0.12f);
  const float layout_height = controls_y_offset + controls_height;
  const float x = std::clamp(active_menu_x() - outer_width * 0.5f,
                             0.0f,
                             std::max(0.0f, s_viewport_width - outer_width));
  const float y = std::clamp(active_menu_y() -
                                 (body_y + body_height * 0.5f),
                             0.0f,
                             std::max(0.0f, s_viewport_height - layout_height));

  set_master_geometry(x, y, outer_width, total_height);

  panel_geometry(s_widgets.header, 0.0f, 0.0f, 0.0f, 0.0f);
  panel_hide(s_widgets.header);
  panel_geometry(s_widgets.body, 0.0f, body_y, outer_width, body_height);
  panel_geometry(s_widgets.footer, 0.0f, frame_bottom, outer_width, border);
  panel_color(s_widgets.header, black);
  panel_color(s_widgets.body, black);
  panel_color(s_widgets.footer, black);

  // One pixel-snapped outer frame. Horizontal and vertical segments use the
  // same `border` value and meet without overlap, so no edge becomes darker
  // or visually thicker at the corners.
  for (int line = 0; line < kBorderLines; ++line) {
    for (int segment = 0; segment < kFrameGradientSlices; ++segment) {
      MonoObject *part = s_widgets.horizontal_borders[line][segment];
      if (line >= 2 || border <= 0.0f) {
        panel_geometry(part, 0, 0, 0, 0);
        panel_hide(part);
        continue;
      }
      const float line_y = line == 0 ? frame_y : frame_bottom;
      const float x0 = outer_width * static_cast<float>(segment) /
                       kFrameGradientSlices;
      const float x1 = outer_width * static_cast<float>(segment + 1) /
                       kFrameGradientSlices;
      const float t = (static_cast<float>(segment) + 0.5f) /
                      kFrameGradientSlices;
      panel_geometry(part, x0, line_y, x1 - x0, border);
      panel_color(part, active_menu_style().dual_color
                            ? mix(left_color(), right_color(), gradient_t(t), 1.0f)
                            : left_color());
    }
  }
  const float frame_inner_y = frame_y + border;
  const float frame_inner_height = frame_bottom - frame_inner_y;
  panel_geometry(s_widgets.left_borders[0], 0, frame_inner_y, border,
                 frame_inner_height);
  panel_geometry(s_widgets.right_borders[0], width + border, frame_inner_y,
                 border, frame_inner_height);
  panel_color(s_widgets.left_borders[0],
              gradient_edge_color(/*right_edge=*/false));
  panel_color(s_widgets.right_borders[0],
              gradient_edge_color(/*right_edge=*/true));
  panel_geometry(s_widgets.left_borders[1], 0, 0, 0, 0);
  panel_geometry(s_widgets.right_borders[1], 0, 0, 0, 0);
  panel_hide(s_widgets.left_borders[1]);
  panel_hide(s_widgets.right_borders[1]);

  replace_font(static_cast<int>(row));
  label_font(s_widgets.title);
  label_font(s_widgets.version);
  label_font(s_widgets.page_title);
  label_font(s_widgets.counter);
  for (size_t i = 0; i < kMaxVisibleOptions; ++i) {
    label_font(s_widgets.rows[i]);
    label_font(s_widgets.values[i]);
    label_font(s_widgets.selector_left[i]);
    label_font(s_widgets.selector_right[i]);
  }

  Set_Property(s_label_class, s_widgets.title, "HorizontalAlignment", 1);
  Set_Property(s_label_class, s_widgets.page_title, "HorizontalAlignment", 1);
  if (s_control_font)
    Set_Property_Invoke(s_label_class, s_widgets.title, "Font",
                        s_control_font);
  set_color(s_label_class, s_widgets.title, "TextColor", white);
  set_color(s_label_class, s_widgets.page_title, "TextColor", white);
  Set_Property(s_label_class, s_widgets.title, "Opacity", 1.0f);
  Set_Property(s_label_class, s_widgets.page_title, "Opacity", 0.0f);
  label_text(s_widgets.title, kMenuBrandName);
  label_geometry(s_widgets.title, 0, branding_y, outer_width, row);
  label_text(s_widgets.page_title, "");
  label_geometry(s_widgets.page_title, 0.0f, 0.0f, 0.0f, 0.0f);
  label_text(s_widgets.version, "");
  label_geometry(s_widgets.version, 0, 0, 0, 0);
  label_text(s_widgets.counter, "");
  label_geometry(s_widgets.counter, 0, 0, 0, 0);
  image_geometry(s_widgets.position_edit_icon, 0, 0, 0, 0, false);

  const float target_highlight =
      body_y + static_cast<float>(state.selected - state.scroll) * row;
  update_highlight_target(state, target_highlight);
  panel_geometry(s_widgets.highlight, border, s_highlight_y, width, row);
  for (int i = 0; i < kGradientSlices; ++i) {
    const float x0 = width * static_cast<float>(i) / kGradientSlices;
    const float x1 = width * static_cast<float>(i + 1) / kGradientSlices;
    const float t = (static_cast<float>(i) + 0.5f) / kGradientSlices;
    panel_geometry(s_widgets.highlight_slices[i], x0, 0, x1 - x0, row);
    panel_color(s_widgets.highlight_slices[i],
                active_menu_style().dual_color
                    ? mix(left_color(), right_color(), gradient_t(t), 0.88f)
                    : left_color(0.88f));
  }

  const float base_value_width = width * 0.42f;
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
    const float row_y = body_y + visible_index * row;
    std::string display_value;
    if (option.type == OptionType::Submenu)
      display_value = ">";
    else if (option.type == OptionType::SliderInt ||
             option.type == OptionType::SliderFloat)
      display_value = option.value;
    else if (option.type == OptionType::Strings ||
             option.type == OptionType::ThemeChoice)
      display_value = option.value;
    else if (option.type == OptionType::Value ||
             option.type == OptionType::ThemeChoice)
      display_value = option.value;
    const bool selector = option.type == OptionType::Strings ||
                          option.type == OptionType::ThemeChoice;
    if (selector)
      display_value = selector_display_value(option);

    Set_Property(s_label_class, s_widgets.rows[visible_index],
                 "HorizontalAlignment", 0);
    Set_Property(s_label_class, s_widgets.values[visible_index],
                 "HorizontalAlignment", 2);
    set_color(s_label_class, s_widgets.rows[visible_index], "TextColor",
              white);
    set_color(s_label_class, s_widgets.values[visible_index], "TextColor",
              white);
    const bool selected = option_index == state.selected;
    const float control_right = border + width;
    const AppendumControlLayout control =
        update_appendum_option_control(visible_index, option, selected, row_y,
                                       row, control_right);
    const SelectorMarkerLayout selector_layout = update_selector_markers(
        visible_index, selector, option.string_index, option.string_count,
        border + width - value_width - text_inset, row_y, value_width, row,
        selector_painted_text_width(display_value.c_str(), row),
        selected ? black : white,
        selected ? Color{0.30f, 0.30f, 0.30f, 0.60f}
                 : Color{0.35f, 0.35f, 0.35f, 0.65f});
    label_text(s_widgets.rows[visible_index], option.title);
    const float text_x = border + text_inset;
    const float title_right = control.toggle || control.slider ||
                                      control.color
                                  ? control.title_right
                                  : border + width - value_width - text_inset;
    label_geometry(s_widgets.rows[visible_index], text_x, row_y,
                   std::max(0.0f, title_right - text_x), row);
    if (option.id == OptionId::MenuMove) {
      const float glyph = std::min(24.0f, row * 0.80f);
      const float title_end = border + text_inset +
                              approximate_text_width(option.title.c_str(), row);
      const float glyph_x = std::min(
          title_end + 5.0f,
          border + width - value_width - text_inset - glyph);
      image_geometry(s_widgets.position_edit_icon, glyph_x,
                     row_y + (row - glyph) * 0.5f + 2.0f, glyph, glyph);
    }
    label_text(s_widgets.values[visible_index], display_value);
    if (control.slider) {
      value_label_geometry(s_widgets.values[visible_index], control.value_x,
                           row_y, control.value_width, row,
                           false);
    } else if (selector_layout.visible) {
      selector_value_label_geometry(s_widgets.values[visible_index],
                                    selector_layout.value_x, row_y,
                                    selector_layout.value_width, row);
    } else {
      value_label_geometry(s_widgets.values[visible_index],
                           border + width - value_width - text_inset, row_y,
                           value_width, row,
                           false);
    }
  }

  // Keep the control legend directly below the menu, matching the reference
  // composition and using the same native UI3 KeyGuide glyphs as the global
  // press-to-open hint.
  update_anomaly_controls(x, y + controls_y_offset, outer_width, row);
  s_dirty = false;
}
