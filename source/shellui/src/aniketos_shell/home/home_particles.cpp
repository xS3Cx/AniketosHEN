#include "aniketos_shell/home/home_particles.hpp"

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

/* A built-in style's particle and what its widget was last given. */
struct Sprite {
  ParticleSim::Particle state;
  uint32_t transform = 0; /* strong: moves and turns it */
  int asset = -1;
  uint32_t color = 0;
  float size = -1, angle = 1e9f;
  float x = -1e9f, y = -1e9f; /* where it was last put */
};
std::vector<Sprite> s_sprites; /* s_particles[i].widget is sprite i's */
const ParticleSim::Preset *s_preset = nullptr;
int s_layer_assets[3][4]; /* index into s_assets per layer texture, or -1 */
ParticleSim::Random s_sim_random;
MonoMethod *s_set_width = nullptr;
MonoMethod *s_set_height = nullptr;
MonoMethod *s_set_image = nullptr;
MonoMethod *s_set_filter_color = nullptr;
MonoMethod *s_set_angle = nullptr;
MonoMethod *s_set_translation = nullptr;
int s_style = -1; /* what the layer was built for */
int s_hue = 0;    /* the tints' turn the widgets have */

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

void set_float(MonoObject *object, MonoMethod *setter, float value) {
  void *arguments[1] = {&value};
  MonoObject *exception = nullptr;
  if (object && setter)
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
  for (Sprite &sprite : s_sprites)
    Reflect::Release(sprite.transform);
  s_sprites.clear();
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
  s_assets.push_back(mono_gchandle_new(asset, /*pinned=*/1));
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

/* Gives the widget what its particle now has: texture, tint and size. */
void dress(Sprite &sprite, MonoObject *box) {
  const ParticleSim::Particle &state = sprite.state;
  int asset = s_layer_assets[state.layer][state.texture];
  for (int texture = 0; asset < 0 && texture < 4; ++texture)
    asset = s_layer_assets[state.layer][texture];
  MonoObject *exception = nullptr;
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

/* The widgets of a built-in style, under `layer`. */
void build_sprites(MonoObject *layer, MonoClass *box_class, MonoMethod *append,
                   int count) {
  const ParticleSim::Preset &preset = *s_preset;
  MonoMethod *set_focusable = Reflect::MethodOf(box_class, "set_Focusable", 1);
  MonoMethod *set_blend = Reflect::MethodOf(box_class, "set_BlendMode", 1);
  MonoMethod *get_transform = Reflect::MethodOf(box_class, "get_Transform", 0);
  bool no = false;
  uint8_t add = 1; /* BlendMode.Add */
  s_sprites.resize(static_cast<size_t>(count));
  s_particles.resize(static_cast<size_t>(count));
  for (int index = 0; index < count; ++index) {
    Sprite &sprite = s_sprites[static_cast<size_t>(index)];
    ParticleSim::Spawn(sprite.state, preset,
                       ParticleSim::LayerFor(preset, index, count),
                       s_sim_random, s_width, s_height, true);
    const ParticleSim::Layer &kind = preset.layers[sprite.state.layer];
    MonoObject *box = mono_object_new(mono_domain_get(), box_class);
    if (!box)
      continue;
    mono_runtime_object_init(box);
    s_particles[static_cast<size_t>(index)].widget =
        mono_gchandle_new(box, /*pinned=*/1);
    MonoObject *exception = nullptr;
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
        sprite.transform = mono_gchandle_new(transform, /*pinned=*/1);
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
  s_layer = mono_gchandle_new(layer, /*pinned=*/1);
  void *name_arguments[1] = {mono_string_new(mono_domain_get(), kLayerName)};
  Reflect::Call(layer, "set_Name", 1, name_arguments);
  MonoMethod *set_width = Reflect::MethodOf(widget_class, "set_Width", 1);
  MonoMethod *set_height = Reflect::MethodOf(widget_class, "set_Height", 1);
  set_float(layer, set_width, s_width);
  set_float(layer, set_height, s_height);

  if (s_preset) {
    s_hue = g_settings.home_particles_hue;
    build_sprites(layer, box_class,
                  Reflect::MethodOf(widget_class, "AppendChild", 1),
                  wanted_count());
    LOG_INFO("[PARTICLES] style '%s': %d particles, %zu textures, %.0fx%.0f",
             s_preset->key, wanted_count(), s_assets.size(), s_width,
             s_height);
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
    particle.widget = mono_gchandle_new(box, /*pinned=*/1);
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

void animate_sprites(float seconds) {
  for (size_t index = 0; index < s_sprites.size(); ++index) {
    Sprite &sprite = s_sprites[index];
    Particle &shown = s_particles[index];
    MonoObject *box = Reflect::Target(shown.widget);
    if (!box)
      continue;
    ParticleSim::Particle &state = sprite.state;
    if (!ParticleSim::Step(state, *s_preset, seconds, s_width, s_height)) {
      ParticleSim::Spawn(state, *s_preset, state.layer, s_sim_random, s_width,
                         s_height, false);
      dress(sprite, box);
    }
    float opacity = ParticleSim::Opacity(state, *s_preset);
    if (opacity <= 0.003f)
      opacity = 0;
    /* Every call into PUI costs, more so at the low clock ShellUI runs at
       in the menus: steps too small to see are not sent. */
    if (std::fabs(opacity - shown.shown) > 0.02f ||
        (opacity == 0 && shown.shown != 0)) {
      set_float(box, s_set_opacity, opacity);
      shown.shown = opacity;
    }
    if (opacity == 0)
      continue; /* unseen: it moves on, its widget need not */
    MonoObject *transform = Reflect::Target(sprite.transform);
    if (std::fabs(state.x - sprite.x) >= 0.75f ||
        std::fabs(state.y - sprite.y) >= 0.75f) {
      sprite.x = state.x;
      sprite.y = state.y;
      /* The transform's translation moves the picture without laying the
         widget out again, and in one call instead of X and Y. */
      if (transform && s_set_translation) {
        float where[3] = {state.x - state.size * 0.5f,
                          state.y - state.size * 0.5f, 0.0f};
        void *arguments[1] = {where};
        MonoObject *exception = nullptr;
        mono_runtime_invoke(s_set_translation, transform, arguments,
                            &exception);
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
  float opacity = 1.0f;
  if (Reflect::FloatProperty(layer, "get_FinalOpacity", &opacity) &&
      opacity <= 0.02f)
    return;
  if (s_preset && g_settings.home_particles_hue != s_hue) {
    s_hue = g_settings.home_particles_hue;
    for (size_t index = 0; index < s_sprites.size(); ++index)
      if (MonoObject *box = Reflect::Target(s_particles[index].widget))
        dress(s_sprites[index], box);
  }
  const float seconds =
      std::clamp(g_settings.home_particles_speed, 25, 300) / 100.0f / 60.0f;
  if (s_preset)
    animate_sprites(seconds);
  else
    animate(seconds);
}

} // namespace HomeParticles
