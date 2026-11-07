/* Copyright (C) 2026 AniketosHEN */

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

/* The plugin folders: <root>/<TITLEID>/ holds the plugins of one game or app,
 * <root>/all/ the plugins of every game. A folder's .sprx/.prx files are loaded
 * into the game, its .elf files are started as their own process.
 *
 *   <root>/<TITLEID>/config.ini   written by the Toolbox:
 *     load_on_startup=true|false  (missing: true)
 *     disabled=menu.sprx,all/fps.elf
 *   <root>/<TITLEID>/status.ini   written by the daemon at each game start:
 *     <plugin name>=loaded|started|running|<error>
 *
 * The daemon reads them under /data, ShellUI under /user/data (same files). */
namespace onion::pluginloader {

inline constexpr std::string_view kAllFolder = "all";

enum class Kind { Sprx, Prx, Elf };
const char *kind_name(Kind kind) noexcept; /* "SPRX", "PRX", "ELF" */

/* A file name the loader takes: .sprx, .prx or .elf (any case), and nothing
   that would break a config line (control characters, '=' or ','). */
bool plugin_kind(std::string_view file_name, Kind *out);
/* A game or app folder name: 9 letters or digits (PPSA01234, CUSA01234). */
bool valid_title(std::string_view title) noexcept;

struct Plugin {
  std::string name; /* file name, "all/<file>" for the all folder */
  std::string path;
  Kind kind = Kind::Sprx;
  bool enabled = true; /* not in the title's disabled list */
  bool all_games() const { return name.starts_with("all/"); }
};

struct Config {
  bool load_on_startup = true;
  std::vector<std::string> disabled;
};

Config parse_config(std::string_view text);
std::string serialize_config(const Config &config);
Config load_config(const std::string &root, std::string_view title);
/* Creates the title's folder when needed; false when it cannot be written. */
bool save_config(const std::string &root, std::string_view title,
                 const Config &config);

/* The title's own plugins, then those of all/, each sorted by name. */
std::vector<Plugin> list(const std::string &root, std::string_view title,
                         const Config &config);

using Status = std::map<std::string, std::string>;
Status load_status(const std::string &root, std::string_view title);
bool save_status(const std::string &root, std::string_view title,
                 const Status &status);

} // namespace onion::pluginloader
