#include "aniketos_shell/rn_scripts/js_string.hpp"
#include "aniketos_shell/shell_menu/shell_menu.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/theme_config.hpp"
#include "aniketos_theme/clock/clock_text.hpp"
#include "clock_internal.hpp"
#include "toolbox_i18n.hpp"

#include <onion/settings.hpp>

#include <algorithm>
#include <cstdio>

namespace ShellClock {

namespace {

time_t s_last_language_retry = static_cast<time_t>(-1);
int s_language_setting = -1;
bool s_language_ready = false;

std::string two_digits(int value) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%02d", value);
  return buffer;
}

int effective_date_order() {
  if (g_settings.shell_clock_date_order != 0)
    return g_settings.shell_clock_date_order;
  switch (toolbox_i18n::active_lang()) {
  case toolbox_i18n::Lang::En: return 2;
  case toolbox_i18n::Lang::ZhHans:
  case toolbox_i18n::Lang::ZhHant:
  case toolbox_i18n::Lang::Ja:
  case toolbox_i18n::Lang::Ko: return 3;
  default: return 1;
  }
}

const char *date_separator() {
  switch (g_settings.shell_clock_date_separator) {
  case 1: return "/";
  case 2: return ".";
  case 3: return "-";
  case 4: return " ";
  case 5: return " · ";
  default:
    if (g_settings.shell_clock_month_style != 0)
      return " ";
    return effective_date_order() == 2 ? "/" : ".";
  }
}

std::string translated_indexed(const char *group, int index,
                               const char *style) {
  char key[64];
  std::snprintf(key, sizeof(key), "clock.%s.%s.list", group, style);
  const std::string list = toolbox_i18n::tr(key);
  size_t begin = 0;
  for (int current = 0; current < index; ++current) {
    begin = list.find('|', begin);
    if (begin == std::string::npos)
      return {};
    ++begin;
  }
  const size_t end = list.find('|', begin);
  return list.substr(begin, end == std::string::npos ? std::string::npos
                                                     : end - begin);
}

std::string json_color(int r, int g, int b) {
  char text[16];
  std::snprintf(text, sizeof(text), "\"#%02x%02x%02x\"", std::clamp(r, 0, 255),
                std::clamp(g, 0, 255), std::clamp(b, 0, 255));
  return text;
}

/* The moving clock colour of effective_clock_color() (clock_label.cpp) for
   the script to compute itself: m is the global animation (1..4, see
   GlobalTheme::Color) or 0 for the background's Rainbow (EvaluateAnimation,
   its glow as h, s, v with the minimums already applied); the animation's
   own clock in ms is Date.now() - o. null while the colour does not move,
   and while a game or app has the screen, so the script stops its 15 Hz
   redraw there. */
std::string json_animation() {
  if (s_game_focused)
    return "null";
  timespec wall{}, mono{};
  clock_gettime(CLOCK_REALTIME, &wall);
  clock_gettime(CLOCK_MONOTONIC, &mono);
  /* from nanoseconds, so the offset stays the same from one ask to the next */
  int64_t offset_ms =
      (static_cast<int64_t>(wall.tv_sec - mono.tv_sec) * 1000000000 +
       (wall.tv_nsec - mono.tv_nsec)) / 1000000;
  char text[160];
  if (AniketosTheme::GlobalTheme::Animated()) {
    std::snprintf(text, sizeof(text),
                  "{\"m\":%d,\"sp\":%.2f,\"r\":%.4f,\"g\":%.4f,\"b\":%.4f,"
                  "\"o\":%lld}",
                  std::clamp(g_settings.vsh_color_animation, 0, 4),
                  std::clamp(g_settings.vsh_color_animation_speed, 25, 400) /
                      100.0,
                  std::clamp(g_settings.vsh_left_r, 0.0f, 1.0f),
                  std::clamp(g_settings.vsh_left_g, 0.0f, 1.0f),
                  std::clamp(g_settings.vsh_left_b, 0.0f, 1.0f),
                  static_cast<long long>(offset_ms));
    return text;
  }
  if (AniketosTheme::GlobalTheme::Active() || !clock_animation_active())
    return "null";
  const AniketosTheme::ThemeConfig config =
      AniketosTheme::ConfigFromSettings(g_settings);
  AniketosTheme::HSV glow = AniketosTheme::RGBToHSV(config.glow);
  /* the start the native clock counts from; not started yet: now */
  const uint64_t started = s_animation_started_ms;
  if (started)
    offset_ms += static_cast<int64_t>(started);
  else
    offset_ms += static_cast<int64_t>(MonotonicMs());
  std::snprintf(text, sizeof(text),
                "{\"m\":0,\"sp\":%.2f,\"h\":%.4f,\"s\":%.4f,\"v\":%.4f,"
                "\"o\":%lld}",
                std::clamp(config.animation_speed, 0.25f, 4.0f), glow.h,
                std::max(glow.s, 0.88f), std::max(glow.v, 0.70f),
                static_cast<long long>(offset_ms));
  return text;
}

/* The names of a month or weekday style, in the menu's language, as a JSON
   array; null for a style without names (numbers) or a language without a
   list. */
std::string json_names(const char *group, int count, int style) {
  if (style == 0)
    return "null";
  std::string out = "[";
  for (int index = 0; index < count; ++index) {
    const std::string name =
        translated_indexed(group, index, style == 1 ? "short" : "long");
    if (name.empty())
      return "null";
    out += (index ? "," : "") + JsString(name);
  }
  return out + "]";
}

} // namespace

