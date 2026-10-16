/*
 * AniketosHEN Akari-style frontend.
 *
 * Visual proportions and interaction semantics are derived from Akari
 * (kernaltrap8/Akari, d264e8b). This is a PS5/PUI reimplementation; no PS3
 * PAF objects, syscall numbers, offsets, or platform assumptions are copied.
 */

#include <onion/distribution.h>
#include "aniketos_shell/akari_menu.hpp"
#include "aniketos_shell/home_bookmark.hpp"
#include "aniketos_shell/home_menu.hpp"

#include "external_symbols.hpp"
#include "fan_control.hpp"
#include "hooked_funcs.hpp"
#include "homeui_top_nav_patch.hpp"
#include "ipc.hpp"
#include "aniketos_runtime.hpp"
#include "shell_clock.hpp"
#include "aniketos_theme/color.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/home_tiles.hpp"
#include "aniketos_theme/shell_background.hpp"
#include "aniketos_theme/theme_config.hpp"
#include "aniketos_theme/theme_manager.hpp"
#include "aniketos_ui/menu_style.hpp"
#include "aniketos_ui/pui_primitives.hpp"
#include "aniketos_shell/easing.hpp"
#include "aniketos_shell/frame_profile.hpp"
#include "aniketos_shell/global_input.hpp"
#include "aniketos_shell/home_particles.hpp"
#include "aniketos_theme/toggle_styles.hpp"
#include "aniketos_shell/home_video.hpp"
#include "aniketos_shell/screen_rotation.hpp"
#include "aniketos_shell/shell_buzzer.hpp"
#include "aniketos_shell/system_dialogs.hpp"
#include "aniketos_shell/power_indicator.hpp"
#include "aniketos_shell/telemetry.hpp"
#include "aniketos_shell/text_units.hpp"
#include "toolbox_i18n.hpp"
#include "controller_tools.hpp"
#include "controller/controller_manager.hpp"
#include "cJSON.hpp"

#include <onion/platform.h>
#include <onion/settings.hpp>
#include <onion/version.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

/* Per-frame code: resolve each managed property once and reuse the compiled
 * accessor (see aniketos_ui/mono_fast.hpp) instead of looking it up by name on
 * every call. Same call syntax and semantics as the hooked_funcs.hpp helpers. */
#include "aniketos_ui/mono_fast.hpp"
#define Get_Property AniketosUI::MonoFast::Get
#define Set_Property AniketosUI::MonoFast::Set
#define Set_Property_Invoke AniketosUI::MonoFast::SetInvoke

MonoObject *CreateUIFont(int size, int style, int weight);
MonoObject *CreateUIColor(float r, float g, float b, float a);
void Widget_Append_Child(MonoObject *widget, MonoObject *child);
void notify(const char *text, ...);
extern onion::Settings g_settings;

