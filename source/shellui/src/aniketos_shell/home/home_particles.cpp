#include "aniketos_shell/home/home_particles.hpp"

#include "aniketos_shell/core/frame_profile.hpp"
#include "aniketos_shell/core/host_scene.hpp"
#include "aniketos_shell/home/particle_sim.hpp"
#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/ui_color.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define EMBEDDED(name)                                                         \
  extern const unsigned char particle_##name##_png_start[];                    \
  extern const unsigned char particle_##name##_png_end[];
EMBEDDED(triangle)
EMBEDDED(circle)
EMBEDDED(cross)
EMBEDDED(square)
EMBEDDED(dot)
#undef EMBEDDED

#define PARTICLE_TEXTURE(name, symbol)                                         \
  extern const unsigned char symbol##_start[];                                 \
  extern const unsigned char symbol##_end[];
#include "particle_textures.inc"
#undef PARTICLE_TEXTURE

namespace HomeParticles {
namespace {

namespace Reflect = AniketosUI::Reflect;

constexpr const char *kDirectory =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/particles";
constexpr const char *kBuiltinDirectory =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/particles/builtin";

struct BuiltinTexture {
  const char *name;
  const unsigned char *start;
  const unsigned char *end;
};
const BuiltinTexture kBuiltinTextures[] = {
#define PARTICLE_TEXTURE(name, symbol) {name, symbol##_start, symbol##_end},
#include "particle_textures.inc"
#undef PARTICLE_TEXTURE
};
constexpr const char *kLayerName = "id_aniketos_home_particles";
constexpr int kMinCount = 20;
constexpr int kMaxCount = 150; /* each one is a widget moved every frame */
constexpr size_t kMaxPictures = 16;

struct DefaultPicture {
  const char *file;
  const unsigned char *start;
  const unsigned char *end;
};
const DefaultPicture kDefaultPictures[] = {
    {"triangle.png", particle_triangle_png_start, particle_triangle_png_end},
    {"circle.png", particle_circle_png_start, particle_circle_png_end},
    {"cross.png", particle_cross_png_start, particle_cross_png_end},
    {"square.png", particle_square_png_start, particle_square_png_end},
    {"dot.png", particle_dot_png_start, particle_dot_png_end},
};

struct Particle {
  uint32_t widget = 0; /* strong handle */
  float x = 0, y = 0;  /* centre, layout units */
  float vx = 0, vy = 0; /* units a second */
  float size = 0;
  float peak = 0;      /* opacity at its brightest */
  float age = 0, life = 1;
  float sway = 0, sway_speed = 0; /* a slow sideways wander */
  float shown = -1;    /* the opacity last given to the widget */
  float put_x = -1e9f, put_y = -1e9f; /* where the widget was last put */
};

std::vector<Particle> s_particles;
std::vector<uint32_t> s_assets; /* ImageAsset per picture, strong */
std::vector<bool> s_asset_is_dot;

/* A built-in style's particle and what its widget or sprite was last given.
   A layer whose particles turn gets an ImageBox each (flock -1): PUI turns
   only widgets. The others share one SpriteListElement per texture, a
   "flock", and each particle is a SpriteUnit in it: a plain record of
   x, y, size and colour that the element draws in one go. */
struct Sprite {
  ParticleSim::Particle state;
  uint32_t transform = 0; /* strong: moves and turns it (widget) */
  uint32_t unit = 0;      /* strong: its SpriteUnit (flock) */
  int flock = -1;
  int asset = -1;
  uint32_t color = 0;
  float size = -1, angle = 1e9f;
  float x = -1e9f, y = -1e9f; /* where it was last put */
};
std::vector<Sprite> s_sprites; /* s_particles[i].widget is sprite i's */
struct Flock {
  uint32_t element = 0; /* strong; the layer's Elements keep it alive */
  uint8_t layer = 0, texture = 0;
};
std::vector<Flock> s_flocks;
int s_flock_of[3][4]; /* flock per layer texture, or -1 */
const ParticleSim::Preset *s_preset = nullptr;
int s_layer_assets[3][4]; /* index into s_assets per layer texture, or -1 */
ParticleSim::Random s_sim_random;
MonoMethod *s_set_width = nullptr;
MonoMethod *s_set_height = nullptr;
MonoMethod *s_set_image = nullptr;
MonoMethod *s_set_filter_color = nullptr;
MonoMethod *s_set_angle = nullptr;
MonoMethod *s_set_translation = nullptr;
/* SpriteUnit: its setters are bare field stores, called compiled. */
MonoMethod *s_unit_set_x = nullptr;
MonoMethod *s_unit_set_y = nullptr;
MonoMethod *s_unit_set_width = nullptr;
MonoMethod *s_unit_set_height = nullptr;
MonoMethod *s_unit_set_opacity = nullptr;
MonoMethod *s_unit_set_color = nullptr;
MonoMethod *s_flock_need_update = nullptr;
/* Vector3 reaches compiled code in two 8-byte slots: x,y then z. */
void (*s_translate)(MonoObject *, uint64_t, uint64_t) = nullptr;
int s_style = -1; /* what the layer was built for */
int s_hue = 0;    /* the tints' turn the widgets have */
/* With the frame profiler on: the simulation against the PUI writes. */
uint64_t s_sim_ns = 0, s_put_ns = 0, s_profiled_frames = 0;

uint32_t s_layer = 0;
float s_width = 1920.0f, s_height = 1080.0f;
int s_frame = 0;
int s_failures = 0;
uint32_t s_random = 0x2545f491u;
MonoMethod *s_set_x = nullptr;
MonoMethod *s_set_y = nullptr;
MonoMethod *s_set_opacity = nullptr;

float random01() {
  s_random ^= s_random << 13;
  s_random ^= s_random >> 17;
  s_random ^= s_random << 5;
  return static_cast<float>(s_random & 0xffffff) / 16777216.0f;
}

float between(float low, float high) { return low + (high - low) * random01(); }

MonoObject *object_property(MonoObject *object, const char *getter) {
  MonoObject *value = nullptr;
  return Reflect::Call(object, getter, 0, nullptr, &value) ? value : nullptr;
}

/* Plain value setters that cannot throw on a live widget: called compiled.
   Tick tears the layer down the frame it is disposed, before any write. */
void set_float(MonoObject *object, MonoMethod *setter, float value) {
  if (!object || !setter)
    return;
  if (auto direct = reinterpret_cast<void (*)(MonoObject *, float)>(
          Reflect::Compiled(setter))) {
    direct(object, value);
    return;
  }
  void *arguments[1] = {&value};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(setter, object, arguments, &exception);
}

/* The defaults, where the user can see and replace them. */
void write_defaults() {
  (void)mkdir_tree(kDirectory);
  for (const DefaultPicture &picture : kDefaultPictures) {
    const std::string path = std::string(kDirectory) + "/" + picture.file;
    struct stat info {};
    if (stat(path.c_str(), &info) == 0)
      continue; /* the user's now, whatever it is */
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
      continue;
    const size_t size = static_cast<size_t>(picture.end - picture.start);
    (void)!write(fd, picture.start, size);
    close(fd);
  }
}

std::vector<std::string> picture_paths() {
  std::vector<std::string> found;
  if (DIR *directory = opendir(kDirectory)) {
    while (dirent *entry = readdir(directory)) {
      const size_t length = std::strlen(entry->d_name);
      if (entry->d_name[0] == '.' || length < 5 ||
          strcasecmp(entry->d_name + length - 4, ".png"))
        continue;
      found.push_back(std::string(kDirectory) + "/" + entry->d_name);
    }
    closedir(directory);
  }
  std::sort(found.begin(), found.end());
  if (found.size() > kMaxPictures)
    found.resize(kMaxPictures);
  return found;
}

void teardown() {
  MonoObject *layer = Reflect::Target(s_layer);
  if (layer && !AniketosUI::HomeScene::IsDisposed(layer)) {
    Reflect::Call(layer, "RemoveFromParent", 0);
    Reflect::Call(layer, "Dispose", 0); /* takes its children with it */
  }
  Reflect::Release(s_layer);
  for (Particle &particle : s_particles)
    Reflect::Release(particle.widget);
  s_particles.clear();
  for (Sprite &sprite : s_sprites) {
    Reflect::Release(sprite.transform);
    Reflect::Release(sprite.unit);
  }
  s_sprites.clear();
  for (Flock &flock : s_flocks)
    Reflect::Release(flock.element);
  s_flocks.clear();
  s_preset = nullptr;
  for (uint32_t &asset : s_assets)
    Reflect::Release(asset);
  s_assets.clear();
  s_asset_is_dot.clear();
}

/* Where a particle starts and how it moves. Most rise out of a source low
   on the right and spread as they go, thinning with distance; the rest are
   scattered over the whole screen and barely move. */
void spawn(Particle &particle, bool dot, bool anywhere_in_life) {
  const float source_x = s_width * 0.72f;
  const float source_y = s_height * 0.95f;
  if (random01() < 0.3f) {
    particle.x = between(0.0f, s_width);
    particle.y = between(0.0f, s_height);
    const float angle = between(0.0f, 6.2832f);
    const float speed = between(2.0f, 9.0f);
    particle.vx = std::cos(angle) * speed;
    particle.vy = std::sin(angle) * speed;
  } else {
    /* a fan opening upwards and to both sides */
    const float angle = between(-3.0f, -0.14f);
    const float reach = std::pow(random01(), 1.6f) * s_width * 0.85f;
    particle.x = source_x + std::cos(angle) * reach;
    particle.y = source_y + std::sin(angle) * reach * 0.8f;
    const float speed = between(6.0f, 26.0f) * (0.35f + reach / s_width);
    particle.vx = std::cos(angle) * speed;
    particle.vy = std::sin(angle) * speed * 0.8f;
  }
  /* depth: far ones are small, dim and slow */
  const float depth = std::pow(random01(), 1.8f);
  if (dot) {
    particle.size = 5.0f + depth * 22.0f;
    particle.peak = 0.18f + depth * 0.5f;
  } else {
    particle.size = 14.0f + depth * 62.0f;
    particle.peak = 0.3f + depth * 0.6f;
  }
  const float pace = 0.4f + depth * 0.9f;
  particle.vx *= pace;
  particle.vy *= pace;
  particle.life = between(14.0f, 34.0f);
  particle.age = anywhere_in_life ? between(0.0f, particle.life) : 0.0f;
  particle.sway = between(0.0f, 6.2832f);
  particle.sway_speed = between(0.15f, 0.5f);
  particle.shown = -1.0f;
}

int wanted_count() {
  return std::clamp(g_settings.home_particles_count, kMinCount, kMaxCount);
}

int current_style() {
  return std::clamp(g_settings.home_particles_style, 0,
                    ParticleSim::PresetCount());
}

/* ImageAsset(string), among its other one-argument constructors. */
MonoMethod *find_asset_ctor(MonoClass *asset_class) {
  if (!asset_class)
    return nullptr;
  void *iter = nullptr;
  while (MonoMethod *method = mono_class_get_methods(asset_class, &iter)) {
    MonoMethodSignature *signature = mono_method_signature(method);
    if (std::strcmp(mono_method_get_name(method), ".ctor") || !signature ||
        mono_signature_get_param_count(signature) != 1)
      continue;
    void *param_iter = nullptr;
    char *type =
        mono_type_get_name(mono_signature_get_params(signature, &param_iter));
    const bool takes_string = type && std::strstr(type, "String");
    if (type && mono_free)
      mono_free(type);
    if (takes_string)
      return method;
  }
  return nullptr;
}

/* The picture at `path` added to s_assets; its index, or -1. */
int open_asset(const std::string &path, MonoClass *asset_class,
               MonoMethod *asset_ctor) {
  MonoObject *asset = mono_object_new(mono_domain_get(), asset_class);
  void *arguments[1] = {mono_string_new(mono_domain_get(), path.c_str())};
  MonoObject *exception = nullptr;
  if (!asset)
    return -1;
  mono_runtime_invoke(asset_ctor, asset, arguments, &exception);
  if (exception) {
    LOG_ERROR("[PARTICLES] '%s' could not be opened", path.c_str());
    return -1;
  }
  s_assets.push_back(mono_gchandle_new(asset, /*pinned=*/0));
  s_asset_is_dot.push_back(path.find("dot") != std::string::npos);
  return static_cast<int>(s_assets.size()) - 1;
}

void load_assets(MonoClass *asset_class, MonoMethod *asset_ctor) {
  for (const std::string &path : picture_paths())
    (void)open_asset(path, asset_class, asset_ctor);
}

/* A built-in texture, written out where PUI can open it. shortcut: a file
   of the same size is taken as current; a redrawn texture of exactly the
   old size would need its file deleted. */
int builtin_asset(const char *name, MonoClass *asset_class,
                  MonoMethod *asset_ctor) {
  for (const BuiltinTexture &texture : kBuiltinTextures) {
    if (std::strcmp(texture.name, name))
      continue;
    const std::string path =
        std::string(kBuiltinDirectory) + "/" + name + ".png";
    const size_t size = static_cast<size_t>(texture.end - texture.start);
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 ||
        static_cast<size_t>(info.st_size) != size) {
      (void)mkdir_tree(kBuiltinDirectory);
      const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd < 0)
        return -1;
      const bool written =
          write(fd, texture.start, size) == static_cast<ssize_t>(size);
      close(fd);
      if (!written)
        return -1;
    }
    return open_asset(path, asset_class, asset_ctor);
  }
  LOG_ERROR("[PARTICLES] no built-in texture '%s'", name);
  return -1;
}

