#pragma once

#include <cstdint>

/* The built-in particle styles behind Home: what each one looks like and how
 * its particles move. Plain arithmetic, nothing of ShellUI's, so the same code
 * drives the widgets on the console and the preview built on a computer. */
namespace ParticleSim {

/* Where a layer's particles come from. Screen: anywhere on it. The edges: just
 * outside that side of the screen. Box: the rectangle x0,y0 to x1,y1, in
 * fractions of the screen (a point when both corners are the same). */
enum class Area : uint8_t { Screen, Top, Bottom, Left, Right, Box };

/* One kind of particle in a style. Sizes and speeds are in layout pixels of a
 * 1920x1080 screen; each particle gets a depth, and the far ones (depth 0)
 * take the low end of every range, the near ones (depth 1) the high end. */
struct Layer {
  float share = 0;              /* of the particles; 0 ends a style's layers */
  const char *textures[4] = {}; /* built-in texture names, picked at random */
  uint32_t colors[6] = {};      /* 0xRRGGBB tints, picked at random; 0 ends */
  Area area = Area::Screen;
  float x0 = 0, y0 = 0, x1 = 1, y1 = 1; /* Box only */
  float direction = 0, spread = 180;     /* degrees: 0 right, 90 down; +- */
  float speed_min = 5, speed_max = 20;   /* px/s */
  float gravity_x = 0, gravity_y = 0;    /* px/s^2 */
  float sway = 0, sway_hz = 0.2f;        /* sideways wander: px/s, Hz */
  float size_min = 8, size_max = 32;     /* px */
  float opacity_min = 0.2f, opacity_max = 0.8f;
  float twinkle = 0, twinkle_hz = 1;     /* how far the opacity dips, Hz */
  float life_min = 10, life_max = 20;    /* s */
  float fade_in = 2, fade_out = 2;       /* s */
  float spin_min = 0, spin_max = 0;      /* degrees/s, either way round */
  float rock = 0; /* degrees it tilts to and fro, in time with its sway */
  float depth_power = 1.5f; /* above 1: most particles far away */
  bool additive = false;    /* light adds up (glows, sparks) */
  bool align = false; /* turned along its motion; such textures point right */
};

struct Preset {
  const char *key; /* i18n vsh.home_particles.style.<key>, logs */
  Layer layers[3];
};

struct Particle {
  uint8_t layer = 0, texture = 0;
  uint32_t color = 0xffffff;
  float x = 0, y = 0, vx = 0, vy = 0; /* centre, px; px/s */
  float size = 0, peak = 0, depth = 0;
  float age = 0, life = 1;
  float sway_phase = 0, sway_hz = 0, sway_x = 0, sway_y = 0;
  float twinkle_phase = 0, twinkle_hz = 0;
  float angle = 0, spin = 0; /* degrees, degrees/s */
  float turn = 0;            /* where the spin has got to; angle adds the rock */
};

struct Random {
  uint32_t state = 0x2545f491u;
  float next(); /* [0, 1) */
  float between(float low, float high) { return low + (high - low) * next(); }
};

/* The built-in styles; menu entry n + 1 is style n (entry 0: the user's
   pictures). */
int PresetCount();
const Preset &PresetAt(int index);

/* The layer particle `index` of `count` belongs to, by the layers' shares. */
int LayerFor(const Preset &preset, int index, int count);

/* A new particle of `layer`. With `prewarm` it is somewhere along its life
   already, so a style starts with the screen filled instead of empty. */
void Spawn(Particle &particle, const Preset &preset, int layer,
           Random &random, float width, float height, bool prewarm);

/* Moves it on by `seconds`; false once it is over (its life is up, or it left
   the screen for good) and should be spawned again. */
bool Step(Particle &particle, const Preset &preset, float seconds, float width,
          float height);

/* Its opacity now: the peak, faded in and out, dipping when it twinkles. */
float Opacity(const Particle &particle, const Preset &preset);

/* A tint `degrees` round the colour wheel; 0 keeps it. White and grey,
   which have no hue to turn, take on that of the angle. */
uint32_t ShiftHue(uint32_t rgb, int degrees);

} // namespace ParticleSim
