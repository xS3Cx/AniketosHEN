/* What a website's Home tile needs from the page and from its icon file
 * (host-testable, no network, Mono or PS5 I/O). Everything here reads bytes
 * from the network, so every length is checked before it is used.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace WebIcon {

constexpr size_t kMaxHtml = 256 * 1024;
constexpr size_t kMaxIcon = 1024 * 1024;
constexpr uint32_t kMaxIconSide = 1024;
constexpr size_t kMaxUrl = 2048;
/* The tile's picture when the site offers none. */
constexpr int kTileSize = 256;

/* Only what the system's image reader takes, and ICO, which it does not. */
enum class Format { Unknown, Png, Jpeg, Gif, WebP, Svg, Ico };

namespace detail {

inline char lower(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline std::string lowered(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), lower);
  return out;
}

inline size_t find_nocase(std::string_view text, std::string_view word,
                          size_t from) {
  for (size_t i = from; i + word.size() <= text.size(); ++i) {
    size_t k = 0;
    while (k < word.size() && lower(text[i + k]) == word[k])
      ++k;
    if (k == word.size())
      return i;
  }
  return std::string_view::npos;
}

struct LinkTag {
  std::string_view rel, href, sizes, type;
  size_t end = 0; /* just past the tag */
  bool complete = false;
};

/* The attributes of the tag whose name ends at `at`. */
inline LinkTag read_link(std::string_view html, size_t at) {
  LinkTag tag;
  while (at < html.size()) {
    while (at < html.size() && (space(html[at]) || html[at] == '/'))
      ++at;
    if (at >= html.size())
      return tag;
    if (html[at] == '>') {
      tag.end = at + 1;
      tag.complete = true;
      return tag;
    }
    const size_t name_start = at;
    while (at < html.size() && !space(html[at]) && html[at] != '=' &&
           html[at] != '>' && html[at] != '/')
      ++at;
    const std::string name =
        lowered(html.substr(name_start, at - name_start));
    while (at < html.size() && space(html[at]))
      ++at;
    std::string_view value;
    if (at < html.size() && html[at] == '=') {
      ++at;
      while (at < html.size() && space(html[at]))
        ++at;
      if (at >= html.size())
        return tag;
      if (html[at] == '"' || html[at] == '\'') {
        const size_t close = html.find(html[at], at + 1);
        if (close == std::string_view::npos)
          return tag;
        value = html.substr(at + 1, close - at - 1);
        at = close + 1;
      } else {
        const size_t value_start = at;
        while (at < html.size() && !space(html[at]) && html[at] != '>')
          ++at;
        value = html.substr(value_start, at - value_start);
      }
    }
    if (name == "rel")
      tag.rel = value;
    else if (name == "href")
      tag.href = value;
    else if (name == "sizes")
      tag.sizes = value;
    else if (name == "type")
      tag.type = value;
  }
  return tag;
}

/* The widest of "32x32 64x64"; 0 when it says nothing usable. */
inline int widest(std::string_view sizes) {
  int best = 0;
  size_t at = 0;
  while (at < sizes.size()) {
    int width = 0;
    bool digits = false;
    while (at < sizes.size() && sizes[at] >= '0' && sizes[at] <= '9') {
      width = std::min(width * 10 + (sizes[at] - '0'), 100000);
      digits = true;
      ++at;
    }
    if (digits && at < sizes.size() && lower(sizes[at]) == 'x')
      best = std::max(best, width);
    while (at < sizes.size() && !space(sizes[at]))
      ++at;
    while (at < sizes.size() && space(sizes[at]))
      ++at;
  }
  return best;
}

/* 0 for a link that is no site icon. Bigger is better for a tile. */
inline int icon_score(const LinkTag &tag) {
  bool plain = false, touch = false;
  const std::string rel = lowered(tag.rel);
  size_t at = 0;
  while (at < rel.size()) {
    size_t end = at;
    while (end < rel.size() && !space(rel[end]))
      ++end;
    const std::string_view token =
        std::string_view(rel).substr(at, end - at);
    plain = plain || token == "icon";
    touch = touch || token == "apple-touch-icon" ||
            token == "apple-touch-icon-precomposed";
    at = end + 1;
  }
  if ((!plain && !touch) || tag.href.empty())
    return 0;
  int score = widest(tag.sizes);
  if (score == 0)
    score = touch ? 180 : 32;
  /* The system's reader for it is the least proven: a raster icon first. */
  const std::string href = lowered(tag.href);
  if (lowered(tag.type) == "image/svg+xml" ||
      href.find(".svg") != std::string::npos)
    score = 1;
  return score;
}

