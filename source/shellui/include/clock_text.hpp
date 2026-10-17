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

} // namespace detail

/* Whether `text` is a time as the system clock shows it; `out` gets it. */
inline bool Parse(const char *text, Time *out = nullptr) {
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
  if (separator != ':' && separator != '.')
    return false;
  ++p;
  if (!detail::digit(s[p]) || !detail::digit(s[p + 1]))
    return false;
  time.minute = (s[p] - '0') * 10 + (s[p + 1] - '0');
  p += 2;
  if (time.hour > 23 || time.minute > 59)
    return false;

  bool before = false;
  bool after = false;
  if (start > detail::kMaxBefore || length - p > detail::kMaxAfter ||
      !detail::marker(s, s + start, &before) ||
      !detail::marker(s + p, s + length, &after) || (before && after))
    return false;
  /* A dot as the separator is Finnish, whose marker comes after the time and
     has no dots; a "Ver. 2.50" is no clock. */
  if (separator == '.') {
    if (before)
      return false;
    for (std::size_t q = p; q < length; ++q)
      if (s[q] == '.')
        return false;
  }
  time.twelve_hour = before || after;
  if (out)
    *out = time;
  return true;
}

} // namespace ClockText
