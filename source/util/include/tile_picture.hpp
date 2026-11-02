/* A website's Home tile picture: its icon file checked and converted, or a
 * plain tile made from its name (host-testable, no network, Mono or PS5 I/O).
 * Icon files come from the network, so every length is checked before use.
 */
#pragma once

#include "png_encode.hpp"
#include "web_icon.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace TilePicture {

using WebIcon::kMaxIconSide;
using WebIcon::kTileSize;

/* Only what the system's image reader takes, and ICO, which it does not. */
enum class Format { Unknown, Png, Jpeg, Gif, WebP, Svg, Ico };

namespace detail {

inline uint32_t le(const uint8_t *data, int bytes) {
  uint32_t value = 0;
  for (int i = 0; i < bytes; ++i)
    value |= static_cast<uint32_t>(data[i]) << (8 * i);
  return value;
}

inline uint32_t be(const uint8_t *data, int bytes) {
  uint32_t value = 0;
  for (int i = 0; i < bytes; ++i)
    value = value << 8 | data[i];
  return value;
}

struct IcoEntry {
  const uint8_t *image = nullptr;
  size_t length = 0;
};

/* The widest picture the file really holds and, among those, the deepest. */
inline IcoEntry largest_ico_entry(const uint8_t *data, size_t size) {
  const size_t count = le(data + 4, 2);
  if (count > (size - 6) / 16)
    return {};
  IcoEntry best;
  long best_rank = -1;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t *entry = data + 6 + i * 16;
    const size_t length = le(entry + 8, 4);
    const size_t offset = le(entry + 12, 4);
    if (offset >= size || length > size - offset || length < 8)
      continue;
    const long width = entry[0] ? entry[0] : 256;
    const long rank = width * 64 + static_cast<long>(le(entry + 6, 2));
    if (rank > best_rank) {
      best_rank = rank;
      best = {data + offset, length};
    }
  }
  return best;
}

/* An ICO's old kind of picture as PNG bytes: an upside-down bitmap without a
   file header, at double its height for its 1-bit transparency mask. */
inline std::vector<uint8_t> ico_bitmap_to_png(const uint8_t *image,
                                              size_t length) {
  if (length < 40)
    return {};
  const size_t header = le(image, 4);
  const int width = static_cast<int>(le(image + 4, 4));
  const int doubled = static_cast<int>(le(image + 8, 4));
  const int bits = static_cast<int>(le(image + 14, 2));
  const uint32_t compression = le(image + 16, 4);
  const size_t palette_used = le(image + 32, 4);
  const int height = doubled / 2;
  if (header < 40 || header > length || width <= 0 || width > 256 ||
      height <= 0 || height > 256 || compression != 0 ||
      (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32))
    return {};
  const size_t palette_size =
      bits > 8 ? 0 : palette_used ? palette_used : size_t{1} << bits;
  if (palette_size > 256 || palette_size * 4 > length - header)
    return {};
  const uint8_t *palette = image + header;
  const size_t colour_stride =
      (static_cast<size_t>(width) * static_cast<size_t>(bits) + 31) / 32 * 4;
  const size_t mask_stride = (static_cast<size_t>(width) + 31) / 32 * 4;
  const size_t colour_start = header + palette_size * 4;
  const size_t colour_size = colour_stride * static_cast<size_t>(height);
  if (colour_size > length - colour_start)
    return {};
  const uint8_t *colours = image + colour_start;
  /* A file cut before the mask just has no transparency. */
  const size_t mask_size = mask_stride * static_cast<size_t>(height);
  const uint8_t *mask = mask_size <= length - colour_start - colour_size
                            ? colours + colour_size
                            : nullptr;
  std::vector<uint8_t> rgba(static_cast<size_t>(width) *
                            static_cast<size_t>(height) * 4);
  bool any_alpha = false;
  for (int y = 0; y < height; ++y) {
    const uint8_t *line =
        colours + colour_stride * static_cast<size_t>(height - 1 - y);
    for (int x = 0; x < width; ++x) {
      uint8_t *pixel =
          rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
      if (bits >= 24) {
        const uint8_t *source = line + static_cast<size_t>(x) * (bits / 8);
        pixel[0] = source[2];
        pixel[1] = source[1];
        pixel[2] = source[0];
        pixel[3] = bits == 32 ? source[3] : 255;
        any_alpha = any_alpha || (bits == 32 && source[3] != 0);
      } else {
        const size_t bit = static_cast<size_t>(x) * bits;
        const uint8_t byte = line[bit / 8];
        const unsigned index =
            (byte >> (8 - bits - bit % 8)) & ((1u << bits) - 1u);
        if (index >= palette_size)
          return {};
        pixel[0] = palette[index * 4 + 2];
        pixel[1] = palette[index * 4 + 1];
        pixel[2] = palette[index * 4];
        pixel[3] = 255;
      }
    }
  }
  /* The mask counts unless the bitmap has an alpha channel of its own; 32-bit
     icons with an empty one were made for the mask. */
  if (bits != 32 || !any_alpha)
    for (int y = 0; y < height; ++y) {
      const uint8_t *mask_line =
          mask ? mask + mask_stride * static_cast<size_t>(height - 1 - y)
               : nullptr;
      for (int x = 0; x < width; ++x) {
        const bool masked =
            mask_line && (mask_line[x / 8] & (0x80 >> (x % 8)));
        rgba[(static_cast<size_t>(y) * width + x) * 4 + 3] = masked ? 0 : 255;
      }
    }
  return Png::Encode(width, height, rgba);
}

