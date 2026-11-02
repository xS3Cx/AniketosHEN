/*
 * Apparition visual theme.
 *
 * Original PS5/PUI implementation of the public Apparition menu's default
 * composition (CF4x99): no source or assets are copied.  The reference uses
 * only a white HUD primitive and the game's default font, so this renderer
 * recreates its measured geometry, layered greys, blue accent and selection
 * behavior with AniketosHEN's existing primitives.
 */

// The header, the footer and the selected row share one neutral surface.
// Global Theme owns the selected text accent only; tinting the selected row
// made the entire switch change colour when Global Theme was enabled.
constexpr Color kApparitionBanner{42.0f / 255.0f, 42.0f / 255.0f,
                                  42.0f / 255.0f, 1.0f};

Color apparition_neutral_option_text() {
  return light_menu_appearance()
             ? Color{0.34f, 0.35f, 0.37f, 0.90f}
             : Color{1.0f, 1.0f, 1.0f, 0.40f};
}

float apparition_platform_glyph_size(float header_height) {
  const float scale = light_menu_appearance() ? 1.50f : 1.22f;
  return std::round(header_height * scale);
}

Color apparition_toggle_outline() {
  return light_menu_appearance()
             ? Color{0.54f, 0.56f, 0.60f, 1.0f}
             : Color{45.0f / 255.0f, 45.0f / 255.0f,
                     45.0f / 255.0f, 1.0f};
}

Color apparition_toggle_fill(bool enabled) {
  return enabled ? right_color() : Color{0.0f, 0.0f, 0.0f, 1.0f};
}

Color apparition_gradient_color(const AniketosUI::MenuStyle &style, float t) {
  return style.dual_color
             ? mix(left_color(), right_color(), gradient_t(t), 1.0f)
             : left_color();
}

Color apparition_blend(Color from, Color to, float t) {
  return Color{from.r + (to.r - from.r) * t,
               from.g + (to.g - from.g) * t,
               from.b + (to.b - from.b) * t,
               from.a + (to.a - from.a) * t};
}

/* The platform's glyph just after the centred title. */
void place_apparition_platform_glyph(float width, float header_y,
                                     float header_height, float title_fallback,
                                     bool visible) {
  const float glyph_size = apparition_platform_glyph_size(header_height);
  const float title_width =
      exact_label_text_width(s_widgets.title, title_fallback);
  const float glyph_gap = std::max(4.0f, glyph_size * 0.20f);
  image_geometry(s_widgets.header_platform_glyph,
                 width * 0.5f + title_width * 0.5f + glyph_gap,
                 header_y + (header_height - glyph_size) * 0.5f, glyph_size,
                 glyph_size, visible);
}

/* The separators, on the two dedicated theme planes. */
void place_apparition_separators(float width, float body_y, float footer_y,
                                 float separator) {
  for (int i = 0; i < kFrameGradientSlices; ++i) {
    const GradientSlice slice =
        gradient_slice(width, i, kFrameGradientSlices);
    panel_geometry(s_widgets.theme_header_slices[i], slice.x,
                   body_y - separator, slice.width, separator);
    panel_geometry(s_widgets.theme_footer_slices[i], slice.x,
                   footer_y - separator, slice.width, separator);
  }
}

void color_apparition_separators(const AniketosUI::MenuStyle &style) {
  for (int i = 0; i < kFrameGradientSlices; ++i) {
    const float t = (static_cast<float>(i) + 0.5f) /
                    static_cast<float>(kFrameGradientSlices);
    const Color line_color = apparition_gradient_color(style, t);
    panel_color(s_widgets.theme_header_slices[i], line_color);
    panel_color(s_widgets.theme_footer_slices[i], line_color);
  }
}

