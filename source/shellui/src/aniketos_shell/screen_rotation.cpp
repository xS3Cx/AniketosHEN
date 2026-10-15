#include "aniketos_shell/screen_rotation.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "shell_clock.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern onion::Settings g_settings;

namespace ScreenRotation {
namespace {

/* Index 0 is the console's own format. The others are the shapes ultrawide
   monitors come in: "21:9" panels are 64:27 (2560x1080) or 43:18
   (3440x1440), and super-ultrawides are 32:9. */
constexpr Format kFormats[] = {
    {"16:9", 16, 9},
    {"21:9", 21, 9},
    {"21:9 (2560x1080)", 64, 27},
    {"21:9 (3440x1440)", 43, 18},
    {"32:9", 32, 9},
};
constexpr int kFormatCount = static_cast<int>(sizeof(kFormats) / sizeof(kFormats[0]));

struct Vector3 {
  float x, y, z;
};

/* The camera and screen as PUI set them up. */
struct Original {
  bool saved = false;
  Vector3 up{}, target{}, position{};
  float left = 0, right = 0, top = 0, bottom = 0; /* the frustum */
  int32_t width = 0, height = 0;
};

Original s_original;
/* What is wanted. */
int s_format = 0;           /* index into kFormats */
int s_turns = 0;            /* quarter turns (experiments) */
bool s_portrait = false;
int s_portrait_width = 1080;
int s_applied_setting = -1; /* the setting value last acted on */
int s_forced_format = -1;   /* debug console: use this instead of the setting */
/* What is in effect: the layout's screen, and layout units to pixels. */
int32_t s_layout_width = 0, s_layout_height = 0;
float s_density = 2.0f;
float s_wanted_right = 0.0f;
int s_frame = 0;
int s_settle_frames = 0; /* keep sizing scenes for a while after a change */
int s_reapplied = 0;
int s_resized = 0;
int s_dimension_updates = 0;

bool changed() {
  return s_original.saved &&
         (s_turns != 0 || s_layout_width != s_original.width ||
          s_layout_height != s_original.height);
}

/* ---- managed helpers ------------------------------------------------- */

MonoClass *ui_values() {
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  return pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI", "UIValues")
             : nullptr;
}

MonoObject *camera() {
  MonoClass *values = ui_values();
  MonoClassField *field =
      values ? mono_class_get_field_from_name(values, "Camera") : nullptr;
  MonoObject *object = nullptr;
  if (field)
    mono_field_static_get_value(mono_class_vtable(mono_domain_get(), values),
                                field, &object);
  return object;
}

/* A value-type field of an object, read or written in place. */
bool raw_field(MonoObject *object, const char *name, void *value, bool write) {
  MonoClassField *field =
      mono_class_get_field_from_name(mono_object_get_class(object), name);
  if (!field)
    return false;
  if (write)
    mono_field_set_value(object, field, value);
  else
    mono_field_get_value(object, field, value);
  return true;
}

bool screen_size(int32_t *width, int32_t *height, bool write) {
  MonoClass *values = ui_values();
  MonoClassField *w =
      values ? mono_class_get_field_from_name(values, "ScreenWidth") : nullptr;
  MonoClassField *h =
      values ? mono_class_get_field_from_name(values, "ScreenHeight") : nullptr;
  if (!w || !h)
    return false;
  MonoVTable *vtable = mono_class_vtable(mono_domain_get(), values);
  if (write) {
    mono_field_static_set_value(vtable, w, width);
    mono_field_static_set_value(vtable, h, height);
  } else {
    mono_field_static_get_value(vtable, w, width);
    mono_field_static_get_value(vtable, h, height);
  }
  return true;
}

MonoMethod *method_of(MonoObject *object, const char *name, int parameters) {
  for (MonoClass *klass = mono_object_get_class(object); klass;
       klass = mono_class_get_parent(klass))
    if (MonoMethod *method =
            mono_class_get_method_from_name(klass, name, parameters))
      return method;
  return nullptr;
}

/* A parameterless call. A boxed struct goes in as it is: the runtime's
   invoke wrapper steps over the box itself, so the struct changes in place. */
MonoObject *call0(MonoObject *object, const char *name) {
  MonoMethod *method = object ? method_of(object, name, 0) : nullptr;
  if (!method)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(method, object, nullptr, &exception);
  return exception ? nullptr : value;
}

MonoObject *field_of(MonoObject *object, const char *name) {
  if (!object)
    return nullptr;
  MonoClassField *field = nullptr;
  for (MonoClass *klass = mono_object_get_class(object); klass && !field;
       klass = mono_class_get_parent(klass))
    field = mono_class_get_field_from_name(klass, name);
  MonoObject *value = nullptr;
  if (field)
    mono_field_get_value(object, field, &value);
  return value;
}

bool size_of(MonoObject *widget, float *width, float *height) {
  MonoObject *w = call0(widget, "get_Width");
  MonoObject *h = call0(widget, "get_Height");
  if (!w || !h)
    return false;
  *width = *static_cast<float *>(mono_object_unbox(w));
  *height = *static_cast<float *>(mono_object_unbox(h));
  return true;
}

void resize(MonoObject *widget, float width, float height) {
  const char *const setters[2] = {"set_Width", "set_Height"};
  float values[2] = {width, height};
  for (int axis = 0; axis < 2; ++axis) {
    MonoMethod *setter = method_of(widget, setters[axis], 1);
    void *arguments[1] = {&values[axis]};
    MonoObject *exception = nullptr;
    if (setter)
      mono_runtime_invoke(setter, widget, arguments, &exception);
  }
}

/* ---- the camera ------------------------------------------------------ */

bool make_target(int width, int height);
void drop_target();
bool s_on_ui_thread = false;
bool s_target_wanted = false; /* asked for before the UI thread was there */

/* Written before a format other than the console's is applied and removed
   once ShellUI has run with it for a while. If ShellUI went down in between,
   the file is still there at the next start and the format is left off, so
   a format that does not work on some firmware cannot keep ShellUI from
   starting. Choosing a format in the menu again clears it. */
constexpr const char *kPending =
    ONION_DIST_USER_DATA_ROOT "/screen_format_pending";
int s_alive_frames = -1; /* counts up while a format is on probation */
bool s_left_off = false;

bool pending_exists() {
  struct stat info {};
  return stat(kPending, &info) == 0;
}

void mark_pending() {
  const int fd = open(kPending, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0)
    close(fd);
  s_alive_frames = 0;
}

/* The original up vector turned by quarter turns about the view axis. */
Vector3 turned(const Vector3 &up, int turns) {
  switch (turns & 3) {
  case 1: return {-up.y, up.x, up.z};
  case 2: return {-up.x, -up.y, up.z};
  case 3: return {up.y, -up.x, up.z};
  default: return up;
  }
}

bool apply() {
  MonoObject *object = camera();
  if (!object)
    return false;
  if (!s_original.saved) {
    if (!raw_field(object, "Up", &s_original.up, false) ||
        !raw_field(object, "Target", &s_original.target, false) ||
        !raw_field(object, "Position", &s_original.position, false) ||
        !raw_field(object, "Left", &s_original.left, false) ||
        !raw_field(object, "Right", &s_original.right, false) ||
        !raw_field(object, "Top", &s_original.top, false) ||
        !raw_field(object, "Bottom", &s_original.bottom, false) ||
        !screen_size(&s_original.width, &s_original.height, false) ||
        s_original.width <= 0 || s_original.height <= 0)
      return false;
    s_original.saved = true;
    MonoClass *values = ui_values();
    MonoClassField *density =
        values ? mono_class_get_field_from_name(values, "PixelDensity")
               : nullptr;
    if (density)
      mono_field_static_get_value(
          mono_class_vtable(mono_domain_get(), values), density, &s_density);
    if (s_density < 0.25f || s_density > 8.0f)
      s_density = 2.0f;
  }

  Vector3 up = turned(s_original.up, s_turns);
  Vector3 target = s_original.target;
  Vector3 position = s_original.position;
  int32_t width = s_original.width;
  int32_t height = s_original.height;
  /* How much more of the layout the frustum takes in, across and down the
     panel. */
  float zoom_x = 1.0f, zoom_y = 1.0f;
  if (s_portrait && (s_turns & 1)) {
    /* The layout's screen is the panel on its side, s_portrait_width units
       across. */
    width = s_portrait_width;
    height = static_cast<int32_t>(
        std::lround(static_cast<double>(width) * s_original.width /
                    s_original.height));
    zoom_x = zoom_y =
        static_cast<float>(width) / static_cast<float>(s_original.height);
  } else if (!(s_turns & 1) && s_format > 0 && s_format < kFormatCount) {
    /* Wider layout, same height, squeezed across the 16:9 picture. */
    const Format &format = kFormats[s_format];
    width = static_cast<int32_t>(
        std::lround(static_cast<double>(height) * format.width /
                    format.height));
    width += width & 1; /* even, so the centre is on a whole unit */
    zoom_x = static_cast<float>(width) / static_cast<float>(s_original.width);
  }
  target.x = position.x = static_cast<float>(width) * 0.5f;
  target.y = position.y = static_cast<float>(height) * 0.5f;
  float left = s_original.left * zoom_x, right = s_original.right * zoom_x;
  float top = s_original.top * zoom_y, bottom = s_original.bottom * zoom_y;
  s_layout_width = width;
  s_layout_height = height;
  s_wanted_right = right;
  /* What PUI is told the screen is. Given a wider screen it does most of
     an ultrawide layout itself: it lowers its pixel density so the wider
     layout spans the picture, and lays out, clips, culls and creates scenes
     for that width. The camera above is the one PUI would place for that
     screen; it draws the layout over the whole picture. The one thing PUI
     gets wrong is the height of the picture in pixels (see transform()). */
  int32_t announced_width = width;
  int32_t announced_height = height;
  MonoMethod *place = method_of(object, "Place", 0);
  MonoObject *exception = nullptr;
  if (!raw_field(object, "Left", &left, true) ||
      !raw_field(object, "Right", &right, true) ||
      !raw_field(object, "Top", &top, true) ||
      !raw_field(object, "Bottom", &bottom, true) ||
      !raw_field(object, "Up", &up, true) ||
      !raw_field(object, "Target", &target, true) ||
      !raw_field(object, "Position", &position, true) ||
      !screen_size(&announced_width, &announced_height, true) || !place)
    return false;
  mono_runtime_invoke(place, object, nullptr, &exception);
  if (!s_on_ui_thread) {
    /* Install() runs on the loader's thread; graphics objects made there
       hang ShellUI. The first Tick() comes back for the target. */
    s_target_wanted = !(s_turns & 1) && width > s_original.width;
  } else if (!(s_turns & 1) && width > s_original.width) {
    s_target_wanted = false;
    /* every column of the picture is used; the rows follow from the shape */
    const int target_width = static_cast<int>(
        std::lround(static_cast<float>(s_original.width) * s_density));
    const int target_height =
        static_cast<int>(std::lround(static_cast<double>(target_width) *
                                     height / width)) & ~1;
    if (!make_target(target_width, target_height))
      LOG_ERROR("[SCREEN] no render target for %dx%d", target_width,
                target_height);
  } else {
    drop_target();
  }
  return !exception;
}

/* ---- the render target of a wider layout -------------------------------
 * A wider layout is not squeezed by tricks in the drawing: PUI draws it
 * into a frame buffer with the layout's own shape, which it is told is the
 * screen (UISystem.SetFrameBufferAsScreen, PUI's own facility). In there one
 * layout unit is the same number of pixels across and down, as on the
 * console's own screen, so widgets, tiles, rounded corners and clipping are
 * all computed the way they always are. PUI does not put that buffer on the
 * display itself; that is done here, once a frame, when it swaps buffers:
 * the buffer is drawn over the whole 16:9 picture, which is the squeeze. */
uint32_t s_target_texture = 0; /* pinned handles */
uint32_t s_target_buffer = 0;
int s_target_width = 0, s_target_height = 0;
bool s_target_active = false;
std::atomic<int> s_swaps{0};
std::atomic<int> s_presented{0};
int s_present_failures = 0;
bool s_present_flip = false; /* debug: the target's rows the other way up */

MonoObject *handle_target(uint32_t handle) {
  return handle ? mono_gchandle_get_target(handle) : nullptr;
}

MonoObject *graphics_context() {
  MonoClass *values = ui_values();
  MonoClassField *field =
      values ? mono_class_get_field_from_name(values, "Renderer") : nullptr;
  MonoObject *renderer = nullptr;
  if (field)
    mono_field_static_get_value(mono_class_vtable(mono_domain_get(), values),
                                field, &renderer);
  return call0(renderer, "get_GraphicsContext");
}

/* A method by name and parameter count whose first parameter's type
   mentions `first` (to tell overloads apart). */
MonoMethod *overload(MonoClass *klass, const char *name, int parameters,
                     const char *first) {
  for (; klass; klass = mono_class_get_parent(klass)) {
    void *iter = nullptr;
    while (MonoMethod *method = mono_class_get_methods(klass, &iter)) {
      if (std::strcmp(mono_method_get_name(method), name))
        continue;
      MonoMethodSignature *signature = mono_method_signature(method);
      if (!signature ||
          static_cast<int>(mono_signature_get_param_count(signature)) !=
              parameters)
        continue;
      void *param_iter = nullptr;
      MonoType *type = mono_signature_get_params(signature, &param_iter);
      char *text = type ? mono_type_get_name(type) : nullptr;
      if (text && std::strstr(text, first) && !std::strchr(text, '&'))
        return method;
    }
  }
  return nullptr;
}

bool invoke(MonoMethod *method, MonoObject *object, void **arguments) {
  static int reported = 0;
  if (!method) {
    if (reported < 8) {
      ++reported;
      LOG_ERROR("[SCREEN] a method was not found");
    }
    return false;
  }
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, object, arguments, &exception);
  if (exception && reported < 8) {
    ++reported;
    MonoObject *inner = nullptr;
    MonoString *text = mono_object_to_string(exception, &inner);
    const char *utf = text && !inner ? mono_string_to_utf8(text) : nullptr;
    LOG_ERROR("[SCREEN] %s threw: %.300s", mono_method_get_name(method),
              utf ? utf : "?");
    if (utf && mono_free)
      mono_free(const_cast<char *>(utf));
  }
  return !exception;
}

bool set_screen_buffer(MonoObject *buffer) {
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  MonoClass *system =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI", "UISystem")
          : nullptr;
  MonoMethod *set =
      system ? mono_class_get_method_from_name(system,
                                               "SetFrameBufferAsScreen", 1)
             : nullptr;
  void *arguments[1] = {buffer};
  return invoke(set, nullptr, arguments);
}

void drop_target() {
  if (s_target_active)
    set_screen_buffer(nullptr);
  s_target_active = false;
  for (uint32_t *handle : {&s_target_buffer, &s_target_texture}) {
    if (MonoObject *object = handle_target(*handle))
      call0(object, "Dispose");
    if (*handle)
      mono_gchandle_free(*handle);
    *handle = 0;
  }
  s_target_width = s_target_height = 0;
}

bool make_target(int width, int height) {
  if (s_target_active && width == s_target_width && height == s_target_height)
    return true;
  drop_target();
  MonoImage *core = getDLLimage("Sce.PlayStation.Core.dll");
  MonoObject *context = graphics_context();
  MonoObject *screen = call0(context, "get_Screen");
  MonoClass *texture_class =
      core ? mono_class_from_name(core, "Sce.PlayStation.Core.Graphics",
                                  "Texture2D")
           : nullptr;
  MonoClass *buffer_class =
      core ? mono_class_from_name(core, "Sce.PlayStation.Core.Graphics",
                                  "FrameBuffer")
           : nullptr;
  MonoClass *option_class =
      core ? mono_class_from_name(core, "Sce.PlayStation.Core.Graphics",
                                  "PixelBufferOption")
           : nullptr;
  MonoClassField *renderable_field =
      option_class
          ? mono_class_get_field_from_name(option_class, "Renderable")
          : nullptr;
  MonoObject *format_boxed = call0(screen, "get_ColorFormat");
  if (!texture_class || !buffer_class || !renderable_field || !format_boxed)
    return false;
  int32_t renderable = 0;
  mono_field_static_get_value(mono_class_vtable(mono_domain_get(), option_class),
                              renderable_field, &renderable);
  /* the same pixel format as the screen, so colours come out the same */
  int32_t format = *static_cast<int32_t *>(mono_object_unbox(format_boxed));
  bool mipmap = false;
  MonoObject *texture = mono_object_new(mono_domain_get(), texture_class);
  void *texture_arguments[5] = {&width, &height, &mipmap, &format, &renderable};
  if (!texture ||
      !invoke(mono_class_get_method_from_name(texture_class, ".ctor", 5),
              texture, texture_arguments))
    return false;
  s_target_texture = mono_gchandle_new(texture, /*pinned=*/1);
  MonoObject *buffer = mono_object_new(mono_domain_get(), buffer_class);
  if (!buffer)
    return false;
  mono_runtime_object_init(buffer);
  s_target_buffer = mono_gchandle_new(buffer, /*pinned=*/1);
  int32_t level = 0;
  void *target_arguments[2] = {texture, &level};
  MonoObject *status = nullptr;
  if (!invoke(overload(buffer_class, "SetColorTarget", 2, "Texture2D"), buffer,
              target_arguments) ||
      !(status = call0(buffer, "get_Status")) ||
      !*static_cast<bool *>(mono_object_unbox(status)) ||
      !set_screen_buffer(buffer)) {
    drop_target();
    return false;
  }
  s_target_width = width;
  s_target_height = height;
  s_target_active = true;
  LOG_INFO("[SCREEN] render target %dx%d format=%d", width, height, format);
  return true;
}

MonoObject *renderer() {
  MonoClass *values = ui_values();
  MonoClassField *field =
      values ? mono_class_get_field_from_name(values, "Renderer") : nullptr;
  MonoObject *object = nullptr;
  if (field)
    mono_field_static_get_value(mono_class_vtable(mono_domain_get(), values),
                                field, &object);
  return object;
}

/* Puts the target on the display: the frame PUI last finished in it is
   drawn as one textured rectangle over the whole screen, with PUI's own
   renderer (texture shader, no blending). The rectangle is given in layout
   units and covers the layout, which the camera maps to the whole picture. */
void present(MonoObject *context) {
  MonoObject *texture = handle_target(s_target_texture);
  MonoObject *buffer = handle_target(s_target_buffer);
  MonoObject *screen = call0(context, "get_Screen");
  MonoObject *draw = renderer();
  if (!texture || !buffer || !screen || !draw)
    return;
  MonoClass *context_class = mono_object_get_class(context);
  MonoClass *renderer_class = mono_object_get_class(draw);
  int32_t zero = 0;
  int32_t width = static_cast<int32_t>(
      std::lround(static_cast<float>(s_original.width) * s_density));
  int32_t height = static_cast<int32_t>(
      std::lround(static_cast<float>(s_original.height) * s_density));
  int32_t texture_shader = 1; /* ShaderType.Texture */
  int32_t overwrite = 3;      /* BlendMode.Overwrite */
  float opaque = 1.0f;
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  /* The matrix is the final one, straight to clip space (it is not put
     through the camera): the unit rectangle becomes the whole screen, its
     first row at the top. */
  float matrix[16] = {};
  matrix[0] = 2.0f;
  matrix[5] = s_present_flip ? 2.0f : -2.0f;
  matrix[10] = 1.0f;
  matrix[12] = -1.0f;
  matrix[13] = s_present_flip ? -1.0f : 1.0f;
  matrix[15] = 1.0f;
  void *screen_arguments[1] = {screen};
  void *rect_arguments[4] = {&zero, &zero, &width, &height};
  void *shader_arguments[1] = {&texture_shader};
  void *blend_arguments[1] = {&overwrite};
  void *texture_arguments[1] = {texture};
  void *opacity_arguments[1] = {&opaque};
  void *color_arguments[1] = {white};
  void *matrix_arguments[1] = {matrix};
  void *buffer_arguments[1] = {buffer};
  MonoMethod *set_buffer =
      mono_class_get_method_from_name(context_class, "SetFrameBuffer", 1);
  const bool ok =
      invoke(set_buffer, context, screen_arguments) &&
      invoke(mono_class_get_method_from_name(context_class, "SetViewport", 4),
             context, rect_arguments) &&
      invoke(mono_class_get_method_from_name(context_class, "SetScissor", 4),
             context, rect_arguments) &&
      invoke(overload(renderer_class, "SetShader", 1, "PUI.ShaderType"), draw,
             shader_arguments) &&
      invoke(mono_class_get_method_from_name(renderer_class, "SetBlendMode", 1),
             draw, blend_arguments) &&
      invoke(overload(renderer_class, "SetTexture", 1, "Texture"), draw,
             texture_arguments) &&
      invoke(mono_class_get_method_from_name(renderer_class, "SetOpacity", 1),
             draw, opacity_arguments) &&
      invoke(overload(renderer_class, "SetColor", 1, "Vector4"), draw,
             color_arguments) &&
      invoke(mono_class_get_method_from_name(renderer_class,
                                             "DrawTextureRectangle", 1),
             draw, matrix_arguments);
  if (ok)
    ++s_presented;
  else
    ++s_present_failures;
  /* PUI goes on drawing into the target. It does not clear a buffer it was
     given as a screen, so each frame would be drawn over the last ones
     (everything that moves leaves copies of itself): the target is cleared
     here, to transparent, as the real screen is. */
  invoke(set_buffer, context, buffer_arguments);
  float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  void *clear_arguments[4] = {&clear[0], &clear[1], &clear[2], &clear[3]};
  int32_t target_width = s_target_width, target_height = s_target_height;
  void *target_rect[4] = {&zero, &zero, &target_width, &target_height};
  invoke(mono_class_get_method_from_name(context_class, "SetViewport", 4),
         context, target_rect);
  invoke(mono_class_get_method_from_name(context_class, "SetScissor", 4),
         context, target_rect);
  invoke(overload(context_class, "SetClearColor", 4, "Single"), context,
         clear_arguments);
  invoke(mono_class_get_method_from_name(context_class, "Clear", 0), context,
         nullptr);
}

/* ---- scenes ---------------------------------------------------------- */

/* Tells the React app drawing under `root` what size its window has now.
   React Native's own DeviceInfo module has UpdateDimensions(width, height),
   which is what changes Dimensions and useWindowDimensions() in the app
   (see update_dimensions below). */
/* The React native modules of the app drawing under `root`, one by one:
   the module table of its registry (a list; the dictionary beside it can
   only be walked with a struct enumerator, which does not survive being
   invoked from here). */
template <typename Visit> void each_module(MonoObject *root, Visit visit) {
  MonoObject *scene = call0(root, "get_ParentScene");
  MonoObject *context = nullptr;
  for (int guard = 0; scene && guard < 8 && !context; ++guard) {
    context = call0(scene, "get_CurrentReactContext");
    scene = field_of(scene, "superScene");
  }
  MonoObject *table = field_of(
      call0(call0(context, "get_Imp"), "get_NMRegistry"), "_moduleTable");
  MonoObject *boxed = call0(table, "get_Count");
  const int32_t count =
      boxed ? *static_cast<int32_t *>(mono_object_unbox(boxed)) : 0;
  MonoMethod *item = table ? method_of(table, "get_Item", 1) : nullptr;
  for (int32_t index = 0; item && index < count && index < 1000; ++index) {
    void *arguments[1] = {&index};
    MonoObject *exception = nullptr;
    MonoObject *definition =
        mono_runtime_invoke(item, table, arguments, &exception);
    MonoObject *module =
        exception ? nullptr : call0(definition, "get_Target");
    if (module && visit(module))
      return;
  }
}

void update_dimensions(MonoObject *root, float width, float height) {
  each_module(root, [&](MonoObject *module) {
    if (std::strcmp(mono_class_get_name(mono_object_get_class(module)),
                    "DeviceInfoModule"))
      return false;
    MonoMethod *update = method_of(module, "UpdateDimensions", 2);
    void *arguments[2] = {&width, &height};
    MonoObject *exception = nullptr;
    if (update)
      mono_runtime_invoke(update, module, arguments, &exception);
    if (update && !exception)
      ++s_dimension_updates;
    return true;
  });
}

struct Resize {
  float to_width, to_height;
  int changed;
};

/* Whether a size is one a screen of ours can have had: the console's own,
   any format's, or a portrait one. Only widgets of such a size are full
   screen and follow the screen. */
bool screen_sized(float width, float height) {
  const float wide = static_cast<float>(s_original.width);
  const float tall = static_cast<float>(s_original.height);
  const auto same = [](float a, float b) { return std::fabs(a - b) < 1.5f; };
  if (same(height, tall)) {
    for (const Format &format : kFormats) {
      long units = std::lround(static_cast<double>(tall) * format.width /
                               format.height);
      units += units & 1;
      if (same(width, static_cast<float>(units)))
        return true;
    }
  }
  /* portrait: 9:16 of any width */
  return width > 700.0f && same(height, std::round(width * wide / tall));
}

/* The root of a scene drawn by React takes the screen's size, and so do the
   full-screen widgets right under it; React's root view lays its tree out
   again when its size changes. Sony's hand-laid-out scenes are left alone:
   resizing all of them took ShellUI down. */
void resize_root(MonoObject *root, void *context) {
  Resize &job = *static_cast<Resize *>(context);
  bool react = false;
  MonoObject *child = call0(root, "get_FirstChild");
  for (int guard = 0; child && guard < 32 && !react; ++guard) {
    react = std::strstr(mono_class_get_name(mono_object_get_class(child)),
                        "ReactRootView") != nullptr;
    child = call0(child, "get_NextSibling");
  }
  if (!react)
    return;
  const auto stale = [&](MonoObject *widget) {
    float width = 0.0f, height = 0.0f;
    return size_of(widget, &width, &height) && screen_sized(width, height) &&
           (std::fabs(width - job.to_width) > 0.5f ||
            std::fabs(height - job.to_height) > 0.5f);
  };
  if (stale(root)) {
    resize(root, job.to_width, job.to_height);
    update_dimensions(root, job.to_width, job.to_height);
    ++job.changed;
  }
  child = call0(root, "get_FirstChild");
  for (int guard = 0; child && guard < 32; ++guard) {
    if (stale(child)) {
      resize(child, job.to_width, job.to_height);
      ++job.changed;
    }
    child = call0(child, "get_NextSibling");
  }
}

/* Brings every React scene to the size the layout has now. */
void fit_scenes() {
  if (!s_original.saved || s_layout_width <= 0)
    return;
  Resize job{static_cast<float>(s_layout_width),
             static_cast<float>(s_layout_height), 0};
  ShellClock::VisitSceneRoots(resize_root, &job);
  s_resized += job.changed;
}

/* ---- clipping --------------------------------------------------------
 * PUI clips with scissor rectangles it works out in physical pixels as if
 * the picture were neither squeezed nor turned (layout units times the
 * pixel density, from the top-left). Each one set on the screen (not on an
 * off-screen buffer, whose contents are not transformed) is moved to where
 * its layout rectangle is really drawn. */
struct Rect {
  int32_t x, y, w, h;
};

using SetInts = void (*)(MonoObject *, int32_t, int32_t, int32_t, int32_t);
using SetValue = void (*)(MonoObject *, uint64_t, uint64_t);
using SetRef = void (*)(MonoObject *, Rect *);
SetInts s_set_ints_original = nullptr;
SetValue s_set_value_original = nullptr;
SetRef s_set_ref_original = nullptr;
bool s_inside = false; /* the overloads may call one another */
int s_log_scissors = 0;
int s_scissor_hooks = 0;
MonoClassField *s_frame_buffer_field = nullptr;
MonoClassField *s_screen_field = nullptr;

bool on_screen(MonoObject *context) {
  if (!s_frame_buffer_field || !s_screen_field || !context)
    return true;
  MonoObject *current = nullptr;
  MonoObject *screen = nullptr;
  mono_field_get_value(context, s_frame_buffer_field, &current);
  mono_field_get_value(context, s_screen_field, &screen);
  return !current || current == screen;
}

void transform(MonoObject *context, Rect &rect) {
  if (s_log_scissors > 0) {
    --s_log_scissors;
    LOG_INFO("[SCREEN] scissor %d,%d %dx%d screen=%d", rect.x, rect.y, rect.w,
             rect.h, on_screen(context) ? 1 : 0);
  }
  if (!changed() || !on_screen(context))
    return;
  /* A wider layout is drawn into a target of its own that PUI takes for
     the screen (see the render target below); everything PUI works out in
     pixels is right there. Only a turned picture needs help. */
  if (s_turns == 0)
    return;
  const float physical_width = static_cast<float>(s_original.width) * s_density;
  const float physical_height =
      static_cast<float>(s_original.height) * s_density;
  const float layout_width = static_cast<float>(s_layout_width);
  const float layout_height = static_cast<float>(s_layout_height);
  /* back to layout units */
  const float x = static_cast<float>(rect.x) / s_density;
  const float y = static_cast<float>(rect.y) / s_density;
  float w = static_cast<float>(rect.w) / s_density;
  float h = static_cast<float>(rect.h) / s_density;
  /* PUI cuts every clip rectangle off at the edge of the screen it knows,
     the console's own. One that reaches that edge was cut there, or is the
     whole screen; either way it goes on to the edge of the layout. */
  if (layout_width > static_cast<float>(s_original.width) &&
      x + w >= static_cast<float>(s_original.width) - 0.5f)
    w = layout_width - x;
  if (layout_height > static_cast<float>(s_original.height) &&
      y + h >= static_cast<float>(s_original.height) - 0.5f)
    h = layout_height - y;
  float px, py, pw, ph; /* physical pixels, from the top-left */
  if (s_turns & 1) {
    /* the layout's x runs along the panel's short side */
    const float scale = physical_height / layout_width;
    if (s_turns == 1) {
      px = y * scale;
      py = (layout_width - x - w) * scale;
    } else {
      px = (layout_height - y - h) * scale;
      py = x * scale;
    }
    pw = h * scale;
    ph = w * scale;
  } else {
    const float scale_x = physical_width / layout_width;
    const float scale_y = physical_height / layout_height;
    px = (s_turns == 2 ? layout_width - x - w : x) * scale_x;
    py = (s_turns == 2 ? layout_height - y - h : y) * scale_y;
    pw = w * scale_x;
    ph = h * scale_y;
  }
  const float x0 = std::fmax(0.0f, std::floor(px));
  const float y0 = std::fmax(0.0f, std::floor(py));
  const float x1 = std::fmin(physical_width, std::ceil(px + pw));
  const float y1 = std::fmin(physical_height, std::ceil(py + ph));
  rect.x = static_cast<int32_t>(x0);
  rect.y = static_cast<int32_t>(y0);
  rect.w = static_cast<int32_t>(std::fmax(0.0f, x1 - x0));
  rect.h = static_cast<int32_t>(std::fmax(0.0f, y1 - y0));
}

void set_ints_hook(MonoObject *context, int32_t x, int32_t y, int32_t w,
                   int32_t h) {
  Rect rect{x, y, w, h};
  const bool outer = !s_inside;
  if (outer)
    transform(context, rect);
  s_inside = true;
  s_set_ints_original(context, rect.x, rect.y, rect.w, rect.h);
  if (outer)
    s_inside = false;
}

/* ImageRect by value: sixteen bytes, in two integer registers */
void set_value_hook(MonoObject *context, uint64_t low, uint64_t high) {
  Rect rect{static_cast<int32_t>(low), static_cast<int32_t>(low >> 32),
            static_cast<int32_t>(high), static_cast<int32_t>(high >> 32)};
  const bool outer = !s_inside;
  if (outer)
    transform(context, rect);
  s_inside = true;
  s_set_value_original(
      context,
      static_cast<uint32_t>(rect.x) |
          (static_cast<uint64_t>(static_cast<uint32_t>(rect.y)) << 32),
      static_cast<uint32_t>(rect.w) |
          (static_cast<uint64_t>(static_cast<uint32_t>(rect.h)) << 32));
  if (outer)
    s_inside = false;
}

void set_ref_hook(MonoObject *context, Rect *rect) {
  Rect copy = rect ? *rect : Rect{};
  const bool outer = !s_inside;
  if (outer && rect)
    transform(context, copy);
  s_inside = true;
  s_set_ref_original(context, rect ? &copy : nullptr);
  if (outer)
    s_inside = false;
}

/* ---- fixed-width layouts ----------------------------------------------
 * React apps do lay themselves out for the window they are given, but
 * Sony's code also has the console's width written into it as a number:
 * Home's pages, the game hub and full-screen covers are styled
 * `width: 1920`. Those stay 1920 wide in a wider layout, so the next page
 * shows at the edge and backgrounds stop short. Every width a React view is
 * given goes through the Yoga node the layout is computed on
 * (Facebook.Yoga.YogaNode), so that is where a width of exactly the console's screen becomes
 * the layout's width. Other values pass through untouched. */
using SetLayoutValue = void (*)(MonoObject *, uint64_t);
SetLayoutValue s_set_width_original = nullptr;
SetLayoutValue s_set_min_width_original = nullptr;
SetLayoutValue s_set_max_width_original = nullptr;
int s_layout_hooks = 0;
std::atomic<int> s_widths_widened{0};

/* A YogaValue: the number, then its unit (1 = points). */
uint64_t widened(uint64_t packed) {
  if (s_turns != 0 || !s_original.saved ||
      s_layout_width <= s_original.width)
    return packed;
  float value;
  const uint32_t bits = static_cast<uint32_t>(packed);
  std::memcpy(&value, &bits, sizeof(value));
  if (static_cast<uint32_t>(packed >> 32) != 1u ||
      std::fabs(value - static_cast<float>(s_original.width)) > 0.01f)
    return packed;
  value = static_cast<float>(s_layout_width);
  uint32_t out;
  std::memcpy(&out, &value, sizeof(out));
  ++s_widths_widened;
  return (packed & 0xffffffff00000000ull) | out;
}

void set_width_hook(MonoObject *node, uint64_t value) {
  s_set_width_original(node, widened(value));
}
void set_min_width_hook(MonoObject *node, uint64_t value) {
  s_set_min_width_original(node, widened(value));
}
void set_max_width_hook(MonoObject *node, uint64_t value) {
  s_set_max_width_original(node, widened(value));
}

void install_layout_hooks() {
  /* Yoga's own node: PUI's UICSSLayoutNode wraps it, and React's shadow
     nodes set their styles on it directly. */
  MonoImage *pui = getDLLimage("Sce.Facebook.CSSLayout.dll");
  MonoClass *node =
      pui ? mono_class_from_name(pui, "Facebook.Yoga", "YogaNode") : nullptr;
  struct {
    const char *name;
    void *hook;
    void **original;
  } const hooks[] = {
      {"set_Width", reinterpret_cast<void *>(&set_width_hook),
       reinterpret_cast<void **>(&s_set_width_original)},
      {"set_MinWidth", reinterpret_cast<void *>(&set_min_width_hook),
       reinterpret_cast<void **>(&s_set_min_width_original)},
      {"set_MaxWidth", reinterpret_cast<void *>(&set_max_width_hook),
       reinterpret_cast<void **>(&s_set_max_width_original)},
  };
  for (const auto &hook : hooks) {
    const uint64_t address =
        node ? Get_Address_of_Method(pui, node, hook.name, 1) : 0;
    if (address && InstallDetour(address, hook.hook, hook.original))
      ++s_layout_hooks;
    else
      *hook.original = nullptr;
  }
  LOG_INFO("[SCREEN] layout width hooks=%d", s_layout_hooks);
}

void take_effect(const char *why) {
  const bool ok = apply();
  if (ok)
    fit_scenes();
  s_frame = 0;
  s_settle_frames = 600;
  LOG_INFO("[SCREEN] %s: format=%s turns=%d layout=%dx%d applied=%d", why,
           kFormats[s_format].name, s_turns, s_layout_width, s_layout_height,
           ok ? 1 : 0);
}

} // namespace

