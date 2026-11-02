/* AniketosHEN's own part of config.ini, for settings.cpp's parser and
   serializer: their calls sit where these keys belong in the file. The keys
   are the persisted format. */

void apply_aniketos_keys(IniParser *parser, Settings *out) {
  out->reports_send =
      parse_bool(ini_get(parser, "reports.send"), out->reports_send);
  out->cheats_visual_menu = parse_bool(ini_get(parser, "cheats.visual_menu"),
                                       out->cheats_visual_menu);
  if (const char *render = ini_get(parser, "cheats.visual_menu_render"))
    out->cheats_visual_render = std::strcmp(render, "pui3") == 0 ? 1 : 0;
}

/* [shell_menu]: keys are the field names. A missing or unparsable value
   keeps the default; range checks belong to the code that uses a value. */
void apply_shell_menu_section(IniParser *parser, Settings *out) {
#define ONION_SHELL_SETTING_BOOL(name, def)                                    \
  out->name = parse_bool(ini_get(parser, "shell_menu." #name), out->name);
#define ONION_SHELL_SETTING_INT(name, def)                                     \
  if (const char *v = ini_get(parser, "shell_menu." #name)) {                  \
    char *end = nullptr;                                                       \
    const long parsed = std::strtol(v, &end, 10);                              \
    if (end != v && parsed >= -1000000 && parsed <= 1000000)                   \
      out->name = static_cast<int>(parsed);                                    \
  }
#define ONION_SHELL_SETTING_FLOAT(name, def)                                   \
  if (const char *v = ini_get(parser, "shell_menu." #name)) {                  \
    char *end = nullptr;                                                       \
    const float parsed = std::strtof(v, &end);                                 \
    if (end != v && parsed == parsed && parsed > -1.0e6f && parsed < 1.0e6f)   \
      out->name = parsed;                                                      \
  }
#define ONION_SHELL_SETTING_STRING(name, def)                                  \
  if (const char *v = ini_get(parser, "shell_menu." #name))                    \
    out->name = v;
#include <onion/shell_menu_settings.def>
#undef ONION_SHELL_SETTING_BOOL
#undef ONION_SHELL_SETTING_INT
#undef ONION_SHELL_SETTING_FLOAT
#undef ONION_SHELL_SETTING_STRING
}

void append_reports_section(std::string &b, const Settings &in) {
  b += "[reports]\n";
  b += "# send shares error reports (failed starts, crashes, failed data imports)\n";
  b += "# with the " ONION_DIST_NAME " maintainers. Console IDs, serial numbers,\n";
  b += "# IP and MAC addresses, account IDs and user names are removed first.\n";
  b += "# Reports are kept in " ONION_DIST_DATA_ROOT "/reports either way.\n";
  b += "# Available values: true, false\n";
  b += "send=" + bool_text(in.reports_send) + "\n";
  b += "\n";
}

void append_cheats_visual_keys(std::string &b, const Settings &in) {
  b += "# visual_menu opens the running game's cheats in a menu over the game\n";
  b += "# with L1+L3+R3. Available values: true, false\n";
  b += "visual_menu=" + bool_text(in.cheats_visual_menu) + "\n";
  b += "# visual_menu_render: pui2 draws the classic mod menu, pui3 shows the\n";
  b += "# system's own menu card. Available values: pui2, pui3\n";
  b += std::string("visual_menu_render=") +
       (in.cheats_visual_render == 1 ? "pui3" : "pui2") + "\n";
}

void append_shell_menu_section(std::string &b, const Settings &in) {
  b += "\n";
  b += "[shell_menu]\n";
  b += "# Shell Menu (L1+R3) and shell appearance. Managed from the menu itself;\n";
  b += "# keys are internal field names and may change between versions.\n";
  char number[48];
#define ONION_SHELL_SETTING_BOOL(name, def)                                    \
  b += #name "=" + bool_text(in.name) + "\n";
#define ONION_SHELL_SETTING_INT(name, def)                                     \
  b += #name "=" + std::to_string(in.name) + "\n";
#define ONION_SHELL_SETTING_FLOAT(name, def)                                   \
  std::snprintf(number, sizeof(number), "%.6g", static_cast<double>(in.name)); \
  b += std::string(#name "=") + number + "\n";
#define ONION_SHELL_SETTING_STRING(name, def)                                  \
  b += #name "=" + in.name + "\n";
#include <onion/shell_menu_settings.def>
#undef ONION_SHELL_SETTING_BOOL
#undef ONION_SHELL_SETTING_INT
#undef ONION_SHELL_SETTING_FLOAT
#undef ONION_SHELL_SETTING_STRING
}
