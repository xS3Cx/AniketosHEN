/* What the tile focus hooks and the space switcher labels share. */
#pragma once

#include "aniketos_ui/mono_reflect.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace AniketosTheme::HomeTiles {

using StringGetter = MonoString *(*)(MonoObject *);

/* Resolved by Install, whatever became of its hooks: the labels work without
   them. Null on a build that lacks them. */
extern MonoClass *s_pui_widget_class;
extern MonoClass *s_focus_settings_class;
extern StringGetter s_get_test_id;

inline uint32_t &handle_of(uint32_t &entry) { return entry; }
template <typename Entry> uint32_t &handle_of(Entry &entry) {
  return entry.handle;
}

/* For a list of weak handles, or of structs holding one: visits the live
   objects until visit(entry, object) returns true, and gives back that entry.
   Handles of collected objects are freed on the way. */
template <typename Entry, typename Visit>
Entry *visit_live(std::vector<Entry> &entries, Visit visit) {
  for (auto it = entries.begin(); it != entries.end();) {
    uint32_t &handle = handle_of(*it);
    MonoObject *object = AniketosUI::Reflect::Target(handle);
    if (!object) {
      AniketosUI::Reflect::Release(handle);
      it = entries.erase(it);
      continue;
    }
    if (visit(*it, object))
      return &*it;
    ++it;
  }
  return nullptr;
}

template <typename Entry>
Entry *find_live(std::vector<Entry> &entries, MonoObject *object) {
  return visit_live(entries, [object](Entry &, MonoObject *live) {
    return live == object;
  });
}

/* 0 when there is no object, or no weak handles in this runtime. */
inline uint32_t weak_handle(MonoObject *object) {
  uint32_t handle = 0;
  AniketosUI::Reflect::Hold(handle, object);
  return handle;
}

} // namespace AniketosTheme::HomeTiles

namespace AniketosTheme::SpaceSwitcherLabels {

/* Fed by the focus layer hook, with every layer's test id. */
void RememberFocusLayer(MonoObject *widget, const std::string &test_id);
void RememberFocusPadding(MonoObject *settings, const std::string &test_id);

} // namespace AniketosTheme::SpaceSwitcherLabels
