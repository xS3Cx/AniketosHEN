uint64_t monotonic_millis() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000u +
         static_cast<uint64_t>(now.tv_nsec) / 1000000u;
}

int active_menu_font_weight();
int active_menu_theme();
const char *tr(const char *key);
const char *page_title(Page page);
uint64_t active_menu_color_frame_ms();

MonoObject *create_pinned_font(int size, int weight, uint32_t &handle) {
  MonoObject *font = CreateUIFont(size, 0, weight);
  if (!font || !mono_gchandle_new)
    return nullptr;
  handle = mono_gchandle_new(font, /*pinned=*/1);
  return handle ? font : nullptr;
}

bool retain_widget(MonoObject *object) {
  if (!object || !mono_gchandle_new)
    return false;
  const uint32_t handle = mono_gchandle_new(object, /*pinned=*/1);
  if (!handle) {
    LOG_ERROR("[ANIVSH:AKARI] failed to pin managed widget=%p",
              static_cast<void *>(object));
    return false;
  }
  s_widget_handles.push_back(handle);
  return true;
}

bool retain_pui_widget(MonoObject *object, void *) {
  return retain_widget(object);
}

bool light_menu_appearance() {
  return g_settings.vsh_menu_appearance == 1;
}

/* Secondary text (hints and other notes beside an option): light on the
   dark menu, dark on the light one, so it stays readable in both. Use it
   for any such text added later. Button glyphs keep their own look. */
Color hint_text_color() {
  return light_menu_appearance() ? Color{0.16f, 0.17f, 0.19f, 0.80f}
                                 : Color{1.0f, 1.0f, 1.0f, 0.55f};
}

bool is_panel_in_slices(MonoObject *panel, MonoObject *const *slices,
                        size_t count) {
  for (size_t i = 0; i < count; ++i)
    if (panel == slices[i])
      return true;
  return false;
}

void release_widgets() {
  if (mono_gchandle_free) {
    for (uint32_t handle : s_widget_handles)
      mono_gchandle_free(handle);
  }
  s_widget_handles.clear();
  AniketosUI::Pui::ForgetAll();
  s_submenu_marker_active.fill(false);
}

void replace_font(int size) {
  const int selected_weight = active_menu_font_weight();
  const int weight = selected_weight == 0
                         ? 300
                         : (selected_weight == 2 ? 700 : 400);
  if (s_font && s_font_handle && s_font_size == size &&
      s_font_weight == weight)
    return;
  if (s_font_handle && mono_gchandle_free)
    mono_gchandle_free(s_font_handle);
  s_font_handle = 0;
  s_font = CreateUIFont(size, 0, weight);
  if (s_font && mono_gchandle_new) {
    s_font_handle = mono_gchandle_new(s_font, /*pinned=*/1);
    if (!s_font_handle) {
      LOG_ERROR("[ANIVSH:AKARI] failed to pin font=%p",
                static_cast<void *>(s_font));
      s_font = nullptr;
    }
  }
  s_font_size = s_font ? size : 0;
  s_font_weight = s_font ? weight : 0;
  s_pui.font = s_font;
}

/* The real class of a widget. Nearly everything here is UI2, but the widgets
 * that live in Home's scene are UI3; properties have the same names on both
 * and must be looked up on the right one. */
MonoClass *cls(MonoObject *object) {
  return object && mono_object_get_class ? mono_object_get_class(object)
                                         : nullptr;
}

const AniketosUI::Pui::Runtime &pui_for(MonoObject *object) {
  MonoClass *klass = cls(object);
  if (!klass || klass == s_pui.label || klass == s_pui.panel ||
      klass == s_pui.image_box)
    return s_pui;
  static AniketosUI::Pui::Runtime other;
  other = s_pui;
  other.label = klass;
  other.panel = klass;
  other.image_box = klass;
  return other;
}

void remove_named(MonoObject *root, const char *name) {
  AniketosUI::Pui::RemoveNamed(s_pui, root, name);
}

