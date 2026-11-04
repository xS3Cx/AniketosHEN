/*
 * The AniketosHEN Shell Menu (L1+R3 on Home).
 *
 * One page model and input, drawn in one of four looks: Akari, Anomaly,
 * Apparition (shell_menu/view.inc and the *_theme.inc files there) or
 * Sweety, the console's own pop-up menu. The Akari and Anomaly looks follow
 * the PS3 VSH menus of those names (Akari: kernaltrap8/Akari, d264e8b); this
 * is a PS5/PUI reimplementation, and no PS3 PAF objects, syscall numbers,
 * offsets or platform assumptions are copied.
 */

#include <onion/distribution.h>
#include "aniketos_shell/shell_menu/shell_menu.hpp"
#include "aniketos_shell/system_ui/account_avatar.hpp"
#include "aniketos_shell/system_ui/devkit_overlay.hpp"
#include "aniketos_shell/system_ui/native_menu.hpp"
#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "aniketos_shell/core/embedded_assets.hpp"
#include "aniketos_shell/rn_scripts/rn_reload.hpp"
#include "aniketos_shell/home/home_tabs_store.hpp"
#include "aniketos_shell/home/home_particles.hpp"
#include "aniketos_shell/home/hidden_apps.hpp"
#include "aniketos_shell/home/home_tabs.hpp"
#include "aniketos_shell/home/title_info.hpp"

#include "external_symbols.hpp"
#include "aniketos_shell/hardware/fan_control.hpp"
#include "hooked_funcs.hpp"
#include "homeui_top_nav_patch.hpp"
#include "ipc.hpp"
#include "aniketos_shell/core/service_available.hpp"
#include "aniketos_theme/clock/shell_clock.hpp"
#include "aniketos_theme/color.hpp"
#include "aniketos_theme/custom_font.hpp"
#include "aniketos_theme/icon_color.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/shell_background.hpp"
#include "aniketos_theme/theme_config.hpp"
#include "aniketos_theme/theme_manager.hpp"
#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/menu_style.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "aniketos_ui/pui_primitives.hpp"
#include "aniketos_ui/small_file.hpp"
#include "aniketos_shell/shell_menu/easing.hpp"
#include "aniketos_shell/core/frame_profile.hpp"
#include "aniketos_shell/input/pad_reader.hpp"
#include "aniketos_theme/toggle_styles.hpp"
#include "aniketos_shell/home/home_video.hpp"
#include "aniketos_shell/hardware/shell_buzzer.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_shell/hardware/power_indicator.hpp"
#include "aniketos_shell/hardware/telemetry.hpp"
#include "toolbox_i18n.hpp"
#include "aniketos_shell/controller/controller_tools.hpp"

#include <onion/platform.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <vector>

/* Per-frame code: resolve each managed property once and reuse the compiled
 * accessor (see aniketos_ui/mono_property_cache.hpp) instead of looking it up by name on
 * every call. Same call syntax and semantics as the hooked_funcs.hpp helpers. */
#include "aniketos_ui/mono_property_cache.hpp"
#define Get_Property AniketosUI::MonoPropertyCache::Get
#define Set_Property AniketosUI::MonoPropertyCache::Set
#define Set_Property_Invoke AniketosUI::MonoPropertyCache::SetInvoke

