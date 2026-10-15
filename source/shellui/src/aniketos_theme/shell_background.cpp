/* Copyright (C) 2026 AniketosHEN — native ShellUI background theming. */

#include "aniketos_theme/shell_background.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <onion/detour.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace AniketosTheme::NativeShellBackground {
namespace {

struct Vector4 {
  float x, y, z, w;
};
struct UIColor {
  float r, g, b, a;
  int32_t color_space;
};
struct NullableUIColor {
  uint8_t has_value;
  uint8_t padding[3];
  UIColor value;
};
static_assert(sizeof(Vector4) == 16);
static_assert(sizeof(UIColor) == 20);
static_assert(sizeof(NullableUIColor) == 24);

MonoImage *s_image = nullptr;
MonoClass *s_plugin_class = nullptr;
MonoMethod *s_get_instance = nullptr;
MonoMethod *s_set_light = nullptr;
MonoMethod *s_set_basemat = nullptr;
MonoMethod *s_set_global_animation = nullptr;
MonoMethod *s_set_background_transition = nullptr;
MonoMethod *s_show_wave = nullptr;
MonoMethod *s_get_show_wave = nullptr;
MonoMethod *s_layer_get_image_mapper = nullptr;
MonoMethod *s_cancel_loading_image = nullptr;
MonoClass *s_transition_param_class = nullptr;
MonoMethod *s_transition_set_type = nullptr;
MonoMethod *s_transition_set_image_uri = nullptr;
MonoMethod *s_transition_set_degree = nullptr;
using BasematThunk = void (*)(MonoObject *, NullableUIColor);
BasematThunk s_set_basemat_thunk = nullptr;
MonoClassField *s_layer_field = nullptr;
MonoClassField *s_transition_type_field = nullptr;
MonoClassField *s_next_image_uri_field = nullptr;
MonoClassField *s_next_blur_uri_field = nullptr;
MonoClassField *s_next_fallback_uri_field = nullptr;
MonoClassField *s_next_overlay_uri_field = nullptr;
uintptr_t s_layer_identity = 0;
bool s_resolved = false;
bool s_original_captured = false;
bool s_capabilities_dumped = false;
constexpr bool kEnableCapabilityDump = false;
Vector4 s_original_light{1.0f, 1.0f, 1.0f, 1.0f};
NullableUIColor s_original_basemat{};

using SetBackgroundTransitionFn = void (*)(MonoObject *, MonoObject *);
SetBackgroundTransitionFn s_set_background_transition_original = nullptr;
bool s_transition_probe_installed = false;
uint32_t s_transition_probe_count = 0;
uint32_t s_suppressed_home_transition_count = 0;
bool s_image_override_enabled = false;
std::string s_image_override_path;
int32_t s_image_override_type = 9;
bool s_force_home_background_enabled = false;
bool s_transition_rewrite_bypass = false;
bool s_native_wave_applied = false;
bool s_original_show_wave = false;
uintptr_t s_native_wave_layer = 0;
bool s_geometry_probe_logged = false;

MonoString *read_string_field(MonoObject *object, MonoClassField *field) {
  if (!object || !field || !mono_field_get_value)
    return nullptr;
  MonoString *value = nullptr;
  mono_field_get_value(object, field, &value);
  return value;
}

const char *string_utf8(MonoString *value) {
  return value && mono_string_to_utf8 ? mono_string_to_utf8(value) : nullptr;
}

void set_background_transition_hook(MonoObject *plugin, MonoObject *param) {
  int32_t type = -1;
  if (param && s_transition_type_field && mono_field_get_value)
    mono_field_get_value(param, s_transition_type_field, &type);

  // Home/Media tile focus emits only the image transition family (5..10).
  // Once the current image has been cleared, swallowing this void call is the
  // safest way to keep ShellUI's own background visible: no title URI reaches
  // the image mapper and no managed setters are re-entered from this detour.
  // Launch, fade-to-black, boot and shutdown transitions use other values and
  // still reach Sony's original method.
  const bool home_image_transition = param && type >= 5 && type <= 10;
  if (!s_transition_rewrite_bypass && s_force_home_background_enabled &&
      !s_image_override_enabled && home_image_transition) {
    if (s_suppressed_home_transition_count < 8)
      LOG_DEBUG("[ANITHEME:TRANSITION] suppressed Home title image type=%d",
                type);
    ++s_suppressed_home_transition_count;
    return;
  }

  // A configured custom wallpaper still replaces only Home/Media tile-focus
  // transitions and preserves all lifecycle transitions.
  if (!s_transition_rewrite_bypass && s_image_override_enabled &&
      home_image_transition && s_transition_set_type &&
      s_transition_set_image_uri && mono_runtime_invoke && mono_string_new) {
    int32_t replacement_type = s_image_override_type;
    MonoObject *exception = nullptr;
    void *type_arguments[] = {&replacement_type};
    mono_runtime_invoke(s_transition_set_type, param, type_arguments,
                        &exception);
    if (!exception) {
      MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
      if (!domain)
        domain = Root_Domain;
      MonoString *uri =
          domain ? mono_string_new(domain, s_image_override_path.c_str())
                 : nullptr;
      if (uri) {
        void *uri_arguments[] = {uri};
        mono_runtime_invoke(s_transition_set_image_uri, param, uri_arguments,
                            &exception);
      }
    }
    if (exception) {
      LOG_ERROR("[ANITHEME:ERR] home image rewrite exception=%p",
                static_cast<void *>(exception));
    } else {
      type = replacement_type;
    }
  }

  if (s_transition_probe_count < 8 && param && mono_field_get_value) {
    if (s_transition_type_field)
      mono_field_get_value(param, s_transition_type_field, &type);
    char *image = const_cast<char *>(
        string_utf8(read_string_field(param, s_next_image_uri_field)));
    char *blur = const_cast<char *>(
        string_utf8(read_string_field(param, s_next_blur_uri_field)));
    char *fallback = const_cast<char *>(
        string_utf8(read_string_field(param, s_next_fallback_uri_field)));
    char *overlay = const_cast<char *>(
        string_utf8(read_string_field(param, s_next_overlay_uri_field)));
    LOG_DEBUG("[ANITHEME:TRANSITION] type=%d image='%s' blur='%s' "
              "fallback='%s' overlay='%s'",
              type, image ? image : "", blur ? blur : "",
              fallback ? fallback : "", overlay ? overlay : "");
    if (mono_free) {
      if (image)
        mono_free(image);
      if (blur)
        mono_free(blur);
      if (fallback)
        mono_free(fallback);
      if (overlay)
        mono_free(overlay);
    }
    ++s_transition_probe_count;
  }
  if (s_set_background_transition_original)
    s_set_background_transition_original(plugin, param);
}

void install_transition_probe() {
  if (s_transition_probe_installed || !s_image)
    return;
  MonoClass *param_class = mono_class_from_name(
      s_image, "Sce.Vsh.ShellUI.BGLayer", "BackgroundTransitionParam");
  if (!param_class)
    return;
  s_transition_type_field = mono_class_get_field_from_name(
      param_class, "<TransitionType>k__BackingField");
  s_next_image_uri_field = mono_class_get_field_from_name(
      param_class, "<NextImageUri>k__BackingField");
  s_next_blur_uri_field = mono_class_get_field_from_name(
      param_class, "<NextBlurImageUri>k__BackingField");
  s_next_fallback_uri_field = mono_class_get_field_from_name(
      param_class, "<NextFallbackImageUri>k__BackingField");
  s_next_overlay_uri_field = mono_class_get_field_from_name(
      param_class, "<NextOverlayImageUri>k__BackingField");
  const uint64_t address =
      Get_Address_of_Method(s_image, "Sce.Vsh.ShellUI.BGLayer", "BGLayerPlugin",
                            "SetBackgroundTransition", 1);
  if (!address)
    return;
  s_transition_probe_installed = InstallDetour(
      address, reinterpret_cast<void *>(&set_background_transition_hook),
      reinterpret_cast<void **>(&s_set_background_transition_original));
  LOG_INFO("[ANITHEME:TRANSITION] persistent-image hook installed=%d "
           "address=0x%llx",
           s_transition_probe_installed ? 1 : 0,
           static_cast<unsigned long long>(address));
}

bool relevant_capability(const char *text) {
  if (!text)
    return false;
  constexpr const char *needles[] = {
      "Background", "BG",      "Basemat",  "Light", "Particle",
      "Image",      "Texture", "Overlay",  "Blur",  "Transition",
      "Animation",  "Shader",  "Material", "Global"};
  for (const char *needle : needles)
    if (std::strstr(text, needle))
      return true;
  return false;
}

void dump_method_signature(MonoClass *klass, MonoMethod *method,
                           bool include_all) {
  if (!klass || !method || !mono_method_get_name || !mono_method_signature ||
      !mono_signature_get_param_count || !mono_signature_get_params ||
      !mono_signature_get_return_type || !mono_type_get_name)
    return;
  const char *method_name = mono_method_get_name(method);
  if (!include_all && !relevant_capability(method_name))
    return;
  MonoMethodSignature *signature = mono_method_signature(method);
  if (!signature)
    return;
  char *return_type =
      mono_type_get_name(mono_signature_get_return_type(signature));
  char params[768]{};
  size_t used = 0;
  void *iterator = nullptr;
  const uint32_t count = mono_signature_get_param_count(signature);
  for (uint32_t index = 0; index < count; ++index) {
    MonoType *type = mono_signature_get_params(signature, &iterator);
    char *type_name = type ? mono_type_get_name(type) : nullptr;
    const int written =
        std::snprintf(params + used, sizeof(params) - used, "%s%s",
                      index ? ", " : "", type_name ? type_name : "?");
    if (type_name && mono_free)
      mono_free(type_name);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(params) - used) {
      used = sizeof(params) - 1;
      break;
    }
    used += static_cast<size_t>(written);
  }
  const char *class_name =
      mono_class_get_name ? mono_class_get_name(klass) : "?";
  LOG_INFO("[ANITHEME:CAP] %s.%s(%s) -> %s", class_name ? class_name : "?",
           method_name ? method_name : "?", params,
           return_type ? return_type : "?");
  if (return_type && mono_free)
    mono_free(return_type);
}

