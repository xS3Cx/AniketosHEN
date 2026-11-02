#include "hooked_funcs.hpp"

MonoObject *CreateUIColorBoxed(float r, float g, float b, float a) {
  MonoClass *ui_color =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UIColor");
  if (!ui_color)
    return nullptr;

  // Keep the boxed value available to callers that need a GC handle. A
  // pointer returned by mono_object_unbox() is only the value payload, not
  // a MonoObject and must never be passed to mono_gchandle_new().
  MonoObject *boxed = New_Object(ui_color);
  if (!boxed)
    return nullptr;
  MonoObject *value = static_cast<MonoObject *>(mono_object_unbox(boxed));
  if (!value)
    return nullptr;

  Invoke<void>(pui_img, ui_color, value, ".ctor", r, g, b, a);
  return boxed;
}
