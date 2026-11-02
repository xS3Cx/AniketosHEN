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
    s_menu_visual_phase_origin_ms = MonotonicMs();
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

/* Akari opening and closing (update_animations): how far the body has grown
   from its middle and how far the text has faded in, 0..1. */
float s_akari_body_reveal = 1.0f;
float s_akari_text_reveal = 1.0f;

bool anomaly_theme() { return active_menu_theme() == 2; }
bool apparition_theme() { return active_menu_theme() == 5; }

/* The Akari look (theme 0; the retired ids 1, 3 and 4 fall back to it). */
bool akari_theme() {
  return !anomaly_theme() && !apparition_theme() &&
         active_menu_theme() != kThemeSweety;
}

/* While the global theme colours the console, Akari keeps its own frame:
   yellow on the left fading to cyan on the right, as on the PS3. */
bool akari_own_palette() {
  return akari_theme() && AniketosTheme::GlobalTheme::Active();
}

/* Anomaly likewise keeps its purple (#5933F2; orange #F25933 on the left
   only with two colours), one colour as it ships. */
bool anomaly_own_palette() {
  return anomaly_theme() && AniketosTheme::GlobalTheme::Active();
}

/* The two PS3 looks draw in the right colour when there is one colour. */
bool ps3_theme() { return akari_theme() || anomaly_theme(); }

bool menu_dual_color() {
  if (akari_own_palette())
    return true;
  if (anomaly_own_palette())
    return false;
  return active_menu_style().dual_color;
}

uint64_t active_menu_color_frame_ms() {
  (void)active_menu_style();
  const uint64_t now = MonotonicMs();
  return now >= s_menu_visual_phase_origin_ms
             ? now - s_menu_visual_phase_origin_ms
             : 0;
}

/* The frame's sampled time while there is one, so all of a frame matches. */
uint64_t sampled_color_frame_ms() {
  return s_menu_color_frame_valid ? s_menu_color_frame_ms
                                  : active_menu_color_frame_ms();
}

Color animated_menu_color(bool right, float alpha) {
  if (akari_own_palette())
    return right ? Color{0.0f, 1.0f, 1.0f, alpha}
                 : Color{1.0f, 1.0f, 0.0f, alpha};
  if (anomaly_own_palette())
    return right ? Color{0.35f, 0.20f, 0.95f, alpha}
                 : Color{0.95f, 0.35f, 0.20f, alpha};
  // Global Customization is the one palette owner for the whole ShellUI.
  // Menu, background, clock, focus and Aura must not each animate their own
  // left/right endpoints.  GlobalTheme::Color is sampled from the same
  // quantized frame by every consumer, so both menu edges intentionally use
  // the same live hue when the global palette is enabled.
  if (AniketosTheme::GlobalTheme::Active()) {
    const AniketosTheme::RGB global =
        AniketosTheme::GlobalTheme::Color(
            s_menu_color_frame_valid ? s_global_color_sample_ms
                                     : MonotonicMs());
    return {global.r, global.g, global.b, alpha};
  }
  return AniketosUI::AnimatedColor(active_menu_style(), right,
                                   sampled_color_frame_ms(), alpha);
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

/* One of `count` equal slices across `width`, and where its middle falls on
   the gradient. Both edges come from the index: a +1 px overlap doubled the
   alpha at every seam, which showed as banding. */
struct GradientSlice {
  float x;
  float width;
  float t;
};

GradientSlice gradient_slice(float width, int index, int count) {
  const float divisions = static_cast<float>(count);
  const float x0 = width * static_cast<float>(index) / divisions;
  const float x1 = width * static_cast<float>(index + 1) / divisions;
  return {x0, x1 - x0, (static_cast<float>(index) + 0.5f) / divisions};
}

Color gradient_edge_color(bool right_edge, float alpha = 1.0f) {
  if (akari_own_palette())
    return animated_menu_color(right_edge, alpha);
  /* one colour: Akari and Anomaly draw everything in the right one */
  if (ps3_theme() && !menu_dual_color())
    return right_color(alpha);
  if (AniketosTheme::GlobalTheme::Active())
    return right_color(alpha);
  return AniketosUI::GradientEdgeColor(active_menu_style(), right_edge,
                                       sampled_color_frame_ms(), alpha);
}

/* The current cursor move comes from a held direction (linear glide). */
bool s_highlight_follow = false;
/* Set by HandleInput for the selection change being processed. */
bool s_selection_from_repeat = false;

void update_highlight_target(const PageState &state, float target_y) {
  const bool identity_changed =
      !s_highlight_initialized || s_highlight_page != state.page ||
      s_highlight_selection_key != state.selected_key ||
      s_highlight_scroll != state.scroll ||
      s_highlight_page_epoch != s_page_focus_epoch;
  const bool position_changed = std::fabs(target_y - s_highlight_to) > 0.1f;

  if (!identity_changed && !position_changed)
    return;

  /* The first frame after opening has no previous visual owner, and when the
     list itself moved under the cursor (wrap-around from the last row to the
     first, or back) the rows changed instantly, so gliding the cursor across
     them would sweep over options that were never selected: place it
     directly. */
  if (!s_highlight_initialized ||
      (s_highlight_page == state.page &&
       s_highlight_scroll != state.scroll && position_changed)) {
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
    s_highlight_started_ms = MonotonicMs();
  }
  /* Same row, different option (the list scrolled under a cursor parked at
     the edge): nothing to do. In particular do not snap a cursor that is
     still on its way there. */

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