/* The textures `preset` uses, once each; false if a layer has none. */
bool load_preset(const ParticleSim::Preset &preset, MonoClass *asset_class,
                 MonoMethod *asset_ctor) {
  std::vector<std::pair<const char *, int>> loaded;
  for (int layer = 0; layer < 3; ++layer) {
    bool any = preset.layers[layer].share <= 0;
    for (int texture = 0; texture < 4; ++texture) {
      const char *name = preset.layers[layer].textures[texture];
      int &asset = s_layer_assets[layer][texture];
      asset = -1;
      if (!name || preset.layers[layer].share <= 0)
        continue;
      for (const auto &[loaded_name, index] : loaded)
        if (!std::strcmp(loaded_name, name))
          asset = index;
      if (asset < 0) {
        asset = builtin_asset(name, asset_class, asset_ctor);
        loaded.emplace_back(name, asset);
      }
      any |= asset >= 0;
    }
    if (!any)
      return false;
  }
  return true;
}

/* A layer's particles turn when they spin or follow their motion. */
bool turns(const ParticleSim::Layer &layer) {
  return layer.spin_min != 0 || layer.spin_max != 0 || layer.rock != 0 ||
         layer.align;
}

/* Gives the widget or unit what its particle now has: texture, tint and
   size. A unit keeps its flock's texture for all its lives. */
