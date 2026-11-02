#pragma once

#include <cstdint>

namespace AniketosUI {

/* Sce.PlayStation.PUI.UIColor and Nullable<UIColor> as managed code lays them
 * out: detours take these by value. Not the 16-byte AniketosUI::Color. */
struct UIColor {
  float r, g, b, a;
  int32_t color_space;
};
struct NullableUIColor {
  uint8_t has_value;
  uint8_t padding[3];
  UIColor value;
};
static_assert(sizeof(UIColor) == 20);
static_assert(sizeof(NullableUIColor) == 24);

} // namespace AniketosUI
