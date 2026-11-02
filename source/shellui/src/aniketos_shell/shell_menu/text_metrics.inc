/* One UTF-8 character; the cursor moves past it. A malformed sequence counts
   as its first byte alone. */
uint32_t next_utf8_character(const unsigned char *&cursor) {
  uint32_t character = *cursor++;
  if ((character & 0xe0u) == 0xc0u &&
      (cursor[0] & 0xc0u) == 0x80u) {
    character = ((character & 0x1fu) << 6u) | (cursor[0] & 0x3fu);
    cursor += 1;
  } else if ((character & 0xf0u) == 0xe0u &&
             (cursor[0] & 0xc0u) == 0x80u &&
             (cursor[1] & 0xc0u) == 0x80u) {
    character = ((character & 0x0fu) << 12u) |
                ((cursor[0] & 0x3fu) << 6u) | (cursor[1] & 0x3fu);
    cursor += 2;
  } else if ((character & 0xf8u) == 0xf0u &&
             (cursor[0] & 0xc0u) == 0x80u &&
             (cursor[1] & 0xc0u) == 0x80u &&
             (cursor[2] & 0xc0u) == 0x80u) {
    character = ((character & 0x07u) << 18u) |
                ((cursor[0] & 0x3fu) << 12u) |
                ((cursor[1] & 0x3fu) << 6u) | (cursor[2] & 0x3fu);
    cursor += 3;
  }
  return character;
}

/* How far a character advances the pen, in ems. */
float character_advance(uint32_t character) {
  // Combining accents do not advance the cursor. East-Asian glyphs and
  // emoji are full-width; accented Latin/Cyrillic/Arabic glyphs advance
  // once, rather than once for every byte of their UTF-8 encoding.
  const bool combining =
      (character >= 0x0300u && character <= 0x036fu) ||
      (character >= 0x1ab0u && character <= 0x1affu) ||
      (character >= 0x1dc0u && character <= 0x1dffu) ||
      (character >= 0x20d0u && character <= 0x20ffu) ||
      (character >= 0xfe20u && character <= 0xfe2fu);
  if (combining)
    return 0.0f;
  const bool full_width =
      (character >= 0x1100u && character <= 0x115fu) ||
      (character >= 0x2e80u && character <= 0xa4cfu) ||
      (character >= 0xac00u && character <= 0xd7a3u) ||
      (character >= 0xf900u && character <= 0xfaffu) ||
      (character >= 0xfe10u && character <= 0xfe6fu) ||
      (character >= 0xff01u && character <= 0xff60u) ||
      (character >= 0xffe0u && character <= 0xffe6u) ||
      (character >= 0x1f300u && character <= 0x1faffu) ||
      (character >= 0x20000u && character <= 0x3fffdu);
  if (full_width)
    return 1.0f;
  if (character == ' ')
    return 0.30f;
  if (character < 0x80u &&
      std::strchr("ilI.,:;'!|", static_cast<int>(character)))
    return 0.29f;
  if (character < 0x80u && std::strchr("MW@", static_cast<int>(character)))
    return 0.88f;
  if (character < 0x80u && std::strchr("mw", static_cast<int>(character)))
    return 0.76f;
  if (character >= 'A' && character <= 'Z')
    return 0.64f;
  if (character >= '0' && character <= '9')
    return 0.56f;
  if (character == 0x2605u)
    return 0.72f;
  return 0.52f;
}

float approximate_text_width(const char *text, float font_size) {
  if (!text)
    return 0.0f;
  float em = 0.0f;
  const auto *cursor = reinterpret_cast<const unsigned char *>(text);
  while (*cursor)
    em += character_advance(next_utf8_character(cursor));
  return em * font_size;
}

float exact_label_text_width(MonoObject *label, float fallback) {
  if (label && s_pui.image && s_label_class) {
    const float measured =
        Invoke<float>(s_pui.image, cls(label), label, "GetTextWidth");
    if (std::isfinite(measured) && measured > 0.0f &&
        measured < s_viewport_width * 2.0f)
      return measured;
  }
  return std::max(0.0f, fallback);
}