/* 5x7, a row a byte: A-Z, 0-9, and a question mark for nothing better. */
constexpr uint8_t kGlyphs[37][7] = {
    {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, /* A */
    {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}, /* B */
    {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}, /* C */
    {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}, /* D */
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}, /* E */
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}, /* F */
    {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}, /* G */
    {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, /* H */
    {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}, /* L */
    {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}, /* N */
    {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, /* O */
    {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}, /* P */
    {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}, /* Q */
    {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}, /* R */
    {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}, /* S */
    {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11}, /* W */
    {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}, /* Y */
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}, /* Z */
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, /* 0 */
    {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}, /* 1 */
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}, /* 2 */
    {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e}, /* 3 */
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, /* 4 */
    {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}, /* 5 */
    {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}, /* 6 */
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, /* 8 */
    {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}, /* 9 */
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, /* ? */
};

} // namespace detail

/* False for a picture far past any icon's size, or a header that says nothing:
   the system decodes a tile's picture whole, in ShellUI, at every Home start. */
inline bool FitsTile(const uint8_t *data, size_t size, Format format) {
  const auto small = [](uint32_t width, uint32_t height) {
    return width && height && width <= kMaxIconSide && height <= kMaxIconSide;
  };
  switch (format) {
  case Format::Png:
    return size >= 24 && small(detail::be(data + 16, 4), detail::be(data + 20, 4));
  case Format::Gif:
    return size >= 10 && small(detail::le(data + 6, 2), detail::le(data + 8, 2));
  case Format::WebP:
    if (size >= 30 && std::memcmp(data + 12, "VP8X", 4) == 0)
      return small(detail::le(data + 24, 3) + 1, detail::le(data + 27, 3) + 1);
    if (size >= 30 && std::memcmp(data + 12, "VP8 ", 4) == 0)
      return small(detail::le(data + 26, 2) & 0x3FFF,
                   detail::le(data + 28, 2) & 0x3FFF);
    if (size >= 25 && std::memcmp(data + 12, "VP8L", 4) == 0) {
      const uint32_t bits = detail::le(data + 21, 4);
      return small((bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1);
    }
    return false;
  case Format::Jpeg:
    for (size_t at = 2; at + 9 <= size && data[at] == 0xFF;) {
      const uint8_t marker = data[at + 1];
      if (marker == 0xFF) {
        ++at;
      } else if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
                 marker != 0xC8 && marker != 0xCC) {
        return small(detail::be(data + at + 7, 2), detail::be(data + at + 5, 2));
      } else {
        at += 2 + detail::be(data + at + 2, 2);
      }
    }
    return false;
  default:
    return false;
  }
}

