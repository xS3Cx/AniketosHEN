/* Frames update_controls still runs from Tick after the legend's text or
   font changed: a label lays its text out asynchronously, so its real width
   and baseline only show a few frames later. */
int s_controls_settle_frames = 0;

/* The legend's border was dropped from the design; its panels stay collapsed. */
void hide_control_frame() {
  collapse_panel(s_widgets.control_backdrop);
  for (MonoObject *panel : s_widgets.control_border_top)
    collapse_panel(panel);
  for (MonoObject *panel : s_widgets.control_border_bottom)
    collapse_panel(panel);
  collapse_panel(s_widgets.control_border_left);
  collapse_panel(s_widgets.control_border_right);
}

/* A parameterless float method of a label (GetTextWidth,
   GetBottomBaselineY); NaN when it is missing or throws. */
float label_metric(MonoObject *label, const char *method) {
  float value = NAN;
  AniketosUI::Reflect::FloatProperty(label, method, &value);
  return value;
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
  constexpr float kChordGap = 3.0f;
  constexpr float kGlyphTextGap = 5.0f;
  constexpr float kIconHeight = 16.0f;
  constexpr float kIconY = (kHeight - kIconHeight) / 2.0f;
  constexpr float kControlTextSize = 14.0f;
  // Text and glyphs share the row's centre line: the baseline sits half a
  // cap height (about 0.7 em in any Latin font) below it.
  constexpr float kBaselineY = kHeight / 2.0f + kControlTextSize * 0.35f;
  constexpr int kSettleFrames = 90;
  constexpr int kStableReads = 3;
  struct LabelMetrics {
    MonoObject *label = nullptr;
    MonoObject *font = nullptr;
    uint64_t host_generation = 0;
    int font_generation = 0;
    std::string text;
    float box = 0.0f;   // the label's width, roomier than its text
    float width = 0.0f; // the text's advance: the estimate until measured
    float baseline = kControlTextSize; // below the label's top
    float read_width = 0.0f;
    float read_baseline = 0.0f;
    int stable_reads = 0;
    float x = -1.0f;
    float y = -1.0f;
  };
  static LabelMetrics metrics[kControlLabelCount];
  const int font_generation = AniketosTheme::CustomFont::Generation();
  const bool settling = s_controls_settle_frames > 0;
  float x = kHorizontalPadding;
  int next_label = 0;
  auto text_item = [&](const char *text) {
    if (next_label >= kControlLabelCount)
      return;
    const int label_index = next_label++;
    MonoObject *label = s_widgets.control_labels[label_index];
    LabelMetrics &m = metrics[label_index];
    const std::string value = text ? text : "";
    if (m.label != label || m.font != s_control_font ||
        m.host_generation != s_host_generation ||
        m.font_generation != font_generation || m.text != value) {
      label_text(label, value);
      if (s_control_font)
        Set_Property_Invoke(cls(label), label, "Font", s_control_font);
      Set_Property(cls(label), label, "MaxLineCount", 1);
      Set_Property(cls(label), label, "TextTrimming", 0);
      // Text starts at the label's left edge and the line at its top; the
      // row places both from the measured width and baseline.
      Set_Property(cls(label), label, "HorizontalAlignment", 0);
      Set_Property(cls(label), label, "VerticalAlignment", 0);
      m = {};
      m.label = label;
      m.font = s_control_font;
      m.host_generation = s_host_generation;
      m.font_generation = font_generation;
      m.text = value;
      // The SST-tuned estimate holds the first frames only.
      m.width = std::max(
          2.0f,
          std::ceil(approximate_text_width(value.c_str(), kControlTextSize)));
      m.box = m.width * 2.0f + 32.0f;
      s_controls_settle_frames = kSettleFrames;
    } else if (settling) {
      // Right after a text or font change GetTextWidth still answers for the
      // previous layout (or with the box): a reading counts once it repeats.
      // GetBottomBaselineY is in the parent's coordinates.
      const float width = label_metric(label, "GetTextWidth");
      const float baseline =
          label_metric(label, "GetBottomBaselineY") -
          Get_Property<float>(cls(label), label, "Y");
      const bool valid = std::isfinite(width) && width > 0.5f &&
                         width < m.box - 0.5f && std::isfinite(baseline) &&
                         baseline > 0.0f && baseline < kHeight;
      if (valid && std::fabs(width - m.read_width) < 0.25f &&
          std::fabs(baseline - m.read_baseline) < 0.25f) {
        if (++m.stable_reads == kStableReads) {
          m.width = std::ceil(width);
          m.baseline = baseline;
        }
      } else {
        m.stable_reads = 0;
      }
      m.read_width = width;
      m.read_baseline = baseline;
    }
    const float y = std::round(kBaselineY - m.baseline);
    if (m.x != x || m.y != y) {
      label_geometry(label, x, y, m.box, kHeight);
      m.x = x;
      m.y = y;
    }
    x += m.width;
  };
  auto icon_item = [&](MonoObject *icon, float width) {
    image_geometry(icon, x, kIconY, width, kIconHeight);
    x += width;
  };
  auto shortcut = [&](const char *action) {
    text_item(tr("vsh.ctrl.press"));
    x += kGlyphTextGap;
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
  hide_control_frame();

  for (int i = next_label; i < kControlLabelCount; ++i) {
    clear_label(s_widgets.control_labels[i]);
    metrics[i] = {};
  }
}

void hide_anomaly_extras() {
  collapse_panel(s_widgets.anomaly_controls);
  for (MonoObject *icon : s_widgets.anomaly_control_icons)
    collapse_image(icon);
  for (MonoObject *label : s_widgets.anomaly_control_labels)
    clear_label(label);
  collapse_image(s_widgets.position_edit_icon);
}
