#pragma once

#include <cstdint>

namespace AniketosTheme {

struct RGB {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

struct HSV {
  float h = 0.0f; // degrees [0, 360)
  float s = 0.0f; // [0, 1]
  float v = 0.0f; // [0, 1]
};

RGB ClampRGB(RGB color);
/* The eight-bit channels the colour is drawn with, as 0xRRGGBB: an animation
   step that leaves this the same changes nothing on screen. */
uint32_t RGBBytes(RGB color);
HSV RGBToHSV(RGB color);
RGB HSVToRGB(HSV color);

// Replaces hue/saturation with the theme while retaining relative source
// brightness, then blends by strength [0,1]. Used for native UI panels.
RGB TintPreservingBrightness(RGB source, RGB theme, float strength);

} // namespace AniketosTheme