void dress(Sprite &sprite, MonoObject *box) {
  ParticleSim::Particle &state = sprite.state;
  MonoObject *exception = nullptr;
  if (sprite.flock >= 0) {
    MonoObject *unit = Reflect::Target(sprite.unit);
    state.texture = s_flocks[static_cast<size_t>(sprite.flock)].texture;
    const uint32_t color = ParticleSim::ShiftHue(state.color, s_hue);
    if (unit && color != sprite.color && s_unit_set_color) {
      /* alpha is the opacity: the next frame writes it */
      AniketosUI::UIColor tint{((color >> 16) & 255) / 255.0f,
                               ((color >> 8) & 255) / 255.0f,
                               (color & 255) / 255.0f, 0.0f, 0};
      void *arguments[1] = {&tint};
      mono_runtime_invoke(s_unit_set_color, unit, arguments, &exception);
      sprite.color = color;
    }
    if (unit && state.size != sprite.size) {
      set_float(unit, s_unit_set_width, state.size);
      set_float(unit, s_unit_set_height, state.size);
      sprite.size = state.size;
    }
    return;
  }
  if (!box)
    return;
  int asset = s_layer_assets[state.layer][state.texture];
  for (int texture = 0; asset < 0 && texture < 4; ++texture)
    asset = s_layer_assets[state.layer][texture];
  if (asset >= 0 && asset != sprite.asset && s_set_image) {
    void *arguments[1] = {Reflect::Target(s_assets[asset])};
    mono_runtime_invoke(s_set_image, box, arguments, &exception);
    sprite.asset = asset;
  }
  const uint32_t color = ParticleSim::ShiftHue(state.color, s_hue);
  if (color != sprite.color && s_set_filter_color) {
    AniketosUI::UIColor tint{((color >> 16) & 255) / 255.0f,
                             ((color >> 8) & 255) / 255.0f,
                             (color & 255) / 255.0f, 1.0f, 0};
    void *arguments[1] = {&tint};
    mono_runtime_invoke(s_set_filter_color, box, arguments, &exception);
    sprite.color = color;
  }
  if (state.size != sprite.size) {
    set_float(box, s_set_width, state.size);
    set_float(box, s_set_height, state.size);
    sprite.size = state.size;
  }
}

