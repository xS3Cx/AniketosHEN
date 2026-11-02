/* Copyright (C) 2026 AniketosHEN
 *
 * Toolbox "Keyboard and Mouse" pages: global switch, per-game profiles,
 * layers, bindings, outputs with analog ramps, mouse-to-stick settings and
 * macros. Profiles are edited in memory and saved through the onion_kbm
 * store after every change; the daemon is then told to reload them.
 *
 * Resources (under Settings.Plugins): "kbm.xml", then "kbm-<path>.xml" where
 * path is "<T>[-l<n>][-b<n>][-o<n>][-m<n>][-s<n>][-<page>]" and T is a title
 * id or "default". Element ids are "id_kbm-<path>-<field>"; navigation links
 * use "id_kbmnav-..." so the press handler never sees them.
 */
#pragma once

#include <string>

namespace kbm_toolbox {

void generate_xml(std::string &xml, const std::string &resource);
/* Values for SettingPage.OnCreating (it ignores the XML values). */
bool control_value(const std::string &id, std::string &out);
/* UI thread, every frame: key capture and deferred page refreshes. */
void Tick();

} // namespace kbm_toolbox