void dump_fields(MonoClass *klass) {
  if (!klass || !mono_class_get_fields || !mono_field_get_name ||
      !mono_field_get_type || !mono_type_get_name)
    return;
  void *iterator = nullptr;
  MonoClassField *first = nullptr;
  uint32_t count = 0;
  while (MonoClassField *field = mono_class_get_fields(klass, &iterator)) {
    // Sony's 11.40 Mono build wraps some metadata iterators back to the first
    // entry instead of returning nullptr.  Keep diagnostics strictly bounded.
    if ((first && field == first) || count++ >= 512) {
      LOG_WARN("[ANITHEME:CAP] field iterator wrapped; stopping class dump");
      break;
    }
    if (!first)
      first = field;
    const char *name = mono_field_get_name(field);
    MonoType *type = mono_field_get_type(field);
    char *type_name = type ? mono_type_get_name(type) : nullptr;
    const int offset =
        mono_field_get_offset ? mono_field_get_offset(field) : -1;
    const uint32_t flags =
        mono_field_get_flags ? mono_field_get_flags(field) : 0;
    alignas(16) uint8_t raw_storage[64]{};
    if ((flags & 0x10u) && mono_field_static_get_value && mono_class_vtable) {
      if (MonoVTable *vtable = mono_class_vtable(Root_Domain, klass))
        mono_field_static_get_value(vtable, field, raw_storage);
    }
    uint64_t raw = 0;
    std::memcpy(&raw, raw_storage, sizeof(raw));
    char *string_value = nullptr;
    if (raw && type_name && std::strcmp(type_name, "System.String") == 0 &&
        mono_string_to_utf8)
      string_value = const_cast<char *>(mono_string_to_utf8(
          reinterpret_cast<MonoString *>(static_cast<uintptr_t>(raw))));
    LOG_INFO("[ANITHEME:CAP] field %s %s offset=%d flags=0x%x raw=0x%llx%s%s",
             type_name ? type_name : "?", name ? name : "?", offset, flags,
             static_cast<unsigned long long>(raw),
             string_value ? " value=" : "", string_value ? string_value : "");
    if (string_value && mono_free)
      mono_free(string_value);
    if (type_name && mono_free)
      mono_free(type_name);
  }
}