namespace AniketosShell::ShellMenu {
namespace {

// One translation unit on purpose: the partitions share Mono/PUI handles whose
// initialization order and lifetime are runtime-sensitive.
#include "state.inc"
#include "background_catalog.inc"
#include "ui_primitives.inc"
#include "page_model.inc"
#include "view.inc"
#include "settings_actions.inc"
#include "sweety_native.inc"

namespace {

void release_bound_host(bool remove_managed_nodes) {
  using AniketosUI::Reflect::Release;
  // Calling RemoveNamed/RemoveFromParent on Scene.Game after BigApp teardown
  // is unsafe: ShellUI may still expose the old native pointer while its
  // managed tree is already being destroyed.  In that transition the scene
  // itself owns destruction; we only release our GC handles and native cache.
  if (remove_managed_nodes && s_host) {
    for (const char *name : {kMenuName, kOverlayName, kControlsName,
                             kAnomalyControlsName, kClockDateName})
      remove_named(s_host, name);
    if (s_clock_layer && !AniketosUI::HomeScene::IsDisposed(s_clock_layer))
      for (MonoObject *widget : {s_widgets.overlay, s_widgets.clock_date,
                                 s_widgets.controls})
        AniketosUI::Pui::Remove(s_pui, widget);
  }
  s_clock_layer = nullptr;
  s_widgets = {};
  release_widgets();
  Release(s_font_handle);
  s_font = nullptr;
  s_font_size = 0;
  s_font_weight = 0;
  Release(s_control_font_handle);
  Release(s_anomaly_title_font_handle);
  Release(s_anomaly_footer_font_handle);
  Release(s_clock_date_font_handle);
  Release(s_overlay_font_handle);
  s_control_font = nullptr;
  s_anomaly_title_font = nullptr;
  s_anomaly_footer_font = nullptr;
  s_clock_date_font = nullptr;
  s_clock_date_font_size = 0;
  s_overlay_font = nullptr;
  s_overlay_font_size = 0;
  s_menu_color_frame_ms = 0;
  s_menu_color_frame_valid = false;
  s_last_menu_animation_frame_ms = 0;
  s_auxiliary_color_only_update = false;
  s_apparition_force_color_refresh = false;
  Release(s_host_handle);
  s_host = nullptr;
  s_attached = false;
  s_animation = AnimationState::Hidden;
  s_page_transition = PageTransitionState::Idle;
  s_page_transition_started_ms = 0;
  // The menu closes here without Hide() and Tick stops until the next host:
  // nothing of this open may outlive it.
  s_moving = false;
  s_armed_option = OptionId::None;
  s_show_serial = false;
  Telemetry::SetLiveView(false);
  s_dirty = true;
  s_color_dirty = true;
  s_overlay_dirty = true;
}

// All colour consumers of a frame use the same page-local timestamp, so a page
// transition cannot inherit an absolute clock phase from another page.
void sample_menu_color_clock(uint64_t now) {
  s_menu_color_frame_ms = active_menu_color_frame_ms();
  s_global_color_sample_ms = now;
  s_menu_color_frame_valid = true;
}

void clamp_menu_position() {
  g_settings.vsh_menu_x = std::clamp(g_settings.vsh_menu_x, 0.0f,
                                     s_viewport_width);
  g_settings.vsh_menu_y = std::clamp(g_settings.vsh_menu_y, 0.0f,
                                     s_viewport_height);
}

/* The style was changed with the menu open: over to the other renderer. */
void follow_renderer_choice() {
  if (SweetyNative::s_active && active_menu_theme() != kThemeSweety) {
    SweetyNative::Hide();
    Show();
  } else if (IsVisible() && SweetyNative::Wanted()) {
    Hide();
    SweetyNative::Show();
  }
}

void poll_confirmation() {
  if (s_confirming == OptionId::None)
    return;
  int button = -1;
  if (!SystemDialogs::PollAlert(&button))
    return;
  const bool agreed = button == 0; /* only on the "show" button */
  if (s_confirming == OptionId::RevealSerialNumber) {
    s_show_serial = agreed;
  } else if (s_confirming == OptionId::DevkitOverlayInfo && agreed) {
    g_settings.devkit_overlay_info = true;
    request_save();
    DevkitOverlay::Refresh();
  }
  s_confirming = OptionId::None;
  Show();
  s_dirty = true;
}

void leave_deleted_tab() {
  if (s_stack.empty() || s_stack.back().page != Page::HomeTabDetail)
    return;
  if (s_home_tab_detail.kind != HomeTabDetailKind::Custom)
    return;
  if (s_animation != AnimationState::Visible || page_transition_active())
    return;
  if (HomeTabsStore::SlotUsed(s_home_tab_detail.slot))
    return;
  begin_page_transition(PageTransitionAction::Pop);
}

void move_menu_with_stick(uint64_t now) {
  if (!s_moving || !IsVisible()) {
    s_last_move_tick_ms = 0;
    return;
  }
  float stick_x = 0.0f;
  float stick_y = 0.0f;
  Input::ReadLeftStick(stick_x, stick_y);
  if (!s_last_move_tick_ms)
    s_last_move_tick_ms = now;
  const float delta_seconds = std::min(0.05f,
      static_cast<float>(now - s_last_move_tick_ms) / 1000.0f);
  s_last_move_tick_ms = now;
  if (std::fabs(stick_x) > 0.001f || std::fabs(stick_y) > 0.001f) {
    constexpr float kMoveSpeed = 720.0f;
    g_settings.vsh_menu_x += stick_x * kMoveSpeed * delta_seconds;
    g_settings.vsh_menu_y += stick_y * kMoveSpeed * delta_seconds;
    clamp_menu_position();
    s_dirty = true;
  }
}

/* The rows as last painted, against the rows a page would be built with. */
bool same_rows(const std::vector<Option> &built,
               const std::vector<Option> &painted) {
  if (built.size() != painted.size())
    return false;
  for (size_t i = 0; i < built.size(); ++i) {
    if (built[i].type != painted[i].type ||
        built[i].string_index != painted[i].string_index ||
        built[i].title != painted[i].title ||
        built[i].value != painted[i].value)
      return false;
  }
  return true;
}

/* Re-assert every widget value now and then while the menu is on screen,
   so anything PUI changed behind the write-skipping shadow (a skin
   re-applied to a label, for instance) cannot stay wrong. A full repaint
   (hundreds of setters): every 20 s, not 5. */
void reassert_widgets(uint64_t now) {
  static uint64_t s_last_reassert_ms = 0;
  if (IsVisible() && now - s_last_reassert_ms >= 20000 &&
      s_page_transition == PageTransitionState::Idle) {
    s_last_reassert_ms = now;
    forget_written_values();
    s_dirty = true;
    s_color_dirty = true;
  }
}

// Every look keeps its text and managed label objects stable while the menu
// is visible.  The animated palette is applied by a colour-only pass after
// the geometry/highlight animation, never by rebuilding the pooled labels.
// That separation keeps the text attached to its row while still allowing
// the selected row, submenu marker and gradient borders to animate.
void pace_color_animation(uint64_t now) {
  if (!IsVisible() || !active_menu_color_animation()) {
    s_last_menu_animation_frame_ms = 0;
    return;
  }
  // Keep colour motion independent from layout invalidation.  16 ms is the
  // smallest practical cadence on ShellUI while avoiding duplicate managed
  // setter work when the host calls Tick more than once per display frame.
  if (!s_last_menu_animation_frame_ms ||
      now - s_last_menu_animation_frame_ms >= 16) {
    s_last_menu_animation_frame_ms = now;
    s_color_dirty = true;
  }
}

void repaint_menu() {
  // A real page/settings change owns the complete widget tree.  Once an
  // Apparition page hand-off has committed its destination, refuse any
  // late dirty request until the clip animation is over; a second managed
  // label rewrite in that interval is enough to expose an empty glyph cache
  // for one frame.
  if (active_menu_theme() == kThemeApparition && page_transition_active()) {
    s_dirty = false;
    return;
  }
  s_auxiliary_color_only_update = false;
  {
    Profile::Scope scope(Profile::kMenuWidgets);
    update_menu_widgets();
  }
  s_auxiliary_color_only_update = false;
  s_color_dirty = false;
}

void handle_move_input(uint32_t buttons_down) {
  if (buttons_down & Left) g_settings.vsh_menu_x -= 10.0f;
  if (buttons_down & Right) g_settings.vsh_menu_x += 10.0f;
  if (buttons_down & Up) g_settings.vsh_menu_y -= 10.0f;
  if (buttons_down & Down) g_settings.vsh_menu_y += 10.0f;
  clamp_menu_position();
  if (buttons_down & (Cross | Circle)) {
    s_moving = false;
    s_last_move_tick_ms = 0;
    request_save();
  }
  s_dirty = true;
}

} // namespace

void Initialize() {
  ensure_background_directory();
  /* The older background-image option is no longer offered; Home's
     background picture is HomeVideo's now. Left on, nothing could turn it
     off. */
  g_settings.shell_background_image_enabled = false;
  (void)AniketosUI::Pui::Resolve(s_pui, pui_img, retain_pui_widget, nullptr);
  s_widget_class = s_pui.widget;
  s_panel_class = s_pui.panel;
  s_label_class = s_pui.label;
  s_image_box_class = s_pui.image_box;
  s_pui_home = s_pui;
  if (pui_img && mono_class_from_name) {
    constexpr const char *kUI3 = "Sce.PlayStation.PUI.UI3";
    s_pui_home.widget = mono_class_from_name(pui_img, kUI3, "Widget");
    s_pui_home.panel = mono_class_from_name(pui_img, kUI3, "Panel");
    s_pui_home.label = mono_class_from_name(pui_img, kUI3, "Label");
    s_pui_home.image_box = mono_class_from_name(pui_img, kUI3, "ImageBox");
    s_ui_node_class =
        mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UINode");
    s_transform_class =
        mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "Transform");
  }
  Telemetry::SetRefreshSeconds(g_settings.overlay_refresh_seconds);
  LOG_INFO("[ANIVSH:MENU] frontend initialized "
           "branding=" ONION_DIST_NAME " native_glyphs=%d "
           "glyph_set=UI3.KeyGuide uri_mode=direct transform=%d",
           s_image_box_class ? 1 : 0,
           s_ui_node_class && s_transform_class ? 1 : 0);
  LOG_INFO("[ANIVSH:PUI2] native header/body/footer renderer enabled");
}

void BindHost(MonoObject *root) {
  if (root == s_host)
    return;
  UnbindHost();
  if (root && mono_gchandle_new) {
    s_host_handle = mono_gchandle_new(root, /*pinned=*/1);
    if (!s_host_handle) {
      LOG_ERROR("[ANIVSH:MENU] failed to pin host root=%p",
                static_cast<void *>(root));
      return;
    }
  }
  s_host = root;
  ++s_host_generation;
  if (s_host && s_widget_class) {
    LOG_INFO("[ANIVSH:MENU] host bind begin root=%p generation=%lu",
             static_cast<void *>(s_host),
             static_cast<unsigned long>(s_host_generation));
    const float width = Get_Property<float>(s_widget_class, s_host, "Width");
    const float height = Get_Property<float>(s_widget_class, s_host, "Height");
    s_viewport_valid = width > 100.0f && height > 100.0f;
    if (s_viewport_valid) {
      s_viewport_width = width;
      s_viewport_height = height;
    }
    LOG_INFO("[ANIVSH:MENU] host bind root=%p viewport=%.0fx%.0f valid=%d generation=%lu",
             static_cast<void *>(s_host), s_viewport_width, s_viewport_height,
             s_viewport_valid ? 1 : 0,
             static_cast<unsigned long>(s_host_generation));
    LOG_INFO("[ANIVSH:MENU] host ready; widgets deferred to first UI tick");
  }
}

void UnbindHost() { release_bound_host(true); }

void EnterGameSession() {
  if (s_game_session)
    return;

  s_shell_stack = s_stack;

  // This runs on the first frame where the foreground game is known, while
  // the previous HomeUI host is still a valid managed tree.
  release_bound_host(true);
  s_stack.clear();
  s_stack.push_back(PageState{});
  s_game_session = true;
  LOG_INFO("[ANIVSH:LIFE] entered isolated game-menu session");
}

void LeaveGameSession() {
  if (!s_game_session)
    return;

  // The game has left the screen or is gone already. Never call into a dying
  // Scene.Game tree.
  release_bound_host(false);
  s_stack = s_shell_stack.empty() ? std::vector<PageState>{PageState{}}
                                  : s_shell_stack;
  s_game_session = false;
  s_dirty = true;
  s_color_dirty = true;
  s_overlay_dirty = true;
  LOG_INFO("[ANIVSH:LIFE] left game-menu session; shell state restored");
}

void UpdateDiagnostics(const Diagnostics &diagnostics) {
  if (diagnostics.tick_count != s_last_diagnostics_tick) {
    s_last_diagnostics_tick = diagnostics.tick_count;
    /* closed on this page, a repaint would only redraw an invisible menu */
    if (IsVisible() && !s_stack.empty() &&
        s_stack.back().page == Page::Information)
      s_dirty = true;
  }
}

void Show() {
  if (!s_host)
    return;
  /* Sweety: drawn by PUI itself, when it can be */
  if (SweetyNative::Wanted() && SweetyNative::Show())
    return;
  if (!s_stack.empty())
    reset_page_selection(s_stack.back());
  // Toolbox refreshes settings when its root page opens.  Do the same before
  // constructing ShellUI labels so a config that became visible after injection
  // (or a language changed through Toolbox) is reflected on the very first
  // ShellUI frame rather than only after visiting Toolbox.
  if (s_save_pending)
    (void)flush_save(); /* never reload over a change not yet written */
  (void)LoadSettings();
  s_overlay_dirty = true;
  forget_written_values();
  create_widgets();
  sample_menu_color_clock(MonotonicMs());
  reset_page_transition();
  ++s_page_focus_epoch;
  s_highlight_page_epoch = s_page_focus_epoch;
  reset_highlight_motion();
  s_animation = AnimationState::Opening;
  s_animation_started_ms = MonotonicMs();
  s_dirty = true;
  s_color_dirty = true;
}

void Hide() {
  SweetyNative::Hide();
  if (s_animation == AnimationState::Hidden)
    return;
  s_animation = AnimationState::Closing;
  reset_page_transition();
  s_animation_started_ms = MonotonicMs();
  s_moving = false;
  s_last_move_tick_ms = 0;
  s_armed_option = OptionId::None;
  s_show_serial = false;
  request_save();
  s_dirty = true;
  s_color_dirty = true;
}

void Toggle() {
  if (IsVisible() || SweetyNative::s_active)
    Hide();
  else
    Show();
}

void Tick() {
  if (!s_host)
    return;
  SweetyNative::Tick();
  follow_renderer_choice();
  poll_confirmation();
  leave_deleted_tab();
  const bool store_changed = HomeTabsStore::ConsumeUiChange();
  const bool tabs_changed = HomeTabs::ConsumeUiChange();
  if (store_changed || tabs_changed)
    s_dirty = true;
  sync_clock_layer();
  update_clock_date();
  update_overlay_fade();
  if (s_controls_settle_frames > 0) {
    --s_controls_settle_frames;
    update_controls();
  }
  const uint64_t now = MonotonicMs();
  static bool s_power_indicator_color_applied = false;
  if (!s_power_indicator_color_applied) {
    PowerIndicator::SetColor(static_cast<PowerIndicator::Color>(std::clamp(
        g_settings.vsh_power_indicator_color, 0, 2)));
    s_power_indicator_color_applied = true;
  }
  move_menu_with_stick(now);
  // Controls and System Info are independent of the menu's open state. Build
  // them on the first safe UI-thread tick after the host has been resolved so
  // the open shortcut and default overlay are visible before the first open.
  create_widgets();
  reassert_widgets(now);
  if (s_save_pending && now >= s_save_due_ms &&
      s_page_transition == PageTransitionState::Idle &&
      (s_animation == AnimationState::Hidden ||
       s_animation == AnimationState::Visible))
    (void)flush_save();
  sample_menu_color_clock(now);
  pace_color_animation(now);
  if (s_dirty)
    repaint_menu();
  if (page_transition_active() && s_page_transition_start_geometry.valid &&
      !s_page_transition_target_geometry.valid) {
    capture_page_transition_target_geometry();
  }
  /* Not the snapshot: reading it every frame would copy it and keep the
     worker sampling with nothing on screen to show its readings. */
  const uint64_t sampled_at_ms = Telemetry::SampledAtMs();
  /* Pages that show sensor readings follow the telemetry worker: sample
     faster while one is open, and repaint when a new sample lands (they used
     to change only when a button was pressed). */
  const bool live_page =
      IsVisible() && !s_stack.empty() &&
      (s_stack.back().page == Page::Fan ||
       s_stack.back().page == Page::Information);
  Telemetry::SetLiveView(live_page);
  static uint64_t s_last_live_sample = 0;
  if (live_page && sampled_at_ms != s_last_live_sample &&
      s_page_transition == PageTransitionState::Idle) {
    s_last_live_sample = sampled_at_ms;
    /* a sample that changes no row is no repaint */
    if (!same_rows(build_page(s_stack.back().page), s_painted_options))
      s_dirty = true;
  }
  const bool overlay_dirty = s_overlay_dirty ||
      sampled_at_ms != s_last_overlay_sample;
  if (overlay_dirty) {
    Profile::Scope scope(Profile::kMenuOverlay);
    update_overlay();
    s_overlay_dirty = false;
  }
  // Advance geometry first so the colour-only pass sees the same interpolated
  // highlight position and transition state as the frame being presented.
  Profile::Scope animation_scope(Profile::kMenuAnimations);
  update_animations();
  // Palette updates are colour-only and never touch the interpolated layout.
  // Keep them running through a page transition so the menu stays on the same
  // global animation phase as the background, clock and focus outline instead
  // of freezing and jumping when the transition finishes.
  if (IsVisible() && s_color_dirty) {
    if (active_menu_theme() == kThemeApparition)
      update_apparition_menu_colors_only();
    else if (anomaly_theme())
      update_anomaly_menu_colors_only();
    else
      update_akari_menu_colors_only();
    s_color_dirty = false;
  }
}

void HandleInput(uint32_t buttons_down, uint32_t repeated) {
  if (!IsVisible() || s_stack.empty())
    return;
  if (page_transition_active())
    return;
  if (s_moving) {
    handle_move_input(buttons_down);
    return;
  }

  PageState &state = s_stack.back();
  std::vector<Option> options = build_page(state.page);
  ensure_selection(state, options);
  if (options.empty())
    return;
  /* The list does not wrap: Up on the first row and Down on the last do
     nothing. Wrapping threw the cursor to the far end of the page on one
     press too many, which read as the cursor jumping away. */
  s_selection_from_repeat = (repeated & (Up | Down)) != 0;
  if ((buttons_down & Up) && state.selected > 0)
    commit_selection(state, options, state.selected - 1);
  if ((buttons_down & Down) && state.selected + 1 < options.size())
    commit_selection(state, options, state.selected + 1);
  if (buttons_down & (Up | Down))
    s_armed_option = OptionId::None;

  /* activate() can push a page and reallocate s_stack: state ends here. */
  const Option &selected = options[state.selected];
  /* Left/Right change a value; on a switch, an action or a link they do
     nothing (they used to run the row's side effects). */
  const bool adjustable =
      selected.type == OptionType::SliderInt ||
      selected.type == OptionType::SliderFloat ||
      selected.type == OptionType::Strings ||
      selected.type == OptionType::ColorSlider;
  if (adjustable && (buttons_down & Left))
    change_option(selected, -1);
  if (adjustable && (buttons_down & Right))
    change_option(selected, 1);
  if (buttons_down & Cross)
    activate(selected);
  /* Square: a colour slider moves the hue, the brightness or the saturation */
  if ((buttons_down & Square) && selected.type == OptionType::ColorSlider) {
    ColorMode &mode = color_mode(selected.id);
    mode = static_cast<ColorMode>((static_cast<int>(mode) + 1) %
                                  static_cast<int>(ColorMode::Count));
    s_dirty = true;
  }
  if (buttons_down & Circle) {
    s_armed_option = OptionId::None;
    if (s_stack.size() > 1)
      begin_page_transition(PageTransitionAction::Pop);
    else
      Hide();
  }
  // begin_page_transition() already committed the destination page and
  // captured its target geometry.  Re-marking the tree dirty here would make
  // the next UI tick run update_menu_widgets() a second time while the
  // pooled labels are being clipped/interpolated, which is the one-frame text
  // disappearance seen when entering or leaving a submenu.  Ordinary input
  // still takes the normal dirty path.
  if (!page_transition_active())
    s_dirty = true;
  if (Profile::enabled && !s_stack.empty()) {
    const PageState &now_state = s_stack.back();
    const std::vector<Option> now_options = build_page(now_state.page);
    LOG_INFO("[SHELLMENU:PROFILE] input=0x%x page=%s selected=%zu scroll=%zu "
             "count=%zu item='%s' highlight_to=%.1f highlight_y=%.1f",
             buttons_down, page_title(now_state.page), now_state.selected,
             now_state.scroll, now_options.size(),
             now_state.selected < now_options.size()
                 ? now_options[now_state.selected].title.c_str()
                 : "<none>",
             s_highlight_to, s_highlight_y);
  }
}

bool IsVisible() {
  return s_animation == AnimationState::Opening ||
         s_animation == AnimationState::Visible;
}

bool IsAttached() { return s_attached; }

} // namespace AniketosShell::ShellMenu