int FormatCount() { return kFormatCount; }

const Format &FormatAt(int index) {
  return kFormats[std::clamp(index, 0, kFormatCount - 1)];
}

float LayoutWidth() {
  return s_layout_width > 0 ? static_cast<float>(s_layout_width) : 1920.0f;
}

float LayoutHeight() {
  return s_layout_height > 0 ? static_cast<float>(s_layout_height) : 1080.0f;
}

void Install() {
  MonoImage *core = getDLLimage("Sce.PlayStation.Core.dll");
  MonoClass *context =
      core ? mono_class_from_name(core, "Sce.PlayStation.Core.Graphics",
                                  "GraphicsContext")
           : nullptr;
  if (!context) {
    LOG_ERROR("[SCREEN] GraphicsContext not found; clipping is not adjusted");
    return;
  }
  s_frame_buffer_field = mono_class_get_field_from_name(context, "frameBuffer");
  s_screen_field = mono_class_get_field_from_name(context, "screen");
  void *iter = nullptr;
  while (MonoMethod *method = mono_class_get_methods(context, &iter)) {
    if (std::strcmp(mono_method_get_name(method), "SetScissor"))
      continue;
    MonoMethodSignature *signature = mono_method_signature(method);
    const int count =
        signature ? static_cast<int>(mono_signature_get_param_count(signature))
                  : -1;
    void *hook = nullptr;
    void **original = nullptr;
    if (count == 4) {
      hook = reinterpret_cast<void *>(&set_ints_hook);
      original = reinterpret_cast<void **>(&s_set_ints_original);
    } else if (count == 1) {
      void *param_iter = nullptr;
      MonoType *type = mono_signature_get_params(signature, &param_iter);
      char *name = type ? mono_type_get_name(type) : nullptr;
      const bool by_ref = name && std::strchr(name, '&');
      hook = by_ref ? reinterpret_cast<void *>(&set_ref_hook)
                    : reinterpret_cast<void *>(&set_value_hook);
      original = by_ref ? reinterpret_cast<void **>(&s_set_ref_original)
                        : reinterpret_cast<void **>(&s_set_value_original);
    }
    if (!hook || *original)
      continue;
    const uint64_t address = mono_compile_method(method);
    if (address && InstallDetour(address, hook, original))
      ++s_scissor_hooks;
    else
      *original = nullptr;
  }
  LOG_INFO("[SCREEN] scissor hooks=%d fields=%d", s_scissor_hooks,
           s_frame_buffer_field && s_screen_field ? 1 : 0);
  install_layout_hooks();
  /* A saved format goes in now, before Home's React app starts: an app
     reads the size of its window when it starts and lays its pages out for
     that width from then on (Home's pages stayed 1920 wide when the format
     was only applied once Home was up). */
  const int wanted = std::clamp(g_settings.screen_format, 0, kFormatCount - 1);
  if (wanted != 0 && pending_exists()) {
    LOG_ERROR("[SCREEN] format '%s' left off: ShellUI did not survive it "
              "last time. Choose it again in the Shell Menu to retry",
              kFormats[wanted].name);
    s_left_off = true;
    s_applied_setting = wanted;
  } else if (wanted != 0 && camera()) {
    mark_pending();
    s_format = wanted;
    s_applied_setting = wanted;
    take_effect("startup");
  }
}

