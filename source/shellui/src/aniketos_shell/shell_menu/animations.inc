void update_highlight_motion(uint64_t now) {
  bool arrived = false;
  const float elapsed_ms = static_cast<float>(now - s_highlight_started_ms);
  if (s_highlight_follow) {
    /* A little longer than the repeat period: the next step always arrives
       while the cursor is still moving, so it never stops between rows. */
    const float t = std::min(
        1.0f, elapsed_ms / (static_cast<float>(Input::kListRepeatMs) * 1.2f));
    s_highlight_y = s_highlight_from +
                    (s_highlight_to - s_highlight_from) * t;
    arrived = t >= 1.0f;
  } else {
    const float t = std::min(
        1.0f, elapsed_ms /
                  (active_menu_theme() == 5 ? 150.0f : kHighlightDurationMs));
    /* Apparition uses a fixed ease-out; the other skins keep the easing the
       user picked on the Animations page. */
    const float eased = active_menu_theme() == 5
                            ? 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t)
                            : Easing::Apply(active_menu_animation(), t);
    s_highlight_y = s_highlight_from +
                    (s_highlight_to - s_highlight_from) * eased;
    arrived = t >= 1.0f;
  }
  if (arrived) {
    s_highlight_y = s_highlight_to;
    s_highlight_started_ms = 0;
    s_highlight_follow = false;
  }
  AniketosUI::Pui::PanelY(s_pui, s_widgets.highlight, s_highlight_y);
  if (Profile::enabled)
    LOG_INFO("[SHELLMENU:PROFILE] cursor t=%llu y=%.1f to=%.1f hold=%d",
             static_cast<unsigned long long>(now), s_highlight_y,
             s_highlight_to, s_highlight_follow ? 1 : 0);
  /* The title/value accent follows the cursor; repaint colours while it
     travels and once more when it lands. */
  if (active_menu_theme() == 5)
    s_color_dirty = true;
}

void update_animations() {
  if (!s_widgets.master)
    return;
  const uint64_t now = MonotonicMs();
  const bool apparition = active_menu_theme() == 5;
  const bool anomaly = active_menu_theme() == 2;
  /* Akari: 200 ms for the frame to fade in and the body to grow, then
     200 ms for the text; closing the other way round */
  const bool akari = akari_theme();
  constexpr float kAkariPhaseMs = 200.0f;
  const bool opening = s_animation == AnimationState::Opening;
  const bool closing = s_animation == AnimationState::Closing;
  const float animation_elapsed =
      static_cast<float>(now - s_animation_started_ms);
  float menu_reveal = 1.0f;
  if (opening || closing) {
    const float duration =
        apparition ? (opening ? kApparitionOpenDurationMs
                              : kApparitionCloseDurationMs)
        : anomaly  ? kAnomalySlideMs
        : akari    ? kAkariPhaseMs * 2.0f
        : opening  ? kOpenDurationMs
                   : kCloseDurationMs;
    const float t = std::min(1.0f, animation_elapsed / duration);
    const float eased = Easing::Apply(active_menu_animation(), t);
    menu_reveal = opening ? eased : 1.0f - eased;
    if (t >= 1.0f) {
      s_animation = opening ? AnimationState::Visible : AnimationState::Hidden;
      if (closing)
        menu_reveal = 0.0f;
    }
  } else if (s_animation == AnimationState::Hidden) {
    menu_reveal = 0.0f;
  }
  menu_reveal = std::clamp(menu_reveal, 0.0f, 1.0f);
  if (akari) {
    float body = 1.0f, text = 1.0f, frame = menu_reveal;
    const float first = std::clamp(animation_elapsed / kAkariPhaseMs, 0.0f, 1.0f);
    const float second = std::clamp(
        (animation_elapsed - kAkariPhaseMs) / kAkariPhaseMs, 0.0f, 1.0f);
    if (s_animation == AnimationState::Opening) {
      body = Easing::Apply(active_menu_animation(), first);
      text = second;
      frame = first;
    } else if (s_animation == AnimationState::Closing) {
      text = 1.0f - first;
      body = 1.0f - Easing::Apply(active_menu_animation(), second);
      frame = 1.0f - second;
    }
    menu_reveal = frame;
    if (body != s_akari_body_reveal || text != s_akari_text_reveal) {
      s_akari_body_reveal = body;
      s_akari_text_reveal = text;
      s_dirty = true;
    }
  } else {
    s_akari_body_reveal = 1.0f;
    s_akari_text_reveal = 1.0f;
  }
  const bool page_transition_was_active = page_transition_active();
  const float page_reveal = update_page_transition(now);
  if (apparition && (opening || closing)) {
    update_apparition_open_close_animation(menu_reveal);
  } else if (anomaly && (s_animation == AnimationState::Opening ||
                         s_animation == AnimationState::Closing)) {
    update_anomaly_open_close(animation_elapsed, opening);
  } else {
    // Non-Apparition themes keep a fixed geometry. The old centre-growing clip
    // changed the master width/height during every open and page repaint, which
    // made the menu overlay visibly jump and scale. Apparition owns its own
    // dedicated reveal animation above; the other themes only fade.
    panel_geometry(s_widgets.master, s_menu_target_x, s_menu_target_y,
                   s_menu_target_width, s_menu_target_height);
    panel_geometry(s_widgets.content, 0.0f, 0.0f,
                   s_menu_target_width, s_menu_target_height);
    Set_Property(s_panel_class, s_widgets.master, "Opacity", menu_reveal);
    if (s_widgets.anomaly_controls) {
      Set_Property(s_panel_class, s_widgets.anomaly_controls, "Opacity",
                   menu_reveal);
      if (anomaly)
        AniketosUI::Pui::PanelY(s_pui, s_widgets.anomaly_controls,
                                s_anomaly_controls_y);
    }
  }

  if (page_transition_was_active || page_transition_active() ||
      page_reveal < 1.0f)
    apply_page_transition_reveal(page_reveal);

  if (s_highlight_started_ms && s_widgets.highlight)
    update_highlight_motion(now);
}
