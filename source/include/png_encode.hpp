/* RGBA pixels written as a PNG file (host-testable, no Mono or PS5 I/O). */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Png {

constexpr uint8_t kMagic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

namespace detail {

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

} // namespace detail

/* A PNG of `width` x `height` RGBA pixels, in stored deflate blocks: the
   picture is written once and read by the system's own reader. */
inline std::vector<uint8_t> Encode(int width, int height,
                                   const std::vector<uint8_t> &rgba) {
  const size_t row = static_cast<size_t>(width) * 4;
  std::vector<uint8_t> out;
  if (width <= 0 || height <= 0 ||
      rgba.size() != row * static_cast<size_t>(height))
    return out;
  out.insert(out.end(), kMagic, kMagic + 8);
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

} // namespace Png
