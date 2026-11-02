/* Cached managed property access for per-frame UI code.
 *
 * Get_Property / Set_Property (hooked_funcs.hpp) resolve the property by name,
 * fetch its accessor and ask the runtime for the compiled address on every
 * call. That is fine for a button press and far too slow for a menu that
 * updates hundreds of widgets per frame. These helpers do the same work once
 * per (class, property) and then call the cached thunk directly.
 *
 * Only pass classes from assemblies that stay loaded for the life of
 * SceShellUI (Sce.PlayStation.PUI): the cache is keyed by the MonoClass
 * pointer and is never invalidated.
 */
#pragma once

#include "external_symbols.hpp"

namespace AniketosUI::MonoPropertyCache {

enum class Kind : int { GetThunk, SetThunk, SetMethod };

/** Compiled getter/setter address, or the setter MonoMethod. Null if absent. */
void *Accessor(MonoClass *klass, const char *property, Kind kind);

template <typename Result>
Result Get(MonoClass *klass, MonoObject *instance, const char *property) {
  void *thunk = Accessor(klass, property, Kind::GetThunk);
  if (!thunk)
    return Result{};
  if (instance) {
    Result (*method)(MonoObject *) = decltype(method)(thunk);
    return method(instance);
  }
  Result (*method)() = decltype(method)(thunk);
  return method();
}

template <typename Param>
void Set(MonoClass *klass, MonoObject *instance, const char *property,
         Param value) {
  if (!instance)
    return;
  void *thunk = Accessor(klass, property, Kind::SetThunk);
  if (!thunk)
    return;
  void (*method)(MonoObject *, Param) = decltype(method)(thunk);
  method(instance, value);
}

template <typename Param>
void SetInvoke(MonoClass *klass, MonoObject *instance, const char *property,
               Param value) {
  if (!instance)
    return;
  MonoMethod *setter =
      static_cast<MonoMethod *>(Accessor(klass, property, Kind::SetMethod));
  if (setter)
    mono_runtime_invoke(setter, instance, (void **)&value, 0);
}

/**
 * A UIColor value for passing to SetInvoke. Returns the same scratch storage
 * on every call (UI thread only): the setter copies the struct, so nothing is
 * allocated per colour change. Falls back to a fresh object if the scratch
 * value could not be created.
 */
MonoObject *ScratchColor(float r, float g, float b, float a);

} // namespace AniketosUI::MonoPropertyCache

namespace AniketosUI { namespace MonoFast = MonoPropertyCache; } // renames.tsv