[[maybe_unused]] void dump_capabilities_once(MonoImage *image) {
  if (s_capabilities_dumped || !image || !mono_class_from_name ||
      !mono_class_get_methods)
    return;
  constexpr const char *classes[] = {
      "BGLayerPlugin",
      "BackgroundLayer",
      "ImageMapManager",
      "BackgroundTransitionParam",
      "BackgroundTransitionInternalParam",
      "BGTransitionParam",
      "BackgroundBasematParam",
      "BGBasematParam",
      "BackgroundBasematType",
      "GlobalBackgroundState",
      "LightParticleFlag",
      "LightFlag",
      "LightRenderModeIndex",
      "BackgroundTransitionFlag",
      "NativeTransitionParamFlags",
      "BackgroundTransitionType",
      "BackgroundTransitionDegree",
      "DisplayedImageType",
      "ImageMode",
      "ImageScaleType",
      "ImageFormat",
      "ImageVersion",
      "BGTransition",
  };
  LOG_INFO("[ANITHEME:CAP] targeted BGLayer capability dump begin");
  for (const char *class_name : classes) {
    MonoClass *klass =
        mono_class_from_name(image, "Sce.Vsh.ShellUI.BGLayer", class_name);
    if (!klass)
      continue;
    LOG_INFO("[ANITHEME:CAP] class=%s", class_name);
    dump_fields(klass);
    void *iterator = nullptr;
    MonoMethod *first = nullptr;
    uint32_t count = 0;
    while (MonoMethod *method = mono_class_get_methods(klass, &iterator)) {
      if ((first && method == first) || count++ >= 1024) {
        LOG_WARN(
            "[ANITHEME:CAP] method iterator wrapped; stopping class dump");
        break;
      }
      if (!first)
        first = method;
      dump_method_signature(klass, method,
                            std::strcmp(class_name, "BGLayerPlugin") != 0);
    }
  }
  s_capabilities_dumped = true;
  LOG_INFO("[ANITHEME:CAP] BGLayer capability dump complete");
}

