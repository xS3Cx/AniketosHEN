bool page_transition_active() {
  return s_page_transition != PageTransitionState::Idle;
}

// Geometry is shared by UI2.Panel and UI2.Label, but the managed property is
// declared on the concrete class (or one of its parents). Reading a label
// through UI2.Panel returns zero on firmwares that do not expose the inherited
// property through that unrelated class. That zero rectangle was the source
// of the disappearing footer and the detached transition text.
MonoClass *resolve_geometry_class(MonoObject *object) {
  if (!object || !mono_object_get_class ||
      !mono_class_get_property_from_name || !mono_class_get_parent)
    return nullptr;
  for (MonoClass *klass = mono_object_get_class(object); klass;
       klass = mono_class_get_parent(klass)) {
    if (mono_class_get_property_from_name(klass, "X"))
      return klass;
  }
  return nullptr;
}

/* A page change reads the geometry of dozens of pooled widgets in one frame.
 * They are instances of three classes, so resolve the declaring class once per
 * class instead of walking the hierarchy by name for every widget. */
MonoClass *cached_geometry_class(MonoObject *object) {
  struct Entry {
    MonoClass *object_class;
    MonoClass *declaring_class;
  };
  static Entry cache[8];
  MonoClass *object_class = cls(object);
  if (!object_class)
    return nullptr;
  for (Entry &entry : cache) {
    if (entry.object_class == object_class)
      return entry.declaring_class;
    if (!entry.object_class) {
      MonoClass *declaring = resolve_geometry_class(object);
      if (declaring)
        entry = {object_class, declaring};
      return declaring;
    }
  }
  return resolve_geometry_class(object);
}

PageTransitionRect read_page_transition_rect(MonoObject *object) {
  MonoClass *klass = cached_geometry_class(object);
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
                                             float t) {
  return {from.x + (to.x - from.x) * t,
          from.y + (to.y - from.y) * t,
          from.width + (to.width - from.width) * t,
          from.height + (to.height - from.height) * t};
}

void read_page_transition_geometry(PageTransitionGeometry &geometry) {
  geometry.master = read_page_transition_rect(s_widgets.master);
  geometry.content = read_page_transition_rect(s_widgets.content);
  geometry.header = read_page_transition_rect(s_widgets.header);
  geometry.body = read_page_transition_rect(s_widgets.body);
  geometry.scroll_clip = read_page_transition_rect(s_widgets.scroll_clip);
  geometry.footer = read_page_transition_rect(s_widgets.footer);
  geometry.page_title = read_page_transition_rect(s_widgets.page_title);
  for (int i = 0; i < kFrameGradientSlices; ++i) {
    geometry.theme_header_slice[i] =
        read_page_transition_rect(s_widgets.theme_header_slices[i]);
    geometry.theme_footer_slice[i] =
        read_page_transition_rect(s_widgets.theme_footer_slices[i]);
  }
}

void capture_page_transition_target_geometry() {
  if (!s_page_transition_start_geometry.valid)
    return;
  s_page_transition_target_geometry.valid = true;
  read_page_transition_geometry(s_page_transition_target_geometry);
}

void place_panel(MonoObject *panel, const PageTransitionRect &rect) {
  panel_geometry(panel, rect.x, rect.y, rect.width, rect.height);
}

void apply_page_transition_vertical(MonoObject *object, float y,
                                    float height) {
  if (!object || !s_panel_class)
    return;
  AniketosUI::Pui::PanelYHeight(s_pui, object, y, height);
}

void reset_highlight_motion() {
  s_highlight_started_ms = 0;
  s_highlight_from = 0.0f;
  s_highlight_to = 0.0f;
  s_highlight_y = 0.0f;
  s_highlight_page = Page::Main;
  s_highlight_selection_key = 0;
  s_highlight_scroll = 0;
  s_highlight_initialized = false;
}

void step_page_stack(PageTransitionAction action, Page target) {
  if (action == PageTransitionAction::Push) {
    PageState next{};
    next.page = target;
    s_stack.push_back(next);
  } else {
    s_stack.pop_back();
  }
}

const char *selected_title(const std::vector<Option> &options,
                           const PageState &state) {
  return state.selected < options.size()
             ? options[state.selected].title.c_str()
             : "<none>";
}

void reset_page_transition() {
  s_page_transition = PageTransitionState::Idle;
  s_page_transition_started_ms = 0;
  s_page_transition_start_geometry = {};
  s_page_transition_target_geometry = {};
}