// Apparition opens as one quiet header/footer unit, then unfolds the list
// between those two bars. Closing performs the exact inverse before fading.
void update_apparition_open_close_animation(float reveal) {
  const ApparitionAnimationLayout &layout = s_apparition_animation_layout;
  if (!layout.valid)
    return;

  const float raw_spread = std::clamp((reveal - 0.12f) / 0.88f, 0.0f, 1.0f);
  const float spread = raw_spread * raw_spread * (3.0f - 2.0f * raw_spread);
  // The dedicated scroll viewport clips every row/control physically.  Fade
  // it in with the opening surface, rather than hiding it until the end.
  const float frame_opacity = std::clamp(reveal / 0.34f, 0.0f, 1.0f);
  // Start with two equal bars joined at the exact vertical centre, then move
  // the header up and the footer down by the same visual amount.
  const float centre_y = layout.total_height * 0.5f;
  const float header_y = (1.0f - spread) *
                             (centre_y - layout.header_height);
  const float footer_y = (1.0f - spread) * centre_y +
                         spread * layout.footer_y;
  const float body_y = header_y + layout.header_height;
  const float exposed_body = std::max(
      0.0f, footer_y - body_y - layout.separator * spread);

  panel_geometry(s_widgets.master, layout.x, layout.y, layout.width,
                 layout.total_height);
  panel_geometry(s_widgets.content, 0.0f, 0.0f, layout.width,
                 layout.total_height);
  panel_geometry(s_widgets.header, 0.0f, header_y, layout.width,
                 layout.header_height);
  panel_geometry(s_widgets.body, 0.0f, body_y, layout.width,
                 exposed_body);
  panel_geometry(s_widgets.scroll_clip, 0.0f, body_y, layout.width,
                 exposed_body);
  panel_geometry(s_widgets.footer, 0.0f, footer_y, layout.width,
                 layout.footer_height);
  label_geometry(s_widgets.title, 0.0f, header_y + layout.text_offset,
                 layout.width, layout.header_height);
  place_apparition_platform_glyph(layout.width, header_y, layout.header_height,
                                  layout.header_height * 2.9f,
                                  frame_opacity > 0.001f);
  label_geometry(s_widgets.page_title, 0.0f,
                 footer_y + layout.text_offset, layout.width,
                 layout.footer_height);
  place_apparition_separators(layout.width, body_y, footer_y, layout.separator);
  Set_Property(s_panel_class, s_widgets.master, "Opacity", frame_opacity);
  Set_Property(s_label_class, s_widgets.title, "Opacity", frame_opacity);
  Set_Property(s_label_class, s_widgets.page_title, "Opacity", frame_opacity);
  Set_Property(s_pui.image_box, s_widgets.header_platform_glyph, "Opacity",
               frame_opacity);
}

/* Row height of the layout drawn last, for the colour pass below. */
float s_apparition_row = 0.0f;

/* How much of the cursor bar lies on the row at `visible_index`, 0 to 1. A
   row is lit by this, so its text changes with the bar and never ahead of
   it: with a direction held the bar glides at a steady speed while the
   selection itself steps, and lighting the selected row at once put the
   colour a row in front of the bar. */
float apparition_bar_overlap(size_t visible_index) {
  const float row = std::max(s_apparition_row, 1.0f);
  return std::clamp(1.0f - std::fabs(static_cast<float>(visible_index) * row -
                                     s_highlight_y) / row,
                    0.0f, 1.0f);
}

// Refresh only the animated Apparition text palette. Geometry is owned by
// update_apparition_menu_widgets() and must not be written again for every
// colour/highlight frame: UI2.Label can briefly expose its previous layout
// while a managed setter is being applied, which produced detached markers
// and stale values in the lower-right corner.
void update_apparition_menu_colors_only() {
  if (s_stack.empty())
    return;

  PageState &state = s_stack.back();
  std::vector<Option> options_scratch;
  const std::vector<Option> &options =
      page_options(state.page, options_scratch);
  // Palette-only redraws never reconcile selection. Focus is committed by
  // input or by the structural page renderer, not by an animation frame.
  if (options.empty() || state.selected >= options.size())
    return;

  const AniketosUI::MenuStyle style = active_menu_style();
  const Color accent = apparition_gradient_color(style, 0.5f);
  set_color(s_label_class, s_widgets.title, "TextColor", accent);
  set_color(s_label_class, s_widgets.page_title, "TextColor", accent);

  const Color neutral = apparition_neutral_option_text();
  const Color title_accent = apparition_gradient_color(style, 0.15f);
  const Color value_accent = apparition_gradient_color(style, 0.85f);
  for (size_t visible_index = 0; visible_index < kMaxVisibleOptions;
       ++visible_index) {
    const size_t option_index = state.scroll + visible_index;
    if (option_index >= options.size())
      continue;
    const Option &option = options[option_index];
    const float bar_overlap = apparition_bar_overlap(visible_index);
    const Color title_color =
        apparition_blend(neutral, title_accent, bar_overlap);
    const Color value_color =
        apparition_blend(neutral, value_accent, bar_overlap);
    set_color(s_label_class, s_widgets.rows[visible_index], "TextColor",
              title_color);
    set_color(s_label_class, s_widgets.values[visible_index], "TextColor",
              value_color);
    if (option.type == OptionType::Submenu) {
      // Sub-menu markers use their own pooled label. The shared value label
      // must never receive the rotated marker transform: reusing that label
      // across pages was the source of detached dots and stale text at the
      // lower-right edge of the clipped list during a page hand-off.
      set_color(s_label_class, s_widgets.selector_right[visible_index],
                "TextColor", value_color);
    } else if (option.type == OptionType::Toggle) {
      panel_color(s_widgets.toggles[visible_index],
                  apparition_toggle_outline());
      panel_color(s_widgets.toggle_inners[visible_index],
                  apparition_toggle_fill(option.value == "1"));
    }
  }
  // Sample every animated surface from this exact frame palette. Otherwise
  // separators and controls retain an older colour than the labels.
  color_apparition_separators(style);
  panel_color(s_widgets.highlight, kApparitionBanner);
  s_apparition_force_color_refresh = false;
}