void LogScissors(int count) { s_log_scissors = count; }
void ForceFormat(int format) { s_forced_format = format; }
void FlipPresent(bool flip) { s_present_flip = flip; }

std::string DebugModules(const char *filter) {
  std::string found;
  int count = 0;
  each_module(ShellClock::GetHomeRoot(), [&](MonoObject *module) {
    ++count;
    const char *name = mono_class_get_name(mono_object_get_class(module));
    if (strcasestr(name, filter)) {
      found += name;
      found += ' ';
    }
    return false;
  });
  return "count=" + std::to_string(count) + ": " + found;
}

bool Set(int quarter_turns, bool portrait_layout, int portrait_width) {
  s_turns = quarter_turns & 3;
  s_portrait = portrait_layout;
  s_portrait_width = portrait_width >= 720 && portrait_width <= 3840
                         ? portrait_width
                         : 1080;
  take_effect("experiment");
  return changed() || s_turns == 0;
}

void Tick() {
  ++s_frame;
  s_on_ui_thread = true;
  if (s_target_wanted)
    apply(); /* now with the render target */
  if (s_alive_frames >= 0 && ++s_alive_frames > 1200) {
    unlink(kPending); /* twenty seconds on screen: it works here */
    s_alive_frames = -1;
  }
  /* PUI does not swap buffers while it draws into a target of ours, so the
     display would stand still: once a frame the target (the frame PUI last
     finished in it) is put on the display and the buffers are swapped here. */
  if (s_target_active) {
    if (MonoObject *context = graphics_context()) {
      present(context);
      invoke(mono_class_get_method_from_name(mono_object_get_class(context),
                                             "SwapBuffers", 0),
             context, nullptr);
      ++s_swaps;
    }
  }
  /* The setting, once Home is there (the scenes are found from it). */
  const int wanted = std::clamp(
      s_forced_format >= 0 ? s_forced_format : g_settings.screen_format, 0,
      kFormatCount - 1);
  if (wanted != s_applied_setting) {
    if (wanted == 0 && s_applied_setting < 0 && !s_original.saved) {
      s_applied_setting = 0; /* nothing to do, nothing touched */
    } else if (ShellClock::GetHomeRoot() && camera()) {
      /* without clipping that follows, lists would be cut in the wrong place */
      if (wanted != 0 && s_scissor_hooks < 3) {
        LOG_ERROR("[SCREEN] format '%s' not applied: clipping hooks=%d",
                  kFormats[wanted].name, s_scissor_hooks);
        s_applied_setting = wanted;
        return;
      }
      s_applied_setting = wanted;
      s_format = wanted;
      s_turns = 0;
      s_portrait = false;
      s_left_off = false;
      if (wanted != 0)
        mark_pending();
      else
        unlink(kPending);
      take_effect("setting");
    }
  }
  if (!s_original.saved || s_frame % 30 != 0)
    return;
  if (s_settle_frames > 0)
    s_settle_frames -= 30;
  if (!changed()) {
    if (s_settle_frames > 0)
      fit_scenes(); /* scenes still in the old shape */
    return;
  }
  /* scenes made since, or sized back by their owner */
  fit_scenes();
  MonoObject *object = camera();
  Vector3 up{};
  float right = 0.0f;
  if (!object || !raw_field(object, "Up", &up, false) ||
      !raw_field(object, "Right", &right, false))
    return;
  const Vector3 wanted_up = turned(s_original.up, s_turns);
  int32_t announced_width = 0, announced_height = 0;
  screen_size(&announced_width, &announced_height, false);
  if (std::fabs(up.x - wanted_up.x) > 0.01f ||
      std::fabs(up.y - wanted_up.y) > 0.01f ||
      std::fabs(right - s_wanted_right) > 0.001f ||
      announced_width != s_layout_width) {
    /* PUI set its screen or camera up again (a video mode change does).
       What was saved at the start still describes the console's own setup,
       which does not depend on the video mode, so it is simply put back. */
    ++s_reapplied;
    apply();
    LOG_INFO("[SCREEN] PUI reset its screen; format put back");
  }
}