/* The format a file really has, whatever the server called it. */
inline Format Sniff(const uint8_t *data, size_t size) {
  if (size >= 8 && std::memcmp(data, Png::kMagic, 8) == 0)
    return Format::Png;
  if (size >= 3 && std::memcmp(data, "\xFF\xD8\xFF", 3) == 0)
    return Format::Jpeg;
  if (size >= 6 && (std::memcmp(data, "GIF87a", 6) == 0 ||
                    std::memcmp(data, "GIF89a", 6) == 0))
    return Format::Gif;
  if (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 &&
      std::memcmp(data + 8, "WEBP", 4) == 0)
    return Format::WebP;
  if (size >= 6 && data[0] == 0 && data[1] == 0 && data[2] == 1 &&
      data[3] == 0 && detail::le(data + 4, 2) > 0)
    return Format::Ico;
  std::string_view text(reinterpret_cast<const char *>(data),
                        std::min<size_t>(size, 2048));
  if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
    text.remove_prefix(3);
  while (!text.empty() && WebIcon::detail::space(text.front()))
    text.remove_prefix(1);
  const std::string start = WebIcon::detail::lowered(text.substr(0, 5));
  if (start.compare(0, 4, "<svg") == 0 ||
      ((start == "<?xml" || start.compare(0, 4, "<!--") == 0) &&
       WebIcon::detail::find_nocase(text, "<svg", 0) != std::string_view::npos))
    return Format::Svg;
  return Format::Unknown;
}

/* The largest picture in an ICO as PNG bytes; empty when there is none we
   can read. New icons hold a PNG as it is. */
inline std::vector<uint8_t> IcoToPng(const uint8_t *data, size_t size) {
  if (Sniff(data, size) != Format::Ico)
    return {};
  const detail::IcoEntry entry = detail::largest_ico_entry(data, size);
  if (!entry.image)
    return {};
  if (Sniff(entry.image, entry.length) == Format::Png)
    return std::vector<uint8_t>(entry.image, entry.image + entry.length);
  return detail::ico_bitmap_to_png(entry.image, entry.length);
}

/* The letter a site's plain tile shows: the title's, or the host's when the
   title starts with something the little font has not got. */
inline char TileLetter(std::string_view title, std::string_view url) {
  const auto usable = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
  };
  const auto upper = [](char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
  };
  while (!title.empty() && WebIcon::detail::space(title.front()))
    title.remove_prefix(1);
  if (!title.empty() && usable(title.front()))
    return upper(title.front());
  const size_t scheme = url.find("://");
  std::string_view host =
      scheme == std::string_view::npos ? url : url.substr(scheme + 3);
  if (host.compare(0, 4, "www.") == 0)
    host.remove_prefix(4);
  for (const char c : host.substr(0, host.find_first_of(":/?#")))
    if (usable(c))
      return upper(c);
  return '?';
}

/* A plain tile: the letter, big, on a dark colour that goes with the site. */
inline std::vector<uint8_t> LetterTile(char letter, std::string_view url) {
  static constexpr uint8_t kColours[6][3] = {
      {0x1f, 0x3a, 0x5f}, {0x2e, 0x4a, 0x3a}, {0x5a, 0x2d, 0x3c},
      {0x46, 0x34, 0x63}, {0x5c, 0x45, 0x21}, {0x25, 0x4f, 0x57}};
  uint32_t hash = 2166136261u;
  for (const char c : url.substr(0, url.find_first_of("/?#", url.find("://") + 3)))
    hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
  const uint8_t *colour = kColours[hash % 6];

  const int index = letter >= 'A' && letter <= 'Z'   ? letter - 'A'
                    : letter >= '0' && letter <= '9' ? letter - '0' + 26
                                                     : 36;
  constexpr int kScale = 16;
  const int left = (kTileSize - 5 * kScale) / 2;
  const int top = (kTileSize - 7 * kScale) / 2;
  std::vector<uint8_t> rgba(static_cast<size_t>(kTileSize) * kTileSize * 4);
  for (int y = 0; y < kTileSize; ++y)
    for (int x = 0; x < kTileSize; ++x) {
      const int gx = (x - left) / kScale, gy = (y - top) / kScale;
      const bool lit = x >= left && y >= top && gx < 5 && gy < 7 &&
                       (detail::kGlyphs[index][gy] & (0x10 >> gx));
      uint8_t *pixel = rgba.data() + (static_cast<size_t>(y) * kTileSize + x) * 4;
      pixel[0] = lit ? 255 : colour[0];
      pixel[1] = lit ? 255 : colour[1];
      pixel[2] = lit ? 255 : colour[2];
      pixel[3] = 255;
    }
  return Png::Encode(kTileSize, kTileSize, rgba);
}

} // namespace TilePicture