void set_color(MonoClass *klass, MonoObject *object, const char *property,
               Color color) {
  AniketosUI::Pui::SetColor(klass, object, property, color);
}

void panel_geometry(MonoObject *panel, float x, float y, float width,
                    float height) {
  AniketosUI::Pui::PanelGeometry(pui_for(panel), panel, x, y, width, height);
}

void panel_color(MonoObject *panel, Color color) {
  if (light_menu_appearance()) {
    if (panel == s_widgets.header || panel == s_widgets.footer) {
      color = {1.0f, 1.0f, 1.0f, 1.0f};
    } else if (panel == s_widgets.body) {
      color = {0.84f, 0.85f, 0.87f, 1.0f};
    } else if (panel == s_widgets.highlight ||
               is_panel_in_slices(panel, s_widgets.highlight_slices,
                                  kGradientSlices)) {
      color = {1.0f, 1.0f, 1.0f, 1.0f};
    }
  }
  AniketosUI::Pui::PanelColor(pui_for(panel), panel, color);
}

void panel_hide(MonoObject *panel) {
  AniketosUI::Pui::PanelHide(pui_for(panel), panel);
}

void label_angle(MonoObject *label, float angle) {
  if (!label || !s_ui_node_class || !s_transform_class)
    return;
  MonoObject *transform =
      Get_Property<MonoObject *>(s_ui_node_class, label, "Transform");
  if (transform)
    Set_Property(s_transform_class, transform, "AngleZ", angle);
}

[[maybe_unused]] MonoObject *retain_panel_transform(MonoObject *panel) {
  if (!panel || !s_ui_node_class || !s_transform_class)
    return nullptr;
  MonoObject *transform =
      Get_Property<MonoObject *>(s_ui_node_class, panel, "Transform");
  if (!transform || !retain_widget(transform))
    return nullptr;

  // Exercise the exact setter/getter pair while the panel is transparent.
  // This is a runtime capability check, not merely a metadata-name check.
  constexpr float kProbeAngle = 0.123f;
  Set_Property(s_transform_class, transform, "AngleZ", kProbeAngle);
  const float observed =
      Get_Property<float>(s_transform_class, transform, "AngleZ");
  Set_Property(s_transform_class, transform, "AngleZ", 0.0f);
  return std::isfinite(observed) &&
                 std::fabs(observed - kProbeAngle) < 0.001f
             ? transform
             : nullptr;
}

[[maybe_unused]] void panel_line(MonoObject *panel, MonoObject *transform, float start_x,
                float start_y, float end_x, float end_y, float thickness) {
  if (!panel || !transform || !s_transform_class)
    return;
  const float dx = end_x - start_x;
  const float dy = end_y - start_y;
  const float length = std::hypot(dx, dy);
  if (!std::isfinite(length) || length < 0.5f) {
    panel_geometry(panel, 0.0f, 0.0f, 0.0f, 0.0f);
    panel_hide(panel);
    return;
  }

  // PUI/Core angles are radians. UI2 rotates a widget around the centre of its
  // bounds by default, so place that centre at the midpoint of the requested
  // segment. Treating the pivot as the top-left corner displaced long lines
  // by roughly half their length after rotation.
  const float angle = std::atan2(dy, dx);
  const float panel_x = (start_x + end_x - length) * 0.5f;
  const float panel_y = (start_y + end_y - thickness) * 0.5f;
  panel_geometry(panel, panel_x, panel_y, length, thickness);
  Set_Property(s_transform_class, transform, "AngleZ", angle);
}

MonoObject *create_panel(MonoObject *parent, const char *name) {
  return AniketosUI::Pui::CreatePanel(s_pui, parent, name);
}

MonoObject *create_label(MonoObject *parent, const char *name, int horizontal,
                         int lines = 1) {
  s_pui.font = s_font;
  return AniketosUI::Pui::CreateLabel(s_pui, parent, name, horizontal, lines);
}

