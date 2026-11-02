#pragma once

#include "aniketos_theme/tiles/space_switcher_labels.hpp" // renames.tsv

struct MonoImage;

namespace AniketosTheme::HomeTiles {

void Install(MonoImage *react_pui, MonoImage *pui);
void Tick();

inline MonoString *DecorateSpaceSwitcherLabel(MonoObject *label, MonoString *text) { return SpaceSwitcherLabels::Decorate(label, text); } // renames.tsv
inline bool IsSpaceSwitcherTabLabel(const std::string &text) { return SpaceSwitcherLabels::IsTabLabel(text); } // renames.tsv
inline bool IsSpaceSwitcherMediaTabLabel(const std::string &text) { return SpaceSwitcherLabels::IsMediaTabLabel(text); } // renames.tsv
inline void UpdateSpaceSwitcherFocusBounds(MonoObject *label, const std::string &text) { SpaceSwitcherLabels::UpdateFocusBounds(label, text); } // renames.tsv

} // namespace AniketosTheme::HomeTiles
