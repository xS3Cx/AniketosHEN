#include "aniketos_shell/home/particle_sim.hpp"

#include <algorithm>
#include <cmath>

namespace ParticleSim {
namespace {

constexpr float kPi = 3.14159265f;

/* Members in declaration order (designated initializers). Each group's file
   holds whole presets, each ending in a comma. */
const Preset kPresets[] = {
#include "particle_presets/group_a.inc"
#include "particle_presets/group_b.inc"
#include "particle_presets/group_c.inc"
#include "particle_presets/group_d.inc"
#include "particle_presets/group_e.inc"
#include "particle_presets/group_f.inc"
#include "particle_presets/group_g.inc"
};

template <typename T, int N> int filled(const T (&items)[N]) {
  int count = 0;
  while (count < N && items[count])
    ++count;
  return count;
}

/* Far ones drift slower, as they would further away. */
float pace(const Particle &particle) { return 0.45f + 0.55f * particle.depth; }

void place(Particle &particle, const Layer &layer, Random &random, float width,
           float height) {
  const float outside = particle.size * 0.5f;
  switch (layer.area) {
  case Area::Screen:
    particle.x = random.between(0, width);
    particle.y = random.between(0, height);
    break;
  case Area::Top:
    particle.x = random.between(0, width);
    particle.y = -outside;
    break;
  case Area::Bottom:
    particle.x = random.between(0, width);
    particle.y = height + outside;
    break;
  case Area::Left:
    particle.x = -outside;
    particle.y = random.between(0, height);
    break;
  case Area::Right:
    particle.x = width + outside;
    particle.y = random.between(0, height);
    break;
  case Area::Box:
    particle.x = random.between(layer.x0, layer.x1) * width;
    particle.y = random.between(layer.y0, layer.y1) * height;
    break;
  }
}

bool on_screen(const Particle &particle, float width, float height) {
  const float margin = particle.size + 4.0f;
  return particle.x > -margin && particle.x < width + margin &&
         particle.y > -margin && particle.y < height + margin;
}

} // namespace

float Random::next() {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<float>(state & 0xffffff) / 16777216.0f;
}

int PresetCount() {
  return static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
}

const Preset &PresetAt(int index) {
  return kPresets[std::clamp(index, 0, PresetCount() - 1)];
}

int LayerFor(const Preset &preset, int index, int count) {
  const float at = (static_cast<float>(index) + 0.5f) /
                   static_cast<float>(std::max(count, 1));
  float total = 0;
  int last = 0;
  for (int layer = 0; layer < 3 && preset.layers[layer].share > 0; ++layer) {
    total += preset.layers[layer].share;
    last = layer;
    if (at < total)
      return layer;
  }
  return last;
}

void Spawn(Particle &particle, const Preset &preset, int layer_index,
           Random &random, float width, float height, bool prewarm) {
  const Layer &layer = preset.layers[layer_index];
  particle.layer = static_cast<uint8_t>(layer_index);
  const int textures = std::max(1, filled(layer.textures));
  particle.texture = static_cast<uint8_t>(
      std::min(textures - 1, static_cast<int>(random.next() * textures)));
  const int colors = filled(layer.colors);
  particle.color =
      colors ? layer.colors[std::min(colors - 1,
                                     static_cast<int>(random.next() * colors))]
             : 0xffffff;

  particle.depth = std::pow(random.next(), std::max(layer.depth_power, 0.1f));
  const float depth = particle.depth;
  particle.size = layer.size_min + (layer.size_max - layer.size_min) * depth;
  particle.peak =
      layer.opacity_min + (layer.opacity_max - layer.opacity_min) * depth;

  const float heading =
      (layer.direction + random.between(-layer.spread, layer.spread)) * kPi /
      180.0f;
  const float speed = random.between(layer.speed_min, layer.speed_max) *
                      pace(particle);
  particle.vx = std::cos(heading) * speed;
  particle.vy = std::sin(heading) * speed;
  particle.sway_x = -std::sin(heading);
  particle.sway_y = std::cos(heading);
  particle.sway_phase = random.between(0, 2 * kPi);
  particle.sway_hz = layer.sway_hz * random.between(0.7f, 1.3f);
  particle.twinkle_phase = random.between(0, 2 * kPi);
  particle.twinkle_hz = layer.twinkle_hz * random.between(0.6f, 1.4f);
  /* upright unless it turns: the console only turns those that do */
  particle.turn =
      layer.spin_min != 0 || layer.spin_max != 0 ? random.between(0, 360) : 0;
  particle.angle = particle.turn;
  particle.spin = random.between(layer.spin_min, layer.spin_max) *
                  (random.next() < 0.5f ? -1.0f : 1.0f);
  particle.life = random.between(layer.life_min, layer.life_max);
  particle.age = 0;
  place(particle, layer, random, width, height);
  if (!prewarm)
    return;

  /* Somewhere along its way already, where that is still on the screen. */
  const float x = particle.x, y = particle.y, vx = particle.vx,
              vy = particle.vy;
  const float gx = layer.gravity_x * pace(particle);
  const float gy = layer.gravity_y * pace(particle);
  for (int attempt = 0; attempt < 6; ++attempt) {
    const float age = random.between(0, particle.life);
    particle.x = x + vx * age + 0.5f * gx * age * age;
    particle.y = y + vy * age + 0.5f * gy * age * age;
    if (on_screen(particle, width, height)) {
      particle.age = age;
      particle.vx = vx + gx * age;
      particle.vy = vy + gy * age;
      return;
    }
  }
  particle.x = x;
  particle.y = y;
}

bool Step(Particle &particle, const Preset &preset, float seconds, float width,
          float height) {
  const Layer &layer = preset.layers[particle.layer];
  const float factor = pace(particle);
  particle.age += seconds;
  particle.vx += layer.gravity_x * factor * seconds;
  particle.vy += layer.gravity_y * factor * seconds;
  particle.sway_phase += 2 * kPi * particle.sway_hz * seconds;
  const float sway = layer.sway * factor * std::sin(particle.sway_phase);
  particle.x += (particle.vx + particle.sway_x * sway) * seconds;
  particle.y += (particle.vy + particle.sway_y * sway) * seconds;
  particle.twinkle_phase += 2 * kPi * particle.twinkle_hz * seconds;
  particle.turn = std::fmod(particle.turn + particle.spin * seconds, 360.0f);
  particle.angle =
      (layer.align ? std::atan2(particle.vy, particle.vx) * 180.0f / kPi
                   : particle.turn) +
      layer.rock * std::sin(particle.sway_phase);
  return particle.age < particle.life && on_screen(particle, width, height);
}

float Opacity(const Particle &particle, const Preset &preset) {
  const Layer &layer = preset.layers[particle.layer];
  const float in =
      layer.fade_in > 0 ? std::min(1.0f, particle.age / layer.fade_in) : 1.0f;
  const float out = layer.fade_out > 0
                        ? std::min(1.0f, (particle.life - particle.age) /
                                             layer.fade_out)
                        : 1.0f;
  const float dip =
      layer.twinkle * (0.5f + 0.5f * std::sin(particle.twinkle_phase));
  return particle.peak * std::max(0.0f, std::min(in, out)) * (1.0f - dip);
}

uint32_t ShiftHue(uint32_t rgb, int degrees) {
  degrees = ((degrees % 360) + 360) % 360;
  if (!degrees)
    return rgb;
  const float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f,
              b = (rgb & 255) / 255.0f;
  const float high = std::max({r, g, b}), low = std::min({r, g, b});
  const float chroma = high - low;
  float hue = 0, saturation = high > 0 ? chroma / high : 0;
  if (saturation < 0.12f) {
    hue = static_cast<float>(degrees);
    saturation = 0.45f;
  } else {
    if (high == r)
      hue = 60.0f * std::fmod((g - b) / chroma + 6.0f, 6.0f);
    else if (high == g)
      hue = 60.0f * ((b - r) / chroma + 2.0f);
    else
      hue = 60.0f * ((r - g) / chroma + 4.0f);
    hue = std::fmod(hue + static_cast<float>(degrees), 360.0f);
  }
  /* back from hue, saturation and value */
  const float c = high * saturation;
  const float x = c * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
  const float m = high - c;
  float out[3] = {};
  switch (static_cast<int>(hue / 60.0f) % 6) {
  case 0: out[0] = c, out[1] = x; break;
  case 1: out[0] = x, out[1] = c; break;
  case 2: out[1] = c, out[2] = x; break;
  case 3: out[1] = x, out[2] = c; break;
  case 4: out[0] = x, out[2] = c; break;
  default: out[0] = c, out[2] = x; break;
  }
  uint32_t shifted = 0;
  for (float channel : out)
    shifted = (shifted << 8) |
              static_cast<uint32_t>(std::lround((channel + m) * 255.0f));
  return shifted;
}

} // namespace ParticleSim