namespace AniketosShell::AkariMenu {
namespace {

constexpr const char *kMenuName = "id_anivsh_akari_menu";
constexpr const char *kMenuBrandName = "Aniketos";
constexpr const char *kOverlayName = "id_anivsh_akari_overlay";
constexpr const char *kControlsName = "id_anivsh_akari_controls";
constexpr const char *kAnomalyControlsName = "id_anivsh_anomaly_controls";
constexpr const char *kClockDateName = "id_anivsh_shell_clock_date";
constexpr const char *kBackgroundDirectory =
    "/user/data/" HEN_FOLDER "/ShellUI/background";
constexpr size_t kMaxVisibleOptions = 10;
constexpr int kGradientSlices = 32;
constexpr int kFrameGradientSlices = 24;
/* Share of a colour slider's track that holds the hues; the rest fades to
   white (the picture assets/home_menu/color_track.png is drawn the same). */
constexpr float kColorTrackHues = 0.92f;
/* What a colour slider moves: Square steps through them. */
enum class ColorMode { Hue, Brightness, Saturation, Count };
/* Brightness and saturation in five-percent steps. */
constexpr int kColorLevels = 20;
constexpr int kControlBorderGradientSlices = kFrameGradientSlices;
constexpr int kBorderLines = 5;
constexpr int kControlLabelCount = 6;
constexpr int kAnomalyControlIconCount = 4;
constexpr int kAnomalyControlLabelCount = 3;
constexpr float kReferenceWidth = 1920.0f;
constexpr float kReferenceHeight = 1080.0f;
constexpr float kOpenDurationMs = 200.0f;
constexpr float kCloseDurationMs = 200.0f;
constexpr float kApparitionOpenDurationMs = 360.0f;
constexpr float kApparitionCloseDurationMs = 320.0f;
// Match the UIKit reference transition. A longer monotonic interval gives
// PUI2 enough frames to move the viewport, separators and footer together.
constexpr float kPageTransitionDurationMs = 300.0f;
constexpr float kHighlightDurationMs = 250.0f;

enum class AnimationState { Hidden, Opening, Visible, Closing };
enum class PageTransitionState { Idle, Collapsing, Expanding };
enum class PageTransitionAction { None, Push, Pop };
enum class Page {
  Main,
  Information,
  ConsoleManagement,
  Power,
  Buzzer,
  PowerIndicator,
  Fan,
  Controller,
  ControllerAnimations,
  ControllerVibration,
  ControllerLeds,
  OSD,
  HomeScreenOSD,
  HomeScreenOSDSettings,
  PerformanceOSD,
  Appearance,
  GlobalCustomization,
  SystemAnimations,
  MenuGradientAnimationColors,
  SystemCustomize,
  Customize,
  ThemeMenu,
  Colors,
  Gradients,
  Animations,
  Borders,
  ScalePosition,
  Text,
  ShellBackground,
  ShellTiles,
  ShellOutline,
  ShellClock,
  HomeVideo,
  ShellToggles,
  ShellUIPanels,
  Settings,
};
enum class OptionType {
  Submenu,
  Toggle,
  SliderInt,
  SliderFloat,
  Strings,
  Action,
  Value,
  ThemeChoice,
  ColorSlider,
};
enum class OptionId {
  None,
  GlobalThemeEnabled,
  GlobalThemeAnimation,
  GlobalThemeAnimationSpeed,
  SystemGlobalAnimation,
  SystemGlobalAnimationSpeed,
  MenuThemeSelector,
  MenuAppearanceSelector,
  MenuThemeAkari,
  MenuThemeAnomaly,
  MenuThemeApparition,
  OverlayEnabled,
  OverlayBackground,
  OverlayAllCpuUsage,
  OverlayPosition,
  OverlayFps,
  OverlayResolution,
  OverlayCpu,
  OverlaySoc,
  OverlayFan,
  OverlayRam,
  OverlayFreeRam,
  OverlayStorage,
  OverlayFirmware,
  OverlayUptime,
  OverlayIp,
  OverlayRefresh,
  OverlayFpsPrecision,
  OverlayTempUnit,
  OverlayMemoryUnit,
  OverlayTextSize,
  OverlayHorizontal,
  OverlayVertical,
  PerformanceOverlayEnabled,
  PerformanceOverlayBackground,
  PerformanceOverlayAllCpuUsage,
  PerformanceOverlayPosition,
  PerformanceOverlayCpu,
  PerformanceOverlayGpu,
  PerformanceOverlayRam,
  PerformanceOverlayIp,
  RevealSerialNumber,
  BuzzerVolume,
  BuzzerNormal,
  BuzzerShort,
  BuzzerError,
  BuzzerLong,
  PowerIndicatorBrightness,
  PowerIndicatorAnimation,
  PowerIndicatorColor,
  PowerIndicatorRestoreNormal,
  FanDynamic,
  FanThreshold,
  ControllerRestore,
  ControllerBreathe,
  ControllerPulse,
  ControllerRainbow,
  ControllerSpectrum,
  ControllerNeonWave,
  ControllerStopAnimation,
  VibrationStrength,
  VibrationShort,
  VibrationLong,
  VibrationHeartbeat,
  VibrationLight,
  VibrationHeavy,
  VibrationWave,
  VibrationStop,
  PlayerLeds,
  LightBrightness,
  LedChase,
  LedCountdown,
  LedBlink,
  LedRestore,
  ShellBackgroundEnabled,
  ShellBackgroundForceHome,
  ShellBackgroundAnimation,
  ShellBackgroundAnimationSpeed,
  ShellBackgroundImageEnabled,
  ShellBackgroundImageTransition,
  ShellBackgroundImageFile,
  ShellBackgroundRefreshImages,
  HomeVideoEnabled,
  HomeVideoFile,
  HomeVideoSound,
  HomeVideoRefresh,
  HomeImageEnabled,
  HomeImageFile,
  HomeImageRefresh,
  HomeParticlesEnabled,
  HomeParticlesCount,
  HomePresenceDot,
  HomeBookmark,
  HomeBookmarkRename,
  OskGlyphKey,
  ShellToggleStyle,
  ShellBackgroundReloadImage,
  ShellBackgroundParticles,
  ShellTilesCircular,
  ShellTilesCornerRadius,
  ShellTilesPS5CaseMockup,
  ShellTilesOutlineEnabled,
  ShellTilesOutlineSyncBackground,
  ShellTilesOutlineAnimation,
  ShellTilesOutlineAnimationSpeed,
  ShellClockEnabled,
  ShellClockSeconds,
  ShellClockDate,
  ShellClockYear,
  ShellClockHourFormat,
  ShellClockDateOrder,
  ShellClockMonthStyle,
  ShellClockWeekdayStyle,
  ShellClockDateSeparator,
  ShellClockTimeFontSize,
  ShellClockDateFontSize,
  ShellClockDateSpacing,
  ShellUIPanelsEnabled,
  ShellUIPanelsAnimation,
  ShellUIPanelsAnimationSpeed,
  ShellUIPanelsStrength,
  ShellUIPanelsOpacity,
  ShellGlowIntensity,
  ShellRestoreOriginal,
  MenuControls,
  CustomizationEnabled,
  /* one gradient slider per colour (OptionType::ColorSlider) */
  ColorGlobal,
  ColorController,
  ColorBackground,
  ColorGlow,
  ColorOutline,
  ColorPanels,
  ColorClockTime,
  ColorClockDate,
  ColorMenuLeft,
  ColorMenuRight,
  MenuAnimation,
  MenuMove,
  MenuResetPosition,
  MenuScale,
  MenuBorderEnabled,
  MenuBorder,
  MenuTextSize,
  MenuFontWeight,
  MenuWidth,
  MenuDualColor,
  MenuGradientDirection,
  MenuColorAnimation,
  MenuColorAnimationSpeed,
  MenuOpacity,
  SaveConfig,
  LoadConfig,
  ResetConfig,
  RefreshHome,
  RestartShell,
  RestMode,
  RebootConsole,
  PowerOff,
  UnloadStack,
};

enum AnomalyControlIcon : int {
  AnomalyControlDpad,
  AnomalyControlL3,
  AnomalyControlCross,
  AnomalyControlCircle,
};

// Direct PS5 UI3 resource URIs recovered from the 11.40 PUI assembly's
// managed string heap. Loading them through the stable UI2 ImageBox avoids
// touching UI3.StyleAsset from an injected UI2 tree (which faults ShellUI),
// while still using the exact native PS5 KeyGuide artwork.
constexpr const char *kKeyGuideCircle =
    "cxml://ui3/image_keyguide_circle";
constexpr const char *kKeyGuideCross = "cxml://ui3/image_keyguide_cross";
constexpr const char *kKeyGuideDpad = "cxml://ui3/image_keyguide_dpad";
constexpr const char *kKeyGuideL1 = "cxml://ui3/image_keyguide_l1";
constexpr const char *kKeyGuideL3 = "cxml://ui3/image_keyguide_l3";
constexpr const char *kKeyGuideSquare = "cxml://ui3/image_keyguide_square";
constexpr const char *kKeyGuideR3 = "cxml://ui3/image_keyguide_r3";
constexpr const char *kIconTagPS5 = "cxml://ui3/iconid_tag_ps5";


extern "C" int sceSystemServiceRequestPowerOff(void);
extern "C" int sceSystemServiceRequestReboot(void);
extern "C" int sceApplicationSystemSuspend(void);

using Color = AniketosUI::Color;

struct Option {
  std::string title;
  OptionType type = OptionType::Value;
  /* the value takes most of the row (see the row layouts) */
  bool wide_value = false;
  OptionId id = OptionId::None;
  Page target = Page::Main;
  std::string value;
  int minimum_i = 0;
  int maximum_i = 0;
  int step_i = 1;
  float minimum_f = 0.0f;
  float maximum_f = 0.0f;
  float step_f = 0.0f;
  int string_count = 0;
  int string_index = 0;
  uint64_t payload = 0;
  std::string external_key;
};

// Implemented with the settings mappings below; page construction uses it to
// expose selector bounds to every theme renderer.
int selector_index_for(OptionId id);

struct PageState {
  Page page = Page::Main;
  size_t selected = 0;
  size_t scroll = 0;
  OptionId selected_id = OptionId::None;
  // Used only to keep focus stable while the current page's dynamic options
  // are rebuilt. Page changes and each menu opening reset selection to row 0.
  uint32_t selected_key = 0;
  bool selection_initialized = false;

};

void reset_page_selection(PageState &state) {
  state.selected = 0;
  state.scroll = 0;
  state.selected_id = OptionId::None;
  state.selected_key = 0;
  state.selection_initialized = false;
}

struct ApparitionAnimationLayout {
  bool valid = false;
  float x = 0.0f;
  float y = 0.0f;
  float width = 1.0f;
  float total_height = 1.0f;
  float header_height = 1.0f;
  float body_height = 0.0f;
  float footer_y = 0.0f;
  float footer_height = 1.0f;
  float separator = 1.0f;
  float text_offset = 0.0f;
};

struct Widgets {
  MonoObject *master = nullptr;
  MonoObject *content = nullptr;
  MonoObject *header = nullptr;
  MonoObject *body = nullptr;
  MonoObject *footer = nullptr;
  MonoObject *scroll_clip = nullptr;
  MonoObject *highlight = nullptr;
  MonoObject *title = nullptr;
  MonoObject *header_platform_glyph = nullptr;
  MonoObject *version = nullptr;
  MonoObject *page_title = nullptr;
  MonoObject *counter = nullptr;
  MonoObject *rows[kMaxVisibleOptions]{};
  MonoObject *values[kMaxVisibleOptions]{};
  MonoObject *selector_left[kMaxVisibleOptions]{};
  MonoObject *selector_right[kMaxVisibleOptions]{};
  // Appendum controls are deliberately layered in painter order.  The old
  // single-panel toggle and three-panel slider could not reproduce its
  // outlined checkbox or two-piece slider knob.
  MonoObject *toggles[kMaxVisibleOptions]{};
  MonoObject *toggle_inners[kMaxVisibleOptions]{};
  MonoObject *toggle_checks[kMaxVisibleOptions]{};
  MonoObject *slider_tracks[kMaxVisibleOptions]{};
  MonoObject *slider_inners[kMaxVisibleOptions]{};
  MonoObject *slider_cores[kMaxVisibleOptions]{};
  MonoObject *slider_fills[kMaxVisibleOptions]{};
  MonoObject *slider_knobs[kMaxVisibleOptions]{};
  MonoObject *slider_knob_inners[kMaxVisibleOptions]{};
  /* the colour slider: its track picture, a second picture laid over it
     (saturation), the "change mode" hint and a line marking the choice */
  MonoObject *color_track_borders[kMaxVisibleOptions]{};
  MonoObject *color_tracks[kMaxVisibleOptions]{};
  MonoObject *color_track_tops[kMaxVisibleOptions]{};
  MonoObject *color_hint_pre[kMaxVisibleOptions]{};
  MonoObject *color_hint_glyphs[kMaxVisibleOptions]{};
  MonoObject *color_hints[kMaxVisibleOptions]{};
  MonoObject *color_knobs[kMaxVisibleOptions]{};
  MonoObject *color_knob_inners[kMaxVisibleOptions]{};
  MonoObject *highlight_slices[kGradientSlices]{};
  MonoObject *theme_header_slices[kFrameGradientSlices]{};
  MonoObject *theme_footer_slices[kFrameGradientSlices]{};
  MonoObject *horizontal_borders[kBorderLines][kFrameGradientSlices]{};
  MonoObject *left_borders[2]{};
  MonoObject *right_borders[2]{};
  MonoObject *controls = nullptr;
  // The root only positions children and always stays transparent.
  MonoObject *control_backdrop = nullptr;
  MonoObject *control_border_top[kControlBorderGradientSlices]{};
  MonoObject *control_border_bottom[kControlBorderGradientSlices]{};
  MonoObject *control_border_left = nullptr;
  MonoObject *control_border_right = nullptr;
  MonoObject *control_icon_l1 = nullptr;
  MonoObject *control_icon_r3 = nullptr;
  MonoObject *control_labels[kControlLabelCount]{};
  MonoObject *anomaly_controls = nullptr;
  MonoObject *anomaly_control_icons[kAnomalyControlIconCount]{};
  MonoObject *anomaly_control_labels[kAnomalyControlLabelCount]{};
  MonoObject *position_edit_icon = nullptr;
  MonoObject *overlay = nullptr;
  MonoObject *clock_date = nullptr;
};

MonoObject *s_host = nullptr;
uint32_t s_host_handle = 0;
MonoClass *s_widget_class = nullptr;
MonoClass *s_panel_class = nullptr;
MonoClass *s_label_class = nullptr;
MonoClass *s_image_box_class = nullptr;
MonoClass *s_text_shadow_class = nullptr;
MonoClass *s_ui_node_class = nullptr;
MonoClass *s_transform_class = nullptr;
AniketosUI::Pui::Runtime s_pui{};
/* Same, but making UI3 widgets: the kind Home's own scene is built from. */
AniketosUI::Pui::Runtime s_pui_home{};
MonoObject *s_font = nullptr;
uint32_t s_font_handle = 0;
int s_font_size = 0;
int s_font_weight = 0;
MonoObject *s_control_font = nullptr;
MonoObject *s_anomaly_title_font = nullptr;
MonoObject *s_anomaly_footer_font = nullptr;
MonoObject *s_clock_date_font = nullptr;
/* The root widget of Home's scene while the date, the Home OSD and the
 * open/close legend are parented to it (as UI3 widgets). There they are part
 * of Home: PUI fades and animates them with the scene, in the same pass as
 * the clock. Null while they sit on our own overlay host instead. */
MonoObject *s_clock_layer = nullptr;
MonoObject *s_overlay_font = nullptr;
uint32_t s_control_font_handle = 0;
uint32_t s_anomaly_title_font_handle = 0;
uint32_t s_anomaly_footer_font_handle = 0;
uint32_t s_clock_date_font_handle = 0;
int s_clock_date_font_size = 0;
uint32_t s_overlay_font_handle = 0;
int s_overlay_font_size = 0;
Widgets s_widgets{};
std::vector<PageState> s_stack{PageState{}};
// Shell navigation and game-mod navigation are separate logical sessions.
// Only one owns the pooled widget tree at a time, but entering a game must
// never overwrite the Shell Menu page/focus state that is restored on HomeUI.
std::vector<PageState> s_shell_stack{PageState{}};
bool s_game_session = false;
AnimationState s_animation = AnimationState::Hidden;
uint64_t s_animation_started_ms = 0;
PageTransitionState s_page_transition = PageTransitionState::Idle;
uint64_t s_page_transition_started_ms = 0;
float s_page_transition_start_height = 1.0f;
struct PageTransitionRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};
struct PageTransitionGeometry {
  bool valid = false;
  PageTransitionRect master{};
  PageTransitionRect content{};
  PageTransitionRect header{};
  PageTransitionRect body{};
  PageTransitionRect scroll_clip{};
  PageTransitionRect footer{};
  PageTransitionRect page_title{};
  PageTransitionRect counter{};
  PageTransitionRect version{};
  PageTransitionRect body_bottom_border[kFrameGradientSlices]{};
  PageTransitionRect theme_header_slice[kFrameGradientSlices]{};
  PageTransitionRect theme_footer_slice[kFrameGradientSlices]{};
  PageTransitionRect footer_top_border[kFrameGradientSlices]{};
  PageTransitionRect footer_bottom_border[kFrameGradientSlices]{};
};
PageTransitionGeometry s_page_transition_start_geometry{};
PageTransitionGeometry s_page_transition_target_geometry{};
bool s_page_transition_text_revealed = true;
uint64_t s_highlight_started_ms = 0;
float s_highlight_from = 0.0f;
float s_highlight_to = 0.0f;
float s_highlight_y = 0.0f;
// The highlight is a single pooled PUI panel shared by every page/theme.
// Position alone is not enough to identify its owner: row 0 on two different
// pages has the same Y. Track page and option identity as well, otherwise a
// page pop or theme change can leave the old row visually selected.
Page s_highlight_page = Page::Main;
uint32_t s_highlight_selection_key = 0;
size_t s_highlight_scroll = 0;
bool s_highlight_initialized = false;
std::array<bool, kMaxVisibleOptions> s_submenu_marker_active{};
struct SliderMotion {
  OptionId id = OptionId::None;
  float value = 0.0f;
  float target = 0.0f;
  uint64_t started_ms = 0;
};
SliderMotion s_slider_motion[kMaxVisibleOptions]{};
float s_viewport_width = kReferenceWidth;
float s_viewport_height = kReferenceHeight;

/* Width of the layout the widgets hung in Home's scene are placed in: the
   menu's own host keeps the console's 16:9, Home follows the screen format. */
float home_layout_width() {
  const float width = ScreenRotation::LayoutWidth();
  return width > s_viewport_width ? width : s_viewport_width;
}
float s_menu_target_x = 0.0f;
float s_menu_target_y = 0.0f;
float s_menu_target_width = 1.0f;
float s_menu_target_height = 1.0f;
bool s_viewport_valid = false;
bool s_attached = false;
bool s_game_host = false;
bool s_dirty = true;
// Color/highlight animation must not masquerade as a layout invalidation.
// Rebuilding the complete managed PUI2 tree for every animation frame causes
// label setters to race the page transition and makes the menu feel slow.
bool s_color_dirty = true;
// A page hand-off reuses the pooled labels for a different option set. Keep
// one explicit full palette pass pending so a stale selected colour cannot
// survive on an unrelated row after returning from a submenu.
bool s_apparition_force_color_refresh = false;
uint64_t s_page_focus_epoch = 0;
uint64_t s_highlight_page_epoch = 0;
// Menu redraws must not relayout the independent telemetry overlay.  Keeping
// this flag separate prevents a theme animation or page repaint from making
// the overlay appear to jump.
bool s_overlay_dirty = true;
bool s_moving = false;
bool s_unload_armed = false;
bool s_show_serial = false;
/* The player LEDs and light brightness last chosen on the LEDs page (not
   saved: the system keeps its own, and "restore" returns to it). */
int s_player_leds = 0;
int s_light_brightness = 0;
/* The "show the identifiers?" dialog is open; Tick waits for the answer. */
bool s_confirming_reveal = false;
std::vector<uint32_t> s_widget_handles;
uint64_t s_host_generation = 0;
uint64_t s_last_overlay_sample = 0;
uint64_t s_last_diagnostics_tick = 0;
uint64_t s_last_move_tick_ms = 0;
uint64_t s_menu_color_frame_ms = 0;
bool s_menu_color_frame_valid = false;
// Absolute monotonic sample used by GlobalTheme. Keep it distinct from the
// menu-local animation phase above so all menu surfaces read one exact global
// colour even when a render crosses a 16 ms quantisation boundary.
uint64_t s_global_color_sample_ms = 0;
uint64_t s_last_menu_animation_frame_ms = 0;
// Shell Menu settings describe one global visual theme. Keep its immutable
// configuration and animation clock outside PageState so changing pages can
// never restart the palette or give two pooled widgets different phases.
AniketosUI::MenuStyle s_menu_visual_style{};
bool s_menu_visual_style_initialized = false;
uint64_t s_menu_visual_phase_origin_ms = 0;
bool s_auxiliary_color_only_update = false;
ApparitionAnimationLayout s_apparition_animation_layout{};
std::vector<std::string> s_background_images;
bool s_background_catalog_scanned = false;

// The frontend intentionally remains one translation unit: these implementation
// partitions share Mono/PUI handles whose initialization order and lifetime are
// runtime-sensitive.  Keeping the state above and grouping behavior below makes
// each responsibility reviewable without changing ABI or object ownership.
/* Settings persistence. Writing config.ini is two synchronous file writes on
 * the UI thread (about 20 ms on a console): doing it on every slider step, and
 * at the first frame of the close animation, is what made the menu stutter.
 * Changes are therefore coalesced and written once the menu has been still for
 * a moment; actions that end the session (power, restart, unload, explicit
 * "Save") call flush_save() and write immediately. */
bool s_save_pending = false;
uint64_t s_save_due_ms = 0;
uint64_t monotonic_millis();

void request_save() {
  s_save_pending = true;
  s_save_due_ms = monotonic_millis() + 400;
}

bool flush_save() {
  s_save_pending = false;
  return SaveSettings();
}

#include "akari_menu/background_catalog.inc"
#include "akari_menu/ui_primitives.inc"
#include "akari_menu/page_model.inc"
#include "akari_menu/view.inc"
#include "akari_menu/settings_actions.inc"

void Initialize() {
  ensure_background_directory();
  scan_background_images(true);
  /* The older background-image option is no longer offered; Home's
     background picture is HomeVideo's now. Left on, nothing could turn it
     off. */
  g_settings.shell_background_image_enabled = false;
  /* Game mods are not part of this distribution: the loader is never
     initialised (no mods directory, no scan) and no page links to it. */
  (void)AniketosUI::Pui::Resolve(s_pui, pui_img, retain_pui_widget, nullptr);
  s_widget_class = s_pui.widget;
  s_panel_class = s_pui.panel;
  s_label_class = s_pui.label;
  s_pui_home = s_pui;
  if (pui_img && mono_class_from_name) {
    s_pui_home.widget =
        mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI3", "Widget");
    s_pui_home.panel =
        mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI3", "Panel");
    s_pui_home.label =
        mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI3", "Label");
    s_pui_home.image_box =
        mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI3", "ImageBox");
  }
  s_image_box_class = s_pui.image_box;
  s_text_shadow_class = pui_img && mono_class_from_name
      ? mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2",
                             "TextShadowSettings")
      : nullptr;
  s_ui_node_class = pui_img && mono_class_from_name
      ? mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UINode")
      : nullptr;
  s_transform_class = pui_img && mono_class_from_name
      ? mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "Transform")
      : nullptr;
  Telemetry::SetRefreshSeconds(g_settings.overlay_refresh_seconds);
  LOG_INFO("[ANIVSH:AKARI] frontend initialized source=Akari-d264e8b "
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
  s_game_host = false;
  if (root && mono_gchandle_new) {
    s_host_handle = mono_gchandle_new(root, /*pinned=*/1);
    if (!s_host_handle) {
      LOG_ERROR("[ANIVSH:AKARI] failed to pin host root=%p",
                static_cast<void *>(root));
      return;
    }
  }
  s_host = root;
  ++s_host_generation;
  if (s_host && s_widget_class) {
    LOG_INFO("[ANIVSH:AKARI] host bind begin root=%p generation=%lu",
             static_cast<void *>(s_host),
             static_cast<unsigned long>(s_host_generation));
    const float width = Get_Property<float>(s_widget_class, s_host, "Width");
    const float height = Get_Property<float>(s_widget_class, s_host, "Height");
    s_viewport_valid = width > 100.0f && height > 100.0f;
    if (s_viewport_valid) {
      s_viewport_width = width;
      s_viewport_height = height;
    }
    LOG_INFO("[ANIVSH:AKARI] host bind root=%p viewport=%.0fx%.0f valid=%d generation=%lu",
             static_cast<void *>(s_host), s_viewport_width, s_viewport_height,
             s_viewport_valid ? 1 : 0,
             static_cast<unsigned long>(s_host_generation));
    LOG_INFO("[ANIVSH:AKARI] host ready; widgets deferred to first UI tick");
  }
}

