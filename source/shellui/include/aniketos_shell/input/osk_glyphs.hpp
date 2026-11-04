#pragma once

#include "external_symbols.hpp"

/* The system's on-screen keyboard (Sce.Vsh.ShellUI.Ime): a key of ours
 * next to Shift that turns the letter keys into the console's own glyphs
 * (private-use characters of the system font), so they can be typed.
 * Everything is found by name at run time. */
namespace OskGlyphs {
void Install();
/* UI thread, once a frame: installs once the keyboard's code is loaded. */
void Tick();
/* Whether `codepoint` is one of the console's glyphs the keyboard's glyph
 * pages offer (any thread). */
bool HasGlyph(uint32_t codepoint);
}
