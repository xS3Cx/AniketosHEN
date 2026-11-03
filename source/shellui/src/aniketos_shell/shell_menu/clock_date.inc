void style_clock_date_label(MonoObject *label) {
  MonoClass *klass = cls(label);
  Set_Property(klass, label, "VerticalAlignment", 0);
  Set_Property(klass, label, "MaxLineCount", 1);
  Set_Property(klass, label, "NumberOfLines", 1);
  Set_Property(klass, label, "TextTrimming", 0);
  Set_Property(klass, label, "FitWidthToText", false);
  Set_Property(klass, label, "FitHeightToText", false);
  Set_Property(klass, label, "EnableThemedColor", false);
  Set_Property(klass, label, "EnableThemedShadowColor", false);
  Set_Property(klass, label, "EnableThemedTextShadow", true);
  Set_Property(klass, label, "HorizontalAlignment", 1);
}

void update_clock_date() {
  /* kept between frames: copying into it reuses its text's buffer */
  static ShellClock::DateOverlayState state;
  ShellClock::GetDateOverlayState(&state);
  /* Part of Home's scene, PUI fades it with the clock; otherwise the same
     frame-by-frame mirror of the clock's opacity as the OSD. */
  float clock_opacity = s_clock_layer ? 1.0f : 0.0f;
  if (!s_clock_layer)
    ShellClock::GetNativeClockOpacity(&clock_opacity);
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

  if (!visible) {
    if (s_widgets.clock_date && std::fabs(applied.opacity) > 0.001f) {
      Set_Property(cls(s_widgets.clock_date), s_widgets.clock_date, "Opacity",
                   0.0f);
      applied.opacity = 0.0f;
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
    style_clock_date_label(s_widgets.clock_date);
    applied = {};
    applied.widget = s_widgets.clock_date;
  }
  MonoObject *label = s_widgets.clock_date;
  MonoClass *klass = cls(label);

  if (!s_clock_date_font || !s_clock_date_font_handle ||
      s_clock_date_font_size != state.font_size) {
    if (s_clock_date_font_handle && mono_gchandle_free)
      mono_gchandle_free(s_clock_date_font_handle);
    s_clock_date_font_handle = 0;
    s_clock_date_font =
        create_pinned_font(state.font_size, 400, s_clock_date_font_handle);
    s_clock_date_font_size = s_clock_date_font ? state.font_size : 0;
  }
  if (s_clock_date_font && applied.font != s_clock_date_font) {
    Set_Property_Invoke(klass, label, "Font", s_clock_date_font);
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
  const float x = std::clamp(
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
  const float y = std::clamp(
      requested_y, 0.0f, std::max(0.0f, s_viewport_height - height));

  label_text(label, state.text);
  set_color(klass, label, "TextColor",
            {state.r / 255.0f, state.g / 255.0f, state.b / 255.0f, 1.0f});
  if (std::fabs(applied.opacity - clock_opacity) > 0.001f) {
    Set_Property(klass, label, "Opacity", clock_opacity);
    applied.opacity = clock_opacity;
  }
  if (std::fabs(applied.x - x) > 0.01f || std::fabs(applied.y - y) > 0.01f ||
      std::fabs(applied.width - width) > 0.01f ||
      std::fabs(applied.height - height) > 0.01f) {
    label_geometry(label, x, y, width, height);
    applied.x = x;
    applied.y = y;
    applied.width = width;
    applied.height = height;
  }
}
