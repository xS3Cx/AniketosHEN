#include "aniketos_shell/core/menu_host.hpp"

#include "hooked_funcs.hpp"
#include "aniketos_shell/shell_menu/shell_menu.hpp"
#include "aniketos_shell/games/cheats_menu.hpp"
#include "aniketos_shell/core/diagnostics.hpp"
#include "aniketos_shell/core/frame_profile.hpp"
#include "aniketos_shell/input/pad_reader.hpp"
#include "aniketos_shell/core/host_scene.hpp"
#include "aniketos_shell/hardware/telemetry.hpp"

#include <onion/proc_query.h>
#include <onion/platform.h>

extern "C" {
#include <onion/proc.h>
}

#include <cstdlib>
#include <cstring>
#include <atomic>
#include <pthread_np.h>
#include <string>
#include <unistd.h>

namespace AniketosShell {
namespace {

constexpr uint64_t kResolveIntervalFrames = 30;
constexpr uint64_t kDiagnosticsIntervalFrames = 60;

ShellContext s_context;
HostSnapshot s_host{};
Diagnostics s_diagnostics{};
bool s_initialized = false;
bool s_attached = false;
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

void refresh_process_diagnostics() {
  /* This runs on the UI thread once a second. It used to enumerate the
     process table for the game PID and query the running title, which cost a
     dropped frame each time for values nothing displays; the Shell Menu host
     only exists while no game is in the foreground anyway. */
  s_diagnostics.shell_pid = getpid();
  s_diagnostics.game_pid = -1;
  s_diagnostics.foreground_pid = s_diagnostics.shell_pid;
  s_diagnostics.title_id = "none";

  s_diagnostics.attached = s_attached;
  s_diagnostics.scene = reinterpret_cast<uintptr_t>(s_host.scene);
  s_diagnostics.root_widget = reinterpret_cast<uintptr_t>(s_host.root);
  s_diagnostics.root_domain = reinterpret_cast<uintptr_t>(Root_Domain);
  s_diagnostics.host_generation = ShellMenu::HostGeneration();
  ShellMenu::UpdateDiagnostics(s_diagnostics);
}

bool tick_foreground_title(const std::string &title_id) {
  /* The Shell Menu belongs to the home screen. While a game or application
     is in the foreground it stays closed and the L1+R3 shortcut is ignored.
     The controller is only taken while the cheats menu (L1+L3+R3) is open
     over the game. */
  (void)title_id;
  (void)Input::ConsumeToggleRequest();
  const bool cheats_requested = Input::ConsumeCheatsToggleRequest();
  const uint32_t buttons_down = Input::ConsumeButtonsDown();
  const uint32_t buttons_repeated = Input::ConsumeRepeatedButtons();
  Input::SetMenuCapturing(
      CheatsMenu::Tick(cheats_requested, buttons_down, buttons_repeated));
  return true;
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
  s_attached = false;
  ShellMenu::BindHost(s_host.root);
  s_diagnostics.host_backend = s_host.path ? s_host.path : "unresolved";
  s_diagnostics.last_resolve = s_host.root ? "host changed" : "no host";
  LOG_INFO("[ANIVSH:LIFE] host changed old=%s/%p new=%s/%p scene=%p type=%s.%s",
           old_path, static_cast<void *>(old_root),
           s_host.path ? s_host.path : "none", static_cast<void *>(s_host.root),
           static_cast<void *>(s_host.scene),
           s_host.scene_namespace ? s_host.scene_namespace : "",
           s_host.scene_type ? s_host.scene_type : "");

  if (ShellMenu::IsVisible())
    s_attached = AttachMenu();
}

} // namespace

const Diagnostics &GetDiagnostics() { return s_diagnostics; }

bool Initialize(MonoImage *app_system, MonoImage *pui) {
  if (s_initialized)
    return true;
  Profile::Initialize();
  ShellMenu::Initialize();
  if (!s_context.Initialize(app_system, pui))
    return false;

  s_diagnostics.shell_pid = getpid();
  s_diagnostics.root_domain = reinterpret_cast<uintptr_t>(Root_Domain);
  s_diagnostics.shell_module_base = module_base("eboot.bin");
  s_diagnostics.pui_module_base =
      module_base("Sce.PlayStation.PUI.dll.sprx");
  LOG_INFO("[ANIVSH] pid=%d process=SceShellUI domain=%p shell_base=0x%lx pui_base=0x%lx",
           getpid(), static_cast<void *>(Root_Domain),
           static_cast<unsigned long>(s_diagnostics.shell_module_base),
           static_cast<unsigned long>(s_diagnostics.pui_module_base));
  s_initialized = true;
  return true;
}

bool ResolveSystemHost() {
  if (!s_initialized)
    return false;
  HostSnapshot next{};
  if (!s_context.Resolve(next)) {
    if (s_host.root) {
      LOG_WARN("[ANIVSH:LIFE] host lost path=%s root=%p",
               s_host.path ? s_host.path : "none",
               static_cast<void *>(s_host.root));
      publish_host({});
      ShellMenu::UnbindHost();
    }
    s_diagnostics.last_resolve = "no selectable root";
    return false;
  }
  publish_host(next);
  s_diagnostics.last_resolve = "ok";
  return true;
}

bool AttachMenu() {
  if (!s_host.root)
    return false;
  ShellMenu::Show();
  s_attached = ShellMenu::IsAttached();
  LOG_INFO("[ANIVSH:PUI] attach backend=%s scene=%p root=%p attached=%d",
           s_host.path ? s_host.path : "none", static_cast<void *>(s_host.scene),
           static_cast<void *>(s_host.root), s_attached ? 1 : 0);
  return s_attached;
}

void DetachMenu() {
  ShellMenu::Hide();
  s_attached = false;
}

void HandleInput(uint32_t buttons_down) {
  if (!buttons_down)
    return;
  ShellMenu::HandleInput(buttons_down);
}

void RequestToggle() {
  std::string running_title;
  int running_app_id = -1;
  if (Get_Running_App_TID(running_title, running_app_id)) {
    LOG_INFO("[ANIVSH:INPUT] shell toggle ignored in game title=%s; use game mod shortcut",
             running_title.c_str());
    return;
  }
  if (!s_host.root)
    (void)ResolveSystemHost();
  ShellMenu::Toggle();
  s_attached = ShellMenu::IsAttached();
  LOG_INFO("[ANIVSH:INPUT] toggle visible=%d attached=%d backend=%s",
           ShellMenu::IsVisible() ? 1 : 0, s_attached ? 1 : 0,
           s_host.path ? s_host.path : "none");
}

void Tick() {
  if (!s_initialized)
    return;

  std::string running_title;
  int running_app_id = -1;
  /* Asking SystemService which BigApp is running is a round trip to another
     process. Home <-> game transitions do not need frame accuracy, so sample
     it four times a second and reuse the answer in between. */
  static bool big_app_running = false;
  static std::string cached_title;
  static uint64_t last_query_ms = 0;
  {
    Profile::Scope scope(Profile::kRunningApp);
    const uint64_t now_ms = Profile::NowNs() / 1000000ull;
    if (!last_query_ms || now_ms - last_query_ms >= 250) {
      last_query_ms = now_ms;
      big_app_running = Get_Running_App_TID(cached_title, running_app_id);
    }
    running_title = cached_title;
  }
  if (big_app_running) {
    (void)running_app_id;
    if (!s_game_session_active) {
      ShellMenu::EnterGameSession();
      s_game_session_active = true;
      s_attached = false;
      Input::SetMenuCapturing(false);
      LOG_INFO("[ANIVSH:LIFE] HomeUI -> game title=%s",
               running_title.c_str());
    }
    s_attached = false;
    (void)tick_foreground_title(running_title);
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
    s_attached = false;
    Input::SetMenuCapturing(false);
    LOG_INFO("[ANIVSH:LIFE] game -> HomeUI; forcing fresh shell host bind");
    (void)ResolveSystemHost();
  }

  Telemetry::ReportShellFrame();
  ++s_diagnostics.tick_count;
  if (s_diagnostics.ui_thread_id < 0) {
    s_diagnostics.ui_thread_id = pthread_getthreadid_np();
    LOG_INFO("[ANIVSH:PUI] UI thread tid=%d", s_diagnostics.ui_thread_id);
  }

  if (!s_host.root ||
      s_diagnostics.tick_count - s_last_resolve_tick >=
          kResolveIntervalFrames) {
    s_last_resolve_tick = s_diagnostics.tick_count;
    (void)ResolveSystemHost();
  }

  if (Input::ConsumeToggleRequest() || Profile::ConsumeScriptToggle())
    RequestToggle();

  const uint32_t buttons_down =
      Input::ConsumeButtonsDown() | Profile::ConsumeScriptButtons();
  const uint32_t buttons_repeated =
      Input::ConsumeRepeatedButtons() | Profile::ConsumeScriptRepeated();
  if (buttons_down) {
    const uint64_t input_start = Profile::g_enabled ? Profile::NowNs() : 0;
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
  s_attached = ShellMenu::IsAttached();
  Input::SetMenuCapturing(ShellMenu::IsVisible());

  if (s_diagnostics.tick_count - s_last_diagnostics_tick >=
      kDiagnosticsIntervalFrames) {
    s_last_diagnostics_tick = s_diagnostics.tick_count;
    refresh_process_diagnostics();
  }
}

bool IsShellAlive() { return s_initialized && Root_Domain && pui_img; }
bool IsAttached() { return s_attached; }
MonoObject *HostRoot() { return s_host.root; }

bool ShouldSuppressShellInput() {
  return Input::IsMenuCapturing();
}

} // namespace AniketosShell