MonoImage *open_loaded_image() {
  if (!Root_Domain || !mono_domain_assembly_open || !mono_assembly_get_image)
    return nullptr;
  constexpr const char *paths[] = {
      "/system_ex/common_ex/lib/Sce.Vsh.ShellUI.BGLayer.dll",
      "Sce.Vsh.ShellUI.BGLayer.dll",
  };
  for (const char *path : paths) {
    if (MonoAssembly *assembly = mono_domain_assembly_open(Root_Domain, path))
      if (MonoImage *image = mono_assembly_get_image(assembly))
        return image;
  }
  return nullptr;
}

MonoObject *current_plugin(MonoObject **exception_out = nullptr) {
  if (!s_get_instance || !mono_runtime_invoke)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *plugin =
      mono_runtime_invoke(s_get_instance, nullptr, nullptr, &exception);
  if (exception_out)
    *exception_out = exception;
  return exception ? nullptr : plugin;
}

bool invoke_light(MonoObject *plugin, const Vector4 &color) {
  if (!plugin || !s_set_light || !mono_runtime_invoke)
    return false;
  Vector4 value = color;
  void *arguments[] = {&value};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(s_set_light, plugin, arguments, &exception);
  if (exception) {
    LOG_ERROR("[ANITHEME:ERR] SetThemedLightColor exception=%p",
              static_cast<void *>(exception));
    return false;
  }
  return true;
}

bool invoke_basemat(MonoObject *plugin, const NullableUIColor &color) {
  if (!plugin || !s_set_basemat_thunk)
    return false;
  // The compiled 11.40 thunk consumes the 24-byte Nullable<UIColor> by value
  // from [rbp+0x10]. mono_runtime_invoke mis-marshals this particular large
  // value type and crashes SceShellUI. A typed direct call follows the actual
  // SysV ABI while still entering Sony's managed logical setter.
  s_set_basemat_thunk(plugin, color);
  return true;
}

bool invoke_value_setter(MonoMethod *method, MonoObject *instance, void *value,
                         const char *name) {
  if (!method || !instance || !mono_runtime_invoke)
    return false;
  void *arguments[] = {value};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, instance, arguments, &exception);
  if (exception) {
    LOG_ERROR("[ANITHEME:ERR] %s exception=%p", name,
              static_cast<void *>(exception));
    return false;
  }
  return true;
}

bool invoke_reference_setter(MonoMethod *method, MonoObject *instance,
                             MonoObject *value, const char *name) {
  if (!method || !instance || !mono_runtime_invoke)
    return false;
  void *arguments[] = {value};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, instance, arguments, &exception);
  if (exception) {
    LOG_ERROR("[ANITHEME:ERR] %s exception=%p", name,
              static_cast<void *>(exception));
    return false;
  }
  return true;
}

int32_t transition_value(ImageTransition transition) {
  switch (transition) {
  case ImageTransition::Ripple:
    return 6;
  case ImageTransition::SlideLeft:
    return 7;
  case ImageTransition::SlideRight:
    return 8;
  case ImageTransition::Fade:
    return 9;
  case ImageTransition::RippleBack:
    return 10;
  case ImageTransition::Count:
    break;
  }
  return 9;
}

int32_t particle_state(ParticleMode mode) {
  switch (mode) {
  // GlobalBackgroundState.None (0) removes Sony's animated layer and exposes
  // black. ParticleSpread (10) is the normal home-screen presentation. When
  // Default is already selected the manager does not issue a setter at all,
  // preserving whichever stock state ShellUI currently owns.
  case ParticleMode::Default:
    return 10;
  case ParticleMode::Bottom:
    return 9;
  case ParticleMode::Spread:
    return 10;
  case ParticleMode::Off:
    return 11;
  case ParticleMode::Count:
    break;
  }
  return 0;
}

