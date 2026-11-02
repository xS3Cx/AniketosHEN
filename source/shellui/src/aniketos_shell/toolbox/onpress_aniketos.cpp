/* Copyright (C) 2026 AniketosHEN — OnPress for our rows of the Toolbox pages. */
#include "onpress.hpp"

/* Read by util before every send, so no restart is needed. */
static OnPressResult id_error_reports(OnPressContext &ctx) {
  const bool enabled = value_as_int(ctx);
  if (enabled == g_settings.reports_send)
    return OnPressResult::EarlyReturn;
  g_settings.reports_send = enabled;
  LOG_INFO("Error reports %s", enabled ? "enabled" : "disabled");
  return OnPressResult::Handled;
}

static OnPressResult id_cheats_visual_menu(OnPressContext &ctx) {
  const bool enabled = ctx.value == "1";
  if (enabled == g_settings.cheats_visual_menu)
    return OnPressResult::EarlyReturn;
  g_settings.cheats_visual_menu = enabled;
  LOG_INFO("Visual cheats menu: %s", enabled ? "Enabled" : "Disabled");
  return OnPressResult::Handled;
}

static OnPressResult id_cheats_visual_render(OnPressContext &ctx) {
  const int render = atoi(ctx.value.c_str()) == 1 ? 1 : 0;
  if (render == g_settings.cheats_visual_render)
    return OnPressResult::EarlyReturn;
  g_settings.cheats_visual_render = render;
  LOG_INFO("Visual cheats menu render: %s", render ? "PUI3" : "PUI2");
  return OnPressResult::Handled;
}

/* The rows of "Find me on": show where that is, nothing to save. */
static OnPressResult find_me(const char *where) {
  notify("%s", where);
  return OnPressResult::EarlyReturn;
}

static const OnPressExactEntry kExact[] = {
    {"id_error_reports", id_error_reports},
    {"id_cheats_visual_menu", id_cheats_visual_menu},
    {"id_cheats_visual_render", id_cheats_visual_render},
    {"id_find_me_x",
     +[](OnPressContext &) { return find_me("X: x.com/Alex0_SH"); }},
    {"id_find_me_youtube",
     +[](OnPressContext &) { return find_me("YouTube: AlexZero"); }},
    {"id_find_me_discord",
     +[](OnPressContext &) { return find_me("Discord: alex0sh"); }},
    {"id_find_me_telegram",
     +[](OnPressContext &) { return find_me("Telegram: t.me/missgoodpussy"); }},
    {"id_find_me_tiktok",
     +[](OnPressContext &) { return find_me("TikTok: tiktok.com/@mealexzero"); }},
};

const OnPressExactEntry *onpress_aniketos_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}
