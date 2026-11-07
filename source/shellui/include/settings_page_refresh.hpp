#pragma once

#include "ps5_settings_ui.hpp"
#include "monodef.h"

#include <functional>

namespace onion::shellui::settings {

using PageSource = std::function<bool(ps5ui::Node &)>;
// Called while serving initial XML. The source must not navigate or publish.
std::string publish(const ps5ui::Node &model, PageSource source);
void bind(MonoObject *page);
// focus: element to focus afterwards; empty keeps the focused element.
void refresh(MonoObject *page, const std::string &focus = {});
void release(MonoObject *page);
// Id of a page (its SettingList id) and of its focused element; empty when
// unknown.
std::string page_id(MonoObject *page);
std::string focused(MonoObject *page);

} // namespace onion::shellui::settings