/* The sprite lists and their units: PUI's SpriteListElement draws any number
   of SpriteUnits from one texture in one go. Resolved by name; on a firmware
   without them every particle is a widget, as before. */
struct FlockClasses {
  MonoClass *element = nullptr, *unit = nullptr;
  MonoMethod *element_ctor = nullptr, *unit_ctor = nullptr;
  MonoMethod *set_image = nullptr, *set_blend = nullptr;
  MonoMethod *set_width = nullptr, *set_height = nullptr;
  bool complete() const {
    return element && unit && element_ctor && unit_ctor && set_image &&
           set_width && set_height && s_unit_set_x && s_unit_set_y &&
           s_unit_set_width && s_unit_set_height && s_unit_set_opacity &&
           s_unit_set_color && s_flock_need_update &&
           Reflect::Compiled(s_unit_set_x) && Reflect::Compiled(s_unit_set_y) &&
           Reflect::Compiled(s_unit_set_opacity) &&
           Reflect::Compiled(s_flock_need_update);
  }
};

FlockClasses flock_classes(MonoImage *pui) {
  FlockClasses found;
  found.element =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "SpriteListElement");
  found.unit = mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "SpriteUnit");
  if (!found.element || !found.unit)
    return found;
  found.element_ctor = Reflect::DeclaredMethod(found.element, ".ctor", 2);
  found.unit_ctor = Reflect::DeclaredMethod(found.unit, ".ctor", 5);
  found.set_image = Reflect::DeclaredMethod(found.element, "set_Image", 1);
  found.set_blend = Reflect::MethodOf(found.element, "set_BlendMode", 1);
  found.set_width = Reflect::MethodOf(found.element, "set_Width", 1);
  found.set_height = Reflect::MethodOf(found.element, "set_Height", 1);
  s_unit_set_x = Reflect::DeclaredMethod(found.unit, "set_X", 1);
  s_unit_set_y = Reflect::DeclaredMethod(found.unit, "set_Y", 1);
  s_unit_set_width = Reflect::DeclaredMethod(found.unit, "set_Width", 1);
  s_unit_set_height = Reflect::DeclaredMethod(found.unit, "set_Height", 1);
  s_unit_set_opacity = Reflect::DeclaredMethod(found.unit, "set_Opacity", 1);
  s_unit_set_color = Reflect::DeclaredMethod(found.unit, "set_Color", 1);
  s_flock_need_update = Reflect::MethodOf(found.element, "NeedUpdate", 0);
  return found;
}

/* The flock for a layer's texture: the first with a texture when that one
   did not load; -1 when the layer's particles are widgets. */
int flock_for(int layer, int texture) {
  for (int candidate = texture, tried = 0; tried < 4;
       candidate = (candidate + 1) % 4, ++tried)
    if (s_flock_of[layer][candidate] >= 0)
      return s_flock_of[layer][candidate];
  return -1;
}

/* One element per texture of every layer that does not turn, with room for
   `members` units; false when the elements could not be made. */
bool build_flocks(MonoObject *layer, const FlockClasses &classes,
                  const int (&members)[3][4]) {
  MonoObject *elements = object_property(layer, "get_Elements");
  if (!elements)
    return false;
  for (int index = 0; index < 3; ++index) {
    const ParticleSim::Layer &kind = s_preset->layers[index];
    for (int texture = 0; texture < 4; ++texture) {
      const int asset = s_layer_assets[index][texture];
      if (kind.share <= 0 || turns(kind) || asset < 0 ||
          !members[index][texture])
        continue;
      MonoObject *element = mono_object_new(mono_domain_get(), classes.element);
      if (!element)
        return false;
      int32_t capacity = members[index][texture];
      void *ctor_arguments[2] = {layer, &capacity};
      MonoObject *exception = nullptr;
      mono_runtime_invoke(classes.element_ctor, element, ctor_arguments,
                          &exception);
      if (exception)
        return false;
      set_float(element, classes.set_width, s_width);
      set_float(element, classes.set_height, s_height);
      void *image_arguments[1] = {Reflect::Target(s_assets[asset])};
      mono_runtime_invoke(classes.set_image, element, image_arguments,
                          &exception);
      uint8_t add = 1; /* BlendMode.Add */
      void *blend_arguments[1] = {&add};
      if (kind.additive && classes.set_blend)
        mono_runtime_invoke(classes.set_blend, element, blend_arguments,
                            &exception);
      void *element_arguments[1] = {element};
      if (!Reflect::Call(elements, "Add", 1, element_arguments))
        return false;
      s_flock_of[index][texture] = static_cast<int>(s_flocks.size());
      s_flocks.push_back({mono_gchandle_new(element, /*pinned=*/0),
                          static_cast<uint8_t>(index),
                          static_cast<uint8_t>(texture)});
    }
  }
  return true;
}