void sync_clock_language(time_t now) {
  if (s_language_setting != g_settings.ui_lang) {
    s_language_setting = g_settings.ui_lang;
    s_language_ready = false;
    s_last_language_retry = static_cast<time_t>(-1);
  }
  if (s_language_ready || s_last_language_retry == now)
    return;
  s_last_language_retry = now;
  toolbox_i18n::apply_system_or_ui_lang(g_settings.ui_lang);
  s_language_ready = true;
  s_force_refresh = true;
}

uint64_t settings_signature() {
  uint64_t value = custom_clock_drawn_here() ? 1u : 0u;
  for (const int setting :
       {g_settings.shell_clock_show_seconds ? 1 : 0,
        g_settings.shell_clock_show_date ? 1 : 0,
        g_settings.shell_clock_show_year ? 1 : 0,
        g_settings.shell_clock_hour_format,
        g_settings.shell_clock_date_order,
        g_settings.shell_clock_month_style,
        g_settings.shell_clock_weekday_style,
        g_settings.shell_clock_date_separator,
        g_settings.shell_clock_time_font_size,
        g_settings.shell_clock_date_font_size,
        std::clamp(g_settings.shell_clock_date_spacing, -80, 160) + 80,
        g_settings.shell_clock_time_r,
        g_settings.shell_clock_time_g,
        g_settings.shell_clock_time_b,
        g_settings.shell_clock_date_r,
        g_settings.shell_clock_date_g,
        g_settings.shell_clock_date_b,
        toolbox_i18n::active_ui_lang_value()})
    value = value * 31u + static_cast<unsigned>(setting);
  return value;
}

std::string format_time(const std::tm &local) {
  const int mode = std::clamp(g_settings.shell_clock_hour_format, 0, 2);
  bool twelve_hour = mode == 2;
  bool leading_zero = mode == 1;
  if (mode == 0) {
    /* follow the system: a marker in any language ("p. m.", "du.", "PM"
       before the time) means 12-hour */
    ClockText::Time stock;
    if (ClockText::Parse(s_native.stock_time.c_str(), &stock,
                         ClockText::Separators::ColonOrDot)) {
      twelve_hour = stock.twelve_hour;
      leading_zero = stock.hour_digits == 2 && stock.hour < 10;
    }
  }
  int hour = local.tm_hour;
  if (twelve_hour) {
    hour %= 12;
    if (hour == 0)
      hour = 12;
  }
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer),
                g_settings.shell_clock_show_seconds ? "%0*d:%02d:%02d"
                                                    : "%0*d:%02d",
                leading_zero ? 2 : 1, hour, local.tm_min, local.tm_sec);
  std::string result(buffer);
  if (twelve_hour)
    result += std::string(" ") +
              toolbox_i18n::tr(local.tm_hour < 12 ? "clock.am" : "clock.pm");
  return result;
}

