#include "aniketos_shell/home/home_particles.hpp"

#include "aniketos_theme/clock/shell_clock.hpp"
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

namespace HomeParticles {
namespace {

namespace Reflect = AniketosUI::Reflect;

constexpr const char *kDirectory =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/particles";
constexpr const char *kLayerName = "id_aniketos_home_particles";
constexpr int kMinCount = 20;
constexpr int kMaxCount = 300;
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
};

std::vector<Particle> s_particles;
std::vector<uint32_t> s_assets; /* ImageAsset per picture, strong */
std::vector<bool> s_asset_is_dot;
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
  if (layer && !ShellClock::IsWidgetDisposed(layer)) {
    Reflect::Call(layer, "RemoveFromParent", 0);
    Reflect::Call(layer, "Dispose", 0); /* takes its children with it */
  }
  Reflect::Release(s_layer);
  for (Particle &particle : s_particles)
    Reflect::Release(particle.widget);
  s_particles.clear();
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

void load_assets(MonoClass *asset_class, MonoMethod *asset_ctor) {
  for (const std::string &path : picture_paths()) {
    MonoObject *asset = mono_object_new(mono_domain_get(), asset_class);
    void *arguments[1] = {mono_string_new(mono_domain_get(), path.c_str())};
    MonoObject *exception = nullptr;
    if (!asset)
      continue;
    mono_runtime_invoke(asset_ctor, asset, arguments, &exception);
    if (exception) {
      LOG_ERROR("[PARTICLES] '%s' could not be opened", path.c_str());
      continue;
    }
    s_assets.push_back(mono_gchandle_new(asset, /*pinned=*/1));
    s_asset_is_dot.push_back(path.find("dot") != std::string::npos);
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

  write_defaults();
  load_assets(asset_class, asset_ctor);
  if (s_assets.empty()) {
    LOG_ERROR("[PARTICLES] no pictures in %s", kDirectory);
    return false;
  }

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
    set_float(box, s_set_x, particle.x - particle.size * 0.5f);
    set_float(box, s_set_y, particle.y - particle.size * 0.5f);
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

} // namespace

void Tick() {
  ++s_frame;
  if (!(AniketosTheme::CustomizationOn() && g_settings.home_particles_enabled)) {
    if (s_layer)
      teardown();
    return;
  }
  MonoObject *layer = Reflect::Target(s_layer);
  if (s_frame % 30 == 0) {
    MonoObject *root = ShellClock::GetHomeRoot();
    if (!root || ShellClock::IsWidgetDisposed(root))
      return;
    if (layer && ShellClock::IsWidgetDisposed(layer)) {
      teardown(); /* Home rebuilt its scene */
      layer = nullptr;
    }
    if (layer && static_cast<int>(s_particles.size()) != wanted_count()) {
      teardown();
      layer = nullptr;
    }
    if (!layer) {
      if (s_failures >= 3)
        return;
      if (!build(root)) {
        teardown();
        ++s_failures;
        return;
      }
      layer = Reflect::Target(s_layer);
    }
    place(root, layer);
  }
  if (!layer)
    return;
  /* nothing to move while Home is out of sight */
  float opacity = 1.0f;
  if (Reflect::FloatProperty(layer, "get_FinalOpacity", &opacity) &&
      opacity <= 0.02f)
    return;
  animate(1.0f / 60.0f);
}

} // namespace HomeParticles