bool invoke_transition(MonoObject *plugin, int32_t type, const char *path) {
  if (!plugin || !s_transition_param_class || !s_set_background_transition ||
      !mono_object_new || !mono_runtime_object_init)
    return false;
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  if (!domain)
    domain = Root_Domain;
  MonoObject *param =
      domain ? mono_object_new(domain, s_transition_param_class) : nullptr;
  if (!param)
    return false;
  mono_runtime_object_init(param);
  const uint32_t handle = mono_gchandle_new ? mono_gchandle_new(param, 1) : 0;
  bool ok = invoke_value_setter(s_transition_set_type, param, &type,
                                "set_TransitionType");
  const int32_t degree = 2; // Sony BackgroundTransitionDegree.Normal.
  ok = invoke_value_setter(s_transition_set_degree, param,
                           const_cast<int32_t *>(&degree), "set_Degree") &&
       ok;
  if (path && *path) {
    MonoString *uri = mono_string_new ? mono_string_new(domain, path) : nullptr;
    ok = uri &&
         invoke_reference_setter(s_transition_set_image_uri, param,
                                 reinterpret_cast<MonoObject *>(uri),
                                 "set_NextImageUri") &&
         ok;
  }
  if (ok) {
    void *arguments[] = {param};
    MonoObject *exception = nullptr;
    mono_runtime_invoke(s_set_background_transition, plugin, arguments,
                        &exception);
    if (exception) {
      LOG_ERROR("[ANITHEME:ERR] SetBackgroundTransition exception=%p",
                static_cast<void *>(exception));
      ok = false;
    }
  }
  if (handle && mono_gchandle_free)
    mono_gchandle_free(handle);
  return ok;
}

bool plausible_light(const Vector4 &value) {
  const float values[] = {value.x, value.y, value.z, value.w};
  for (float channel : values)
    if (!std::isfinite(channel) || channel < -0.01f || channel > 8.0f)
      return false;
  return true;
}

bool capture_original(MonoObject *plugin, MonoObject *layer) {
  if (s_original_captured || !plugin || !layer || !mono_object_get_class ||
      !mono_class_get_method_from_name || !mono_class_get_field_from_name ||
      !mono_field_get_value || !mono_runtime_invoke)
    return s_original_captured;

  MonoClass *layer_class = mono_object_get_class(layer);
  if (!layer_class)
    return false;
  MonoClassField *light_field =
      mono_class_get_field_from_name(layer_class, "themedLightColor");
  if (light_field) {
    alignas(8) uint8_t raw_light[32]{};
    mono_field_get_value(layer, light_field, raw_light);
    Vector4 captured{};
    std::memcpy(&captured, raw_light, sizeof(captured));
    if (plausible_light(captured))
      s_original_light = captured;
  }

  MonoMethod *get_transition =
      mono_class_get_method_from_name(layer_class, "get_BGTransition", 0);
  MonoObject *transition_exception = nullptr;
  MonoObject *transition =
      get_transition ? mono_runtime_invoke(get_transition, layer, nullptr,
                                           &transition_exception)
                     : nullptr;
  if (!transition || transition_exception)
    return false;
  MonoClass *transition_class = mono_object_get_class(transition);
  MonoClassField *basemat_field =
      transition_class ? mono_class_get_field_from_name(
                             transition_class, "themedBackgroundBasematColor")
                       : nullptr;
  if (!basemat_field)
    return false;
  alignas(8) uint8_t raw_basemat[32]{};
  mono_field_get_value(transition, basemat_field, raw_basemat);
  std::memcpy(&s_original_basemat, raw_basemat, sizeof(s_original_basemat));
  s_original_captured = true;
  LOG_INFO("[ANITHEME:STATE] captured original layer=%p "
           "light=(%.4f,%.4f,%.4f,%.4f) basemat.has=%u "
           "basemat=(%.4f,%.4f,%.4f,%.4f) space=%d",
           static_cast<void *>(layer), s_original_light.x, s_original_light.y,
           s_original_light.z, s_original_light.w, s_original_basemat.has_value,
           s_original_basemat.value.r, s_original_basemat.value.g,
           s_original_basemat.value.b, s_original_basemat.value.a,
           s_original_basemat.value.color_space);
  return true;
}

} // namespace

MonoObject *CurrentUi3Scene() {
  if (!Resolve())
    return nullptr;
  return current_plugin();
}