/* The widgets and sprite units of a built-in style, under `layer`. */
void build_sprites(MonoObject *layer, MonoClass *box_class, MonoMethod *append,
                   MonoImage *pui, int count) {
  const ParticleSim::Preset &preset = *s_preset;
  MonoMethod *set_focusable = Reflect::MethodOf(box_class, "set_Focusable", 1);
  MonoMethod *set_blend = Reflect::MethodOf(box_class, "set_BlendMode", 1);
  MonoMethod *get_transform = Reflect::MethodOf(box_class, "get_Transform", 0);
  bool no = false;
  uint8_t add = 1; /* BlendMode.Add */
  s_sprites.resize(static_cast<size_t>(count));
  s_particles.resize(static_cast<size_t>(count));

  /* The flocks first, sized to their members: a unit's texture is settled
     by the texture that loaded, as a widget's would be. */
  const FlockClasses classes = flock_classes(pui);
  const bool flocks = classes.complete();
  int members[3][4] = {};
  for (int index = 0; index < count; ++index) {
    ParticleSim::Particle &state = s_sprites[static_cast<size_t>(index)].state;
    ParticleSim::Spawn(state, preset, ParticleSim::LayerFor(preset, index, count),
                       s_sim_random, s_width, s_height, true);
    if (!flocks || turns(preset.layers[state.layer]))
      continue;
    for (int texture = state.texture, tried = 0; tried < 4;
         texture = (texture + 1) % 4, ++tried)
      if (s_layer_assets[state.layer][texture] >= 0) {
        state.texture = static_cast<uint8_t>(texture);
        break;
      }
    ++members[state.layer][state.texture];
  }
  for (auto &row : s_flock_of)
    for (int &flock : row)
      flock = -1;
  if (flocks && !build_flocks(layer, classes, members)) {
    LOG_ERROR("[PARTICLES] sprite lists unavailable: widgets instead");
    for (Flock &flock : s_flocks)
      Reflect::Release(flock.element); /* the layer's Elements still draw them */
    s_flocks.clear();
    for (auto &row : s_flock_of)
      for (int &flock : row)
        flock = -1;
  }

  for (int index = 0; index < count; ++index) {
    Sprite &sprite = s_sprites[static_cast<size_t>(index)];
    const ParticleSim::Layer &kind = preset.layers[sprite.state.layer];
    MonoObject *exception = nullptr;
    sprite.flock = flock_for(sprite.state.layer, sprite.state.texture);
    if (sprite.flock >= 0) {
      MonoObject *element =
          Reflect::Target(s_flocks[static_cast<size_t>(sprite.flock)].element);
      MonoObject *units = object_property(element, "get_Units");
      MonoObject *unit = mono_object_new(mono_domain_get(), classes.unit);
      if (!units || !unit) {
        sprite.flock = -1;
        continue;
      }
      float zero = 0;
      AniketosUI::UIColor clear{1, 1, 1, 0, 0};
      void *unit_arguments[5] = {&zero, &zero, &zero, &zero, &clear};
      mono_runtime_invoke(classes.unit_ctor, unit, unit_arguments, &exception);
      void *add_arguments[1] = {unit};
      if (exception || !Reflect::Call(units, "Add", 1, add_arguments)) {
        sprite.flock = -1;
        continue;
      }
      sprite.unit = mono_gchandle_new(unit, /*pinned=*/0);
      dress(sprite, nullptr);
      continue;
    }
    MonoObject *box = mono_object_new(mono_domain_get(), box_class);
    if (!box)
      continue;
    mono_runtime_object_init(box);
    s_particles[static_cast<size_t>(index)].widget =
        mono_gchandle_new(box, /*pinned=*/0);
    void *focus_arguments[1] = {&no};
    if (set_focusable)
      mono_runtime_invoke(set_focusable, box, focus_arguments, &exception);
    if (kind.additive && set_blend) {
      void *blend_arguments[1] = {&add};
      mono_runtime_invoke(set_blend, box, blend_arguments, &exception);
    }
    if (get_transform) {
      exception = nullptr;
      MonoObject *transform =
          mono_runtime_invoke(get_transform, box, nullptr, &exception);
      if (transform && !exception)
        sprite.transform = mono_gchandle_new(transform, /*pinned=*/0);
    }
    dress(sprite, box);
    set_float(box, s_set_opacity, 0.0f);
    void *child_arguments[1] = {box};
    if (append)
      mono_runtime_invoke(append, layer, child_arguments, &exception);
  }
}