/* The frame's shared border panels: this look draws none. */
void hide_apparition_borders() {
  for (MonoObject *border : s_widgets.left_borders)
    collapse_panel(border);
  for (MonoObject *border : s_widgets.right_borders)
    collapse_panel(border);
  for (auto &line : s_widgets.horizontal_borders)
    for (MonoObject *part : line)
      collapse_panel(part);
}

void hide_apparition_row(size_t index) {
  label_text(s_widgets.rows[index], "");
  label_text(s_widgets.values[index], "");
  label_geometry(s_widgets.rows[index], 0.0f, 0.0f, 0.0f, 0.0f);
  label_geometry(s_widgets.values[index], 0.0f, 0.0f, 0.0f, 0.0f);
  label_angle(s_widgets.values[index], 0.0f);
  Set_Property(s_label_class, s_widgets.rows[index], "Opacity", 0.0f);
  Set_Property(s_label_class, s_widgets.values[index], "Opacity", 0.0f);
  clear_selector_markers(index);
  hide_appendum_option_control(index);
  hide_color_hint(index);
}

// Original `base.gsc`: BoolBack is 10x10 and BoolOpt is 8x8, centred
// eight pixels from the right edge.  The old 15px PUI control made
// both the checkbox and its switch row visibly oversized.
// Returns the checkbox's left edge.
float update_apparition_toggle(size_t index, const Option &option, float width,
                               float row_y, float row, float scale) {
  const float box = 10.0f * scale;
  const float box_x = width - 8.0f * scale - box * 0.5f;
  const float box_y = row_y + (row - box) * 0.5f;
  panel_geometry(s_widgets.toggles[index], box_x, box_y, box, box);
  panel_color(s_widgets.toggles[index], apparition_toggle_outline());
  const float inner = 8.0f * scale;
  panel_geometry(s_widgets.toggle_inners[index], box_x + (box - inner) * 0.5f,
                 box_y + (box - inner) * 0.5f, inner, inner);
  panel_color(s_widgets.toggle_inners[index],
              apparition_toggle_fill(option.value == "1"));
  return box_x;
}