inline void replace_all(std::string &text, const char *from, const char *to) {
  const size_t length = std::strlen(from);
  for (size_t at = text.find(from); at != std::string::npos;
       at = text.find(from, at + 1))
    text.replace(at, length, to);
}

inline void put_be32(std::vector<uint8_t> &out, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8)
    out.push_back(static_cast<uint8_t>(value >> shift));
}

inline uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

inline void put_chunk(std::vector<uint8_t> &out, const char *type,
                      const std::vector<uint8_t> &body) {
  put_be32(out, static_cast<uint32_t>(body.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), body.begin(), body.end());
  put_be32(out, crc32(out.data() + start, out.size() - start));
}

inline uint32_t le(const uint8_t *data, int bytes) {
  uint32_t value = 0;
  for (int i = 0; i < bytes; ++i)
    value |= static_cast<uint32_t>(data[i]) << (8 * i);
  return value;
}

constexpr uint8_t kPngMagic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

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

inline bool ValidTileId(std::string_view id) {
  if (id.size() != 9 || id.substr(0, 4) != "WEBT")
    return false;
  return std::all_of(id.begin() + 4, id.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}

inline bool ValidPageUrl(std::string_view url) {
  if (url.empty() || url.size() > kMaxUrl)
    return false;
  if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0)
    return false;
  return std::none_of(url.begin(), url.end(), [](char c) {
    return static_cast<unsigned char>(c) <= 0x20 || c == 0x7f;
  });
}

/* The best site icon a page names in its head, as written there. */
inline std::string FindIconHref(std::string_view html) {
  html = html.substr(0, kMaxHtml);
  const size_t head_end = detail::find_nocase(html, "</head", 0);
  if (head_end != std::string_view::npos)
    html = html.substr(0, head_end);
  std::string_view best;
  int best_score = 0;
  size_t at = 0;
  while ((at = detail::find_nocase(html, "<link", at)) !=
         std::string_view::npos) {
    at += 5;
    if (at >= html.size())
      break;
    if (!detail::space(html[at]) && html[at] != '/')
      continue;
    const detail::LinkTag tag = detail::read_link(html, at);
    if (!tag.complete)
      break;
    at = tag.end;
    const int score = detail::icon_score(tag);
    if (score > best_score) {
      best_score = score;
      best = tag.href;
    }
  }
  std::string href(best);
  detail::replace_all(href, "&amp;", "&");
  return href;
}

/* `href` against the page it came from; empty for anything but http(s). */
inline std::string ResolveUrl(const std::string &base, std::string_view href) {
  while (!href.empty() && detail::space(href.front()))
    href.remove_prefix(1);
  while (!href.empty() && detail::space(href.back()))
    href.remove_suffix(1);
  const size_t scheme = base.find("://");
  if (href.empty() || scheme == std::string::npos)
    return {};
  const size_t host_end = base.find_first_of("/?#", scheme + 3);
  const std::string origin = base.substr(0, host_end);
  std::string out;
  const std::string start = detail::lowered(href.substr(0, 8));
  const size_t colon = href.find(':');
  const size_t slash = href.find_first_of("/?#");
  if (start.compare(0, 7, "http://") == 0 ||
      start.compare(0, 8, "https://") == 0) {
    out = href;
  } else if (href.compare(0, 2, "//") == 0) {
    out = base.substr(0, scheme + 1) + std::string(href);
  } else if (colon != std::string_view::npos &&
             (slash == std::string_view::npos || colon < slash)) {
    return {}; /* data:, javascript: and the like */
  } else if (href.front() == '/') {
    out = origin + std::string(href);
  } else {
    std::string path =
        host_end == std::string::npos ? "/" : base.substr(host_end);
    path = path.substr(0, path.find_first_of("?#"));
    if (href.front() != '?')
      path = path.substr(0, path.rfind('/') + 1);
    if (path.empty())
      path = "/";
    out = origin + path + std::string(href);
  }
  return ValidPageUrl(out) ? out : std::string();
}