void switch_page_at_once(PageTransitionAction action, Page target) {
  step_page_stack(action, target);
  /* a new page starts at its first row; going back finds the row it was
     left from, as on the PS3 menus */
  if (!s_stack.empty() && action == PageTransitionAction::Push)
    reset_page_selection(s_stack.back());
  reset_page_transition();
  update_menu_widgets();
  snap_highlight_to_target();
  s_highlight_page_epoch = s_page_focus_epoch;
  const PageState &entered = s_stack.back();
  const std::vector<Option> entered_options = build_page(entered.page);
  LOG_INFO("[ANIVSH:STATE] transition_commit page=%s selected=%zu/%zu "
           "selected_id=%d item=%s scroll=%zu focus_epoch=%lu "
           "highlight=%.2f",
           page_title(entered.page), entered.selected, entered_options.size(),
           static_cast<int>(entered.selected_id),
           selected_title(entered_options, entered), entered.scroll,
           static_cast<unsigned long>(s_page_focus_epoch), s_highlight_to);
  s_dirty = true;
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
  LOG_INFO("[ANIVSH:STATE] transition_begin action=%s from=%s "
           "selected=%zu/%zu selected_id=%d item=%s scroll=%zu theme=%d",
           action == PageTransitionAction::Push ? "push" : "pop",
           page_title(outgoing.page), outgoing.selected,
           outgoing_options.size(), static_cast<int>(outgoing.selected_id),
           selected_title(outgoing_options, outgoing), outgoing.scroll,
           active_menu_theme());
  ++s_page_focus_epoch;
  // The expanding UIScrollView-style transition is an Apparition-specific
  // composition. The other themes historically switch pages immediately and
  // must keep their own native layout/paint timing unchanged.
  if (active_menu_theme() != 5) {
    switch_page_at_once(action, target);
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
    read_page_transition_geometry(s_page_transition_start_geometry);
    s_page_transition_start_height =
        std::max(1.0f, s_page_transition_start_geometry.master.height);
  } else {
    s_page_transition_start_height = std::max(1.0f, s_menu_target_height);
  }
  const uint64_t profile_t1 = profile_t0 ? Profile::NowNs() : 0;
  step_page_stack(action, target);
  if (!s_stack.empty())
    reset_page_selection(s_stack.back());
  // A page switch must be a single UI hand-off. Build the destination now,
  // while the old rectangles are still retained above, so no render can show
  // the old value pool with the new row labels (or vice versa). This is also
  // important on ShellUI builds where managed property setters are observed
  // in the same frame rather than deferred to the next tick.
  reset_highlight_motion();
  update_menu_widgets();
  const uint64_t profile_t2 = profile_t0 ? Profile::NowNs() : 0;
  // A page hand-off selects the destination page's first valid row. The
  // structural layout above computes that exact destination, so do not replay
  // the normal Up/Down cursor animation from y=0 while the viewport expands;
  // that made the highlight visibly land on a different switch on exit.
  snap_highlight_to_target();
  s_apparition_force_color_refresh = true;
  s_color_dirty = true;
  s_highlight_page_epoch = s_page_focus_epoch;
  const PageState &entered = s_stack.back();
  const std::vector<Option> entered_options = build_page(entered.page);
  LOG_INFO("[ANIVSH:STATE] transition_commit page=%s selected=%zu/%zu "
           "selected_id=%d item=%s scroll=%zu focus_epoch=%lu "
           "highlight=%.2f color_frame=%lu",
           page_title(entered.page), entered.selected, entered_options.size(),
           static_cast<int>(entered.selected_id),
           selected_title(entered_options, entered), entered.scroll,
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
  s_page_transition_started_ms = MonotonicMs();
  // The destination labels were committed above and remain live throughout
  // the clip expansion.  Do not schedule a second complete PUI2 relayout on
  // the next tick; that duplicate pooled-label rewrite was another source of
  // intermittent disappearing text.
  s_dirty = false;
}

float update_page_transition(uint64_t now) {
  if (s_page_transition == PageTransitionState::Idle)
    return 1.0f;

  const float t = std::min(
      1.0f, static_cast<float>(now - s_page_transition_started_ms) /
                kPageTransitionDurationMs);
  if (t >= 1.0f) {
    s_page_transition = PageTransitionState::Idle;
    return 1.0f;
  }
  // Page resizing must stay monotonic even when the user selected a Back or
  // other overshooting menu easing for open/close animations. UIKit's submenu
  // resize is a calm in-out curve, so keep that curve local to page changes.
  return Easing::Apply(/*In-out Sine=*/3, t);
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
    const PageTransitionGeometry &from = s_page_transition_start_geometry;
    const PageTransitionGeometry &to = s_page_transition_target_geometry;
    place_panel(s_widgets.master,
                lerp_page_transition_rect(from.master, to.master, progress));
    place_panel(s_widgets.content,
                lerp_page_transition_rect(from.content, to.content, progress));
    place_panel(s_widgets.header,
                lerp_page_transition_rect(from.header, to.header, progress));
    const PageTransitionRect body_current =
        lerp_page_transition_rect(from.body, to.body, progress);
    place_panel(s_widgets.body, body_current);
    place_panel(s_widgets.scroll_clip,
                lerp_page_transition_rect(from.scroll_clip, to.scroll_clip,
                                          progress));
    const PageTransitionRect footer_current =
        lerp_page_transition_rect(from.footer, to.footer, progress);
    place_panel(s_widgets.footer, footer_current);
    const PageTransitionRect page_title_current =
        lerp_page_transition_rect(from.page_title, to.page_title, progress);
    label_geometry(s_widgets.page_title, page_title_current.x,
                   page_title_current.y, page_title_current.width,
                   page_title_current.height);
    for (int i = 0; i < kFrameGradientSlices; ++i) {
      const PageTransitionRect header_slice = lerp_page_transition_rect(
          from.theme_header_slice[i], to.theme_header_slice[i], progress);
      const PageTransitionRect footer_slice = lerp_page_transition_rect(
          from.theme_footer_slice[i], to.theme_footer_slice[i], progress);
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