[[maybe_unused]] MonoObject *create_text_outline(Color color) {
  if (!s_text_shadow_class || !s_label_class)
    return nullptr;
  MonoObject *shadow = New_Object(s_text_shadow_class);
  if (!shadow)
    return nullptr;
  mono_runtime_object_init(shadow);
  set_color(s_text_shadow_class, shadow, "Color", color);
  // BorderStyle is a real PUI glyph shader. Zero offsets keep the stroke
  // symmetric around the glyph instead of turning it into a drop shadow.
  Set_Property(s_text_shadow_class, shadow, "HorizontalOffset", 0.0f);
  Set_Property(s_text_shadow_class, shadow, "VerticalOffset", 0.0f);
  Set_Property(s_text_shadow_class, shadow, "Style", 1); // BorderStyle
  return retain_widget(shadow) ? shadow : nullptr;
}

[[maybe_unused]] void apply_text_outline(MonoObject *label, MonoObject *shadow) {
  if (!label || !shadow)
    return;
  Set_Property(cls(label), label, "EnableThemedShadowColor", false);
  Set_Property_Invoke(cls(label), label, "TextShadow", shadow);
}

[[maybe_unused]] float exact_label_text_width(MonoObject *label,
                                               float fallback) {
  if (label && s_pui.image && s_label_class) {
    const float measured =
        Invoke<float>(s_pui.image, cls(label), label, "GetTextWidth");
    if (std::isfinite(measured) && measured > 0.0f &&
        measured < s_viewport_width * 2.0f)
      return measured;
  }
  return std::max(0.0f, fallback);
}

MonoObject *create_system_image(MonoObject *parent, const char *name,
                                const char *uri) {
  return AniketosUI::Pui::CreateSystemImage(s_pui, parent, name, uri);
}

void label_geometry(MonoObject *label, float x, float y, float width,
                    float height) {
  AniketosUI::Pui::LabelGeometry(pui_for(label), label, x, y, width, height);
}

void label_text(MonoObject *label, const std::string &text);

// A submenu marker is rotated in a compact, centred box. Rotating the normal
// right-aligned value label leaves the glyph at the far edge of its wide
// value column, so the transform makes it appear outside the switch.
void value_label_geometry(MonoObject *label, float x, float y, float width,
                          float height, bool submenu) {
  if (!label || !s_label_class)
    return;
  Set_Property(cls(label), label, "Opacity", 1.0f);
  if (submenu) {
    const float size = std::min(width, height);
    Set_Property(cls(label), label, "HorizontalAlignment", 1);
    label_geometry(label, x + width - size, y + (height - size) * 0.5f, size,
                   size);
    label_angle(label, 1.57079632679f);
    return;
  }
  Set_Property(cls(label), label, "VerticalAlignment", 2);
  Set_Property(cls(label), label, "HorizontalAlignment", 2);
  label_angle(label, 0.0f);
  label_geometry(label, x, y, width, height);
}

void selector_value_label_geometry(MonoObject *label, float x, float y,
                                   float width, float height) {
  if (!label || !s_label_class)
    return;
  Set_Property(cls(label), label, "Opacity", 1.0f);
  // Selector text starts immediately after the left marker. Right-aligning
  // it inside the old wide value column was the source of the visible gap.
  Set_Property(cls(label), label, "HorizontalAlignment", 0);
  Set_Property(cls(label), label, "VerticalAlignment", 2);
  label_angle(label, 0.0f);
  label_geometry(label, x, y, width, height);
}

struct SelectorMarkerLayout {
  bool visible = false;
  float value_x = 0.0f;
  float value_width = 0.0f;
};

std::string selector_display_value(const Option &option) {
  if (option.string_count <= 0)
    return option.value;
  const int current = std::clamp(option.string_index, 0,
                                 option.string_count - 1);
  char position[32];
  std::snprintf(position, sizeof(position), " [%d/%d]", current + 1,
                option.string_count);
  // Keep the complete selector in one native PUI2 label.  Separately placed
  // labels have inconsistent text metrics between ShellUI firmware builds,
  // which repeatedly detached the left marker from its value.
  return std::string("< ") + option.value + position + " >";
}