std::string Status() {
  char text[520];
  Vector3 up{}, target{}, position{};
  float right = 0.0f;
  int32_t width = 0, height = 0;
  MonoObject *object = camera();
  if (object) {
    raw_field(object, "Up", &up, false);
    raw_field(object, "Target", &target, false);
    raw_field(object, "Position", &position, false);
    raw_field(object, "Right", &right, false);
  }
  screen_size(&width, &height, false);
  std::snprintf(text, sizeof(text),
                "format=%s setting=%d turns=%d portrait=%d camera=%d "
                "up=(%g,%g) target=(%g,%g) right=%g screen=%dx%d reapplied=%d "
                "resized=%d hooks=%d+%d widened=%d density=%g target=%dx%d active=%d "
                "swaps=%d presented=%d failed=%d",
                kFormats[s_format].name, g_settings.screen_format, s_turns,
                s_portrait ? 1 : 0, object ? 1 : 0, static_cast<double>(up.x),
                static_cast<double>(up.y), static_cast<double>(target.x),
                static_cast<double>(target.y), static_cast<double>(right),
                width, height, s_reapplied, s_resized, s_scissor_hooks,
                s_layout_hooks, s_widths_widened.load(),
                static_cast<double>(s_density), s_target_width,
                s_target_height, s_target_active ? 1 : 0, s_swaps.load(),
                s_presented.load(), s_present_failures);
  return text;
}

} // namespace ScreenRotation
