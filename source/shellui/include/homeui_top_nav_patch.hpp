/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#pragma once

struct MonoImage;

/*
 * HomeUI RNPS/Hermes top-nav patch:
 * inserts the OnionHEN icon-button slot between Search and Settings by reusing
 * a hidden system entry.
 *
 * Disable for A/B crash isolation (must recompile shellui):
 *   -D SHELLUI_HOMEUI_TOP_NAV_PATCH=0
 * or CMake: -DONIONHEN_SHELLUI_HOMEUI_TOP_NAV_PATCH=OFF
 */
#ifndef SHELLUI_HOMEUI_TOP_NAV_PATCH
#define SHELLUI_HOMEUI_TOP_NAV_PATCH 1
#endif

/* Whether the Home bundle last loaded got the top-bar icon from the byte
 * patch. When it did not (a firmware without a profile), the script
 * home_js.cpp puts into Home adds the icon instead. */
bool homeui_top_nav_patched();

/* What the last Home bundle's tiles ended up with, so the focus outline and
 * the arc labels match them: the rounding the patch gave them (percent of a
 * full circle; -1 when they kept Sony's shape, -2 before a Home bundle was
 * seen), and the focus frame's corner in pixels (negative before a Home
 * bundle was seen). */
int homeui_tile_rounding();
float homeui_tile_focus_radius();

void patch_homeui_top_nav(unsigned char *buffer, int *size_ptr,
                          int buffer_capacity);
void install_homeui_top_nav_hooks(MonoImage *react_pui);
