#pragma once

#include "external_symbols.hpp"

#include <string>

namespace AniketosShell::Firmware {

/** Read the firmware using the SDK kernel version first, then the optional
 * libkernel_sys export. The returned structure always includes a printable
 * major.minor string when either source returns a valid version. */
bool Read(OrbisKernelSwVersion &out);

/** The version as people write it, major.minor ("11.40", "13.42"), made from
 * the version number itself, so the same on every console; the system's own
 * text is longer ("13.420.000"). Empty if neither is usable. */
std::string Short(const OrbisKernelSwVersion &firmware);

} // namespace AniketosShell::Firmware
