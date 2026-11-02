/* Copyright (C) 2026 AniketosHEN — native ShellUI background theming. */

#include "aniketos_theme/shell_background.hpp"

#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/ui_color.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <onion/detour.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace AniketosTheme::NativeShellBackground {
namespace {

using AniketosUI::NullableUIColor;
using AniketosUI::Reflect::MethodOf;

constexpr const char *kBgLayerNamespace = "Sce.Vsh.ShellUI.BGLayer";
constexpr int32_t kCustomImageFade = 9; // Sony BackgroundTransitionType

struct Vector4 {
  float x, y, z, w;
};
static_assert(sizeof(Vector4) == 16);

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
Vector4 s_original_light{1.0f, 1.0f, 1.0f, 1.0f};
NullableUIColor s_original_basemat{};

using SetBackgroundTransitionFn = void (*)(MonoObject *, MonoObject *);
SetBackgroundTransitionFn s_set_background_transition_original = nullptr;
bool s_transition_hook_installed = false;
uint32_t s_transition_log_count = 0;
uint32_t s_suppressed_home_transition_count = 0;
bool s_image_override_enabled = false;
std::string s_image_override_path;
int32_t s_image_override_type = kCustomImageFade;
bool s_force_home_background_enabled = false;
bool s_transition_rewrite_bypass = false;
bool s_native_wave_applied = false;
bool s_original_show_wave = false;
uintptr_t s_native_wave_layer = 0;

MonoDomain *current_domain() {
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  return domain ? domain : Root_Domain;
}

/* For the log: mono_free the result. */
char *string_field_utf8(MonoObject *object, MonoClassField *field) {
  if (!object || !field || !mono_field_get_value || !mono_string_to_utf8)
    return nullptr;
  MonoString *value = nullptr;
  mono_field_get_value(object, field, &value);
  return value ? const_cast<char *>(mono_string_to_utf8(value)) : nullptr;
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
      MonoDomain *domain = current_domain();
      MonoString *uri =
          domain ? mono_string_new(domain, s_image_override_path.c_str())
                 : nullptr;
      if (uri) {
        void *uri_arguments[] = {uri};
        mono_runtime_invoke(s_transition_set_image_uri, param, uri_arguments,
                            &exception);
      }
    }
    if (exception)
      LOG_ERROR("[ANITHEME:ERR] home image rewrite exception=%p",
                static_cast<void *>(exception));
  }

  if (s_transition_log_count < 8 && param && mono_field_get_value) {
    if (s_transition_type_field)
      mono_field_get_value(param, s_transition_type_field, &type);
    char *const uris[] = {string_field_utf8(param, s_next_image_uri_field),
                          string_field_utf8(param, s_next_blur_uri_field),
                          string_field_utf8(param, s_next_fallback_uri_field),
                          string_field_utf8(param, s_next_overlay_uri_field)};
    LOG_DEBUG("[ANITHEME:TRANSITION] type=%d image='%s' blur='%s' "
              "fallback='%s' overlay='%s'",
              type, uris[0] ? uris[0] : "", uris[1] ? uris[1] : "",
              uris[2] ? uris[2] : "", uris[3] ? uris[3] : "");
    if (mono_free)
      for (char *uri : uris)
        if (uri)
          mono_free(uri);
    ++s_transition_log_count;
  }
  if (s_set_background_transition_original)
    s_set_background_transition_original(plugin, param);
}