bool Resolve() {
  if (!Root_Domain || !mono_class_from_name ||
      !mono_class_get_method_from_name || !mono_class_get_field_from_name ||
      !mono_field_get_value)
    return false;
  if (!s_image)
    s_image = open_loaded_image();
  if (!s_image)
    return false;
  if constexpr (kEnableCapabilityDump)
    dump_capabilities_once(s_image);
  if (!s_plugin_class) {
    s_plugin_class = mono_class_from_name(s_image, "Sce.Vsh.ShellUI.BGLayer",
                                          "BGLayerPlugin");
    s_get_instance =
        s_plugin_class
            ? mono_class_get_method_from_name(s_plugin_class, "get_Instance", 0)
            : nullptr;
    s_set_light = s_plugin_class ? mono_class_get_method_from_name(
                                       s_plugin_class, "SetThemedLightColor", 1)
                                 : nullptr;
    s_set_basemat = s_plugin_class ? mono_class_get_method_from_name(
                                         s_plugin_class,
                                         "SetThemedBackgroundBasematColor", 1)
                                   : nullptr;
    s_set_global_animation =
        s_plugin_class ? mono_class_get_method_from_name(
                             s_plugin_class, "SetGlobalBackgroundAnimation", 1)
                       : nullptr;
    s_set_background_transition =
        s_plugin_class ? mono_class_get_method_from_name(
                             s_plugin_class, "SetBackgroundTransition", 1)
                       : nullptr;
    s_show_wave =
        s_plugin_class
            ? mono_class_get_method_from_name(s_plugin_class, "ShowWave", 1)
            : nullptr;
    s_transition_param_class = mono_class_from_name(
        s_image, "Sce.Vsh.ShellUI.BGLayer", "BackgroundTransitionParam");
    MonoClass *background_layer_class = mono_class_from_name(
        s_image, "Sce.Vsh.ShellUI.BGLayer", "BackgroundLayer");
    s_get_show_wave =
        background_layer_class
            ? mono_class_get_method_from_name(background_layer_class,
                                              "get_ShowWave", 0)
            : nullptr;
    MonoClass *image_mapper_class = mono_class_from_name(
        s_image, "Sce.Vsh.ShellUI.BGLayer", "ImageMapManager");
    s_layer_get_image_mapper =
        background_layer_class
            ? mono_class_get_method_from_name(background_layer_class,
                                              "get_ImageMapper", 0)
            : nullptr;
    s_cancel_loading_image =
        image_mapper_class ? mono_class_get_method_from_name(
                                 image_mapper_class, "CancelLoadingImage", 0)
                           : nullptr;
    s_transition_set_type =
        s_transition_param_class
            ? mono_class_get_method_from_name(s_transition_param_class,
                                              "set_TransitionType", 1)
            : nullptr;
    s_transition_set_image_uri =
        s_transition_param_class
            ? mono_class_get_method_from_name(s_transition_param_class,
                                              "set_NextImageUri", 1)
            : nullptr;
    s_transition_set_degree =
        s_transition_param_class
            ? mono_class_get_method_from_name(s_transition_param_class,
                                              "set_Degree", 1)
            : nullptr;
    if (s_set_basemat && mono_compile_method)
      s_set_basemat_thunk =
          reinterpret_cast<BasematThunk>(mono_compile_method(s_set_basemat));
    s_layer_field =
        s_plugin_class
            ? mono_class_get_field_from_name(s_plugin_class, "m_layer")
            : nullptr;
    install_transition_probe();
  }
  if (!s_get_instance || !s_set_light || !s_set_basemat_thunk || !s_layer_field)
    return false;

  MonoObject *exception = nullptr;
  MonoObject *plugin = current_plugin(&exception);
  MonoObject *layer = nullptr;
  if (plugin)
    mono_field_get_value(plugin, s_layer_field, &layer);
  if (!plugin || !layer || exception) {
    s_resolved = false;
    s_layer_identity = 0;
    return false;
  }

  const uintptr_t identity = reinterpret_cast<uintptr_t>(layer);
  const bool changed = identity != s_layer_identity;
  s_layer_identity = identity;
  s_resolved = capture_original(plugin, layer);
  if (changed && s_resolved)
    LOG_INFO("[ANITHEME:HOOK] native BGLayer context ready plugin=%p layer=%p",
             static_cast<void *>(plugin), static_cast<void *>(layer));
  return s_resolved;
}

bool IsResolved() { return s_resolved; }
uintptr_t LayerIdentity() { return s_layer_identity; }