bool build(MonoObject *root) {
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  MonoClass *widget_class =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Widget")
          : nullptr;
  MonoClass *box_class =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "ImageBox")
          : nullptr;
  MonoClass *asset_class =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI", "ImageAsset")
          : nullptr;
  MonoMethod *asset_ctor = find_asset_ctor(asset_class);
  if (!widget_class || !box_class || !asset_ctor ||
      !mono_class_get_method_from_name(widget_class, ".ctor", 0) ||
      !mono_class_get_method_from_name(box_class, ".ctor", 0)) {
    LOG_ERROR("[PARTICLES] classes widget=%d box=%d asset=%d",
              widget_class ? 1 : 0, box_class ? 1 : 0, asset_ctor ? 1 : 0);
    return false;
  }
  s_set_x = Reflect::MethodOf(box_class, "set_X", 1);
  s_set_y = Reflect::MethodOf(box_class, "set_Y", 1);
  s_set_opacity = Reflect::MethodOf(box_class, "set_Opacity", 1);
  if (!s_set_x || !s_set_y || !s_set_opacity) {
    LOG_ERROR("[PARTICLES] setters x=%d y=%d opacity=%d", s_set_x ? 1 : 0,
              s_set_y ? 1 : 0, s_set_opacity ? 1 : 0);
    return false;
  }

  MonoClass *transform_class =
      mono_class_from_name(pui, "Sce.PlayStation.PUI", "Transform");
  s_set_angle = transform_class
                    ? Reflect::MethodOf(transform_class, "set_AngleZ", 1)
                    : nullptr;
  s_set_translation =
      transform_class ? Reflect::MethodOf(transform_class, "set_Translation", 1)
                      : nullptr;
  s_translate = reinterpret_cast<decltype(s_translate)>(
      Reflect::Compiled(s_set_translation));
  s_set_filter_color = Reflect::MethodOf(box_class, "set_FilterColor", 1);
  s_set_image = Reflect::MethodOf(box_class, "set_Image", 1);
  s_set_width = Reflect::MethodOf(widget_class, "set_Width", 1);
  s_set_height = Reflect::MethodOf(widget_class, "set_Height", 1);

  const int style = current_style();
  if (style == 0) {
    write_defaults();
    load_assets(asset_class, asset_ctor);
  } else {
    s_preset = &ParticleSim::PresetAt(style - 1);
    if (!load_preset(*s_preset, asset_class, asset_ctor)) {
      LOG_ERROR("[PARTICLES] style '%s' has a layer without textures",
                s_preset->key);
      return false;
    }
  }
  if (s_assets.empty()) {
    LOG_ERROR("[PARTICLES] no pictures in %s", kDirectory);
    return false;
  }
  s_style = style;

  Reflect::FloatProperty(root, "get_Width", &s_width);
  Reflect::FloatProperty(root, "get_Height", &s_height);
  MonoObject *layer = mono_object_new(mono_domain_get(), widget_class);
  if (!layer)
    return false;
  mono_runtime_object_init(layer);
  s_layer = mono_gchandle_new(layer, /*pinned=*/0);
  void *name_arguments[1] = {mono_string_new(mono_domain_get(), kLayerName)};
  Reflect::Call(layer, "set_Name", 1, name_arguments);
  MonoMethod *set_width = Reflect::MethodOf(widget_class, "set_Width", 1);
  MonoMethod *set_height = Reflect::MethodOf(widget_class, "set_Height", 1);
  set_float(layer, set_width, s_width);
  set_float(layer, set_height, s_height);

  if (s_preset) {
    s_hue = g_settings.home_particles_hue;
    build_sprites(layer, box_class,
                  Reflect::MethodOf(widget_class, "AppendChild", 1), pui,
                  wanted_count());
    int units = 0;
    for (const Sprite &sprite : s_sprites)
      units += sprite.flock >= 0;
    LOG_INFO("[PARTICLES] style '%s': %d particles (%d in %zu sprite lists), "
             "%zu textures, %.0fx%.0f",
             s_preset->key, wanted_count(), units, s_flocks.size(),
             s_assets.size(), s_width, s_height);
    return true;
  }

  /* dots are the bulk of it; the symbols stand out among them */
  std::vector<size_t> dots, symbols;
  for (size_t index = 0; index < s_assets.size(); ++index)
    (s_asset_is_dot[index] ? dots : symbols).push_back(index);
  const int count = wanted_count();
  s_particles.resize(static_cast<size_t>(count));
  bool no = false;
  void *focus_arguments[1] = {&no};
  MonoMethod *set_focusable = Reflect::MethodOf(box_class, "set_Focusable", 1);
  MonoMethod *set_image = Reflect::MethodOf(box_class, "set_Image", 1);
  MonoMethod *append = Reflect::MethodOf(widget_class, "AppendChild", 1);
  for (Particle &particle : s_particles) {
    bool dot = !dots.empty() && (symbols.empty() || random01() < 0.55f);
    const std::vector<size_t> &pool = dot ? dots : symbols;
    const size_t picture =
        pool[static_cast<size_t>(random01() * static_cast<float>(pool.size())) %
             pool.size()];
    MonoObject *box = mono_object_new(mono_domain_get(), box_class);
    if (!box)
      continue;
    mono_runtime_object_init(box);
    particle.widget = mono_gchandle_new(box, /*pinned=*/0);
    spawn(particle, dot, true);
    set_float(box, set_width, particle.size);
    set_float(box, set_height, particle.size);
    void *image_arguments[1] = {Reflect::Target(s_assets[picture])};
    MonoObject *exception = nullptr;
    if (set_image)
      mono_runtime_invoke(set_image, box, image_arguments, &exception);
    if (set_focusable)
      mono_runtime_invoke(set_focusable, box, focus_arguments, &exception);
    set_float(box, s_set_opacity, 0.0f);
    void *child_arguments[1] = {box};
    if (append)
      mono_runtime_invoke(append, layer, child_arguments, &exception);
  }
  LOG_INFO("[PARTICLES] %d particles, %zu pictures (%zu symbols), %.0fx%.0f",
           count, s_assets.size(), symbols.size(), s_width, s_height);
  return true;
}

