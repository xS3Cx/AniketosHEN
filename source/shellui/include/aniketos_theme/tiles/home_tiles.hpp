#pragma once

#include <string>

struct MonoImage;
struct MonoObject;
struct MonoString;

namespace AniketosTheme::HomeTiles {

void Install(MonoImage *react_pui, MonoImage *pui);
void RememberArcBadge(MonoObject *raw_text, const char *title_id,
                      const char *version, bool show_title_id,
                      bool show_version);
void PropagateArcBadge(MonoObject *parent, MonoObject *child);
MonoString *DecorateSpaceSwitcherLabel(MonoObject *label, MonoString *text);
bool IsSpaceSwitcherTabLabel(const std::string &text);
bool IsSpaceSwitcherMediaTabLabel(const std::string &text);
void UpdateSpaceSwitcherFocusBounds(MonoObject *label,
                                    const std::string &text);
void Tick();
void ClearTrackedLayers();

} // namespace AniketosTheme::HomeTiles
