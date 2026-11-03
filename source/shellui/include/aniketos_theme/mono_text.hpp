/* Looking at a MonoString's UTF-16 text in place: the hooks on Sony's hot
 * setters (Panel colours, Label text, focus layers) only need to know whether
 * a name is one of a few ASCII words, which costs nothing this way. Converting
 * with Mono_to_String (mono_string_to_utf8 + std::string) is for the hit. */
#pragma once

#include "monodef.h"

#include <cstdint>

namespace AniketosTheme::MonoText {

inline int32_t Length(const MonoString *text) {
  return text ? text->length : 0;
}

inline bool EqualsAscii(const MonoString *text, const char *ascii) {
  const int32_t length = Length(text);
  int32_t i = 0;
  for (; i < length && ascii[i]; ++i)
    if (text->chars[i] != static_cast<unsigned char>(ascii[i]))
      return false;
  return i == length && !ascii[i];
}

inline bool StartsWithAscii(const MonoString *text, const char *ascii) {
  const int32_t length = Length(text);
  for (int32_t i = 0; ascii[i]; ++i)
    if (i >= length || text->chars[i] != static_cast<unsigned char>(ascii[i]))
      return false;
  return true;
}

inline bool ContainsAscii(const MonoString *text, const char *ascii) {
  const int32_t length = Length(text);
  for (int32_t start = 0; start < length; ++start) {
    int32_t i = 0;
    while (ascii[i] && start + i < length &&
           text->chars[start + i] == static_cast<unsigned char>(ascii[i]))
      ++i;
    if (!ascii[i])
      return true;
  }
  return false;
}

/* FNV-1a over the characters; 0 for an empty or missing text. */
inline uint64_t Hash(const MonoString *text) {
  const int32_t length = Length(text);
  if (length <= 0)
    return 0;
  uint64_t hash = 1469598103934665603ull;
  for (int32_t i = 0; i < length; ++i)
    hash = (hash ^ text->chars[i]) * 1099511628211ull;
  return hash ? hash : 1;
}

/* A digit followed by ':' or '.': the shape of every clock time
   (ClockText::Parse needs at least that). */
inline bool TimeShape(const MonoString *text) {
  const int32_t length = Length(text);
  for (int32_t i = 0; i + 1 < length; ++i)
    if (text->chars[i] >= '0' && text->chars[i] <= '9' &&
        (text->chars[i + 1] == ':' || text->chars[i + 1] == '.'))
      return true;
  return false;
}

} // namespace AniketosTheme::MonoText