void hide_selector_marker(MonoObject *label) {
  if (!label)
    return;
  label_text(label, "");
  label_geometry(label, 0.0f, 0.0f, 0.0f, 0.0f);
  Set_Property(cls(label), label, "Opacity", 0.0f);
  label_angle(label, 0.0f);
}

void clear_selector_markers(size_t visible_index) {
  if (visible_index >= kMaxVisibleOptions)
    return;
  if (!s_submenu_marker_active[visible_index])
    return;
  hide_selector_marker(s_widgets.selector_left[visible_index]);
  hide_selector_marker(s_widgets.selector_right[visible_index]);
  s_submenu_marker_active[visible_index] = false;
}

SelectorMarkerLayout update_selector_markers(
    size_t visible_index, bool, int, int, float, float, float, float, float,
    Color, Color) {
  // Arrows now belong to selector_display_value(). Do not clear the dedicated
  // submenu marker here: doing so on every scroll frame creates a visible
  // hide/show state between two managed PUI2 setter batches. The row renderer
  // owns marker activation and clears it only when the slot changes type.
  return {};
}

void prepare_submenu_marker_slot(size_t visible_index, bool submenu) {
  if (visible_index >= kMaxVisibleOptions)
    return;
  if (!submenu)
    clear_selector_markers(visible_index);
}

void update_submenu_marker(size_t visible_index, float x, float y, float size,
                           float angle, Color color) {
  if (visible_index >= kMaxVisibleOptions)
    return;
  MonoObject *marker = s_widgets.selector_right[visible_index];
  if (!marker)
    return;
  if (!s_submenu_marker_active[visible_index]) {
    label_text(marker, "∴");
    Set_Property(s_label_class, marker, "HorizontalAlignment", 1);
    Set_Property(s_label_class, marker, "VerticalAlignment", 2);
    Set_Property(s_label_class, marker, "Opacity", 1.0f);
    s_submenu_marker_active[visible_index] = true;
  }
  label_geometry(marker, x, y, size, size);
  label_angle(marker, angle);
  set_color(s_label_class, marker, "TextColor", color);
}

[[maybe_unused]] void label_position(MonoObject *label, float x, float y) {
  if (!label || !s_label_class)
    return;
  Set_Property(cls(label), label, "X", x);
  Set_Property(cls(label), label, "Y", y);
}

void image_geometry(MonoObject *image, float x, float y, float width,
                    float height, bool visible = true) {
  AniketosUI::Pui::ImageGeometry(pui_for(image), image, x, y, width, height, visible);
}

void label_text(MonoObject *label, const std::string &text) {
  AniketosUI::Pui::LabelText(pui_for(label), label, text);
}

void label_font(MonoObject *label) {
  s_pui.font = s_font;
  AniketosUI::Pui::LabelFont(pui_for(label), label);
}

bool same_menu_style(const AniketosUI::MenuStyle &a,
                     const AniketosUI::MenuStyle &b) {
  return std::fabs(a.x - b.x) < 0.0001f &&
         std::fabs(a.y - b.y) < 0.0001f &&
         std::fabs(a.width - b.width) < 0.0001f &&
         std::fabs(a.border - b.border) < 0.0001f &&
         std::fabs(a.text_size - b.text_size) < 0.0001f &&
         std::fabs(a.scale - b.scale) < 0.0001f &&
         std::fabs(a.background_opacity - b.background_opacity) < 0.0001f &&
         a.border_enabled == b.border_enabled &&
         a.dual_color == b.dual_color &&
         a.gradient_direction == b.gradient_direction &&
         a.color_animation == b.color_animation &&
         a.color_animation_speed == b.color_animation_speed &&
         std::fabs(a.left.r - b.left.r) < 0.0001f &&
         std::fabs(a.left.g - b.left.g) < 0.0001f &&
         std::fabs(a.left.b - b.left.b) < 0.0001f &&
         std::fabs(a.right.r - b.right.r) < 0.0001f &&
         std::fabs(a.right.g - b.right.g) < 0.0001f &&
         std::fabs(a.right.b - b.right.b) < 0.0001f;
}