void install_transition_hook() {
  if (s_transition_hook_installed || !s_transition_param_class)
    return;
  MonoClass *param_class = s_transition_param_class;
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
      Get_Address_of_Method(s_image, kBgLayerNamespace, "BGLayerPlugin",
                            "SetBackgroundTransition", 1);
  if (!address)
    return;
  s_transition_hook_installed = InstallDetour(
      address, reinterpret_cast<void *>(&set_background_transition_hook),
      reinterpret_cast<void **>(&s_set_background_transition_original));
  LOG_INFO("[ANITHEME:TRANSITION] persistent-image hook installed=%d "
           "address=0x%llx",
           s_transition_hook_installed ? 1 : 0,
           static_cast<unsigned long long>(address));
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

MonoObject *layer_of(MonoObject *plugin) {
  MonoObject *layer = nullptr;
  if (plugin && s_layer_field)
    mono_field_get_value(plugin, s_layer_field, &layer);
  return layer;
}

bool invoke_setter(MonoMethod *method, MonoObject *instance, void *value,
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

bool invoke_light(MonoObject *plugin, const Vector4 &color) {
  Vector4 value = color;
  return invoke_setter(s_set_light, plugin, &value, "SetThemedLightColor");
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

int32_t transition_value(ImageTransition transition) {
  switch (transition) {
  case ImageTransition::Ripple:
    return 6;
  case ImageTransition::SlideLeft:
    return 7;
  case ImageTransition::SlideRight:
    return 8;
  case ImageTransition::Fade:
    return kCustomImageFade;
  case ImageTransition::RippleBack:
    return 10;
  case ImageTransition::Count:
    break;
  }
  return kCustomImageFade;
}

int32_t particle_state(ParticleMode mode) {
  switch (mode) {
  // GlobalBackgroundState.None (0) removes Sony's animated layer and exposes
  // black. ParticleSpread (10) is the normal home-screen presentation. When
  // Default is already selected the manager does not issue a setter at all,
  // preserving whichever stock state ShellUI currently owns.
  case ParticleMode::Default:
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
  MonoDomain *domain = current_domain();
  MonoObject *param =
      domain ? mono_object_new(domain, s_transition_param_class) : nullptr;
  if (!param)
    return false;
  mono_runtime_object_init(param);
  uint32_t handle = mono_gchandle_new ? mono_gchandle_new(param, 1) : 0;
  bool ok = invoke_setter(s_transition_set_type, param, &type,
                          "set_TransitionType");
  int32_t degree = 2; // Sony BackgroundTransitionDegree.Normal.
  ok = invoke_setter(s_transition_set_degree, param, &degree, "set_Degree") &&
       ok;
  if (path && *path) {
    MonoString *uri = mono_string_new ? mono_string_new(domain, path) : nullptr;
    ok = uri &&
         invoke_setter(s_transition_set_image_uri, param, uri,
                       "set_NextImageUri") &&
         ok;
  }
  ok = ok && invoke_setter(s_set_background_transition, plugin, param,
                           "SetBackgroundTransition");
  AniketosUI::Reflect::Release(handle);
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

bool clear_native_image(bool disable_override) {
  if (disable_override)
    DisableImageOverride();
  if (!Resolve())
    return false;
  MonoObject *plugin = current_plugin();
  MonoObject *layer = layer_of(plugin);
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
  const bool ok = invoke_transition(plugin, kCustomImageFade, nullptr);
  s_transition_rewrite_bypass = old_bypass;
  return ok;
}

} // namespace

bool Resolve() {
  if (!Root_Domain || !mono_class_from_name ||
      !mono_class_get_method_from_name || !mono_class_get_field_from_name ||
      !mono_field_get_value)
    return false;
  if (!s_image)
    s_image = open_loaded_image();
  if (!s_image)
    return false;
  if (!s_plugin_class) {
    s_plugin_class = mono_class_from_name(s_image, kBgLayerNamespace,
                                          "BGLayerPlugin");
    s_get_instance = MethodOf(s_plugin_class, "get_Instance", 0);
    s_set_light = MethodOf(s_plugin_class, "SetThemedLightColor", 1);
    s_set_basemat =
        MethodOf(s_plugin_class, "SetThemedBackgroundBasematColor", 1);
    s_set_global_animation =
        MethodOf(s_plugin_class, "SetGlobalBackgroundAnimation", 1);
    s_set_background_transition =
        MethodOf(s_plugin_class, "SetBackgroundTransition", 1);
    s_show_wave = MethodOf(s_plugin_class, "ShowWave", 1);
    s_transition_param_class = mono_class_from_name(
        s_image, kBgLayerNamespace, "BackgroundTransitionParam");
    MonoClass *background_layer_class =
        mono_class_from_name(s_image, kBgLayerNamespace, "BackgroundLayer");
    s_get_show_wave = MethodOf(background_layer_class, "get_ShowWave", 0);
    MonoClass *image_mapper_class =
        mono_class_from_name(s_image, kBgLayerNamespace, "ImageMapManager");
    s_layer_get_image_mapper =
        MethodOf(background_layer_class, "get_ImageMapper", 0);
    s_cancel_loading_image =
        MethodOf(image_mapper_class, "CancelLoadingImage", 0);
    s_transition_set_type =
        MethodOf(s_transition_param_class, "set_TransitionType", 1);
    s_transition_set_image_uri =
        MethodOf(s_transition_param_class, "set_NextImageUri", 1);
    s_transition_set_degree =
        MethodOf(s_transition_param_class, "set_Degree", 1);
    if (s_set_basemat && mono_compile_method)
      s_set_basemat_thunk =
          reinterpret_cast<BasematThunk>(mono_compile_method(s_set_basemat));
    s_layer_field =
        s_plugin_class
            ? mono_class_get_field_from_name(s_plugin_class, "m_layer")
            : nullptr;
    install_transition_hook();
  }
  if (!s_get_instance || !s_set_light || !s_set_basemat_thunk || !s_layer_field)
    return false;

  MonoObject *exception = nullptr;
  MonoObject *plugin = current_plugin(&exception);
  MonoObject *layer = layer_of(plugin);
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
  const bool ok = invoke_setter(s_set_global_animation, plugin, &state,
                                "SetGlobalBackgroundAnimation");
  if (ok)
    LOG_INFO("[ANITHEME:PARTICLE] native mode=%s state=%d",
             ParticleModeName(mode), state);
  return ok;
}

bool ShowStockWave() {
  if (!Resolve() || !s_show_wave || !mono_runtime_invoke ||
      !mono_object_unbox)
    return false;
  if (s_native_wave_applied && s_native_wave_layer == s_layer_identity)
    return true;

  MonoObject *plugin = current_plugin();
  MonoObject *layer = layer_of(plugin);
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
  if (!invoke_setter(s_show_wave, plugin, &enabled, "ShowWave"))
    return false;
  s_original_show_wave = original;
  s_native_wave_layer = s_layer_identity;
  s_native_wave_applied = true;
  LOG_INFO("[ANITHEME:WAVE] stock wave shown layer=%p original_show=%d",
           reinterpret_cast<void *>(s_layer_identity), original ? 1 : 0);
  return true;
}

void RestoreStockWave() {
  if (s_native_wave_applied && s_native_wave_layer == s_layer_identity &&
      Resolve() && s_show_wave) {
    MonoObject *plugin = current_plugin();
    bool original = s_original_show_wave;
    if (plugin)
      (void)invoke_setter(s_show_wave, plugin, &original, "ShowWave restore");
  }
  s_native_wave_applied = false;
  s_native_wave_layer = 0;
  DisableImageOverride();
}

bool ApplyImage(const char *path, ImageTransition transition) {
  struct stat info{};
  if (!path || !*path || stat(path, &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_size <= 0) {
    LOG_ERROR("[ANITHEME:IMAGE] unreadable image path='%s'", path ? path : "");
    return true;
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
  if (enabled && !s_image_override_enabled && !clear_native_image(false))
    return false;
  s_force_home_background_enabled = enabled;
  if (!enabled)
    s_suppressed_home_transition_count = 0;
  if (changed)
    LOG_INFO("[ANITHEME:IMAGE] force ShellUI background on Home enabled=%d",
             enabled ? 1 : 0);
  return true;
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
