#pragma once

#include <cstddef>

/* The system clock's text as Sony writes it, in every system language.
 *
 * The time is the hour (one or two digits), the language's separator (':',
 * or '.' in Finnish) and two digits of minutes. A 12-hour clock puts a
 * marker before or after it, with or without a space and sometimes with
 * dots: "10:18 PM", "10:18 p. m." (Spanish), "du.10:18" (Hungarian),
 * "PM10:18" (Chinese), "10:18 після полудня" (Ukrainian). Arabic wraps the
 * time in a right-to-left mark. (HomeUI's msgid_time_am_var,
 * msgid_time_pm_var and msgid_time_separator, all 31 languages, the same on
 * 11.40 and 13.42.)
 *
 * Any other digit, a second time or a line break means the text is not the
 * clock, which keeps out labels such as "23.8  5:47". */
namespace ClockText {

/* The separators Parse accepts between the hour and the minutes. ':' is
 * every system language's but Finnish. '.' is Finnish only ("22.18",
 * "9.05 PM"), and it is also how versions and sizes are written ("6.00",
 * "10.40", "1.25 GB"), so it is asked for only where the text is known to
 * come from the clock. ClockLabel also takes seconds after the minutes, with
 * the same separator ("22:18:05", "9:05:07 PM"): the clock our script draws
 * (home_inject_js.inc) in Sony's clock label. */
enum class Separators { Colon, ColonOrDot, ClockLabel };

struct Time {
  int hour = 0; /* as shown */
  int minute = 0;
  int hour_digits = 0;      /* 2 for "09:05", 1 for "9:05" */
  bool twelve_hour = false; /* a marker before or after the time */
};

namespace detail {

/* The longest markers, in bytes: "오후 " before, " після полудня" after. */
inline constexpr std::size_t kMaxBefore = 12;
inline constexpr std::size_t kMaxAfter = 32;

inline bool digit(unsigned char ch) { return ch >= '0' && ch <= '9'; }

/* Bytes of the space or direction mark at `p`, 0 for anything else: space,
 * tab, U+00A0, U+200E/U+200F, U+202A to U+202F, U+2066 to U+2069. */
inline std::size_t blank(const unsigned char *p) {
  if (p[0] == ' ' || p[0] == '\t')
    return 1;
  if (p[0] == 0xC2 && p[1] == 0xA0)
    return 2;
  if (p[0] == 0xE2 && p[1] == 0x80 &&
      (p[2] == 0x8E || p[2] == 0x8F || (p[2] >= 0xAA && p[2] <= 0xAF)))
    return 3;
  if (p[0] == 0xE2 && p[1] == 0x81 && p[2] >= 0xA6 && p[2] <= 0xA9)
    return 3;
  return 0;
}

/* Whether [begin, end) can stand next to the time: no digit, colon or line
 * break. `letters` tells whether it holds more than spaces and marks. */
inline bool marker(const unsigned char *begin, const unsigned char *end,
                   bool *letters) {
  *letters = false;
  for (const unsigned char *p = begin; p < end;) {
    if (digit(*p) || *p == ':' || *p == '\n' || *p == '\r')
      return false;
    if (const std::size_t skip = blank(p)) {
      p += skip;
      continue;
    }
    *letters = true;
    ++p;
  }
  return true;
}

/* Whether [p, end) is " AM" or " PM", the only 12-hour marker Finnish has
 * (msgid_time_am_var / msgid_time_pm_var: "%time% AM", "%time% PM"). */
inline bool finnish_marker(const unsigned char *p, const unsigned char *end) {
  std::size_t skip = 0;
  std::size_t blanks = 0;
  while (p < end && (skip = blank(p)) != 0) {
    p += skip;
    ++blanks;
  }
  if (!blanks || end - p < 2 || (p[0] != 'A' && p[0] != 'P') || p[1] != 'M')
    return false;
  for (p += 2; p < end; p += skip)
    if (!(skip = blank(p)))
      return false;
  return true;
}

} // namespace detail

/* Whether `text` is a time as the system clock shows it; `out` gets it. */
inline bool Parse(const char *text, Time *out = nullptr,
                  Separators separators = Separators::Colon) {
  if (!text)
    return false;
  const auto *s = reinterpret_cast<const unsigned char *>(text);
  std::size_t length = 0;
  while (s[length] && length <= 64)
    ++length;
  if (length < 4 || length > 48)
    return false;

  /* the first digit starts the time */
  std::size_t start = 0;
  while (start < length && !detail::digit(s[start]))
    ++start;
  if (start == length)
    return false;
  std::size_t p = start;
  Time time;
  time.hour = s[p++] - '0';
  time.hour_digits = 1;
  if (detail::digit(s[p])) {
    time.hour = time.hour * 10 + (s[p++] - '0');
    time.hour_digits = 2;
  }
  const unsigned char separator = s[p];
  if (separator != ':' &&
      !(separator == '.' && separators != Separators::Colon))
    return false;
  ++p;
  if (!detail::digit(s[p]) || !detail::digit(s[p + 1]))
    return false;
  time.minute = (s[p] - '0') * 10 + (s[p + 1] - '0');
  p += 2;
  if (time.hour > 23 || time.minute > 59)
    return false;
  if (separators == Separators::ClockLabel && s[p] == separator &&
      detail::digit(s[p + 1]) && detail::digit(s[p + 2]) &&
      (s[p + 1] - '0') * 10 + (s[p + 2] - '0') <= 59)
    p += 3;

  bool before = false;
  bool after = false;
  if (start > detail::kMaxBefore || length - p > detail::kMaxAfter ||
      !detail::marker(s, s + start, &before) ||
      !detail::marker(s + p, s + length, &after) || (before && after))
    return false;
  /* A dot as the separator is Finnish: nothing before the time, and after it
     nothing or " AM"/" PM". "Ver. 2.50" and "1.25 GB" are no clock. */
  if (separator == '.' &&
      (before || (after && !detail::finnish_marker(s + p, s + length))))
    return false;
  time.twelve_hour = before || after;
  if (out)
    *out = time;
  return true;
}

} // namespace ClockText