/* The layer sits right under Home's React tree: over the wallpaper and the
   video, under everything Home draws. */
void place(MonoObject *root, MonoObject *layer) {
  /* usually in place already: two reads, not a walk of Home's children */
  MonoObject *next = object_property(layer, "get_NextSibling");
  if (next && object_property(layer, "get_Parent") == root &&
      std::strstr(mono_class_get_name(mono_object_get_class(next)),
                  "ReactRootView"))
    return;
  MonoObject *react = nullptr;
  MonoObject *child = object_property(root, "get_FirstChild");
  for (int guard = 0; child && guard < 32 && !react; ++guard) {
    if (std::strstr(mono_class_get_name(mono_object_get_class(child)),
                    "ReactRootView"))
      react = child;
    else
      child = object_property(child, "get_NextSibling");
  }
  if (!react)
    return;
  MonoObject *parent = object_property(layer, "get_Parent");
  if (parent == root && object_property(layer, "get_NextSibling") == react)
    return;
  if (parent)
    Reflect::Call(layer, "RemoveFromParent", 0);
  void *arguments[2] = {layer, react};
  Reflect::Call(root, "InsertChildBelow", 2, arguments);
}

void animate(float seconds) {
  for (Particle &particle : s_particles) {
    MonoObject *box = Reflect::Target(particle.widget);
    if (!box)
      continue;
    particle.age += seconds;
    const float margin = particle.size;
    if (particle.age >= particle.life || particle.x < -margin ||
        particle.x > s_width + margin || particle.y < -margin ||
        particle.y > s_height + margin) {
      const bool dot = particle.size < 28.0f && particle.peak < 0.7f;
      const float size = particle.size;
      spawn(particle, dot, false);
      particle.size = size; /* the widget keeps its size */
    }
    particle.sway += particle.sway_speed * seconds;
    particle.x += (particle.vx + std::sin(particle.sway) * 4.0f) * seconds;
    particle.y += particle.vy * seconds;
    /* a step too small to see is not sent: each one lays the widget out */
    if (std::fabs(particle.x - particle.put_x) >= 0.75f ||
        std::fabs(particle.y - particle.put_y) >= 0.75f) {
      particle.put_x = particle.x;
      particle.put_y = particle.y;
      set_float(box, s_set_x, particle.x - particle.size * 0.5f);
      set_float(box, s_set_y, particle.y - particle.size * 0.5f);
    }
    /* fades in over the first seconds of its life and out over the last */
    const float in = std::min(1.0f, particle.age / 3.0f);
    const float out = std::min(1.0f, (particle.life - particle.age) / 3.0f);
    const float opacity = particle.peak * std::max(0.0f, std::min(in, out));
    if (std::fabs(opacity - particle.shown) > 0.01f) {
      set_float(box, s_set_opacity, opacity);
      particle.shown = opacity;
    }
  }
}

/* A unit is five bare stores and its flock's one NeedUpdate a frame, so it
   is written every frame, to the pixel. */
void put_unit(Sprite &sprite, float opacity) {
  MonoObject *unit = Reflect::Target(sprite.unit);
  if (!unit)
    return;
  const ParticleSim::Particle &state = sprite.state;
  set_float(unit, s_unit_set_x, state.x - state.size * 0.5f);
  set_float(unit, s_unit_set_y, state.y - state.size * 0.5f);
  set_float(unit, s_unit_set_opacity, opacity);
}

void put_widget(Sprite &sprite, Particle &shown, MonoObject *box,
                float opacity) {
  const ParticleSim::Particle &state = sprite.state;
  /* Every call into PUI costs, more so at the low clock ShellUI runs at
     in the menus: steps too small to see are not sent. */
  if (std::fabs(opacity - shown.shown) > 0.02f ||
      (opacity == 0 && shown.shown != 0)) {
    set_float(box, s_set_opacity, opacity);
    shown.shown = opacity;
  }
  if (opacity == 0)
    return; /* unseen: it moves on, its widget need not */
  MonoObject *transform = Reflect::Target(sprite.transform);
  if (std::fabs(state.x - sprite.x) >= 0.75f ||
      std::fabs(state.y - sprite.y) >= 0.75f) {
    sprite.x = state.x;
    sprite.y = state.y;
    /* The transform's translation moves the picture without laying the
       widget out again, and in one call instead of X and Y. */
    if (transform && s_translate) {
      const float where[2] = {state.x - state.size * 0.5f,
                              state.y - state.size * 0.5f};
      uint64_t xy;
      std::memcpy(&xy, where, sizeof xy);
      s_translate(transform, xy, 0);
    } else if (transform && s_set_translation) {
      float where[3] = {state.x - state.size * 0.5f,
                        state.y - state.size * 0.5f, 0.0f};
      void *arguments[1] = {where};
      MonoObject *exception = nullptr;
      mono_runtime_invoke(s_set_translation, transform, arguments, &exception);
    } else {
      set_float(box, s_set_x, state.x - state.size * 0.5f);
      set_float(box, s_set_y, state.y - state.size * 0.5f);
    }
  }
  if (transform && turns(s_preset->layers[state.layer]) &&
      std::fabs(state.angle - sprite.angle) > 0.75f) {
    /* Transform.AngleZ is in radians */
    set_float(transform, s_set_angle, state.angle * 3.14159265f / 180.0f);
    sprite.angle = state.angle;
  }
}

