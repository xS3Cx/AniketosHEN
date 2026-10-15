#pragma once

namespace AniketosShell::TextUnits {

// Use ordinary UI-font characters here; the PS Symbol Web PUA glyph is not
// present in the font used by injected ShellUI labels.
inline constexpr char kCelsiusSuffix[] = "\xC2\xB0" "C";
inline constexpr char kCelsiusPlaceholder[] = "--\xC2\xB0" "C";

} // namespace AniketosShell::TextUnits
