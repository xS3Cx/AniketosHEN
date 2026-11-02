constexpr const char *kMenuName = "id_anivsh_shell_menu";
constexpr const char *kMenuBrandName = "Aniketos";
constexpr const char *kOverlayName = "id_anivsh_shell_overlay";
constexpr const char *kControlsName = "id_anivsh_shell_controls";
constexpr const char *kAnomalyControlsName = "id_anivsh_anomaly_controls";
constexpr const char *kClockDateName = "id_anivsh_shell_clock_date";
constexpr const char *kBackgroundDirectory =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/background";
constexpr size_t kMaxVisibleOptions = 10;
/* Sweety (vsh_menu_theme 6): the menu's pages shown as the console's own
   pop-up menus, see shell_menu/sweety_native.inc. */
constexpr int kThemeSweety = 6;
constexpr int kThemeApparition = 5;
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
constexpr int kAnomalyControlIconCount = 5;
constexpr int kAnomalyControlLabelCount = 4;
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
// An ordinary UI-font character: the PS Symbol Web PUA glyph is not in the
// font of injected ShellUI labels.
constexpr char kCelsiusSuffix[] = "\xC2\xB0" "C";

enum class AnimationState { Hidden, Opening, Visible, Closing };
enum class PageTransitionState { Idle, Expanding };
enum class PageTransitionAction { Push, Pop };
enum class Page {
  Main,
  /* the console: information, power, fan, power light, buzzer */
  ConsoleManagement,
  Information,
  HiddenApps,
  Power,
  Fan,
  PowerIndicator,
  Buzzer,
  /* the controller */
  Controller,
  ControllerAnimations,
  ControllerVibration,
  ControllerLeds,
  /* the Home screen: background, clock, tabs, tiles, profile */
  HomeScreen,
  HomeVideo,
  HomeBgVideo,
  HomeBgImage,
  HomeBgEffects,
  HomeParticles,
  ShellClock,
  HomeTabs,
  ShellTiles,
  HomeProfile,
  /* the look of the console's own interface */
  SystemCustomize,
  GlobalCustomization,
  ShellBackground,
  ShellOutline,
  ShellUIPanels,
  /* overlays on Home and in games */
  OSD,
  HomeScreenOSD,
  HomeScreenOSDSettings,
  PerformanceOSD,
  /* the look of this menu */
  Customize,
  Colors,
  ScalePosition,
  Keyboard,
  /* what a DevKit/TestKit shows on Home */
  Debug,
  /* AniketosHEN itself */
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
  ColorSlider,
};
enum class OptionId {
  None,
  GlobalThemeEnabled,
  GlobalThemeAnimation,
  GlobalThemeAnimationSpeed,
  MenuThemeSelector,
  MenuAppearanceSelector,
  OverlayEnabled,
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
  PerformanceOverlayFps,
  PerformanceOverlayFan,
  RevealSerialNumber,
  BuzzerVolume,
  BuzzerShort,
  BuzzerError,
  BuzzerLong,
  PowerIndicatorColor,
  FanDynamic,
  FanThreshold,
  ControllerRestore,
  ControllerBreathe,
  ControllerPulse,
  ControllerRainbow,
  ControllerSpectrum,
  ControllerNeonWave,
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
  ShellBackgroundPs3Waves,
  HomeVideoEnabled,
  HomeBackgroundGlobal,
  HomeVideoFile,
  HomeVideoSound,
  HomeVideoVolume,
  HomeVideoRefresh,
  AccountAvatarEnabled,
  AccountAvatarFile,
  AccountAvatarRefresh,
  CustomFontEnabled,
  PadSpeakerMirror,
  PadSpeakerVolume,
  CustomFontFile,
  DevkitOverlayInfo,
  DevkitOverlayExpired,
  HomeImageEnabled,
  HomeImageFile,
  HomeImageRefresh,
  HomeBackgroundDim,
  HomeBackgroundHaze,
  HomeBackgroundVignette,
  HomeBackgroundEdges,
  HomeParticlesEnabled,
  HomeParticlesCount,
  HomePresenceDot,
  HomeTabRenameGames,
  HomeTabRenameMedia,
  HomeTabRestoreNames,
  HomeEmulatorsTab,
  HomeTabIcons,
  HomeBookmark,
  HomeBookmarkRename,
  HomeHideApps,
  HiddenAppShow,
  HiddenAppsShowAll,
  OskGlyphKey,
  OskEmoji,
  MouseControl,
  ShellToggleStyle,
  ShellBackgroundParticles,
  ShellTilesCircular,
  ShellTilesCornerRadius,
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
  LogoColorEnabled,
  /* one gradient slider per colour (OptionType::ColorSlider) */
  ColorGlobal,
  ColorController,
  ColorController2,
  ColorBackground,
  ColorGlow,
  ColorOutline,
  ColorPanels,
  ColorClockTime,
  ColorClockDate,
  ColorLogo,
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
  /* the D-pad again, for "Increment" beside "Scroll" */
  AnomalyControlDpad2,
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

using AniketosUI::MonotonicMs;
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
  PageTransitionRect theme_header_slice[kFrameGradientSlices]{};
  PageTransitionRect theme_footer_slice[kFrameGradientSlices]{};
};
PageTransitionGeometry s_page_transition_start_geometry{};
PageTransitionGeometry s_page_transition_target_geometry{};
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

/* Width of the layout the widgets hung in Home's scene are placed in. */
float home_layout_width() { return s_viewport_width; }
float s_menu_target_x = 0.0f;
float s_menu_target_y = 0.0f;
float s_menu_target_width = 1.0f;
float s_menu_target_height = 1.0f;
bool s_viewport_valid = false;
bool s_attached = false;
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
/* An action that asks to be pressed twice (power off, reset, unload): the one
   pressed once, until the cursor or the page moves on. */
OptionId s_armed_option{};
bool s_show_serial = false;
/* The player LEDs and light brightness last chosen on the LEDs page (not
   saved: the system keeps its own, and "restore" returns to it). */
int s_player_leds = 0;
int s_light_brightness = 0;
/* The option whose "show the identifiers?" dialog is open (serial number,
   DevKit info panel); Tick waits for the answer. */
OptionId s_confirming = OptionId::None;
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

/* Settings persistence. Writing config.ini is two synchronous file writes on
 * the UI thread (about 20 ms on a console): doing it on every slider step, and
 * at the first frame of the close animation, is what made the menu stutter.
 * Changes are therefore coalesced and written once the menu has been still for
 * a moment; actions that end the session (power, restart, unload, explicit
 * "Save") call flush_save() and write immediately. */
bool s_save_pending = false;
uint64_t s_save_due_ms = 0;

void request_save() {
  s_save_pending = true;
  s_save_due_ms = MonotonicMs() + 400;
}

bool flush_save() {
  s_save_pending = false;
  return SaveSettings();
}
