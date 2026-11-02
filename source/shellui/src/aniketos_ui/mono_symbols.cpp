#include "aniketos_ui/mono_symbols.hpp"

#include "defs.h"
#include "ps5/kernel.h"

int libmono_handle = 0;
extern int g_libmono_handle __attribute__((alias("libmono_handle"))); // renames.tsv

uint32_t (*mono_gchandle_new_weakref)(MonoObject *obj,
                                     int track_resurrection) = nullptr;
MonoObject *(*mono_gchandle_get_target)(uint32_t gchandle) = nullptr;
MonoMethod* (*mono_class_get_methods)(MonoClass* klass, void** iter) = nullptr;
const char* (*mono_class_get_name)(MonoClass* klass) = nullptr;
const char* (*mono_class_get_namespace)(MonoClass* klass) = nullptr;
const char* (*mono_method_get_name)(MonoMethod* method) = nullptr;
uint32_t (*mono_method_get_flags)(MonoMethod* method, uint32_t* iflags) = nullptr;
MonoMethodSignature* (*mono_method_signature)(MonoMethod* method) = nullptr;
uint32_t (*mono_signature_get_param_count)(MonoMethodSignature* sig) = nullptr;
MonoType* (*mono_signature_get_params)(MonoMethodSignature* sig, void** iter) = nullptr;
MonoType* (*mono_signature_get_return_type)(MonoMethodSignature* sig) = nullptr;
char* (*mono_type_get_name)(MonoType* type) = nullptr;
MonoClassField* (*mono_class_get_fields)(MonoClass* klass, void** iter) = nullptr;
const char* (*mono_field_get_name)(MonoClassField* field) = nullptr;
MonoType* (*mono_field_get_type)(MonoClassField* field) = nullptr;
int (*mono_field_get_offset)(MonoClassField* field) = nullptr;
uint32_t (*mono_field_get_flags)(MonoClassField* field) = nullptr;
void (*mono_field_static_get_value)(MonoVTable* vt, MonoClassField* field,
                                    void* value) = nullptr;
MonoObject* (*mono_object_clone)(MonoObject* obj) = nullptr;
MonoClass* (*mono_class_get_parent)(MonoClass* klass) = nullptr;
void (*mono_field_set_value)(MonoObject* obj, MonoClassField* field,
                             void* value) = nullptr;

void *libmono_symbol(const char *name) {
  return reinterpret_cast<void *>(kernel_dynlib_dlsym(-1, libmono_handle, name));
}

void ResolveReflectionSymbols(int libmono) {
  libmono_handle = libmono;

  KERNEL_DLSYM(libmono, mono_gchandle_new_weakref);
  KERNEL_DLSYM(libmono, mono_gchandle_get_target);
  KERNEL_DLSYM(libmono, mono_class_get_methods);
  KERNEL_DLSYM(libmono, mono_class_get_name);
  KERNEL_DLSYM(libmono, mono_class_get_namespace);
  KERNEL_DLSYM(libmono, mono_method_get_name);
  KERNEL_DLSYM(libmono, mono_method_get_flags);
  KERNEL_DLSYM(libmono, mono_method_signature);
  KERNEL_DLSYM(libmono, mono_signature_get_param_count);
  KERNEL_DLSYM(libmono, mono_signature_get_params);
  KERNEL_DLSYM(libmono, mono_signature_get_return_type);
  KERNEL_DLSYM(libmono, mono_type_get_name);
  KERNEL_DLSYM(libmono, mono_class_get_fields);
  KERNEL_DLSYM(libmono, mono_field_get_name);
  KERNEL_DLSYM(libmono, mono_field_get_type);
  KERNEL_DLSYM(libmono, mono_field_get_offset);
  KERNEL_DLSYM(libmono, mono_field_get_flags);
  KERNEL_DLSYM(libmono, mono_field_static_get_value);
  KERNEL_DLSYM(libmono, mono_object_clone);
  KERNEL_DLSYM(libmono, mono_class_get_parent);
  KERNEL_DLSYM(libmono, mono_field_set_value);
}
