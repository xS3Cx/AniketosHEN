#pragma once

#include "monodef.h"

#include <cstdint>
#include <string>
#include <string_view>

void update_progress_show(std::uint32_t task_id);
void generate_hen_update_progress_xml(std::string &xml_buffer);
void update_progress_attach_panel(std::string_view id, MonoObject *widget);
void update_progress_bind_page(MonoObject *page);
bool update_progress_handle_popping(MonoObject *outgoing);
void shellui_poll_update_progress(void);