void animate_sprites(float seconds) {
  const bool profiled = AniketosShell::Profile::enabled;
  const uint64_t started = profiled ? AniketosShell::Profile::NowNs() : 0;
  /* the simulation first, all of it, then the writes: measured apart */
  for (size_t index = 0; index < s_sprites.size(); ++index) {
    ParticleSim::Particle &state = s_sprites[index].state;
    if (ParticleSim::Step(state, *s_preset, seconds, s_width, s_height))
      continue;
    ParticleSim::Spawn(state, *s_preset, state.layer, s_sim_random, s_width,
                       s_height, false);
    dress(s_sprites[index], Reflect::Target(s_particles[index].widget));
  }
  const uint64_t simulated = profiled ? AniketosShell::Profile::NowNs() : 0;
  for (size_t index = 0; index < s_sprites.size(); ++index) {
    Sprite &sprite = s_sprites[index];
    float opacity = ParticleSim::Opacity(sprite.state, *s_preset);
    if (opacity <= 0.003f)
      opacity = 0;
    if (sprite.flock >= 0)
      put_unit(sprite, opacity);
    else if (MonoObject *box = Reflect::Target(s_particles[index].widget))
      put_widget(sprite, s_particles[index], box, opacity);
  }
  /* every flock has members, and they were all just written */
  if (auto need_update = reinterpret_cast<void (*)(MonoObject *)>(
          Reflect::Compiled(s_flock_need_update)))
    for (const Flock &flock : s_flocks)
      if (MonoObject *element = Reflect::Target(flock.element))
        need_update(element);
  if (!profiled)
    return;
  s_sim_ns += simulated - started;
  s_put_ns += AniketosShell::Profile::NowNs() - simulated;
  if (++s_profiled_frames % 300 == 0) {
    LOG_INFO("[PARTICLES:PROFILE] %zu particles (%zu lists): sim %.1f us, "
             "pui %.1f us a frame",
             s_sprites.size(), s_flocks.size(),
             static_cast<double>(s_sim_ns) / 300.0 / 1000.0,
             static_cast<double>(s_put_ns) / 300.0 / 1000.0);
    s_sim_ns = s_put_ns = 0;
  }
}

} // namespace

int StyleCount() { return 1 + ParticleSim::PresetCount(); }

const char *StyleName(int style) {
  static std::vector<std::string> keys;
  if (keys.empty()) {
    keys.emplace_back("vsh.home_particles.style.pictures");
    for (int index = 0; index < ParticleSim::PresetCount(); ++index)
      keys.push_back(std::string("vsh.home_particles.style.") +
                     ParticleSim::PresetAt(index).key);
  }
  return keys[static_cast<size_t>(
                  std::clamp(style, 0, static_cast<int>(keys.size()) - 1))]
      .c_str();
}

void Tick() {
  ++s_frame;
  if (!(AniketosTheme::CustomizationOn() && g_settings.home_particles_enabled)) {
    if (s_layer)
      teardown();
    return;
  }
  MonoObject *layer = Reflect::Target(s_layer);
  if (s_frame % 30 == 0) {
    MonoObject *root = AniketosUI::HomeScene::Root();
    if (!root || AniketosUI::HomeScene::IsDisposed(root))
      return;
    if (layer && AniketosUI::HomeScene::IsDisposed(layer)) {
      teardown(); /* Home rebuilt its scene */
      layer = nullptr;
    }
    if (layer && (static_cast<int>(s_particles.size()) != wanted_count() ||
                  s_style != current_style())) {
      teardown();
      layer = nullptr;
    }
    if (!layer) {
      static int failed_style = -1; /* another style gets its own tries */
      if (failed_style != current_style())
        s_failures = 0;
      if (s_failures >= 3)
        return;
      if (!build(root)) {
        teardown();
        failed_style = current_style();
        ++s_failures;
        return;
      }
      layer = Reflect::Target(s_layer);
    }
    place(root, layer);
  }
  /* nothing to move while Home is out of sight: a game has the screen, or
     Home is faded out (parked at about 0.02) */
  if (!layer ||
      AniketosShell::CurrentFocusLayer() == AniketosShell::FocusLayer::Game)
    return;
  /* The setters are called compiled, with no catch for the exception a
     disposed widget throws: Home rebuilding its scene is seen the same
     frame, not at the next check. One field read. */
  if (AniketosUI::HomeScene::IsDisposed(layer)) {
    teardown();
    return;
  }
  float opacity = 1.0f;
  if (Reflect::FloatProperty(layer, "get_FinalOpacity", &opacity) &&
      opacity <= 0.02f)
    return;
  if (s_preset && g_settings.home_particles_hue != s_hue) {
    s_hue = g_settings.home_particles_hue;
    for (size_t index = 0; index < s_sprites.size(); ++index)
      dress(s_sprites[index], Reflect::Target(s_particles[index].widget));
  }
  const float seconds =
      std::clamp(g_settings.home_particles_speed, 25, 300) / 100.0f / 60.0f;
  if (s_preset)
    animate_sprites(seconds);
  else
    animate(seconds);
}

} // namespace HomeParticles