void BindGameHost(MonoObject *root) {
  if (!root)
    return;
  if (root != s_host)
    BindHost(root);
  s_game_host = s_host == root;
}

void release_bound_host(bool remove_managed_nodes) {
  // Calling RemoveNamed/RemoveFromParent on Scene.Game after BigApp teardown
  // is unsafe: ShellUI may still expose the old native pointer while its
  // managed tree is already being destroyed.  In that transition the scene
  // itself owns destruction; we only release our GC handles and native cache.
  if (remove_managed_nodes && s_host) {
    remove_named(s_host, kMenuName);
    remove_named(s_host, kOverlayName);
    remove_named(s_host, kControlsName);
    remove_named(s_host, kAnomalyControlsName);
    remove_named(s_host, kClockDateName);
    if (s_clock_layer && !ShellClock::IsWidgetDisposed(s_clock_layer))
      for (MonoObject *widget : {s_widgets.overlay, s_widgets.clock_date,
                                 s_widgets.controls})
        AniketosUI::Pui::Remove(s_pui, widget);
  }
  s_clock_layer = nullptr;
  s_widgets = {};
  release_widgets();
  if (s_font_handle && mono_gchandle_free)
    mono_gchandle_free(s_font_handle);
  s_font_handle = 0;
  s_font = nullptr;
  s_font_size = 0;
  s_font_weight = 0;
  if (s_control_font_handle && mono_gchandle_free)
    mono_gchandle_free(s_control_font_handle);
  if (s_anomaly_title_font_handle && mono_gchandle_free)
    mono_gchandle_free(s_anomaly_title_font_handle);
  if (s_anomaly_footer_font_handle && mono_gchandle_free)
    mono_gchandle_free(s_anomaly_footer_font_handle);
  if (s_clock_date_font_handle && mono_gchandle_free)
    mono_gchandle_free(s_clock_date_font_handle);
  if (s_overlay_font_handle && mono_gchandle_free)
    mono_gchandle_free(s_overlay_font_handle);
  s_control_font_handle = 0;
  s_anomaly_title_font_handle = 0;
  s_anomaly_footer_font_handle = 0;
  s_clock_date_font_handle = 0;
  s_overlay_font_handle = 0;
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
  s_overlay_dirty = true;
  if (s_host_handle && mono_gchandle_free)
    mono_gchandle_free(s_host_handle);
  s_host_handle = 0;
  s_host = nullptr;
  s_attached = false;
  s_game_host = false;
  s_animation = AnimationState::Hidden;
  s_page_transition = PageTransitionState::Idle;
  s_page_transition_started_ms = 0;
  s_dirty = true;
  s_color_dirty = true;
  s_overlay_dirty = true;
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

  // BigApp is already gone here. Never call into the dying Scene.Game tree.
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
    if (!s_stack.empty() && s_stack.back().page == Page::Information)
      s_dirty = true;
  }
}

