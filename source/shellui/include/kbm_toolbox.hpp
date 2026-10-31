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

#include <onion/kbm_profile.hpp>

#include <string>
#include <string_view>

namespace kbm_toolbox {

/* ---- used by the Toolbox hooks ---------------------------------------- */
void generate_xml(std::string &xml, const std::string &resource);
/* Values for SettingPage.OnCreating (it ignores the XML values). */
bool control_value(const std::string &id, std::string &out);
/* UI thread, every frame: key capture and deferred page refreshes. */
void Tick();

/* ---- shared by kbm_toolbox_xml.cpp and onpress_kbm.cpp ---------------- */
struct Path {
  std::string t; /* title id, "default", or "all" for the main page */
  int l = -1, b = -1, o = -1, m = -1, s = -1;
  std::string word; /* resource: page name; element id: field */
};

/* "id_kbm-<path>-<field>" → path + field. False for anything else,
   including list items ("<list id>.<value>"). */
bool parse_id(std::string_view id, Path *out);
std::string resource(const Path &p, const char *page = nullptr);

bool get_value(const Path &p, std::string &out);
/* Changes the profile being edited (or the global settings for "all");
   false when the path is gone or the value is refused. */
bool set_value(const Path &p, const std::string &value);
/* Saves the edited profile and asks the daemon to apply it. */
bool save(const Path &p);

/* Structure edits on the profile being edited; each one saves. */
void create_profile(const Path &p, bool from_template);
void delete_profile(const Path &p);
/* what: layer, bind, macro, step, or an output kind (button, axis, trigger,
   layer, macro, scale, suspend, block). *created: the new item's path. */
bool add_item(const Path &p, const std::string &what, Path *created);
/* Deletes what the path points at; returns how many pages to close (0:
   nothing deleted). Pages under it show "deleted" until another opens. */
int delete_item(const Path &p);
bool clear_modifiers(const Path &p);
enum class SetInput { Ok, Gone, Reserved, Failed };
/* A binding's input and modifiers, or the pause key for t == "all". */
SetInput set_input(const Path &p, onion::kbm::Input input,
                   const onion::kbm::Input *modifiers);

std::string input_name(onion::kbm::Input input);
void tell_daemon();

} // namespace kbm_toolbox
