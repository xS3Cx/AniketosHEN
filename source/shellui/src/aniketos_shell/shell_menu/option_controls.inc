/*
 * Shared Appendum option controls.
 *
 * The geometry and painter order below follow the local Appendum reference's
 * DrawToggle/DrawIntSlider implementation: slider 100x8; 1/2/3 px insets;
 * and a 4 px black + 2 px light two-layer knob. The PUI checkbox uses the
 * requested compact 12 px outer size because its solid panel reads heavier
 * than Appendum's game shader. Every visual theme calls this same function.
 */

struct AppendumControlLayout {
  bool toggle = false;
  bool slider = false;
  bool color = false;
  float title_right = 0.0f;
  float value_x = 0.0f;
  float value_width = 0.0f;
};

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

void hide_appendum_option_control(size_t index) {
  if (index >= kMaxVisibleOptions)
    return;
  collapse_panel(s_widgets.toggles[index]);
  collapse_panel(s_widgets.toggle_inners[index]);
  collapse_panel(s_widgets.toggle_checks[index]);
  collapse_panel(s_widgets.slider_tracks[index]);
  collapse_panel(s_widgets.slider_inners[index]);
  collapse_panel(s_widgets.slider_cores[index]);
  collapse_panel(s_widgets.slider_fills[index]);
  collapse_panel(s_widgets.slider_knobs[index]);
  collapse_panel(s_widgets.slider_knob_inners[index]);
  collapse_image(s_widgets.color_tracks[index]);
  collapse_image(s_widgets.color_track_tops[index]);
  collapse_panel(s_widgets.color_track_borders[index]);
  collapse_panel(s_widgets.color_knobs[index]);
  collapse_panel(s_widgets.color_knob_inners[index]);
}

/* The hint before a selected colour slider is cleared only when it was
   shown: clearing and setting it again on each redraw made it blink. */
bool s_color_hint_shown[kMaxVisibleOptions]{};

void hide_color_hint(size_t index) {
  if (index >= kMaxVisibleOptions || !s_color_hint_shown[index])
    return;
  s_color_hint_shown[index] = false;
  for (MonoObject *label :
       {s_widgets.color_hint_pre[index], s_widgets.color_hints[index]})
    clear_label(label);
  collapse_image(s_widgets.color_hint_glyphs[index]);
}

Color appendum_slider_fill(const Option &option) {
  return option.id == OptionId::MenuOpacity ? Color{1.0f, 1.0f, 1.0f, 1.0f}
                                            : Color{0.0f, 0.0f, 0.0f, 0.9f};
}

/* The scale a skin lays its rows out at. Apparition draws the default
   size at 1.5 (apparition_theme.inc) and sets its text max(1, round(scale))
   lower in the row; anything placed beside its titles has to use the same
   numbers or it stands a pixel off them. */
float row_layout_scale() {
  const AniketosUI::MenuStyle style = active_menu_style();
  if (apparition_theme() && std::fabs(style.scale - 1.0f) < 0.01f)
    return 1.5f;
  return AniketosUI::Scale(style);
}

float row_text_offset() {
  return apparition_theme() ? std::max(1.0f, std::round(row_layout_scale()))
                            : 0.0f;
}

/* "Press [Square] To Change Mode [1/3]": text, the button's glyph, text. */
struct ColorHint {
  const char *pre = "";
  char post[128] = "";
  float pre_width = 0.0f;
  float post_width = 0.0f;
  float glyph = 0.0f;
  float spacing = 0.0f;
  float width = 0.0f;
};