void Show() {
  if (!s_host)
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
  AniketosUI::Pui::ForgetAll();
  create_widgets();
  // All colour consumers in this Tick use the same page-local timestamp.  A
  // page transition therefore cannot change gradient direction/progress by
  // accidentally reusing another page's absolute clock value.
  s_menu_color_frame_ms = active_menu_color_frame_ms();
  s_global_color_sample_ms = monotonic_millis();
  s_menu_color_frame_valid = true;
  s_page_transition = PageTransitionState::Idle;
  s_page_transition_started_ms = 0;
  s_page_transition_start_geometry = {};
  s_page_transition_target_geometry = {};
  s_page_transition_text_revealed = true;
  ++s_page_focus_epoch;
  s_highlight_page_epoch = s_page_focus_epoch;
  s_highlight_started_ms = 0;
  s_highlight_from = 0.0f;
  s_highlight_to = 0.0f;
  s_highlight_y = 0.0f;
  s_highlight_page = Page::Main;
  s_highlight_selection_key = 0;
  s_highlight_scroll = 0;
  s_highlight_initialized = false;
  s_animation = AnimationState::Opening;
  s_animation_started_ms = monotonic_millis();
  s_dirty = true;
  s_color_dirty = true;
}

void Hide() {
  if (s_animation == AnimationState::Hidden)
    return;
  s_animation = AnimationState::Closing;
  s_page_transition = PageTransitionState::Idle;
  s_page_transition_started_ms = 0;
  s_page_transition_start_geometry = {};
  s_page_transition_target_geometry = {};
  s_page_transition_text_revealed = true;
  s_animation_started_ms = monotonic_millis();
  s_moving = false;
  s_last_move_tick_ms = 0;
  s_unload_armed = false;
  s_show_serial = false;
  request_save();
  s_dirty = true;
  s_color_dirty = true;
}

