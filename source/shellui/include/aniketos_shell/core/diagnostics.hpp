#pragma once

#include <cstdint>
#include <string>

namespace AniketosShell {

struct Diagnostics {
  int shell_pid = -1;
  int foreground_pid = -1;
  int game_pid = -1;
  int ui_thread_id = -1;
  uintptr_t shell_module_base = 0;
  uintptr_t pui_module_base = 0;
  uintptr_t root_domain = 0;
  uintptr_t scene = 0;
  uintptr_t root_widget = 0;
  uint64_t host_generation = 0;
  uint64_t tick_count = 0;
  bool attached = false;
  std::string title_id;
  std::string host_backend = "unresolved";
  std::string input_backend = "libScePad passive reader";
  std::string last_resolve = "not started";
};

const Diagnostics &GetDiagnostics();

} // namespace AniketosShell