AniketosUI::MenuStyle active_menu_style() {
  const AniketosUI::MenuStyle configured =
      AniketosUI::StyleFromShellSettings(g_settings);
  if (!s_menu_visual_style_initialized ||
      !same_menu_style(s_menu_visual_style, configured)) {
    s_menu_visual_style = configured;
    s_menu_visual_style_initialized = true;
    s_menu_visual_phase_origin_ms = monotonic_millis();
    s_menu_color_frame_valid = false;
    LOG_INFO("[ANIVSH:STATE] global style snapshot page=%s style=%p "
             "theme=%d animation=%d direction=%d "
             "left=%.3f,%.3f,%.3f "
             "right=%.3f,%.3f,%.3f",
             s_stack.empty() ? "<none>" : page_title(s_stack.back().page),
             static_cast<void *>(&s_menu_visual_style),
             g_settings.vsh_menu_theme, configured.color_animation,
             configured.gradient_direction, configured.left.r,
             configured.left.g, configured.left.b, configured.right.r,
             configured.right.g, configured.right.b);
  }
  return s_menu_visual_style;
}

int active_menu_theme() {
  return g_settings.vsh_menu_theme;
}

int active_menu_animation() {
  return g_settings.vsh_animation;
}

int active_menu_font_weight() {
  return g_settings.vsh_font_weight;
}

bool active_menu_color_animation() {
  return active_menu_style().color_animation != 0;
}

bool show_menu_controls() {
  return g_settings.vsh_show_controls;
}

float active_menu_x() { return active_menu_style().x; }
float active_menu_y() { return active_menu_style().y; }

const char *open_control_text() {
  return tr("vsh.ctrl.to_open");
}

const char *close_control_text() {
  return tr("vsh.ctrl.to_close");
}

float menu_text_size() {
  return AniketosUI::TextSize(active_menu_style());
}

float menu_width() {
  return AniketosUI::Width(active_menu_style());
}

float menu_border() {
  return AniketosUI::Border(active_menu_style());
}

Color animated_menu_color(bool right, float alpha) {
  // Global Customization is the one palette owner for the whole ShellUI.
  // Menu, background, clock, focus and Aura must not each animate their own
  // left/right endpoints.  GlobalTheme::Color is sampled from the same
  // quantized frame by every consumer, so both menu edges intentionally use
  // the same live hue when the global palette is enabled.
  if (AniketosTheme::GlobalTheme::Active()) {
    const AniketosTheme::RGB global =
        AniketosTheme::GlobalTheme::Color(
            s_menu_color_frame_valid ? s_global_color_sample_ms
                                     : monotonic_millis());
    return {global.r, global.g, global.b, alpha};
  }
  const uint64_t frame_ms = s_menu_color_frame_valid
                                ? s_menu_color_frame_ms
                                : active_menu_color_frame_ms();
  return AniketosUI::AnimatedColor(active_menu_style(), right, frame_ms,
                                  alpha);
}

uint64_t active_menu_color_frame_ms() {
  (void)active_menu_style();
  const uint64_t now = monotonic_millis();
  return now >= s_menu_visual_phase_origin_ms
             ? now - s_menu_visual_phase_origin_ms
             : 0;
}

Color left_color(float alpha = 1.0f) {
  return animated_menu_color(false, alpha);
}

Color right_color(float alpha = 1.0f) {
  return animated_menu_color(true, alpha);
}

float gradient_t(float t) {
  return AniketosUI::GradientT(active_menu_style(), t);
}

Color mix(Color left, Color right, float t, float alpha) {
  return AniketosUI::Mix(left, right, t, alpha);
}

Color gradient_edge_color(bool right_edge, float alpha = 1.0f) {
  if (AniketosTheme::GlobalTheme::Active())
    return right_color(alpha);
  const uint64_t frame_ms = s_menu_color_frame_valid
                                ? s_menu_color_frame_ms
                                : active_menu_color_frame_ms();
  return AniketosUI::GradientEdgeColor(active_menu_style(), right_edge,
                                      frame_ms, alpha);
}

