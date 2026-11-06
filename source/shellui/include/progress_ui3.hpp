#pragma once

#include "monodef.h"

#include <cstdint>
#include <string>

namespace progress_ui3 {

struct BoundWidget {
  MonoObject *object = nullptr;
  uint32_t handle = 0;
};

void release_widget(BoundWidget &widget);
bool pin_widget(BoundWidget &bound, MonoObject *object, const char *name);
MonoObject *new_update_progress_panel();
MonoObject *widget_child_at(MonoObject *root, int index);
int widget_children_count(MonoObject *widget);
bool append_panel(MonoObject *widget, MonoObject *panel);
bool set_label_text(MonoObject *object, const std::string &text);
bool set_progress_bar_progress(MonoObject *object, float value);
bool set_progress_bar_status(MonoObject *object, int value);
bool read_progress_bar_progress(MonoObject *object, float &value);

struct Panel {
  BoundWidget host;
  BoundWidget panel;
  BoundWidget title_label;
  BoundWidget phase_label;
  BoundWidget percent_label;
  BoundWidget cancel_label;
  BoundWidget progress_bar;

  bool attach(MonoObject *widget);
  void release();
  bool valid() const { return progress_bar.object != nullptr; }
};

}
