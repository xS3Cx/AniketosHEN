#pragma once

#include <string>

struct MonoObject;
struct MonoString;

namespace AniketosTheme::SpaceSwitcherLabels {

/* The text a Home Games or Media tab label should show: `text` itself unless
   the label is one of those tabs and still has no glyph. */
MonoString *Decorate(MonoObject *label, MonoString *text);
/* True for a stock tab caption that Decorate would dress. */
bool IsTabLabel(const std::string &text);
/* True for the Media caption, with or without our glyph. */
bool IsMediaTabLabel(const std::string &text);
/* Grows the shared focus frame to fit tab labels widened by their glyphs. */
void UpdateFocusBounds(MonoObject *label, const std::string &text);

} // namespace AniketosTheme::SpaceSwitcherLabels
