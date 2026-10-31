#pragma once

#include <onion/distribution.h>

#include <string>
#include <vector>

/* The user's font in place of the system's Latin font, everywhere ShellUI
 * draws text. ShellUI's text engine (libScePsm, FreeType inside) opens every
 * font file with sceKernelOpen; its import of that function is pointed at a
 * filter that hands it the user's file when it asks for one of the SST Latin
 * fonts (SST-Roman.otf, SST-Bold.otf, ...). Script fonts (Japanese, Arabic,
 * Thai, ...) stay the system's. Nothing on disk changes: the import is patched
 * in ShellUI's memory only and is gone when ShellUI restarts.
 *
 * Driven by the Shell Menu settings custom_font_enabled and custom_font_path
 * (a file in kDirectory). */
namespace AniketosTheme::CustomFont {

/* as ShellUI sees it; over FTP it is ONION_DIST_DATA_ROOT "/fonts" */
constexpr const char *kDirectory = ONION_DIST_USER_DATA_ROOT "/fonts";

void Install();

/* UI thread, once a frame: applies a changed setting. */
void Tick();

/* Changes whenever text switches to another font (the user's or the
   system's): widths and baselines measured before are stale then. */
int Generation();

/* The font files in kDirectory (.otf/.ttf), sorted. */
const std::vector<std::string> &Files(bool rescan = false);
std::string FileName(const std::string &path);

} // namespace AniketosTheme::CustomFont