uint32_t s_previous_selection_key = 0;
/* The current cursor move comes from a held direction (linear glide). */
bool s_highlight_follow = false;
/* Set by HandleInput for the selection change being processed. */
bool s_selection_from_repeat = false;

/* 0..1: how far the cursor has come on its current move. */
[[maybe_unused]] float highlight_progress() {
  /* While a direction is held the accent simply sits on the selected row;
     cross-fading it on every step made the text flicker. */
  if (!s_highlight_started_ms || s_highlight_follow)
    return 1.0f;
  const float span = std::fabs(s_highlight_to - s_highlight_from);
  if (span < 0.5f)
    return 1.0f;
  return std::clamp(1.0f - std::fabs(s_highlight_to - s_highlight_y) / span,
                    0.0f, 1.0f);
}
uint32_t option_selection_key(const Option &option);

/* Index of the option the cursor is moving away from, or options.size(). */
[[maybe_unused]] size_t previous_selection_index(const std::vector<Option> &options) {
  if (!s_previous_selection_key || !s_highlight_started_ms ||
      s_highlight_follow)
    return options.size();
  for (size_t i = 0; i < options.size(); ++i) {
    if (option_selection_key(options[i]) == s_previous_selection_key)
      return i;
  }
  return options.size();
}

void update_highlight_target(const PageState &state, float target_y) {
  const bool identity_changed =
      !s_highlight_initialized || s_highlight_page != state.page ||
      s_highlight_selection_key != state.selected_key ||
      s_highlight_scroll != state.scroll ||
      s_highlight_page_epoch != s_page_focus_epoch;
  const bool position_changed = std::fabs(target_y - s_highlight_to) > 0.1f;

  if (!identity_changed && !position_changed)
    return;

  const uint64_t now = monotonic_millis();
  if (!s_highlight_initialized) {
    // The first frame after opening has no previous visual owner. Snap it
    // directly instead of animating from the origin/top-left corner.
    s_highlight_from = target_y;
    s_highlight_to = target_y;
    s_highlight_y = target_y;
    s_highlight_started_ms = 0;
  } else if (s_highlight_page == state.page &&
             s_highlight_scroll != state.scroll && position_changed) {
    /* The list itself moved under the cursor (wrap-around from the last row
       to the first, or back): the rows changed instantly, so gliding the
       cursor across them would sweep over options that were never selected.
       Place it directly. */
    s_highlight_from = target_y;
    s_highlight_to = target_y;
    s_highlight_y = target_y;
    s_highlight_started_ms = 0;
  } else if (position_changed) {
    /* Every move is a timed glide from wherever the cursor is now.
       - Held direction: linear, lasting exactly one auto-repeat period, so
         consecutive steps join into one constant-speed motion with no pause
         or lurch between rows.
       - Single press: a short eased move. */
    s_highlight_follow = s_selection_from_repeat;
    s_highlight_from = s_highlight_y;
    s_highlight_to = target_y;
    s_highlight_started_ms = now;
  }
  /* Same row, different option (the list scrolled under a cursor parked at
     the edge): nothing to do. In particular do not snap a cursor that is
     still on its way there. */
  /* Which option the cursor is leaving, by identity. Deriving it from the
     cursor's pixel position coloured an unrelated row whenever the list had
     scrolled since. */
  if (s_highlight_selection_key != state.selected_key)
    s_previous_selection_key =
        s_highlight_page == state.page && s_highlight_started_ms != 0
            ? s_highlight_selection_key
            : 0;

  s_highlight_page = state.page;
  s_highlight_selection_key = state.selected_key;
  s_highlight_scroll = state.scroll;
  s_highlight_initialized = true;
  s_highlight_page_epoch = s_page_focus_epoch;
}

void snap_highlight_to_target() {
  s_highlight_from = s_highlight_to;
  s_highlight_y = s_highlight_to;
  s_highlight_started_ms = 0;
  s_highlight_follow = false;
  if (s_widgets.highlight)
    AniketosUI::Pui::PanelY(s_pui, s_widgets.highlight, s_highlight_y);
}