void update_apparition_menu_widgets() {
  PageState &state = s_stack.back();
  std::vector<Option> options_scratch;
  const std::vector<Option> &options =
      page_options(state.page, options_scratch);
  ensure_selection(state, options);
  hide_theme_accent_slices();
  hide_anomaly_extras();

  // Apparition has its own compact presentation defaults.  They intentionally
  // do not alter the shared settings used by the other Shell Menu themes.
  const AniketosUI::MenuStyle style = active_menu_style();
  const bool default_scale = std::fabs(style.scale - 1.0f) < 0.01f;
  const float scale = default_scale ? 1.50f : AniketosUI::Scale(style);
  const float base_row = std::round(18.0f * scale);
  // The native PUI font is larger than the game HUD's default font.  A 15px
  // baseline keeps the text compact and preserves the user's text-size choice.
  const float font_size = std::max(
      10.0f, std::round(style.text_size * (15.0f / 18.0f)));
  const float row = std::max(base_row, font_size + 4.0f * scale);
  s_apparition_row = row;
  const bool default_width = std::fabs(style.width - 450.0f) < 0.01f;
  // 325 is Apparition's AniketosHEN default. Once the Width setting is changed from
  // the global default, use it directly so the personalisation page has an
  // immediate, visible effect.
  const float width = std::round(default_width
                                     ? 325.0f * scale
                                     : std::min(menu_width(), 900.0f * scale));
  const float edge = 2.0f;
  const float inset = 4.0f * scale;
  const size_t visible = std::min(kMaxVisibleOptions, options.size());
  const float body_height =
      std::max(row, row * static_cast<float>(visible));
  // Header, footer and an option row use the identical vertical band.  It
  // keeps every centred label on one shared baseline at all scale values.
  const float body_y = row;
  const float footer_height = row;
  // UI2 centres the UIFont metric box rather than the visible glyphs.  The
  // default face's ascent/descent is top-heavy, so one common downward
  // optical correction keeps title, footer and all option values aligned.
  const float text_offset = std::max(1.0f, std::round(scale));
  const float footer_y = body_y + body_height + edge;
  const float total_height = footer_y + footer_height;
  // Apparition's bars and its expanding list stay centred on the configured
  // menu point throughout the entire opening/closing transition.
  const float x = std::clamp(style.x - width * 0.5f, 0.0f,
                             std::max(0.0f, s_viewport_width - width));
  const float y = std::clamp(style.y - total_height * 0.5f, 0.0f,
                             std::max(0.0f, s_viewport_height - total_height));

  // Values sampled from the supplied Apparition reference frame. ShellUI's
  // Apparition surface is deliberately fully opaque, independent of the
  // general Menu Opacity setting.
  const Color accent = apparition_gradient_color(style, 0.5f);
  constexpr Color background{20.0f / 255.0f, 20.0f / 255.0f, 20.0f / 255.0f,
                             1.0f};
  set_master_geometry(x, y, width, total_height);
  s_apparition_animation_layout = {true, x, y, width, total_height, body_y,
                                    body_height, footer_y, footer_height,
                                    edge, text_offset};
  // Header and footer are solid equal-height bands; the opaque body is their
  // separate middle surface.
  panel_geometry(s_widgets.header, 0.0f, 0.0f, width, body_y);
  panel_geometry(s_widgets.body, 0.0f, body_y, width, body_height);
  panel_geometry(s_widgets.footer, 0.0f, footer_y, width, footer_height);
  panel_color(s_widgets.header, kApparitionBanner);
  panel_color(s_widgets.body, background);
  panel_color(s_widgets.footer, kApparitionBanner);
  panel_geometry(s_widgets.scroll_clip, 0.0f, body_y, width, body_height);
  Set_Property(s_panel_class, s_widgets.body, "Opacity", 1.0f);
  Set_Property(s_panel_class, s_widgets.scroll_clip, "Opacity", 1.0f);
  Set_Property(s_panel_class, s_widgets.highlight, "Opacity", 1.0f);
  // Use the two dedicated theme planes for the separator and footer line.
  // They are created before labels and stay inside the master's clip bounds;
  // generic border slices are not reliable after a theme switch.
  place_apparition_separators(width, body_y, footer_y, edge);
  color_apparition_separators(style);
  hide_apparition_borders();

  replace_font(std::max(1, static_cast<int>(std::lround(font_size))));
  label_font(s_widgets.title);
  label_font(s_widgets.page_title);
  label_font(s_widgets.counter);
  for (size_t i = 0; i < kMaxVisibleOptions; ++i) {
    option_row_fonts(i);
    Set_Property(s_label_class, s_widgets.rows[i], "HorizontalAlignment", 0);
    Set_Property(s_label_class, s_widgets.values[i], "HorizontalAlignment", 2);
    for (MonoObject *label : {s_widgets.rows[i], s_widgets.values[i]}) {
      Set_Property(s_label_class, label, "VerticalAlignment", 2);
      Set_Property(s_label_class, label, "FitHeightToText", false);
      Set_Property(s_label_class, label, "MaxLineCount", 1);
    }
  }
  for (MonoObject *label : {s_widgets.title, s_widgets.page_title}) {
    Set_Property(s_label_class, label, "HorizontalAlignment", 1);
    Set_Property(s_label_class, label, "VerticalAlignment", 2);
    Set_Property(cls(label), label, "FitWidthToText", false);
    Set_Property(cls(label), label, "FitHeightToText", false);
    Set_Property(cls(label), label, "MaxLineCount", 1);
  }
  set_color(s_label_class, s_widgets.title, "TextColor", accent);
  Set_Property(s_label_class, s_widgets.title, "Opacity", 1.0f);
  label_text(s_widgets.title, menu_header_text());
  label_geometry(s_widgets.title, 0.0f, text_offset, width, body_y);
  place_apparition_platform_glyph(
      width, 0.0f, body_y, approximate_text_width(menu_header_text(), font_size),
      true);
  clear_label(s_widgets.version);
  set_color(s_label_class, s_widgets.page_title, "TextColor", accent);
  Set_Property(s_label_class, s_widgets.page_title, "Opacity", 1.0f);
  label_text(s_widgets.page_title, menu_footer_text());
  label_geometry(s_widgets.page_title, 0.0f, footer_y + text_offset, width,
                 footer_height);
  // Apparition's OptionCounter remains disabled.
  clear_label(s_widgets.counter);

  const float target_highlight =
      static_cast<float>(state.selected - state.scroll) * row;
  update_highlight_target(state, target_highlight);
  panel_geometry(s_widgets.highlight, 0.0f, s_highlight_y, width, row);
  panel_color(s_widgets.highlight, kApparitionBanner);
  for (MonoObject *slice : s_widgets.highlight_slices)
    collapse_panel(slice);

  // Apparition's SpotlightCursor dims all non-selected entries.
  // Fade the former selection out while the destination fades in.  The
  // previous implementation temporarily painted every row white, causing
  // text to jump ahead of the smooth scroller.
  const Color neutral = apparition_neutral_option_text();
  const Color title_accent = apparition_gradient_color(style, 0.15f);
  const Color value_accent = apparition_gradient_color(style, 0.85f);
  const float base_value_width = width * 0.56f;
  for (size_t visible_index = 0; visible_index < kMaxVisibleOptions;
       ++visible_index) {
    const size_t option_index = state.scroll + visible_index;
    const float row_y = visible_index * row;
    if (option_index >= options.size()) {
      hide_apparition_row(visible_index);
      continue;
    }

    const Option &option = options[option_index];
    /* a row whose value is too long for the usual column (a console
       identifier) gives it most of the width; its title is short */
    const float value_width =
        option.wide_value ? width * 0.80f : base_value_width;
    if (option.type != OptionType::Submenu)
      clear_selector_markers(visible_index);
    hide_appendum_option_control(visible_index);
    if (option.type != OptionType::ColorSlider)
      hide_color_hint(visible_index);
    Set_Property(s_label_class, s_widgets.rows[visible_index], "Opacity",
                 1.0f);
    Set_Property(s_label_class, s_widgets.values[visible_index], "Opacity",
                 1.0f);
    const float bar_overlap = apparition_bar_overlap(visible_index);
    const Color title_color =
        apparition_blend(neutral, title_accent, bar_overlap);
    const Color value_color =
        apparition_blend(neutral, value_accent, bar_overlap);
    float title_right = width - inset;
    // A submenu's value stays empty: its marker is laid out in its own stable
    // label below, and the shared value pool must stay empty and unrotated so
    // it can never leak a previous page's value glyph while the viewport is
    // being resized.
    std::string display_value;

    if (option.type == OptionType::Toggle) {
      title_right = update_apparition_toggle(visible_index, option, width,
                                             row_y, row, scale) -
                    inset;
    } else if (option.type == OptionType::Strings) {
      display_value = selector_display_value(option);
    } else if (option.type == OptionType::SliderInt ||
               option.type == OptionType::SliderFloat) {
      display_value = "< " + option.value + " >";
    } else if (option.type == OptionType::Value) {
      display_value = option.value;
    } else if (option.type == OptionType::ColorSlider) {
      title_right = update_appendum_option_control(
                        visible_index, option,
                        option_index == state.selected, row_y, row,
                        width - inset)
                        .title_right;
    }

    label_text(s_widgets.rows[visible_index], option.title);
    label_geometry(s_widgets.rows[visible_index], inset, row_y + text_offset,
                   std::max(0.0f, title_right - inset - inset), row);
    set_color(s_label_class, s_widgets.rows[visible_index], "TextColor",
              title_color);
    label_text(s_widgets.values[visible_index], display_value);
    if (option.type == OptionType::Submenu) {
      // The shared value slot may have been a rotated submenu/value on the
      // previous page. Explicitly neutralise and collapse it before the
      // dedicated marker is shown, otherwise UI2 can paint its old glyph
      // once at the lower-right edge while the clip rectangle changes.
      label_angle(s_widgets.values[visible_index], 0.0f);
      label_geometry(s_widgets.values[visible_index], 0.0f, 0.0f, 0.0f,
                     0.0f);
      Set_Property(s_label_class, s_widgets.values[visible_index],
                   "Opacity", 0.0f);
      update_submenu_marker(visible_index, width - inset - row,
                            row_y + text_offset, row, 1.57079632679f,
                            value_color);
    } else {
      value_label_geometry(s_widgets.values[visible_index],
                           width - inset - value_width,
                           row_y + text_offset, value_width, row);
    }
    set_color(s_label_class, s_widgets.values[visible_index], "TextColor",
              value_color);
  }

  update_controls();
  s_dirty = false;
}