ColorHint measure_color_hint(const Option &option, float font, float scale) {
  ColorHint hint;
  hint.pre = tr("vsh.color.change_mode_pre");
  std::snprintf(hint.post, sizeof(hint.post), "%s [%d/%d]",
                tr("vsh.color.change_mode_post"), option.string_index + 1,
                option.string_count);
  hint.glyph = std::round(font * 1.2f);
  hint.spacing = std::round(3.0f * scale);
  hint.pre_width = *hint.pre ? approximate_text_width(hint.pre, font) + 2.0f : 0.0f;
  hint.post_width = approximate_text_width(hint.post, font) + 2.0f;
  hint.width = (*hint.pre ? hint.pre_width + hint.spacing : 0.0f) + hint.glyph +
               hint.spacing + hint.post_width;
  return hint;
}

/* The hint stands in the middle of the space between the title and the
   track. Its text takes the title label's own top and height as UI2 left
   them (measured on a console: the title box is 21 px where the row is
   27, which put a row-high hint 2 px lower), so both are centred in the
   same box and share a baseline; the glyph is centred on that box too.
   Returns where the hint begins. */
float place_color_hint(size_t index, const ColorHint &hint, float row_y,
                       float row, float free_begin, float free_end) {
  MonoObject *title = s_widgets.rows[index];
  /* the line the titles' text is set on */
  float line_y = row_y + row_text_offset();
  float line_h = row;
  if (title && s_label_class) {
    const float title_y = Get_Property<float>(s_label_class, title, "Y");
    const float title_h = Get_Property<float>(s_label_class, title, "Height");
    if (title_h > 1.0f && title_y >= row_y - 1.0f && title_y < row_y + row) {
      line_y = title_y;
      line_h = title_h;
    }
  }
  static MonoObject *faced[kMaxVisibleOptions][2] = {};
  MonoObject *pre_label = s_widgets.color_hint_pre[index];
  MonoObject *post_label = s_widgets.color_hints[index];
  MonoObject *labels[2] = {pre_label, post_label};
  for (int i = 0; i < 2; ++i)
    if (faced[index][i] != s_font) {
      label_font(labels[i]);
      faced[index][i] = s_font;
    }
  const float free = free_end - free_begin;
  float cursor = std::round(free > hint.width
                                ? free_begin + (free - hint.width) * 0.5f
                                : free_end - hint.width);
  const float hint_x = cursor;
  if (*hint.pre) {
    label_text(pre_label, hint.pre);
    label_geometry(pre_label, cursor, line_y, hint.pre_width, line_h);
    set_color(s_label_class, pre_label, "TextColor", hint_text_color());
    cursor += hint.pre_width + hint.spacing;
  } else {
    clear_label(pre_label);
  }
  /* the glyph centred on the same middle line as the text */
  image_geometry(s_widgets.color_hint_glyphs[index], cursor,
                 std::round(line_y + (line_h - hint.glyph) * 0.5f), hint.glyph,
                 hint.glyph);
  cursor += hint.glyph + hint.spacing;
  label_text(post_label, hint.post);
  label_geometry(post_label, cursor, line_y, hint.post_width, line_h);
  set_color(s_label_class, post_label, "TextColor", hint_text_color());
  s_color_hint_shown[index] = true;
  return hint_x;
}

/* The colour slider. Its track shows what it moves: the hues fading to
   white, black to white for brightness, or white to the colour for
   saturation; a knob filled with the chosen colour sits on it. On the
   selected row a smaller hint before it says that Square changes the mode,
   and the track gives up width so that the title still fits. Returns where
   the title has to end. */