bool Apply(const RGB &base, const RGB &glow, float glow_intensity,
           bool log_change) {
  if (!Resolve())
    return false;
  MonoObject *plugin = current_plugin();
  if (!plugin)
    return false;
  const RGB safe_base = ClampRGB(base);
  const RGB safe_glow = ClampRGB(glow);
  const float intensity = std::clamp(glow_intensity, 0.0f, 2.0f);
  const NullableUIColor basemat{
      1, {0, 0, 0}, {safe_base.r, safe_base.g, safe_base.b, 1.0f, 0}};
  const Vector4 light{safe_glow.r * intensity, safe_glow.g * intensity,
                      safe_glow.b * intensity, 1.0f};
  LOG_DEBUG("[ANITHEME:HOOK] apply begin layer=%p abi=nullable-stack24",
            reinterpret_cast<void *>(s_layer_identity));
  const bool base_ok = invoke_basemat(plugin, basemat);
  LOG_DEBUG("[ANITHEME:HOOK] basemat setter returned ok=%d", base_ok ? 1 : 0);
  const bool light_ok = invoke_light(plugin, light);
  if (base_ok && light_ok && log_change) {
    LOG_INFO("[ANITHEME:COLOR] applied native base=(%.3f,%.3f,%.3f) "
             "glow=(%.3f,%.3f,%.3f) intensity=%.2f layer=%p",
             safe_base.r, safe_base.g, safe_base.b, safe_glow.r, safe_glow.g,
             safe_glow.b, intensity,
             reinterpret_cast<void *>(s_layer_identity));
  }
  return base_ok && light_ok;
}

const char *ParticleModeName(ParticleMode mode) {
  switch (mode) {
  case ParticleMode::Default:
    return "PS5 Default";
  case ParticleMode::Bottom:
    return "Bottom";
  case ParticleMode::Spread:
    return "Spread";
  case ParticleMode::Off:
    return "Off";
  case ParticleMode::Count:
    break;
  }
  return "PS5 Default";
}

const char *ImageTransitionName(ImageTransition transition) {
  switch (transition) {
  case ImageTransition::Ripple:
    return "Ripple";
  case ImageTransition::SlideLeft:
    return "Slide left";
  case ImageTransition::SlideRight:
    return "Slide right";
  case ImageTransition::Fade:
    return "Fade";
  case ImageTransition::RippleBack:
    return "Ripple back";
  case ImageTransition::Count:
    break;
  }
  return "Fade";
}

bool SetParticles(ParticleMode mode) {
  if (!Resolve() || !s_set_global_animation)
    return false;
  MonoObject *plugin = current_plugin();
  int32_t state = particle_state(mode);
  const bool ok = invoke_value_setter(s_set_global_animation, plugin, &state,
                                      "SetGlobalBackgroundAnimation");
  if (ok)
    LOG_INFO("[ANITHEME:PARTICLE] native mode=%s state=%d",
             ParticleModeName(mode), state);
  return ok;
}

bool ApplyProceduralWave(float) {
  if (!Resolve() || !s_show_wave || !mono_runtime_invoke ||
      !mono_object_unbox)
    return false;
  if (s_native_wave_applied && s_native_wave_layer == s_layer_identity)
    return true;

  if (!s_geometry_probe_logged) {
    const uint64_t setup = Get_Address_of_Method(
        s_image, "Sce.Vsh.ShellUI.BGLayer", "BackgroundLayerForDebugMenu",
        "Setup", 1);
    const uint64_t set_debug = Get_Address_of_Method(
        s_image, "Sce.Vsh.ShellUI.BGLayer", "BGLayerNative",
        "SetDebugParameter", 2);
    const uint64_t update = Get_Address_of_Method(
        s_image, "Sce.Vsh.ShellUI.BGLayer", "BGLayerNative", "Update", 1);
    LOG_INFO("[ANITHEME:GEOMETRY-PROBE] runtime methods setup=0x%llx "
             "set_debug=0x%llx update=0x%llx",
             static_cast<unsigned long long>(setup),
             static_cast<unsigned long long>(set_debug),
             static_cast<unsigned long long>(update));
    s_geometry_probe_logged = true;
  }

  MonoObject *plugin = current_plugin();
  MonoObject *layer = nullptr;
  if (plugin && s_layer_field)
    mono_field_get_value(plugin, s_layer_field, &layer);
  if (!plugin || !layer)
    return false;

  bool original = false;
  MonoObject *exception = nullptr;
  MonoObject *boxed = s_get_show_wave
                          ? mono_runtime_invoke(s_get_show_wave, layer, nullptr,
                                                &exception)
                          : nullptr;
  if (exception || !boxed) {
    LOG_ERROR("[ANITHEME:WAVE] get_ShowWave exception=%p result=%p",
              static_cast<void *>(exception), static_cast<void *>(boxed));
    return false;
  }
  if (void *raw = mono_object_unbox(boxed))
    std::memcpy(&original, raw, sizeof(original));

  bool enabled = true;
  if (!invoke_value_setter(s_show_wave, plugin, &enabled, "ShowWave"))
    return false;
  s_original_show_wave = original;
  s_native_wave_layer = s_layer_identity;
  s_native_wave_applied = true;
  LOG_INFO("[ANITHEME:WAVE] stock wave visibility retained layer=%p "
           "original_show=%d; no geometry override applied yet",
           reinterpret_cast<void *>(s_layer_identity), original ? 1 : 0);
  return true;
}