float approximate_text_width(const char *text, float font_size,
                             bool bold = false) {
  if (!text)
    return 0.0f;
  float em = 0.0f;
  const auto *cursor = reinterpret_cast<const unsigned char *>(text);
  while (*cursor) {
    uint32_t character = *cursor++;
    if ((character & 0xe0u) == 0xc0u &&
        (cursor[0] & 0xc0u) == 0x80u) {
      character = ((character & 0x1fu) << 6u) | (cursor[0] & 0x3fu);
      cursor += 1;
    } else if ((character & 0xf0u) == 0xe0u &&
               (cursor[0] & 0xc0u) == 0x80u &&
               (cursor[1] & 0xc0u) == 0x80u) {
      character = ((character & 0x0fu) << 12u) |
                  ((cursor[0] & 0x3fu) << 6u) | (cursor[1] & 0x3fu);
      cursor += 2;
    } else if ((character & 0xf8u) == 0xf0u &&
               (cursor[0] & 0xc0u) == 0x80u &&
               (cursor[1] & 0xc0u) == 0x80u &&
               (cursor[2] & 0xc0u) == 0x80u) {
      character = ((character & 0x07u) << 18u) |
                  ((cursor[0] & 0x3fu) << 12u) |
                  ((cursor[1] & 0x3fu) << 6u) | (cursor[2] & 0x3fu);
      cursor += 3;
    }

    // Combining accents do not advance the cursor. East-Asian glyphs and
    // emoji are full-width; accented Latin/Cyrillic/Arabic glyphs advance
    // once, rather than once for every byte of their UTF-8 encoding.
    const bool combining =
        (character >= 0x0300u && character <= 0x036fu) ||
        (character >= 0x1ab0u && character <= 0x1affu) ||
        (character >= 0x1dc0u && character <= 0x1dffu) ||
        (character >= 0x20d0u && character <= 0x20ffu) ||
        (character >= 0xfe20u && character <= 0xfe2fu);
    if (combining)
      continue;
    const bool full_width =
        (character >= 0x1100u && character <= 0x115fu) ||
        (character >= 0x2e80u && character <= 0xa4cfu) ||
        (character >= 0xac00u && character <= 0xd7a3u) ||
        (character >= 0xf900u && character <= 0xfaffu) ||
        (character >= 0xfe10u && character <= 0xfe6fu) ||
        (character >= 0xff01u && character <= 0xff60u) ||
        (character >= 0xffe0u && character <= 0xffe6u) ||
        (character >= 0x1f300u && character <= 0x1faffu) ||
        (character >= 0x20000u && character <= 0x3fffdu);
    if (full_width) {
      em += 1.0f;
    } else if (character == ' ')
      em += 0.30f;
    else if (character < 0x80u &&
             std::strchr("ilI.,:;'!|", static_cast<int>(character)))
      em += 0.29f;
    else if (character < 0x80u &&
             std::strchr("MW@", static_cast<int>(character)))
      em += 0.88f;
    else if (character < 0x80u &&
             std::strchr("mw", static_cast<int>(character)))
      em += 0.76f;
    else if (character >= 'A' && character <= 'Z')
      em += 0.64f;
    else if (character >= '0' && character <= '9')
      em += 0.56f;
    else if (character == 0x2605u)
      em += 0.72f;
    else
      em += 0.52f;
  }
  return em * font_size * (bold ? 1.04f : 1.0f);
}

// GetTextWidth reports the label's current layout box on some PUI2 builds,
// not only the painted glyph width.  It therefore cannot be used to place a
// selector group: a stale/wide box moves the left marker far away from the
// value while the value itself remains short.  The UTF-8-aware approximation
// above is stable across firmware and is the correct width for this layout.
float selector_painted_text_width(const char *text, float font_size) {
  return std::max(16.0f, approximate_text_width(text, font_size));
}

// Menu strings live in the shared toolbox catalog under the vsh.* namespace,
// so the VSH frontend follows the same UI language as the Toolbox XML.