float update_color_slider(size_t index, const Option &option, bool selected,
                          float row_y, float row, float right) {
  const float scale = row_layout_scale();
  const float gap = std::round(8.0f * scale);
  /* the same face and size as the menu's own text */
  const float font = static_cast<float>(std::max(s_font_size, 10));
  const ColorHint hint = selected ? measure_color_hint(option, font, scale)
                                  : ColorHint{};
  const float title_width =
      approximate_text_width(option.title.c_str(), font) * 1.1f +
      6.0f * scale;
  const float room = right - 8.0f * scale - title_width - gap -
                     (selected ? hint.width + gap : 0.0f);
  const float width = std::round(
      std::clamp(room, 47.0f * scale, 73.0f * scale));
  const float height = std::max(6.0f, std::round(row * 0.45f));
  const float x = std::round(right - width - 4.0f * scale);
  const float y = std::round(row_y + (row - height) * 0.5f);
  const ColorMode mode = static_cast<ColorMode>(option.string_index);
  const AniketosTheme::RGB current = color_of(option.id);

  const char *track = ColorTrackPath(
      mode == ColorMode::Brightness   ? ColorTrack::Gray
      : mode == ColorMode::Saturation ? ColorTrack::Solid
                                      : ColorTrack::Hues);
  if (s_color_track_image[index] != track) {
    AniketosUI::Pui::LoadSystemImage(s_pui, s_widgets.color_tracks[index],
                                     track);
    s_color_track_image[index] = track;
  }
  /* saturation: a pill of the full colour with white fading out over it */
  AniketosTheme::HSV full = AniketosTheme::RGBToHSV(current);
  full.s = 1.0f;
  full.v = 1.0f;
  const AniketosTheme::RGB tint = mode == ColorMode::Saturation
                                      ? AniketosTheme::HSVToRGB(full)
                                      : AniketosTheme::RGB{1.0f, 1.0f, 1.0f};
  set_color(s_image_box_class, s_widgets.color_tracks[index], "FilterColor",
            {tint.r, tint.g, tint.b, 1.0f});
  /* stretched to the box: the default keeps the picture's proportions,
     which held the track a few pixels high whatever height it was given */
  for (MonoObject *picture :
       {s_widgets.color_tracks[index], s_widgets.color_track_tops[index]})
    Set_Property(s_image_box_class, picture, "ImageScaleType", 1);
  /* a black stroke round the track, like the marker's */
  const float stroke = 1.0f;
  panel_geometry(s_widgets.color_track_borders[index], x - stroke, y - stroke,
                 width + stroke * 2.0f, height + stroke * 2.0f);
  panel_color(s_widgets.color_track_borders[index], {0.0f, 0.0f, 0.0f, 1.0f});
  image_geometry(s_widgets.color_tracks[index], x, y, width, height);
  image_geometry(s_widgets.color_track_tops[index], x, y, width, height,
                 mode == ColorMode::Saturation);

  const int position =
      std::clamp(std::atoi(option.value.c_str()), 0, option.maximum_i);
  float centre;
  if (mode != ColorMode::Hue) {
    centre = x + width * static_cast<float>(position) / kColorLevels;
  } else if (position >= kColorWhite) {
    centre = x + width * (1.0f + kColorTrackHues) * 0.5f;
  } else {
    centre = x + width * kColorTrackHues * (position + 0.5f) / kColorWhite;
  }
  /* a thin line across the track: dark edges round the chosen colour */
  const float mark_w = std::max(3.0f, std::round(3.0f * scale));
  /* as tall as the track with its stroke */
  const float mark_h = height + stroke * 2.0f;
  centre = std::clamp(centre, x + mark_w * 0.5f, x + width - mark_w * 0.5f);
  const float mark_x = std::round(centre - mark_w * 0.5f);
  const float mark_y = y - stroke;
  panel_geometry(s_widgets.color_knobs[index], mark_x, mark_y, mark_w, mark_h);
  panel_color(s_widgets.color_knobs[index], {0.0f, 0.0f, 0.0f, 1.0f});
  const AniketosTheme::RGB chosen = color_with(current, mode, position);
  panel_geometry(s_widgets.color_knob_inners[index], mark_x + 1.0f,
                 mark_y + 1.0f, mark_w - 2.0f, mark_h - 2.0f);
  panel_color(s_widgets.color_knob_inners[index],
              {chosen.r, chosen.g, chosen.b, 1.0f});

  if (!selected) {
    hide_color_hint(index);
    return x - gap;
  }
  return place_color_hint(index, hint, row_y, row, 4.0f * scale + title_width,
                          x - gap) -
         gap;
}

