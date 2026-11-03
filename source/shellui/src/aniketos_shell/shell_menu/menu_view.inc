/* Slice `i` of the Akari cursor, faded with the text while opening and
   closing. */
void color_akari_highlight_slice(int i, float text_opacity) {
  const float t = gradient_slice(1.0f, i, kGradientSlices).t;
  panel_color(s_widgets.highlight_slices[i],
              menu_dual_color()
                  ? mix(left_color(), right_color(), gradient_t(t),
                        0.4f * text_opacity)
                  : right_color(0.4f * text_opacity));
}

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
  s_global_color_sample_ms = MonotonicMs();
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
  const std::vector<Option> &options =
      page_options(state.page, s_painted_options);
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
  /* opening and closing grow and shrink the body about its middle, the
     header and footer travelling with its edges */
  const float body_height =
      std::round(layout.body_height * std::clamp(s_akari_body_reveal, 0.0f, 1.0f));
  const float shift = std::round((layout.body_height - body_height) * 0.5f);
  const float header_y = layout.header_y + shift;
  const float header_bottom = layout.header_bottom + shift;
  const float body_y = layout.body_y + shift;
  const float body_bottom = body_y + body_height;
  const float footer_y = layout.footer_y - shift;
  /* the rows start this far into the body (Akari's 10-unit margin) */
  const float rows_y = body_y + layout.body_padding;
  const float text_opacity = std::clamp(s_akari_text_reveal, 0.0f, 1.0f);
  set_master_geometry(layout.x, layout.y, outer_width, layout.total_height);
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

  apply_font(layout.font);
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
  for (MonoObject *label :
       {s_widgets.title, s_widgets.page_title, s_widgets.counter})
    Set_Property(s_label_class, label, "Opacity", text_opacity);
  /* the footer: the page's name on the left, the position on the right,
     2.5 units (3.75 px) in from the frame */
  const float footer_inset = row * 0.125f;
  set_color(s_label_class, s_widgets.page_title, "TextColor",
            Color{1.0f, 1.0f, 1.0f, 1.0f});
  label_text(s_widgets.page_title, page_title(state.page));
  label_geometry(s_widgets.page_title, border + footer_inset, footer_y,
                 width * 0.7f - footer_inset, row);
  char counter[32];
  std::snprintf(counter, sizeof(counter), "[%zu/%zu]",
                options.empty() ? 0 : state.selected + 1, options.size());
  label_text(s_widgets.counter, counter);
  label_geometry(s_widgets.counter, border + width * 0.7f, footer_y,
                 width * 0.3f - footer_inset, row);

  const float target_highlight =
      rows_y + static_cast<float>(state.selected - state.scroll) * row;
  update_highlight_target(state, target_highlight);
  panel_geometry(s_widgets.highlight, border, s_highlight_y, width, row);
  for (int i = 0; i < kGradientSlices; ++i) {
    const GradientSlice slice = gradient_slice(width, i, kGradientSlices);
    panel_geometry(s_widgets.highlight_slices[i], slice.x, 0, slice.width, row);
    color_akari_highlight_slice(i, text_opacity);
  }

  const float text_x = border + layout.padding;
  const float content_right = border + width - layout.padding;
  const float control_right = border + width;
  const float base_value_width = width * 0.43f;
  const float column_gap = std::max(8.0f, row * 0.25f);
  for (size_t visible_index = 0; visible_index < kMaxVisibleOptions;
       ++visible_index) {
    const size_t option_index = state.scroll + visible_index;
    const bool exists = option_index < options.size();
    const float row_y = rows_y + visible_index * row;
    if (!exists || text_opacity <= 0.0f) {
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
    Set_Property(s_label_class, s_widgets.rows[visible_index], "Opacity",
                 text_opacity);
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
    const bool selector = option.type == OptionType::Strings;
    std::string display_value;
    if (option.type == OptionType::Submenu) {
      display_value = ">";
    } else if (selector && selected) {
      /* Akari: the plain choice, and "< choice >" on the row the cursor is
         on, without the arrow that has nothing beyond it */
      const bool first = option.string_index <= 0;
      const bool last = option.string_index + 1 >= option.string_count;
      display_value = std::string(first ? "  " : "< ") + option.value +
                      (last ? "  " : " >");
    } else if (selector || option.type == OptionType::Value || control.slider) {
      display_value = option.value;
    }
    label_text(s_widgets.values[visible_index], display_value);
    value_label_geometry(s_widgets.values[visible_index],
                         control.slider ? control.value_x : value_x, row_y,
                         control.slider ? control.value_width : value_width,
                         row);
    /* after the geometry, which leaves the label fully opaque */
    Set_Property(s_label_class, s_widgets.values[visible_index], "Opacity",
                 text_opacity);
  }

  update_controls();
  s_dirty = false;
}

/* A colour animation frame of the Akari layout: only what takes the palette
   (frame, cursor, the switches and sliders that are on) is written again. */
void update_akari_menu_colors_only() {
  if (s_stack.empty())
    return;
  for (int i = 0; i < 2; ++i) {
    panel_color(s_widgets.left_borders[i],
                gradient_edge_color(/*right_edge=*/false));
    panel_color(s_widgets.right_borders[i],
                gradient_edge_color(/*right_edge=*/true));
  }
  for (int line = 0; line < kBorderLines; ++line)
    for (int i = 0; i < kFrameGradientSlices; ++i)
      color_frame_slice(line, i);
  const float text_opacity = std::clamp(s_akari_text_reveal, 0.0f, 1.0f);
  for (int i = 0; i < kGradientSlices; ++i)
    color_akari_highlight_slice(i, text_opacity);
  if (text_opacity <= 0.0f)
    return;
  const size_t scroll = s_stack.back().scroll;
  for (size_t i = 0; i < kMaxVisibleOptions; ++i)
    if (scroll + i < s_painted_options.size())
      color_option_control(i, s_painted_options[scroll + i]);
}
