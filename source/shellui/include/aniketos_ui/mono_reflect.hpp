/* Looking at managed objects by name: the handful of reflection calls every
 * feature that reaches into PUI or ShellUI needs. */
#pragma once

#include "external_symbols.hpp"

#include <cstdint>

namespace AniketosUI::Reflect {

inline MonoObject *Target(uint32_t handle) {
  return handle && mono_gchandle_get_target ? mono_gchandle_get_target(handle)
                                            : nullptr;
}

inline void Release(uint32_t &handle) {
  if (handle && mono_gchandle_free)
    mono_gchandle_free(handle);
  handle = 0;
}

inline void Hold(uint32_t &handle, MonoObject *object, bool strong = false) {
  Release(handle);
  if (!object)
    return;
  if (strong)
    handle = mono_gchandle_new(object, 0);
  else if (mono_gchandle_new_weakref)
    handle = mono_gchandle_new_weakref(object, 0);
}

/* On `klass` itself, never a parent: a firmware that moved the method loses the
 * feature, and nothing of the same name and arity gets called in its place. */
inline MonoMethod *DeclaredMethod(MonoClass *klass, const char *name,
                                  int parameters) {
  return klass ? mono_class_get_method_from_name(klass, name, parameters)
               : nullptr;
}
MonoMethod *MethodOf(MonoClass *klass, const char *name, int parameters);
MonoMethod *MethodOf(MonoObject *object, const char *name, int parameters);
MonoClassField *FindField(MonoObject *object, const char *name);
MonoObject *FieldValue(MonoObject *object, const char *name);
MonoClass *NestedClass(MonoClass *outer, const char *name);
/* The value of an enum member, by its name. */
bool EnumValue(MonoClass *type, const char *member, int32_t *value);
/* An assembly already loaded, by its image name; the system may load it late. */
MonoImage *LoadedImage(const char *name);
/* True when the method was found and did not throw. */
bool Call(MonoObject *object, const char *name, int parameters,
          void **arguments = nullptr, MonoObject **result = nullptr);
/* *out stays as it was on failure, so a caller's default survives. */
bool FloatProperty(MonoObject *object, const char *getter, float *out);
/* The same, for a caller with no default: 0 on failure. */
float FloatProperty(MonoObject *object, const char *getter);

} // namespace AniketosUI::Reflect
