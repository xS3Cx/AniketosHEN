/* Host tests for colouring the logo (no PS5/Mono). */
#include "test_harness.h"

#include "logo_tint.hpp"

#include <cstdint>

using namespace LogoTint;

namespace {

int test_a_blue_pixel_takes_the_colour() {
  uint8_t pixel[4] = {20, 80, 240, 255};
  Apply(pixel, 1, 255, 0, 0);
  TEST_ASSERT_EQ_INT(240, pixel[0]);
  TEST_ASSERT_TRUE(pixel[1] < 30 && pixel[2] < 30);
  TEST_ASSERT_EQ_INT(255, pixel[3]);
  return 0;
}

int test_white_and_transparent_stay() {
  uint8_t white[4] = {255, 255, 255, 255};
  Apply(white, 1, 255, 0, 0);
  TEST_ASSERT_EQ_INT(255, white[1]);
  uint8_t clear[4] = {20, 80, 240, 0};
  Apply(clear, 1, 255, 0, 0);
  TEST_ASSERT_EQ_INT(80, clear[1]);
  return 0;
}

int test_white_takes_the_colour_out() {
  uint8_t pixel[4] = {20, 80, 240, 255};
  Apply(pixel, 1, 255, 255, 255);
  TEST_ASSERT_EQ_INT(240, pixel[0]);
  TEST_ASSERT_EQ_INT(240, pixel[1]);
  TEST_ASSERT_EQ_INT(240, pixel[2]);
  return 0;
}

int test_every_hue_comes_back() {
  for (int rgb = 0; rgb < 6; ++rgb) {
    const uint8_t r = rgb == 0 || rgb == 1 || rgb == 5 ? 255 : 0;
    const uint8_t g = rgb >= 1 && rgb <= 3 ? 255 : 0;
    const uint8_t b = rgb >= 3 && rgb <= 5 ? 255 : 0;
    uint8_t pixel[4] = {0, 0, 255, 255};
    Apply(pixel, 1, r, g, b);
    TEST_ASSERT_EQ_INT(r, pixel[0]);
    TEST_ASSERT_EQ_INT(g, pixel[1]);
    TEST_ASSERT_EQ_INT(b, pixel[2]);
  }
  return 0;
}

} // namespace

extern "C" int test_logo_tint_suite(void) {
  int failures = 0;
  failures += onion_test_run("logo_tint.a_blue_pixel_takes_the_colour",
                             test_a_blue_pixel_takes_the_colour);
  failures += onion_test_run("logo_tint.white_and_transparent_stay",
                             test_white_and_transparent_stay);
  failures += onion_test_run("logo_tint.white_takes_the_colour_out",
                             test_white_takes_the_colour_out);
  failures += onion_test_run("logo_tint.every_hue_comes_back",
                             test_every_hue_comes_back);
  return failures;
}