void Toggle() {
  if (IsVisible())
    Hide();
  else
    Show();
}

void Tick() {
  if (!s_host)
    return;
  if (s_confirming_reveal) {
    int button = -1;
    if (SystemDialogs::PollAlert(&button)) {
      s_confirming_reveal = false;
      Show();
      s_show_serial = button == 0; /* only on the "show" button */
      s_dirty = true;
    }
  }
  if (!s_game_host) {
    sync_clock_layer();
    update_clock_date();
    update_overlay_fade();
  } else
    hide_game_shell_extras();
  const uint64_t now = monotonic_millis();
  static bool s_power_indicator_color_applied = false;
  if (!s_power_indicator_color_applied) {
    PowerIndicator::SetColor(static_cast<PowerIndicator::Color>(std::clamp(
        g_settings.vsh_power_indicator_color, 0, 2)));
    s_power_indicator_color_applied = true;
  }
  // Kura's current ICC implementation exposes firmware presets and a
  // steady state only. Do not replay legacy animation presets every frame.
  PowerIndicator::Tick(PowerIndicator::Animation::Normal);
  if (s_moving && IsVisible()) {
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
      g_settings.vsh_menu_x = std::clamp(g_settings.vsh_menu_x, 0.0f,
                                        s_viewport_width);
      g_settings.vsh_menu_y = std::clamp(g_settings.vsh_menu_y, 0.0f,
                                        s_viewport_height);
      s_dirty = true;
    }
  } else {
    s_last_move_tick_ms = 0;
  }
  // Controls and System Info are independent of the menu's open state. Build
  // them on the first safe UI-thread tick after the host has been resolved so
  // the open shortcut and default overlay are visible before the first open.
  create_widgets();
  /* Re-assert every widget value a few times a minute while the menu is on
     screen, so anything PUI changed behind the write-skipping shadow (a skin
     re-applied to a label, for instance) cannot stay wrong. */
  static uint64_t s_last_reassert_ms = 0;
  if (IsVisible() && now - s_last_reassert_ms >= 5000 &&
      s_page_transition == PageTransitionState::Idle) {
    s_last_reassert_ms = now;
    AniketosUI::Pui::ForgetAll();
    s_dirty = true;
    s_color_dirty = true;
  }
  if (s_save_pending && now >= s_save_due_ms &&
      s_page_transition == PageTransitionState::Idle &&
      (s_animation == AnimationState::Hidden ||
       s_animation == AnimationState::Visible))
    (void)flush_save();
  // All colour consumers in this Tick use the same page-local timestamp.  A
  // page transition cannot inherit an absolute process-clock phase from a
  // different page instance.
  s_menu_color_frame_ms = active_menu_color_frame_ms();
  s_global_color_sample_ms = now;
  s_menu_color_frame_valid = true;
  // Apparition keeps its text and managed label objects stable while the menu
  // is visible.  Its animated palette is applied by a colour-only pass after
  // the geometry/highlight animation, never by rebuilding the pooled labels.
  // That separation keeps the text attached to its row while still allowing
  // the selected row, submenu marker and gradient borders to animate.
  const bool animate_menu_colors =
      active_menu_color_animation();
  if (IsVisible() && animate_menu_colors) {
    // Keep colour motion independent from layout invalidation.  16 ms is the
    // smallest practical cadence on ShellUI while avoiding duplicate managed
    // setter work when the host calls Tick more than once per display frame.
    if (!s_last_menu_animation_frame_ms ||
        now - s_last_menu_animation_frame_ms >= 16) {
      s_last_menu_animation_frame_ms = now;
      if (active_menu_theme() == 5) {
        s_color_dirty = true;
      } else
        s_dirty = true;
    }
  } else {
    s_last_menu_animation_frame_ms = 0;
  }
  const bool menu_was_dirty = s_dirty;
  if (menu_was_dirty) {
    // A real page/settings change owns the complete widget tree.  Once an
    // Apparition page hand-off has committed its destination, refuse any
    // late dirty request until the clip animation is over; a second managed
    // label rewrite in that interval is enough to expose an empty glyph cache
    // for one frame.
    if (!(active_menu_theme() == 5 && page_transition_active())) {
      s_auxiliary_color_only_update = false;
      {
        Profile::Scope scope(Profile::kMenuWidgets);
        update_menu_widgets();
      }
      s_auxiliary_color_only_update = false;
      s_color_dirty = false;
    } else {
      s_dirty = false;
    }
  }
  if (page_transition_active() && s_page_transition_start_geometry.valid &&
      !s_page_transition_target_geometry.valid) {
    capture_page_transition_target_geometry();
  }
  const Telemetry::Snapshot telemetry = Telemetry::GetSnapshot();
  /* Pages that show sensor readings follow the telemetry worker: sample
     faster while one is open, and repaint when a new sample lands (they used
     to change only when a button was pressed). */
  const bool live_page =
      IsVisible() && !s_stack.empty() &&
      (s_stack.back().page == Page::Fan ||
       s_stack.back().page == Page::Information);
  Telemetry::SetLiveView(live_page);
  static uint64_t s_last_live_sample = 0;
  if (live_page && telemetry.sampled_at_ms != s_last_live_sample &&
      s_page_transition == PageTransitionState::Idle) {
    s_last_live_sample = telemetry.sampled_at_ms;
    s_dirty = true;
  }
  const bool overlay_dirty = s_overlay_dirty ||
      telemetry.sampled_at_ms != s_last_overlay_sample;
  if (overlay_dirty) {
    Profile::Scope scope(Profile::kMenuOverlay);
    if (!s_game_host) {
      update_overlay();
    } else {
      hide_game_shell_extras();
    }
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
  if (IsVisible() && active_menu_theme() == 5 && s_color_dirty) {
    update_apparition_menu_colors_only();
    s_color_dirty = false;
  }
  if (s_game_host)
    hide_game_shell_extras();
}