float animated_slider_progress(size_t index, const Option &option,
                              float target) {
  if (index >= kMaxVisibleOptions)
    return target;
  SliderMotion &motion = s_slider_motion[index];
  const uint64_t now = MonotonicMs();
  if (motion.id != option.id) {
    motion.id = option.id;
    motion.value = target;
    motion.target = target;
    motion.started_ms = 0;
    return target;
  }
  if (std::fabs(target - motion.target) > 0.0001f) {
    motion.value = motion.started_ms ? animated_slider_progress(index, option,
        motion.target) : motion.target;
    motion.target = target;
    motion.started_ms = now;
  }
  if (!motion.started_ms)
    return motion.target;
  constexpr float kDurationMs = 150.0f;
  const float t = std::min(1.0f,
      static_cast<float>(now - motion.started_ms) / kDurationMs);
  const float eased = Easing::Apply(active_menu_animation(), t);
  const float value = motion.value + (motion.target - motion.value) * eased;
  if (t >= 1.0f) {
    motion.value = motion.target;
    motion.started_ms = 0;
  } else {
    s_dirty = true;
  }
  return value;
}

/* The PS3 looks' own controls, in their 20-unit row: a solid square of
   0.7 row (on: Akari's right colour, Anomaly's white; off: dark grey)
   and a quarter-width line with a white knob, both 5 units (a quarter
   row) from the right edge. */
void update_ps3_option_control(size_t index, const Option &option, float row_y,
                               float row, float content_right,
                               AppendumControlLayout &layout) {
  const bool anomaly = anomaly_theme();
  const Color on = anomaly ? Color{1.0f, 1.0f, 1.0f, 1.0f}
                           : gradient_edge_color(true);
  const float inset = row * 0.25f;
  const Color dark{0.15f, 0.15f, 0.15f, 1.0f};
  if (layout.toggle) {
    const float box = std::round(row * 0.7f);
    const float x = std::round(content_right - inset - box);
    const float y = std::round(row_y + (row - box) * 0.5f);
    panel_geometry(s_widgets.toggles[index], x, y, box, box);
    panel_color(s_widgets.toggles[index], option.value == "1" ? on : dark);
    layout.title_right = x - inset;
    return;
  }
  const float track_w = menu_width() * (anomaly ? 4.0f / 3.0f : 1.0f) * 0.25f;
  const float track_h = std::max(1.0f, std::round(row * 0.1f));
  const float x = std::round(content_right - inset - track_w);
  const float y = std::round(row_y + (row - track_h) * 0.5f);
  const float progress = std::clamp(slider_progress(option), 0.0f, 1.0f);
  panel_geometry(s_widgets.slider_tracks[index], x, y, track_w, track_h);
  panel_color(s_widgets.slider_tracks[index], dark);
  if (progress > 0.0f) {
    panel_geometry(s_widgets.slider_fills[index], x, y, track_w * progress,
                   track_h);
    panel_color(s_widgets.slider_fills[index], on);
  }
  const float knob_w = std::max(1.0f, std::round(row * 0.1f));
  const float knob_h = std::round(row * 0.4f);
  panel_geometry(s_widgets.slider_knobs[index],
                 std::round(x + track_w * progress - knob_w * 0.5f),
                 std::round(row_y + (row - knob_h) * 0.5f), knob_w, knob_h);
  panel_color(s_widgets.slider_knobs[index], {1.0f, 1.0f, 1.0f, 1.0f});
  layout.value_width = std::max(
      row * 1.5f,
      std::ceil(approximate_text_width(option.value.c_str(), row * 0.8f)) +
          2.0f);
  layout.value_x = x - inset - layout.value_width;
  layout.title_right = layout.value_x - inset;
}

