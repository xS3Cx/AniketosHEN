#pragma once

#include "external_symbols.hpp"
#include "aniketos_ui/menu_style.hpp"

#include <string>

namespace AniketosUI::Pui {

using RetainCallback = bool (*)(MonoObject *object, void *context);

struct Runtime {
  MonoImage *image = nullptr;
  MonoClass *widget_base = nullptr;
  MonoClass *widget = nullptr;
  MonoClass *panel = nullptr;
  MonoClass *label = nullptr;
  MonoClass *image_box = nullptr;
  MonoObject *font = nullptr;
  RetainCallback retain = nullptr;
  void *retain_context = nullptr;
};

bool Resolve(Runtime &runtime, MonoImage *image, RetainCallback retain,
             void *retain_context);
MonoString *String(const std::string &value);
MonoObject *Find(const Runtime &runtime, MonoObject *root, const char *name);
void RemoveNamed(const Runtime &runtime, MonoObject *root, const char *name);
void Remove(const Runtime &runtime, MonoObject *object);
/* True for a widget created through CreatePanel/CreateLabel here. The theme
 * hooks on PUI's own widgets use it to leave ours alone. */
bool IsOwn(MonoObject *object);
void SetColor(MonoClass *klass, MonoObject *object, const char *property,
              Color color);
bool AppendChild(const Runtime &runtime, MonoObject *parent,
                 MonoObject *child);
MonoObject *CreatePanel(const Runtime &runtime, MonoObject *parent,
                        const char *name);
MonoObject *CreateLabel(const Runtime &runtime, MonoObject *parent,
                        const char *name, int horizontal, int lines = 1);
MonoObject *CreateSystemImage(const Runtime &runtime, MonoObject *parent,
                              const char *name, const char *uri);
void LoadSystemImage(const Runtime &runtime, MonoObject *image,
                     const char *uri);
void PanelGeometry(const Runtime &runtime, MonoObject *panel, float x, float y,
                   float width, float height);
/* Move a panel vertically (and optionally resize it) keeping the write-skipping
 * shadow in step. Use these instead of setting "Y"/"Height" directly. */
void PanelY(const Runtime &runtime, MonoObject *panel, float y);
void PanelYHeight(const Runtime &runtime, MonoObject *panel, float y,
                  float height);
void PanelColor(const Runtime &runtime, MonoObject *panel, Color color);
void PanelHide(const Runtime &runtime, MonoObject *panel);
void LabelGeometry(const Runtime &runtime, MonoObject *label, float x, float y,
                   float width, float height);
void ImageGeometry(const Runtime &runtime, MonoObject *image, float x, float y,
                   float width, float height, bool visible = true);
void LabelText(const Runtime &runtime, MonoObject *label,
               const std::string &text);
void LabelFont(const Runtime &runtime, MonoObject *label);

/* Panel geometry, colours and label text are only written when they differ
 * from the last value written through this module. Drop that memory for one
 * widget (it was created or released) or for all of them (widgets released,
 * or a periodic re-assert of every value). */
void Forget(MonoObject *object);
void ForgetAll();

} // namespace AniketosUI::Pui