void HandleInput(uint32_t buttons_down, uint32_t repeated) {
  if (!IsVisible() || s_stack.empty())
    return;
  if (page_transition_active())
    return;
  if (s_moving) {
    if (buttons_down & Left) g_settings.vsh_menu_x -= 10.0f;
    if (buttons_down & Right) g_settings.vsh_menu_x += 10.0f;
    if (buttons_down & Up) g_settings.vsh_menu_y -= 10.0f;
    if (buttons_down & Down) g_settings.vsh_menu_y += 10.0f;
    g_settings.vsh_menu_x = std::clamp(g_settings.vsh_menu_x, 0.0f,
                                      s_viewport_width);
    g_settings.vsh_menu_y = std::clamp(g_settings.vsh_menu_y, 0.0f,
                                      s_viewport_height);
    if (buttons_down & (Cross | Circle))
      s_moving = false;
    if (!s_moving) {
      s_last_move_tick_ms = 0;
      request_save();
    }
    s_dirty = true;
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

  if (buttons_down & Left)
    change_option(options[state.selected], -1);
  if (buttons_down & Right)
    change_option(options[state.selected], 1);
  if (buttons_down & Cross)
    activate(options[state.selected]);
  /* Square: a colour slider moves the hue, the brightness or the saturation */
  if ((buttons_down & Square) &&
      options[state.selected].type == OptionType::ColorSlider) {
    ColorMode &mode = color_mode(options[state.selected].id);
    mode = static_cast<ColorMode>((static_cast<int>(mode) + 1) %
                                  static_cast<int>(ColorMode::Count));
    s_dirty = true;
  }
  if (buttons_down & Circle) {
    s_unload_armed = false;
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
  if (Profile::g_enabled && !s_stack.empty()) {
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

bool IsGameHost() { return s_game_host; }

bool IsAttached() { return s_attached; }

uint64_t HostGeneration() { return s_host_generation; }

} // namespace AniketosShell::AkariMenu
