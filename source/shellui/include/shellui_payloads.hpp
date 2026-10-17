/* The payloads in the payloads folders, and starting or stopping one: shared
 * by the Toolbox's Payloads page and the HEN button's menu. */
#pragma once

#include "shellui_types.hpp"

#include <vector>

/* Every payload in the payloads folders (internal storage, then USB), in
   load order (toolbox_xml.cpp). */
std::vector<PayloadEntry> shellui_scan_payloads();

/* Starts the payload through util; a notification says how it went
   (onpress_payloads.cpp). */
void shellui_payload_start(const PayloadEntry &entry);

/* Stops the payload if it runs. False: it was not running. */
bool shellui_payload_stop(const PayloadEntry &entry);