// Controls remain fully lit on every row. The scroller alone communicates
// selection, so toggles and sliders never look disabled while unfocused.
constexpr Color kControlOutline{1.0f, 1.0f, 1.0f, 1.0f};

// Keep every Appendum ratio exact at non-default menu scales. PUI accepts
// float geometry, so rounding each inset separately would incorrectly turn
// a 1.5 px inset into 2 px while the 100 px track became 150 px.
void update_appendum_toggle(size_t index, const Option &option, float row_y,
                            float row, float content_right, float scale,
                            AppendumControlLayout &layout) {
  const float inset_1 = 1.0f * scale;
  const float inset_3 = 3.0f * scale;
  const float edge_gap = 5.0f * scale;
  // PUI panels read a touch heavier than Appendum's game shader. A 12 px
  // outer box preserves its compact visual weight in this renderer.
  const float box = 12.0f * scale;
  const float x = std::round(content_right - edge_gap - box);
  const float y = std::round(row_y + (row - box) * 0.5f);
  panel_geometry(s_widgets.toggles[index], x, y, box, box);
  panel_color(s_widgets.toggles[index], kControlOutline);
  panel_geometry(s_widgets.toggle_inners[index], x + inset_1, y + inset_1,
                 std::max(0.0f, box - inset_1 * 2.0f),
                 std::max(0.0f, box - inset_1 * 2.0f));
  panel_color(s_widgets.toggle_inners[index], {0.0f, 0.0f, 0.0f, 0.9f});

  if (option.value == "1") {
    panel_geometry(s_widgets.toggle_checks[index], x + inset_3, y + inset_3,
                   std::max(0.0f, box - inset_3 * 2.0f),
                   std::max(0.0f, box - inset_3 * 2.0f));
    // The active mark follows the right edge of the configured gradient.
    // The row surface remains owned by the header/highlight renderer.
    panel_color(s_widgets.toggle_checks[index],
                gradient_edge_color(/*right_edge=*/true));
  }
  layout.title_right = x - edge_gap;
}

void update_appendum_slider(size_t index, const Option &option, float row_y,
                            float row, float content_right, float scale,
                            AppendumControlLayout &layout) {
  const float inset_1 = 1.0f * scale;
  const float inset_2 = 2.0f * scale;
  const float edge_gap = 5.0f * scale;
  const float slider_width = 100.0f * scale;
  const float slider_height = 8.0f * scale;
  const float x = std::round(content_right - edge_gap - slider_width);
  const float y = std::round(row_y + (row - slider_height) * 0.5f);
  const float progress = animated_slider_progress(
      index, option, std::clamp(slider_progress(option), 0.0f, 1.0f));
  const float inner_width =
      std::max(0.0f, slider_width - inset_1 * 2.0f);
  const float inner_height =
      std::max(0.0f, slider_height - inset_1 * 2.0f);
  const float fill_width = inner_width * progress;
  const float endpoint = x + fill_width;

  // Outer frame -> black inner -> light core -> fill, exactly in source
  // painter order.
  panel_geometry(s_widgets.slider_tracks[index], x, y, slider_width,
                 slider_height);
  panel_color(s_widgets.slider_tracks[index], kControlOutline);
  panel_geometry(s_widgets.slider_inners[index], x + inset_1, y + inset_1,
                 inner_width, inner_height);
  panel_color(s_widgets.slider_inners[index], {0.0f, 0.0f, 0.0f, 1.0f});
  panel_geometry(s_widgets.slider_cores[index], x + inset_2, y + inset_2,
                 std::max(0.0f, slider_width - inset_2 * 2.0f),
                 std::max(0.0f, slider_height - inset_2 * 2.0f));
  panel_color(s_widgets.slider_cores[index], kControlOutline);
  if (fill_width > 0.0f) {
    panel_geometry(s_widgets.slider_fills[index], x + inset_1, y + inset_1,
                   fill_width, inner_height);
    panel_color(s_widgets.slider_fills[index], appendum_slider_fill(option));
  }

  // The endpoint intentionally follows Appendum's `(width - 2) * progress`
  // coordinate rather than the outer track width.
  panel_geometry(s_widgets.slider_knobs[index], endpoint - inset_2,
                 y - inset_2, 4.0f * scale, slider_height + 4.0f * scale);
  panel_color(s_widgets.slider_knobs[index], {0.0f, 0.0f, 0.0f, 1.0f});
  panel_geometry(s_widgets.slider_knob_inners[index], endpoint - inset_1,
                 y - inset_1, 2.0f * scale, slider_height + 2.0f * scale);
  panel_color(s_widgets.slider_knob_inners[index], kControlOutline);

  // Source positions the track 5 px from the right edge and the value's
  // right edge 10 px plus the track width from that same edge: 5 px remain
  // between the value and the track.
  const float value_gap = 5.0f * scale;
  layout.value_width = std::max(
      row * 1.5f,
      std::ceil(approximate_text_width(option.value.c_str(), row)) +
          inset_1 * 2.0f);
  const float value_right = x - value_gap;
  layout.value_x = value_right - layout.value_width;
  layout.title_right = layout.value_x - edge_gap;
}