/* False for a picture far past any icon's size, or a header that says nothing:
   the system decodes a tile's picture whole, in ShellUI, at every Home start. */
inline bool FitsTile(const uint8_t *data, size_t size, Format format) {
  const auto small = [](uint32_t width, uint32_t height) {
    return width && height && width <= kMaxIconSide && height <= kMaxIconSide;
  };
  const auto be = [&](size_t at, int bytes) {
    uint32_t value = 0;
    for (int i = 0; i < bytes; ++i)
      value = value << 8 | data[at + i];
    return value;
  };
  switch (format) {
  case Format::Png:
    return size >= 24 && small(be(16, 4), be(20, 4));
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
        return small(be(at + 7, 2), be(at + 5, 2));
      } else {
        at += 2 + be(at + 2, 2);
      }
    }
    return false;
  default:
    return false;
  }
}

/* The format a file really has, whatever the server called it. */
inline Format Sniff(const uint8_t *data, size_t size) {
  if (size >= 8 && std::memcmp(data, detail::kPngMagic, 8) == 0)
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
  while (!text.empty() && detail::space(text.front()))
    text.remove_prefix(1);
  const std::string start = detail::lowered(text.substr(0, 5));
  if (start.compare(0, 4, "<svg") == 0 ||
      ((start == "<?xml" || start.compare(0, 4, "<!--") == 0) &&
       detail::find_nocase(text, "<svg", 0) != std::string_view::npos))
    return Format::Svg;
  return Format::Unknown;
}

/* A PNG of `width` x `height` RGBA pixels, in stored deflate blocks: the
   tile's picture is written once and read by the system's own reader. */
inline std::vector<uint8_t> EncodePng(int width, int height,
                                      const std::vector<uint8_t> &rgba) {
  const size_t row = static_cast<size_t>(width) * 4;
  std::vector<uint8_t> out;
  if (width <= 0 || height <= 0 ||
      rgba.size() != row * static_cast<size_t>(height))
    return out;
  out.insert(out.end(), detail::kPngMagic, detail::kPngMagic + 8);
  std::vector<uint8_t> header;
  detail::put_be32(header, static_cast<uint32_t>(width));
  detail::put_be32(header, static_cast<uint32_t>(height));
  header.insert(header.end(), {8, 6, 0, 0, 0}); /* 8-bit RGBA */
  detail::put_chunk(out, "IHDR", header);

  std::vector<uint8_t> raw;
  raw.reserve((row + 1) * static_cast<size_t>(height));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0); /* no filter */
    const uint8_t *line = rgba.data() + row * static_cast<size_t>(y);
    raw.insert(raw.end(), line, line + row);
  }
  std::vector<uint8_t> zlib = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (const uint8_t byte : raw) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t at = 0; at < raw.size();) {
    const size_t take = std::min<size_t>(raw.size() - at, 65535);
    const bool last = at + take == raw.size();
    zlib.push_back(last ? 1 : 0);
    zlib.push_back(static_cast<uint8_t>(take));
    zlib.push_back(static_cast<uint8_t>(take >> 8));
    zlib.push_back(static_cast<uint8_t>(~take));
    zlib.push_back(static_cast<uint8_t>(~take >> 8));
    zlib.insert(zlib.end(), raw.begin() + static_cast<ptrdiff_t>(at),
                raw.begin() + static_cast<ptrdiff_t>(at + take));
    at += take;
  }
  detail::put_be32(zlib, (b << 16) | a);
  detail::put_chunk(out, "IDAT", zlib);
  detail::put_chunk(out, "IEND", {});
  return out;
}

/* The largest picture in an ICO as PNG bytes; empty when there is none we
   can read. New icons hold a PNG as it is, old ones an upside-down bitmap
   without a file header, at double its height for its 1-bit transparency
   mask. */
