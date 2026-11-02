#pragma once

namespace AniketosShell::Buzzer {

// These values are the system's one-shot ICC beep patterns.  They do not
// modify the console's persisted Beep and Light preferences.
enum class Pattern {
  Short = 1,
  Error = 2,
  Long = 3,
};

// Official, persisted PS5 "Beep and Light" volume levels.  The console's
// physical buzzer has no programmable pitch API; these are its supported
// Low/Medium/High acoustic levels.
enum class Volume {
  High = 0,
  Medium = 1,
  Low = 2,
};

// Called during ShellUI startup with exports dynamically resolved from the
// running console's libkernel.  Null exports leave the feature unavailable.
void Configure(void *set_buzzer, void *set_dim_setting);

bool IsSupported();

// Emits one hardware buzzer pattern.  No addresses, firmware offsets, or
// persisted ICC preference changes are used.
bool PlayTest(Pattern pattern);

// Changes the PS5 system buzzer volume and plays a short confirmation beep.
bool SetVolumeAndConfirm(Volume volume);

} // namespace AniketosShell::Buzzer
