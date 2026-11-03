#include "aniketos_shell/core/menu_host.hpp"

#include "hooked_funcs.hpp"
#include "aniketos_shell/shell_menu/shell_menu.hpp"
#include "aniketos_shell/games/cheats_menu.hpp"
#include "aniketos_shell/core/diagnostics.hpp"
#include "aniketos_shell/core/frame_profile.hpp"
#include "aniketos_shell/input/pad_reader.hpp"
#include "aniketos_shell/core/host_scene.hpp"
#include "aniketos_shell/hardware/telemetry.hpp"
#include "aniketos_ui/monotonic_clock.hpp"

#include <onion/platform.h>

extern "C" {
#include <onion/proc.h>
}

#include <cstdlib>
#include <cstring>
#include <pthread_np.h>
#include <string>
#include <unistd.h>

namespace AniketosShell {
namespace {

constexpr uint64_t kResolveIntervalFrames = 30;
constexpr uint64_t kDiagnosticsIntervalFrames = 60;

HostScene s_scene;
HostSnapshot s_host{};
Diagnostics s_diagnostics{};
int s_ui_thread_id = -1;
bool s_initialized = false;
bool s_game_session_active = false;
uint64_t s_last_resolve_tick = 0;
uint64_t s_last_diagnostics_tick = 0;

uintptr_t module_base(const char *name) {
  module_info_t *info = get_module_info(getpid(), name);
  if (!info)
    return 0;
  uintptr_t base = 0;
  for (const module_section_t &section : info->sections) {
    if (section.vaddr && (!base || section.vaddr < base))
      base = static_cast<uintptr_t>(section.vaddr);
  }
  free(info);
  return base > 0x10000 ? base : 0;
}

void tick_cheats_menu() {
  /* The Shell Menu belongs to the home screen. While a game or application
     is in the foreground it stays closed and the L1+R3 shortcut is ignored.
     The controller is only taken while the cheats menu (L1+L3+R3) is open
     over the game. */
  (void)Input::ConsumeToggleRequest();
  const bool cheats_requested = Input::ConsumeCheatsToggleRequest();
  const uint32_t buttons_down = Input::ConsumeButtonsDown();
  const uint32_t buttons_repeated = Input::ConsumeRepeatedButtons();
  Input::SetMenuCapturing(
      CheatsMenu::Tick(cheats_requested, buttons_down, buttons_repeated));
}

void attach_menu() {
  if (!s_host.root)
    return;
  ShellMenu::Show();
  LOG_INFO("[ANIVSH:PUI] attach backend=%s scene=%p root=%p attached=%d",
           s_host.path ? s_host.path : "none", static_cast<void *>(s_host.scene),
           static_cast<void *>(s_host.root), ShellMenu::IsAttached() ? 1 : 0);
}

void publish_host(const HostSnapshot &next) {
  const bool changed = next.root != s_host.root || next.scene != s_host.scene ||
                       !s_host.path || !next.path ||
                       std::strcmp(next.path, s_host.path) != 0;
  if (!changed)
    return;

  const char *old_path = s_host.path ? s_host.path : "none";
  MonoObject *old_root = s_host.root;
  s_host = next;
  ShellMenu::BindHost(s_host.root);
  LOG_INFO("[ANIVSH:LIFE] host changed old=%s/%p new=%s/%p scene=%p type=%s.%s",
           old_path, static_cast<void *>(old_root),
           s_host.path ? s_host.path : "none", static_cast<void *>(s_host.root),
           static_cast<void *>(s_host.scene),
           s_host.scene_namespace ? s_host.scene_namespace : "",
           s_host.scene_type ? s_host.scene_type : "");

  if (ShellMenu::IsVisible())
    attach_menu();
}

void resolve_host() {
  HostSnapshot next{};
  if (s_scene.Resolve(next)) {
    publish_host(next);
    return;
  }
  if (s_host.root) {
    LOG_WARN("[ANIVSH:LIFE] host lost path=%s root=%p",
             s_host.path ? s_host.path : "none",
             static_cast<void *>(s_host.root));
    publish_host({});
    ShellMenu::UnbindHost();
  }
}

void request_toggle() {
  if (!s_host.root)
    resolve_host();
  ShellMenu::Toggle();
  LOG_INFO("[ANIVSH:INPUT] toggle visible=%d attached=%d backend=%s",
           ShellMenu::IsVisible() ? 1 : 0, ShellMenu::IsAttached() ? 1 : 0,
           s_host.path ? s_host.path : "none");
}

} // namespace

bool Initialize(MonoImage *app_system, MonoImage *pui) {
  if (s_initialized)
    return true;
  Profile::Initialize();
  ShellMenu::Initialize();
  if (!s_scene.Initialize(app_system, pui)) {
    LOG_WARN("Shell Menu unavailable: host classes not resolved");
    return false;
  }

  LOG_INFO("[ANIVSH] pid=%d process=SceShellUI domain=%p shell_base=0x%lx pui_base=0x%lx",
           getpid(), static_cast<void *>(Root_Domain),
           static_cast<unsigned long>(module_base("eboot.bin")),
           static_cast<unsigned long>(
               module_base("Sce.PlayStation.PUI.dll.sprx")));
  s_initialized = true;
  return true;
}

void Tick() {
  if (!s_initialized)
    return;

  /* Asking SystemService which BigApp is running is a round trip to another
     process. Home <-> game transitions do not need frame accuracy, so sample
     it four times a second and reuse the answer in between; its title is
     asked for only when a game comes in, for the log. */
  static bool big_app_running = false;
  static uint64_t last_query_ms = 0;
  {
    Profile::Scope scope(Profile::kRunningApp);
    const uint64_t now_ms = AniketosUI::MonotonicMs();
    if (!last_query_ms || now_ms - last_query_ms >= 250) {
      last_query_ms = now_ms;
      big_app_running = sceSystemServiceGetAppIdOfRunningBigApp() >= 0;
    }
  }
  /* In front, not merely running: a game left running behind Home or a
     system app hands the Shell Menu back to them until it has the screen
     again. Under the Control Center and our cards over a game the layer
     stays Game; without the field the running BigApp is all there is. */
  const FocusLayer focus =
      big_app_running ? CurrentFocusLayer() : FocusLayer::Unknown;
  if (big_app_running && focus != FocusLayer::TopMenu &&
      focus != FocusLayer::ShellApp) {
    if (!s_game_session_active) {
      std::string running_title;
      int running_app_id = -1;
      (void)Get_Running_App_TID(running_title, running_app_id);
      ShellMenu::EnterGameSession();
      s_game_session_active = true;
      Input::SetMenuCapturing(false);
      LOG_INFO("[ANIVSH:LIFE] HomeUI -> game title=%s",
               running_title.c_str());
    }
    tick_cheats_menu();
    return;
  }

  (void)Input::ConsumeCheatsToggleRequest();
  if (s_game_session_active) {
    CheatsMenu::Close();
    ShellMenu::LeaveGameSession();
    s_game_session_active = false;
    // Invalidate the independent resolver cache. Without this, an unchanged
    // HomeUI root is treated as already published and the Shell Menu never
    // receives BindHost after returning from Scene.Game.
    s_host = {};
    s_last_resolve_tick = 0;
    Input::SetMenuCapturing(false);
    LOG_INFO("[ANIVSH:LIFE] game -> HomeUI; forcing fresh shell host bind");
    resolve_host();
  }

  Telemetry::ReportShellFrame();
  ++s_diagnostics.tick_count;
  if (s_ui_thread_id < 0) {
    s_ui_thread_id = pthread_getthreadid_np();
    LOG_INFO("[ANIVSH:PUI] UI thread tid=%d", s_ui_thread_id);
  }

  if (!s_host.root ||
      s_diagnostics.tick_count - s_last_resolve_tick >=
          kResolveIntervalFrames) {
    s_last_resolve_tick = s_diagnostics.tick_count;
    resolve_host();
  }

  if (Input::ConsumeToggleRequest() || Profile::ConsumeScriptToggle())
    request_toggle();

  const uint32_t buttons_down =
      Input::ConsumeButtonsDown() | Profile::ConsumeScriptButtons();
  const uint32_t buttons_repeated =
      Input::ConsumeRepeatedButtons() | Profile::ConsumeScriptRepeated();
  if (buttons_down) {
    const uint64_t input_start = Profile::enabled ? Profile::NowNs() : 0;
    ShellMenu::HandleInput(buttons_down, buttons_repeated);
    if (input_start) {
      const uint64_t elapsed = Profile::NowNs() - input_start;
      Profile::Add(Profile::kMenuInput, elapsed);
      if (elapsed > 2000000ull)
        LOG_INFO("[SHELLMENU:PROFILE] slow input buttons=0x%x took=%llu us",
                 buttons_down,
                 static_cast<unsigned long long>(elapsed / 1000));
    }
  }

  {
    Profile::Scope scope(Profile::kMenuTick);
    ShellMenu::Tick();
  }
  Input::SetMenuCapturing(ShellMenu::IsVisible());

  if (s_diagnostics.tick_count - s_last_diagnostics_tick >=
      kDiagnosticsIntervalFrames) {
    s_last_diagnostics_tick = s_diagnostics.tick_count;
    ShellMenu::UpdateDiagnostics(s_diagnostics);
  }
}

MonoObject *HostRoot() { return s_host.root; }

bool ShouldSuppressShellInput() { return Input::IsMenuCapturing(); }

bool FilterPad(GamePadData &data) {
  if (!ShouldSuppressShellInput())
    return false;
  data.Buttons = None;
  data.ButtonsDown = None;
  return true;
}

} // namespace AniketosShell
