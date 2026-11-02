/* Opt-in frame profiler and scripted input for the Shell Menu.
 *
 * Off unless the file ONION_DIST_USER_DATA_ROOT "/shellmenu_profile" exists
 * when SceShellUI is injected; then every 300 rendered frames one log line
 * reports the average and worst time spent in each section of the render
 * hook. With profiling on, ".../shellmenu_script" is also consumed: a string
 * of T (toggle menu) U D L R X (cross) O (circle) Q (square), S (Settings)
 * H (Home), P C (dumps) and '.' (wait), one step every 12 frames (lower-case
 * u/d: a held direction, every 8 frames), so the menu can be exercised from a
 * PC without a controller. When the marker file is absent a Scope costs one
 * branch.
 */
#pragma once

#include <cstdint>

namespace AniketosShell::Profile {

enum Section : int {
  kShellClock,
  kSharedUi,
  kTheme,
  kShellManager,
  kRunningApp,
  kMenuInput,
  kMenuTick,
  kMenuWidgets,
  kMenuOverlay,
  kMenuAnimations,
  kHomeTiles,
  kSectionCount,
};

extern bool enabled;

void Initialize();
/** Close the frame: publish statistics, feed scripted input. UI thread. */
void EndFrame();
/** Scripted toggle request / buttons for this frame (0 when none). */
bool ConsumeScriptToggle();
uint32_t ConsumeScriptButtons();
uint32_t ConsumeScriptRepeated();

uint64_t NowNs();
void Add(Section section, uint64_t elapsed_ns);

class Scope {
public:
  explicit Scope(Section section)
      : section_(section), start_(enabled ? NowNs() : 0) {}
  ~Scope() {
    if (start_)
      Add(section_, NowNs() - start_);
  }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

private:
  Section section_;
  uint64_t start_;
};

} // namespace AniketosShell::Profile
