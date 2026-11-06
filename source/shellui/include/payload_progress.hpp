#pragma once

#include "monodef.h"

#include <cstdint>
#include <string>
#include <string_view>

void payload_progress_show(uint32_t task_id);
bool payload_progress_open_page(void);
void generate_payload_progress_xml(std::string &xml_buffer);
void payload_progress_attach_panel(std::string_view id, MonoObject *widget);
void payload_progress_bind_page(MonoObject *page);
bool payload_progress_handle_popping(MonoObject *outgoing);
void shellui_poll_payload_progress(void);
