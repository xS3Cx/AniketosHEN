#pragma once

#include <cstdint>

namespace AniketosShell::Input {

/** Auto-repeat period of a held up/down direction, in milliseconds. */
inline constexpr unsigned kListRepeatMs = 125;

bool Start();
bool ConsumeToggleRequest();
uint32_t ConsumeButtonsDown();
/** Which of the buttons just consumed came from holding a direction rather
 * than a new press. Call right after ConsumeButtonsDown(). */
uint32_t ConsumeRepeatedButtons();
/** Consume one button edge captured while the game-mod menu was hidden. */
bool ConsumeGameButtonPress(uint32_t button);
// Latest left-stick position, normalized to -1..1 after a radial dead zone.
// Safe to sample from ShellUI's UI thread; returns zero while not capturing.
void ReadLeftStick(float &x, float &y);
void SetMenuCapturing(bool capturing);
bool IsMenuCapturing();

// Borrowed libScePad handle owned by the passive reader thread. Callers must
// never close it. Returns -1 while no foreground controller is connected.
int ActivePadHandle();

} // namespace AniketosShell::Input
