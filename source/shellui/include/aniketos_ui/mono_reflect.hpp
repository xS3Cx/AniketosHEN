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

/* Direct calls, for per-frame code.
 *
 * mono_runtime_invoke boxes every value it returns, walks the arguments and
 * sets up an exception frame: the entry of the compiled method called like a
 * C function (the instance first, then the arguments, by value) costs a
 * fraction of that and allocates nothing. The address is kept per method, so
 * a call after the first is a cache probe and the call itself.
 *
 * No managed exception can be caught through it, so only methods that cannot
 * throw belong here: property getters, and setters of plain values (X, Y,
 * Opacity, Visible, ...). Anything that may throw keeps Call. The method must
 * be looked up on the object's own class (MethodOf), never on a base class
 * cast, so an override is what runs. */
void *Compiled(MonoMethod *method);
void *Thunk(MonoClass *klass, const char *name, int parameters);
void *Thunk(MonoObject *object, const char *name, int parameters);

/* A no-argument getter; false (and *out untouched) when it is not there. */
template <typename Result>
bool Read(MonoObject *object, const char *getter, Result *out) {
  Result (*method)(MonoObject *) =
      reinterpret_cast<decltype(method)>(Thunk(object, getter, 0));
  if (!method)
    return false;
  *out = method(object);
  return true;
}

/* A one-argument setter of a value or an object; false when not there. */
template <typename Param>
bool Write(MonoObject *object, const char *setter, Param value) {
  void (*method)(MonoObject *, Param) =
      reinterpret_cast<decltype(method)>(Thunk(object, setter, 1));
  if (!method)
    return false;
  method(object, value);
  return true;
}

} // namespace AniketosUI::Reflect
