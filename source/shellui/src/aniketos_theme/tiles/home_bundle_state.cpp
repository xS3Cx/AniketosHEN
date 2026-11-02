/* Copyright (C) 2026 AniketosHEN — what the patches left in the Home bundle. */

#include "homeui_top_nav_patch.hpp"

#include <atomic>
#include <cmath>

/* Set when a Home bundle was given the top-bar icon by the byte patch. */
static std::atomic<bool> s_top_nav_patched{false};

/* What the last Home bundle's tiles ended up with: the rounding (percent of
   a full circle, -1 for Sony's shape; -2 until a Home bundle was seen) and
   the focus frame's corner in thousandths of a pixel (-1 until then). */
static std::atomic<int> s_tile_rounding_percent{-2};
static std::atomic<int> s_tile_focus_radius_milli{-1};

bool homeui_top_nav_patched() {
  return s_top_nav_patched.load(std::memory_order_relaxed);
}

void homeui_note_top_nav_patched(bool patched) {
  s_top_nav_patched.store(patched, std::memory_order_relaxed);
}

int homeui_tile_rounding() {
  return s_tile_rounding_percent.load(std::memory_order_relaxed);
}

float homeui_tile_focus_radius() {
  const int milli = s_tile_focus_radius_milli.load(std::memory_order_relaxed);
  return milli < 0 ? -1.0f : static_cast<float>(milli) / 1000.0f;
}

void homeui_publish_tiles(int rounding, double focus_radius) {
  s_tile_focus_radius_milli.store(
      static_cast<int>(std::lround(focus_radius * 1000.0)),
      std::memory_order_relaxed);
  s_tile_rounding_percent.store(rounding, std::memory_order_relaxed);
}
