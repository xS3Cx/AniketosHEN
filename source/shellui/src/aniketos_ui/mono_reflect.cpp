#include "aniketos_ui/mono_reflect.hpp"

#include "aniketos_ui/mono_property_cache.hpp"

#include <cstring>

namespace AniketosUI::Reflect {

MonoMethod *MethodOf(MonoClass *klass, const char *name, int parameters) {
  /* Kept per class: the runtime compares the name with every method of every
     class up the chain, and per-frame code asks the same questions again. */
  return MonoPropertyCache::Method(klass, name, parameters);
}

MonoMethod *MethodOf(MonoObject *object, const char *name, int parameters) {
  return object ? MethodOf(mono_object_get_class(object), name, parameters)
                : nullptr;
}

MonoClassField *FindField(MonoObject *object, const char *name) {
  if (!object)
    return nullptr;
  for (MonoClass *klass = mono_object_get_class(object); klass;
       klass = mono_class_get_parent(klass))
    if (MonoClassField *field = mono_class_get_field_from_name(klass, name))
      return field;
  return nullptr;
}

MonoObject *FieldValue(MonoObject *object, const char *name) {
  static MonoObject *(*value_object)(MonoDomain *, MonoClassField *,
                                     MonoObject *) = nullptr;
  if (!value_object)
    value_object = reinterpret_cast<decltype(value_object)>(
        libmono_symbol("mono_field_get_value_object"));
  MonoClassField *field = value_object ? FindField(object, name) : nullptr;
  return field ? value_object(mono_domain_get(), field, object) : nullptr;
}

MonoClass *NestedClass(MonoClass *outer, const char *name) {
  static MonoClass *(*nested_types)(MonoClass *, void **) = nullptr;
  if (!nested_types)
    nested_types = reinterpret_cast<decltype(nested_types)>(
        libmono_symbol("mono_class_get_nested_types"));
  if (!outer || !nested_types)
    return nullptr;
  void *iter = nullptr;
  while (MonoClass *inner = nested_types(outer, &iter))
    if (!std::strcmp(mono_class_get_name(inner), name))
      return inner;
  return nullptr;
}

bool EnumValue(MonoClass *type, const char *member, int32_t *value) {
  MonoClassField *field =
      type ? mono_class_get_field_from_name(type, member) : nullptr;
  if (!field)
    return false;
  mono_field_static_get_value(mono_class_vtable(mono_domain_get(), type), field,
                              value);
  return true;
}

MonoImage *LoadedImage(const char *name) {
  static MonoImage *(*image_loaded)(const char *) = nullptr;
  if (!image_loaded)
    image_loaded = reinterpret_cast<decltype(image_loaded)>(
        libmono_symbol("mono_image_loaded"));
  return image_loaded ? image_loaded(name) : nullptr;
}

bool Call(MonoObject *object, const char *name, int parameters,
          void **arguments, MonoObject **result) {
  MonoMethod *method = MethodOf(object, name, parameters);
  if (!method)
    return false;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(method, object, arguments, &exception);
  if (result)
    *result = value;
  return !exception;
}

bool FloatProperty(MonoObject *object, const char *getter, float *out) {
  /* Direct first: a float getter cannot throw. Invoke if it has no entry. */
  if (object && Read(object, getter, out))
    return true;
  MonoObject *value = nullptr;
  if (!Call(object, getter, 0, nullptr, &value) || !value)
    return false;
  *out = *static_cast<float *>(mono_object_unbox(value));
  return true;
}

void *Compiled(MonoMethod *method) {
  return method ? MonoPropertyCache::CompiledMethod(method) : nullptr;
}

void *Thunk(MonoClass *klass, const char *name, int parameters) {
  return Compiled(MethodOf(klass, name, parameters));
}

void *Thunk(MonoObject *object, const char *name, int parameters) {
  return object ? Thunk(mono_object_get_class(object), name, parameters)
                : nullptr;
}

float FloatProperty(MonoObject *object, const char *getter) {
  float value = 0.0f;
  FloatProperty(object, getter, &value);
  return value;
}

} // namespace AniketosUI::Reflect