std::string format_date(const std::tm &local) {
  const int month_style = std::clamp(g_settings.shell_clock_month_style, 0, 2);
  const int weekday_style =
      std::clamp(g_settings.shell_clock_weekday_style, 0, 2);
  const std::string day = two_digits(local.tm_mday);
  std::string month =
      month_style == 0
          ? two_digits(local.tm_mon + 1)
          : translated_indexed("month", local.tm_mon,
                               month_style == 1 ? "short" : "long");
  /* A language without a list of month names still gets a complete date. */
  if (month.empty())
    month = two_digits(local.tm_mon + 1);
  const std::string year = g_settings.shell_clock_show_year
                               ? std::to_string(local.tm_year + 1900)
                               : std::string();
  const char *separator = date_separator();
  const int order = effective_date_order();
  std::string result;
  auto append = [&](const std::string &component) {
    if (component.empty())
      return;
    if (!result.empty())
      result += separator;
    result += component;
  };
  if (order == 2) {
    append(month);
    append(day);
    append(year);
  } else if (order == 3) {
    append(year);
    append(month);
    append(day);
  } else {
    append(day);
    append(month);
    append(year);
  }
  if (weekday_style != 0) {
    const std::string weekday = translated_indexed(
        "weekday", local.tm_wday,
        weekday_style == 1 ? "short" : "long");
    if (!weekday.empty())
      result = weekday + ", " + result;
  }
  return result;
}

std::string ScriptConfig() {
  std::string out =
      std::string("{\"on\":") + (custom_clock_on() ? "true" : "false");
  /* the script asks again every 100 ms instead of every 2 s while the menu
     that edits the clock is open */
  out += ",\"hot\":";
  out += AniketosShell::ShellMenu::IsVisible() ? "true" : "false";
  out += ",\"seconds\":";
  out += g_settings.shell_clock_show_seconds ? "true" : "false";
  out += ",\"hour\":" +
         std::to_string(std::clamp(g_settings.shell_clock_hour_format, 0, 2));
  out += ",\"am\":" + JsString(toolbox_i18n::tr("clock.am"));
  out += ",\"pm\":" + JsString(toolbox_i18n::tr("clock.pm"));
  out += ",\"date\":";
  out += g_settings.shell_clock_show_date ? "true" : "false";
  out += ",\"order\":" + std::to_string(effective_date_order());
  out += ",\"sep\":" + JsString(date_separator());
  out += ",\"year\":";
  out += g_settings.shell_clock_show_year ? "true" : "false";
  out += ",\"months\":" +
         json_names("month", 12,
                    std::clamp(g_settings.shell_clock_month_style, 0, 2));
  out += ",\"weekdays\":" +
         json_names("weekday", 7,
                    std::clamp(g_settings.shell_clock_weekday_style, 0, 2));
  out += ",\"tc\":" + json_color(g_settings.shell_clock_time_r,
                                  g_settings.shell_clock_time_g,
                                  g_settings.shell_clock_time_b);
  /* 0 keeps Sony's size: the setting as it comes is no size anyone chose */
  static const int default_time_size =
      onion::Settings{}.shell_clock_time_font_size;
  out += ",\"ts\":" +
         std::to_string(g_settings.shell_clock_time_font_size ==
                                default_time_size
                            ? 0
                            : std::clamp(g_settings.shell_clock_time_font_size,
                                         8, 96));
  out += ",\"anim\":" + json_animation();
  out += ",\"dc\":" + json_color(g_settings.shell_clock_date_r,
                                  g_settings.shell_clock_date_g,
                                  g_settings.shell_clock_date_b);
  out += ",\"ds\":" +
         std::to_string(std::clamp(g_settings.shell_clock_date_font_size, 8, 96));
  /* the gap between the digits and the date, as the overlay had it */
  out += ",\"gap\":" +
         std::to_string(std::max(0, 6 + std::clamp(
                                         g_settings.shell_clock_date_spacing,
                                         -80, 160)));
  return out + "}";
}

} // namespace ShellClock
