#include "aniketos_shell/core/frame_profile.hpp"

#include "hooked_funcs.hpp"
#include "aniketos_theme/clock/shell_clock.hpp"

#include <onion/distribution.h>
#include <onion/log.h>

#include <cstdio>
#include "external_symbols.hpp"
#include <string>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace AniketosShell::Profile {

bool g_enabled = false;

namespace {

constexpr const char *kMarker = ONION_DIST_USER_DATA_ROOT "/shellmenu_profile";
constexpr const char *kScript = ONION_DIST_USER_DATA_ROOT "/shellmenu_script";
constexpr int kReportFrames = 300;
constexpr int kScriptPollFrames = 30;
constexpr int kScriptStepFrames = 12;

constexpr const char *kNames[kSectionCount] = {
    "clock", "shared_ui", "theme", "shell_mgr", "running_app", "menu_input", "menu_tick",
    "menu_widgets", "menu_overlay", "menu_anim", "home_tiles"};

uint64_t s_total_ns[kSectionCount];
uint64_t s_max_ns[kSectionCount];
uint64_t s_frame_ns[kSectionCount];
int s_frames = 0;
uint64_t s_window_start_ns = 0;

char s_script[128];
size_t s_script_length = 0;
size_t s_script_at = 0;
int s_script_countdown = 0;
int s_poll_countdown = 0;
bool s_pending_toggle = false;
uint32_t s_pending_buttons = 0;
uint32_t s_pending_repeated = 0;

void load_script() {
  const int fd = open(kScript, O_RDONLY);
  if (fd < 0)
    return;
  const ssize_t got = read(fd, s_script, sizeof(s_script) - 1);
  close(fd);
  if (got <= 0)
    return; /* still being uploaded: look again on the next poll */
  unlink(kScript);
  s_script_length = got > 0 ? static_cast<size_t>(got) : 0;
  s_script_at = 0;
  s_script_countdown = 0;
  LOG_INFO("[SHELLMENU:PROFILE] script loaded steps=%zu", s_script_length);
}

void step_script() {
  if (s_script_at >= s_script_length) {
    if (--s_poll_countdown <= 0) {
      s_poll_countdown = kScriptPollFrames;
      load_script();
    }
    return;
  }
  if (--s_script_countdown > 0)
    return;
  s_script_countdown = kScriptStepFrames;
  const char step = s_script[s_script_at++];
  /* Lower-case u/d: a held direction - auto-repeat cadence, flagged as such. */
  if (step == 'u' || step == 'd') {
    const uint32_t button = step == 'u' ? Up : Down;
    s_pending_buttons |= button;
    s_pending_repeated |= button;
    s_script_countdown = 8; /* ~133 ms at 60 fps, close to kListRepeatMs */
    return;
  }
  /* S / H: open system Settings / go back to Home, to reproduce scene
     changes without a controller. */
  if (step == 'S') {
    GoToURI("pssettings:play?mode=settings");
    return;
  }
  if (step == 'P') {
    ShellClock::DebugDumpPuiImage(ONION_DIST_USER_DATA_ROOT "/pui_dump.dll");
    return;
  }
  if (step == 'C') {
    static int dump_index = 0;
    char tag[16];
    std::snprintf(tag, sizeof(tag), "n%d", dump_index++);
    ShellClock::DebugDumpProperties(tag);
    return;
  }
  if (step == 'H') {
    GoToURI("pshomeui:navigateToHome");
    return;
  }
  switch (step) {
  case 'T': s_pending_toggle = true; break;
  case 'U': s_pending_buttons |= Up; break;
  case 'D': s_pending_buttons |= Down; break;
  case 'L': s_pending_buttons |= Left; break;
  case 'R': s_pending_buttons |= Right; break;
  case 'X': s_pending_buttons |= Cross; break;
  case 'O': s_pending_buttons |= Circle; break;
  case 'Q': s_pending_buttons |= Square; break;
  default: break; /* '.', whitespace: wait one step */
  }
}

} // namespace

uint64_t NowNs() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(now.tv_nsec);
}

void Initialize() {
  struct stat st {};
  g_enabled = stat(kMarker, &st) == 0;
  if (!g_enabled)
    return;
  LOG_INFO("[SHELLMENU:PROFILE] enabled by %s", kMarker);
  /* What one log record costs the calling thread on this console. */
  const uint64_t start = NowNs();
  for (int i = 0; i < 8; ++i)
    LOG_INFO("[SHELLMENU:PROFILE] log cost probe %d ........................",
             i);
  LOG_INFO("[SHELLMENU:PROFILE] one log record costs %llu us",
           static_cast<unsigned long long>((NowNs() - start) / 8 / 1000));
}

void Add(Section section, uint64_t elapsed_ns) {
  s_frame_ns[section] += elapsed_ns;
}

bool ConsumeScriptToggle() {
  const bool toggle = s_pending_toggle;
  s_pending_toggle = false;
  return toggle;
}

uint32_t ConsumeScriptRepeated() {
  const uint32_t buttons = s_pending_repeated;
  s_pending_repeated = 0;
  return buttons;
}

uint32_t ConsumeScriptButtons() {
  const uint32_t buttons = s_pending_buttons;
  s_pending_buttons = 0;
  return buttons;
}

void EndFrame() {
  if (!g_enabled)
    return;
  step_script();
  {
    static int countdown = 0;
    static char last[768];
    if (--countdown <= 0) {
      countdown = 15;
      char now_state[768];
      ShellClock::DebugSnapshot(now_state, sizeof(now_state));
      if (std::strcmp(now_state, last) != 0) {
        std::memcpy(last, now_state, sizeof(last));
        LOG_INFO("[SHELLMENU:PROFILE] clock %s", now_state);
      }
    }
  }
  for (int i = 0; i < kSectionCount; ++i) {
    s_total_ns[i] += s_frame_ns[i];
    if (s_frame_ns[i] > s_max_ns[i])
      s_max_ns[i] = s_frame_ns[i];
    s_frame_ns[i] = 0;
  }
  if (!s_window_start_ns)
    s_window_start_ns = NowNs();
  if (++s_frames < kReportFrames)
    return;

  const uint64_t now = NowNs();
  char line[512];
  size_t used = 0;
  for (int i = 0; i < kSectionCount; ++i) {
    const int n = std::snprintf(
        line + used, sizeof(line) - used, " %s=%llu/%llu", kNames[i],
        static_cast<unsigned long long>(s_total_ns[i] / s_frames / 1000),
        static_cast<unsigned long long>(s_max_ns[i] / 1000));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line) - used)
      break;
    used += static_cast<size_t>(n);
  }
  LOG_INFO("[SHELLMENU:PROFILE] fps=%.1f avg/max us:%s",
           s_frames * 1e9 / static_cast<double>(now - s_window_start_ns),
           line);
  std::memset(s_total_ns, 0, sizeof(s_total_ns));
  std::memset(s_max_ns, 0, sizeof(s_max_ns));
  s_frames = 0;
  s_window_start_ns = now;
}

} // namespace AniketosShell::Profile
