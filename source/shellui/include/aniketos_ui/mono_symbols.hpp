#pragma once

#include "external_symbols.hpp"

/* libmonosgen's module handle, for symbols looked up after start-up. */
extern int libmono_handle;
void *libmono_symbol(const char *name);

/* Stores libmono_handle and resolves the pointers below. None of them is in
   the start-up check, so a firmware without one only loses what uses it. The
   two GC handle lookups are public exports their callers do not null-check. */
void ResolveReflectionSymbols(int libmono);

extern uint32_t (*mono_gchandle_new_weakref)(MonoObject *obj,
                                             int track_resurrection);
extern MonoObject *(*mono_gchandle_get_target)(uint32_t gchandle);
extern MonoMethod* (*mono_class_get_methods)(MonoClass* klass, void** iter);
extern const char* (*mono_class_get_name)(MonoClass* klass);
extern const char* (*mono_class_get_namespace)(MonoClass* klass);
extern const char* (*mono_method_get_name)(MonoMethod* method);
extern uint32_t (*mono_method_get_flags)(MonoMethod* method, uint32_t* iflags);
extern MonoMethodSignature* (*mono_method_signature)(MonoMethod* method);
extern uint32_t (*mono_signature_get_param_count)(MonoMethodSignature* sig);
extern MonoType* (*mono_signature_get_params)(MonoMethodSignature* sig,
                                              void** iter);
extern MonoType* (*mono_signature_get_return_type)(MonoMethodSignature* sig);
extern char* (*mono_type_get_name)(MonoType* type);
extern int (*mono_field_get_offset)(MonoClassField* field);
extern void (*mono_field_static_get_value)(MonoVTable* vt,
                                           MonoClassField* field,
                                           void* value);
extern MonoObject* (*mono_object_clone)(MonoObject* obj);
extern MonoClass* (*mono_class_get_parent)(MonoClass* klass);
extern MonoClassField* (*mono_class_get_fields)(MonoClass* klass, void** iter);
extern const char* (*mono_field_get_name)(MonoClassField* field);
extern MonoType* (*mono_field_get_type)(MonoClassField* field);
extern uint32_t (*mono_field_get_flags)(MonoClassField* field);
struct MonoTableInfo;

extern const MonoTableInfo* (*mono_image_get_table_info)(MonoImage* image, int table_id);
extern int (*mono_table_info_get_rows)(const MonoTableInfo* table);
extern MonoClass* (*mono_class_get)(MonoImage* image, uint32_t type_token);
extern void (*mono_field_set_value)(MonoObject* obj, MonoClassField* field,
                                    void* value);
