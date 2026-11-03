/* Host tests for the built-in particle styles (no PS5/Mono). */
#include "test_harness.h"

#include "aniketos_shell/home/particle_sim.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace ParticleSim;

namespace {

/* Relative to source/util/tests, where the tests run. */
constexpr const char *kTextures = "../../shellui/assets/particles/builtin/";

int test_every_style_is_complete() {
  TEST_ASSERT_TRUE(PresetCount() > 0);
  for (int index = 0; index < PresetCount(); ++index) {
    const Preset &preset = PresetAt(index);
    TEST_ASSERT_TRUE(preset.key && preset.key[0]);
    for (int other = 0; other < index; ++other)
      TEST_ASSERT_TRUE(std::strcmp(preset.key, PresetAt(other).key) != 0);
    float shares = 0;
    for (const Layer &layer : preset.layers) {
      if (layer.share <= 0)
        break;
      shares += layer.share;
      TEST_ASSERT_TRUE(layer.textures[0] != nullptr);
      for (const char *texture : layer.textures) {
        if (!texture)
          break;
        struct stat info {};
        const std::string path = std::string(kTextures) + texture + ".png";
        TEST_ASSERT_TRUE(stat(path.c_str(), &info) == 0);
      }
      TEST_ASSERT_TRUE(layer.size_min > 0 && layer.size_min <= layer.size_max);
      TEST_ASSERT_TRUE(layer.speed_min <= layer.speed_max);
      TEST_ASSERT_TRUE(layer.life_min > 0 && layer.life_min <= layer.life_max);
      TEST_ASSERT_TRUE(layer.opacity_min >= 0 && layer.opacity_max <= 1 &&
                       layer.opacity_min <= layer.opacity_max);
      TEST_ASSERT_TRUE(layer.twinkle >= 0 && layer.twinkle <= 1);
    }
    TEST_ASSERT_TRUE(shares > 0.99f && shares < 1.01f);
  }
  return 0;
}

/* Ten seconds of every style: nothing runs off to infinity, and a fair part
   of the particles is on screen and visible. */
int test_every_style_keeps_the_screen_alive() {
  const float width = 1920, height = 1080;
  for (int index = 0; index < PresetCount(); ++index) {
    const Preset &preset = PresetAt(index);
    std::vector<Particle> particles(110);
    Random random;
    for (size_t i = 0; i < particles.size(); ++i)
      Spawn(particles[i], preset,
            LayerFor(preset, static_cast<int>(i), 110), random, width, height,
            true);
    for (int frame = 0; frame < 600; ++frame)
      for (Particle &particle : particles)
        if (!Step(particle, preset, 1.0f / 60.0f, width, height))
          Spawn(particle, preset, particle.layer, random, width, height,
                false);
    int visible = 0;
    for (const Particle &particle : particles) {
      TEST_ASSERT_TRUE(std::isfinite(particle.x) && std::isfinite(particle.y));
      const float opacity = Opacity(particle, preset);
      TEST_ASSERT_TRUE(opacity >= 0 && opacity <= 1);
      if (opacity > 0.02f && particle.x >= 0 && particle.x <= width &&
          particle.y >= 0 && particle.y <= height)
        ++visible;
    }
    TEST_ASSERT_TRUE(visible >= 6);
  }
  return 0;
}

int test_layers_split_by_share() {
  Preset preset{};
  preset.key = "split";
  preset.layers[0].share = 0.25f;
  preset.layers[1].share = 0.75f;
  int first = 0;
  for (int i = 0; i < 100; ++i)
    first += LayerFor(preset, i, 100) == 0;
  TEST_ASSERT_TRUE(first == 25);
  return 0;
}

int test_hue_turns_colours_and_tints_white() {
  TEST_ASSERT_TRUE(ShiftHue(0x3366cc, 0) == 0x3366cc);
  TEST_ASSERT_TRUE(ShiftHue(0x3366cc, 360) == 0x3366cc);
  TEST_ASSERT_TRUE(ShiftHue(0xff0000, 120) == 0x00ff00);
  TEST_ASSERT_TRUE(ShiftHue(0xff0000, 240) == 0x0000ff);
  TEST_ASSERT_TRUE(ShiftHue(0xff0000, -120) == 0x0000ff);
  /* white gets the angle's own hue, at its brightness */
  const uint32_t white = ShiftHue(0xffffff, 120);
  TEST_ASSERT_TRUE(((white >> 8) & 255) == 255 && (white >> 16) < 255 &&
                   (white & 255) < 255);
  return 0;
}

} // namespace

extern "C" int test_particle_sim_suite(void) {
  int failures = 0;
  failures += onion_test_run("particle_sim.every_style_is_complete",
                             test_every_style_is_complete);
  failures += onion_test_run("particle_sim.every_style_keeps_the_screen_alive",
                             test_every_style_keeps_the_screen_alive);
  failures += onion_test_run("particle_sim.layers_split_by_share",
                             test_layers_split_by_share);
  failures += onion_test_run("particle_sim.hue_turns_colours_and_tints_white",
                             test_hue_turns_colours_and_tints_white);
  return failures;
}