void DisableProceduralWave() {
  if (s_native_wave_applied && s_native_wave_layer == s_layer_identity &&
      Resolve() && s_show_wave) {
    MonoObject *plugin = current_plugin();
    bool original = s_original_show_wave;
    if (plugin)
      (void)invoke_value_setter(s_show_wave, plugin, &original,
                                "ShowWave restore");
  }
  s_native_wave_applied = false;
  s_native_wave_layer = 0;
  DisableImageOverride();
}

namespace {

bool clear_native_image(bool disable_override) {
  if (disable_override) {
    s_image_override_enabled = false;
    s_image_override_path.clear();
  }
  if (!Resolve())
    return false;
  MonoObject *plugin = current_plugin();
  MonoObject *layer = nullptr;
  if (plugin && s_layer_field)
    mono_field_get_value(plugin, s_layer_field, &layer);
  if (layer && s_layer_get_image_mapper && s_cancel_loading_image &&
      mono_runtime_invoke) {
    MonoObject *exception = nullptr;
    MonoObject *mapper = mono_runtime_invoke(s_layer_get_image_mapper, layer,
                                             nullptr, &exception);
    if (mapper && !exception)
      mono_runtime_invoke(s_cancel_loading_image, mapper, nullptr, &exception);
    if (exception)
      LOG_WARN("[ANITHEME:IMAGE] cancel pending load exception=%p",
               static_cast<void *>(exception));
  }
  // This matches the empty CustomImageFade transition emitted by the stock
  // ShellUI when it removes a title image.
  const bool old_bypass = s_transition_rewrite_bypass;
  s_transition_rewrite_bypass = true;
  const bool ok = invoke_transition(plugin, 9, nullptr);
  s_transition_rewrite_bypass = old_bypass;
  return ok;
}

} // namespace

bool ApplyImage(const char *path, ImageTransition transition) {
  struct stat info{};
  if (!path || !*path || stat(path, &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_size <= 0) {
    LOG_ERROR("[ANITHEME:IMAGE] unreadable image path='%s'", path ? path : "");
    return false;
  }
  if (!Resolve())
    return false;
  MonoObject *plugin = current_plugin();
  const int32_t type = transition_value(transition);
  const bool old_enabled = s_image_override_enabled;
  const std::string old_path = s_image_override_path;
  const int32_t old_type = s_image_override_type;
  s_image_override_enabled = true;
  s_image_override_path = path;
  s_image_override_type = type;
  const bool ok = invoke_transition(plugin, type, path);
  if (!ok) {
    s_image_override_enabled = old_enabled;
    s_image_override_path = old_path;
    s_image_override_type = old_type;
  }
  if (ok)
    LOG_INFO("[ANITHEME:IMAGE] native image applied path='%s' "
             "transition=%s type=%d bytes=%lld",
             path, ImageTransitionName(transition), type,
             static_cast<long long>(info.st_size));
  return ok;
}

bool SetForceHomeBackground(bool enabled) {
  if (!Resolve())
    return false;
  const bool changed = enabled != s_force_home_background_enabled;
  bool ok = true;
  if (enabled && !s_image_override_enabled)
    ok = clear_native_image(false);
  if (!ok)
    return false;
  s_force_home_background_enabled = enabled;
  if (!enabled)
    s_suppressed_home_transition_count = 0;
  if (changed)
    LOG_INFO("[ANITHEME:IMAGE] force ShellUI background on Home enabled=%d",
             enabled ? 1 : 0);
  return ok;
}

void DisableImageOverride() {
  s_image_override_enabled = false;
  s_image_override_path.clear();
}

bool ClearImage() {
  const bool ok = clear_native_image(true);
  if (ok)
    LOG_INFO("[ANITHEME:IMAGE] native custom image cleared");
  return ok;
}

bool RestoreOriginal() {
  if (!Resolve() || !s_original_captured)
    return false;
  MonoObject *plugin = current_plugin();
  if (!plugin)
    return false;
  const bool base_ok = invoke_basemat(plugin, s_original_basemat);
  const bool light_ok = invoke_light(plugin, s_original_light);
  if (base_ok && light_ok)
    LOG_INFO("[ANITHEME:COLOR] exact captured Sony state restored layer=%p",
             reinterpret_cast<void *>(s_layer_identity));
  return base_ok && light_ok;
}

} // namespace AniketosTheme::NativeShellBackground