/* For a colour animation frame of the Akari layout: the parts of a row's
   control in the palette's colour, as update_ps3_option_control and
   update_appendum_toggle colour them (Anomaly's are white). */
void color_option_control(size_t index, const Option &option) {
  const Color on = gradient_edge_color(/*right_edge=*/true);
  const bool slider = option.type == OptionType::SliderInt ||
                      option.type == OptionType::SliderFloat;
  if (option.type == OptionType::Toggle && option.value == "1")
    panel_color(ps3_theme() ? s_widgets.toggles[index]
                            : s_widgets.toggle_checks[index],
                on);
  else if (ps3_theme() && slider && slider_progress(option) > 0.0f)
    panel_color(s_widgets.slider_fills[index], on);
}

AppendumControlLayout update_appendum_option_control(
    size_t index, const Option &option, bool selected, float row_y,
    float row, float content_right) {
  AppendumControlLayout layout{};
  layout.title_right = content_right;
  if (index >= kMaxVisibleOptions)
    return layout;

  hide_appendum_option_control(index);
  // Visible slots are reused while paging and scrolling. Restore neutral text
  // first so a submenu cannot inherit the 0.5/0.6 alpha of the checkbox or
  // slider that occupied the same slot on the previous frame.
  set_color(s_label_class, s_widgets.rows[index], "TextColor",
            {1.0f, 1.0f, 1.0f, 1.0f});
  set_color(s_label_class, s_widgets.values[index], "TextColor",
            {1.0f, 1.0f, 1.0f, 1.0f});
  Set_Property(s_label_class, s_widgets.rows[index], "VerticalAlignment", 2);
  Set_Property(s_label_class, s_widgets.values[index], "VerticalAlignment", 2);
  if (option.type != OptionType::ColorSlider)
    hide_color_hint(index);
  if (option.type == OptionType::ColorSlider) {
    layout.color = true;
    layout.title_right = update_color_slider(index, option, selected, row_y,
                                             row, content_right);
    return layout;
  }
  layout.toggle = option.type == OptionType::Toggle;
  layout.slider = option.type == OptionType::SliderInt ||
                  option.type == OptionType::SliderFloat;
  if (!layout.toggle && !layout.slider)
    return layout;

  if (ps3_theme()) {
    update_ps3_option_control(index, option, row_y, row, content_right, layout);
    return layout;
  }
  const float scale = AniketosUI::Scale(active_menu_style());
  if (layout.toggle)
    update_appendum_toggle(index, option, row_y, row, content_right, scale,
                           layout);
  else
    update_appendum_slider(index, option, row_y, row, content_right, scale,
                           layout);
  return layout;
}
