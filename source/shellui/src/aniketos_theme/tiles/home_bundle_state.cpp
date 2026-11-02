/* Copyright (C) 2026 AniketosHEN — what the patches left in the Home bundle. */

#include "homeui_top_nav_patch.hpp"

#include <atomic>
#include <cmath>

/* Set when a Home bundle was given the top-bar icon by the byte patch. */
static std::atomic<bool> s_top_nav_patched{false};

/* The focus frame's corner on the last Home bundle's tiles, in thousandths of
   a pixel (-1 until a Home bundle was seen). */
static std::atomic<int> s_tile_focus_radius_milli{-1};

bool homeui_top_nav_patched() {
  return s_top_nav_patched.load(std::memory_order_relaxed);
}

void homeui_note_top_nav_patched(bool patched) {
  s_top_nav_patched.store(patched, std::memory_order_relaxed);
}

float homeui_tile_focus_radius() {
  const int milli = s_tile_focus_radius_milli.load(std::memory_order_relaxed);
  return milli < 0 ? -1.0f : static_cast<float>(milli) / 1000.0f;
}

void homeui_publish_tiles(double focus_radius) {
  s_tile_focus_radius_milli.store(
      static_cast<int>(std::lround(focus_radius * 1000.0)),
      std::memory_order_relaxed);
}