inline std::vector<uint8_t> IcoToPng(const uint8_t *data, size_t size) {
  if (size < 6 || Sniff(data, size) != Format::Ico)
    return {};
  const size_t count = detail::le(data + 4, 2);
  if (count > (size - 6) / 16)
    return {};
  size_t best_offset = 0, best_length = 0;
  long best_rank = -1;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t *entry = data + 6 + i * 16;
    const size_t length = detail::le(entry + 8, 4);
    const size_t offset = detail::le(entry + 12, 4);
    if (offset >= size || length > size - offset || length < 8)
      continue;
    const long width = entry[0] ? entry[0] : 256;
    const long rank = width * 64 + static_cast<long>(detail::le(entry + 6, 2));
    if (rank > best_rank) {
      best_rank = rank;
      best_offset = offset;
      best_length = length;
    }
  }
  if (best_rank < 0)
    return {};
  const uint8_t *image = data + best_offset;
  if (Sniff(image, best_length) == Format::Png)
    return std::vector<uint8_t>(image, image + best_length);

  if (best_length < 40)
    return {};
  const size_t header = detail::le(image, 4);
  const int width = static_cast<int>(detail::le(image + 4, 4));
  const int doubled = static_cast<int>(detail::le(image + 8, 4));
  const int bits = static_cast<int>(detail::le(image + 14, 2));
  const uint32_t compression = detail::le(image + 16, 4);
  const size_t palette_used = detail::le(image + 32, 4);
  const int height = doubled / 2;
  if (header < 40 || header > best_length || width <= 0 || width > 256 ||
      height <= 0 || height > 256 || compression != 0 ||
      (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32))
    return {};
  const size_t palette_size =
      bits > 8 ? 0 : palette_used ? palette_used : size_t{1} << bits;
  if (palette_size > 256 || palette_size * 4 > best_length - header)
    return {};
  const uint8_t *palette = image + header;
  const size_t colour_stride =
      (static_cast<size_t>(width) * static_cast<size_t>(bits) + 31) / 32 * 4;
  const size_t mask_stride = (static_cast<size_t>(width) + 31) / 32 * 4;
  const size_t colour_start = header + palette_size * 4;
  const size_t colour_size = colour_stride * static_cast<size_t>(height);
  if (colour_size > best_length - colour_start)
    return {};
  const uint8_t *colours = image + colour_start;
  /* A file cut before the mask just has no transparency. */
  const size_t mask_size = mask_stride * static_cast<size_t>(height);
  const uint8_t *mask = mask_size <= best_length - colour_start - colour_size
                            ? colours + colour_size
                            : nullptr;
  std::vector<uint8_t> rgba(static_cast<size_t>(width) *
                            static_cast<size_t>(height) * 4);
  bool any_alpha = false;
  for (int y = 0; y < height; ++y) {
    const size_t stored_row = static_cast<size_t>(height - 1 - y);
    const uint8_t *line = colours + colour_stride * stored_row;
    const uint8_t *mask_line = mask ? mask + mask_stride * stored_row : nullptr;
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
    /* the mask counts unless the bitmap has an alpha channel of its own */
    if (bits != 32 && mask_line)
      for (int x = 0; x < width; ++x)
        if (mask_line[x / 8] & (0x80 >> (x % 8)))
          rgba[(static_cast<size_t>(y) * width + x) * 4 + 3] = 0;
  }
  /* 32-bit icons with an empty alpha channel were made for the mask */
  if (bits == 32 && !any_alpha)
    for (int y = 0; y < height; ++y) {
      const size_t stored_row = static_cast<size_t>(height - 1 - y);
      for (int x = 0; x < width; ++x) {
        const bool masked =
            mask && (mask[mask_stride * stored_row + x / 8] & (0x80 >> (x % 8)));
        rgba[(static_cast<size_t>(y) * width + x) * 4 + 3] = masked ? 0 : 255;
      }
    }
  return EncodePng(width, height, rgba);
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
  while (!title.empty() && detail::space(title.front()))
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
  return EncodePng(kTileSize, kTileSize, rgba);
}

} // namespace WebIcon
