#include "aniketos_shell/core/shell_features.hpp"

#include "aniketos_shell/controller/controller_manager.hpp"
#include "aniketos_shell/core/frame_profile.hpp"
#include "aniketos_shell/core/host_scene.hpp"
#include "aniketos_shell/core/menu_host.hpp"
#include "aniketos_shell/games/trophy_tools.hpp"
#include "aniketos_shell/hardware/pad_speaker.hpp"
#include "aniketos_shell/hardware/telemetry.hpp"
#include "aniketos_shell/home/control_center_entry.hpp"
#include "aniketos_shell/home/hen_menu.hpp"
#include "aniketos_shell/home/home_particles.hpp"
#include "aniketos_shell/home/home_tabs.hpp"
#include "aniketos_shell/home/home_tabs_store.hpp"
#include "aniketos_shell/home/home_video.hpp"
#include "aniketos_shell/home/web_tiles.hpp"
#include "aniketos_shell/input/kbm_capture.hpp"
#include "aniketos_shell/input/mouse_control.hpp"
#include "aniketos_shell/input/osk_glyphs.hpp"
#include "aniketos_shell/input/pad_reader.hpp"
#include "aniketos_shell/rn_scripts/rn_reload.hpp"
#include "aniketos_shell/rn_scripts/script_injector.hpp"
#include "aniketos_shell/system_ui/account_avatar.hpp"
#include "aniketos_shell/system_ui/devkit_overlay.hpp"
#include "aniketos_shell/system_ui/migration_notice.hpp"
#include "aniketos_shell/system_ui/popup_menu.hpp"
#include "aniketos_shell/toolbox/kbm_toolbox.hpp"
#include "payload_center.hpp"
#include "aniketos_theme/clock/shell_clock.hpp"
#include "aniketos_theme/custom_font.hpp"
#include "aniketos_theme/icon_color.hpp"
#include "aniketos_theme/panel_tint.hpp"
#include "aniketos_theme/theme_manager.hpp"
#include "aniketos_theme/tiles/home_tiles.hpp"
#include "aniketos_theme/toggle_styles.hpp"

#include <onion/log.h>

namespace Profile = AniketosShell::Profile;

namespace ShellFeatures {

void Install(MonoImage *react_pui, MonoImage *pui) {
  /* Shell appearance features controlled from the Shell Menu. Each resolves
     its managed types by name and stays disabled on its own if they are not
     there; none of them is required for the Toolbox. */
  AniketosTheme::HomeTiles::Install(react_pui, pui);
  AniketosTheme::PanelTint::Install(pui);
  AniketosTheme::IconColor::Install(pui);
  AniketosTheme::ToggleStyles::Install(pui);
  ShellClock::Install(pui);
  SystemDialogs::Install(pui);
  ControlCenterEntry::Install(react_pui);
  HomeTabs::Install(pui);
  HomeTabsStore::Install();
  OskGlyphs::Install();
  ScriptInjector::Install(react_pui);
  AccountAvatar::Install(pui);
  DevkitOverlay::Install();
  AniketosTheme::CustomFont::Install();
}

void StartWorkers() {
  /* Workers live outside PUI.Application.Update: Home is React/RNPS and
     does not run that callback. Either one failing leaves the menu off
     or without live metrics; nothing else depends on them. */
  if (!AniketosShell::Telemetry::Start())
    LOG_WARN("Shell Menu: telemetry worker did not start");
  if (!AniketosShell::Input::Start())
    LOG_WARN("Shell Menu: controller shortcut reader did not start");
  (void)AniketosHEN::Controller::Start();
}

void BeforeNativeFrame() {
  /* Who has the screen, once for the frame: every tick below asks. */
  AniketosShell::BeginFrame();
  {
    Profile::Scope scope(Profile::kPolls);
    shellui_poll_presence_reload();
  }
  {
    Profile::Scope scope(Profile::kSharedUi);
    AniketosTheme::ToggleStyles::Tick();
  }
  /* Shell appearance (clock, panels, background) then the Shell Menu itself:
     resolve its host, drain controller input, animate. All are no-ops until
     their Install()/Initialize() has succeeded. */
  {
    Profile::Scope scope(Profile::kShellClock);
    ShellClock::Tick();
  }
  {
    Profile::Scope scope(Profile::kHomeFeatures);
    HenMenu::Tick();
    ControlCenterEntry::Tick();
    MigrationNotice::Tick();
    HomeTabs::Tick();
    HomeTabsStore::Tick();
    WebTiles::Tick();
    onion::shellui::payload_center::Tick();
  }
  {
    Profile::Scope scope(Profile::kInput);
    OskGlyphs::Tick();
  }
  {
    Profile::Scope scope(Profile::kHomeFeatures);
    HomeVideo::Tick();
    AccountAvatar::Tick();
    DevkitOverlay::Tick();
  }
  {
    Profile::Scope scope(Profile::kParticles);
    HomeParticles::Tick();
  }
  {
    Profile::Scope scope(Profile::kHomeFeatures);
    TrophyTools::Tick();
    AniketosTheme::CustomFont::Tick();
    PadSpeaker::Tick();
  }
  {
    Profile::Scope scope(Profile::kSharedUi);
    AniketosTheme::PanelTint::Tick();
    AniketosTheme::IconColor::Tick();
  }
  {
    Profile::Scope scope(Profile::kTheme);
    AniketosTheme::Tick();
  }
  {
    Profile::Scope scope(Profile::kShellManager);
    AniketosShell::Tick();
  }
  Profile::Scope scope(Profile::kInput);
  KbmCapture::Tick();
  kbm_toolbox::Tick();
  MouseControl::Tick();
}

AfterNativeFrame::~AfterNativeFrame() {
  {
    Profile::Scope scope(Profile::kHomeTiles);
    AniketosTheme::HomeTiles::Tick();
  }
  Profile::EndFrame();
}

} // namespace ShellFeatures
