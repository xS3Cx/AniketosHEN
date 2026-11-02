/* Host tests for finding, converting and making a website's tile picture
   (no network, no PS5). */
#include "test_harness.h"

#include "web_icon.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace WebIcon;

namespace {

using Bytes = std::vector<uint8_t>;

#define TEXT_IS(expected, value)                                               \
  do {                                                                         \
    const std::string text__ = (value);                                        \
    TEST_ASSERT_STREQ(expected, text__.c_str());                               \
  } while (0)

void put_le(Bytes &out, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i)
    out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

uint32_t be32(const Bytes &data, size_t at) {
  return (uint32_t{data[at]} << 24) | (uint32_t{data[at + 1]} << 16) |
         (uint32_t{data[at + 2]} << 8) | data[at + 3];
}

/* An ICO holding one bitmap, `width` square, with its AND mask. */
Bytes bmp_ico(int width, int bits, const Bytes &palette, const Bytes &colours,
              const Bytes &mask) {
  Bytes dib;
  put_le(dib, 40, 4);
  put_le(dib, width, 4);
  put_le(dib, width * 2, 4);
  put_le(dib, 1, 2);
  put_le(dib, bits, 2);
  dib.resize(40);
  dib.insert(dib.end(), palette.begin(), palette.end());
  dib.insert(dib.end(), colours.begin(), colours.end());
  dib.insert(dib.end(), mask.begin(), mask.end());
  Bytes ico = {0, 0, 1, 0, 1, 0};
  ico.push_back(static_cast<uint8_t>(width));
  ico.push_back(static_cast<uint8_t>(width));
  ico.insert(ico.end(), {0, 0, 1, 0});
  put_le(ico, bits, 2);
  put_le(ico, static_cast<uint32_t>(dib.size()), 4);
  put_le(ico, 22, 4);
  ico.insert(ico.end(), dib.begin(), dib.end());
  return ico;
}

/* The pixel data of a PNG of ours: one stored block, past each row's filter
   byte. */
Bytes png_pixels(const Bytes &png, int width) {
  const size_t data = 8 + 25 + 8 + 2 + 5;
  Bytes out;
  const size_t row = static_cast<size_t>(width) * 4;
  for (size_t at = data; at + row + 1 <= data + (row + 1) * width;
       at += row + 1)
    out.insert(out.end(), png.begin() + static_cast<ptrdiff_t>(at + 1),
               png.begin() + static_cast<ptrdiff_t>(at + 1 + row));
  return out;
}

int test_finds_the_best_icon() {
  const std::string html =
      "<html><HEAD>"
      "<link rel=stylesheet href=/a.css>"
      "<LINK REL='shortcut icon' href='/small.ico'>"
      "<link href=\"/big.png?a=1&amp;b=2\" sizes=\"16x16 192x192\" rel=\"icon\"/>"
      "<link rel=mask-icon href=/mask.svg>"
      "</head><body><link rel=icon sizes=512x512 href=/late.png></body>";
  TEXT_IS("/big.png?a=1&b=2", FindIconHref(html));
  TEXT_IS("/t.png", FindIconHref("<link rel=icon href=/i.ico>"
                                  "<link rel=apple-touch-icon href=/t.png>"));
  TEXT_IS("/i.ico", FindIconHref("<link rel=icon type=image/svg+xml href=/s.svg>"
                                 "<link rel=icon href=/i.ico>"));
  return 0;
}

int test_odd_html_is_harmless() {
  TEST_ASSERT_TRUE(FindIconHref("").empty());
  TEST_ASSERT_TRUE(FindIconHref("<link").empty());
  TEST_ASSERT_TRUE(FindIconHref("<link rel='icon href=/x").empty());
  TEST_ASSERT_TRUE(FindIconHref("<linked rel=icon href=/x>").empty());
  TEST_ASSERT_TRUE(FindIconHref("<link rel=icon>").empty());
  return 0;
}

int test_resolves_links() {
  const std::string page = "https://a.example:8080/dir/page.html?x=/y#z";
  TEXT_IS("https://a.example:8080/i.png", ResolveUrl(page, "/i.png"));
  TEXT_IS("https://a.example:8080/dir/i.png", ResolveUrl(page, "i.png"));
  TEXT_IS("https://a.example:8080/dir/../i.png", ResolveUrl(page, " ../i.png "));
  TEXT_IS("https://cdn.example/i.png", ResolveUrl(page, "//cdn.example/i.png"));
  TEXT_IS("http://b.example/i.png", ResolveUrl(page, "http://b.example/i.png"));
  TEXT_IS("http://127.0.0.1:8080/favicon.ico", ResolveUrl("http://127.0.0.1:8080", "/favicon.ico"));
  TEXT_IS("http://127.0.0.1/i.png", ResolveUrl("http://127.0.0.1", "i.png"));
  TEST_ASSERT_TRUE(ResolveUrl(page, "data:image/png;base64,AAAA").empty());
  TEST_ASSERT_TRUE(ResolveUrl(page, "javascript:alert(1)").empty());
  TEST_ASSERT_TRUE(ResolveUrl(page, "").empty());
  TEST_ASSERT_TRUE(ResolveUrl(page, "a b.png").empty());
  return 0;
}

int test_valid_values() {
  TEST_ASSERT_TRUE(ValidTileId("WEBT00001"));
  TEST_ASSERT_TRUE(!ValidTileId("WEBT0001"));
  TEST_ASSERT_TRUE(!ValidTileId("../../etc"));
  TEST_ASSERT_TRUE(!ValidTileId("PPSA00001"));
  TEST_ASSERT_TRUE(ValidPageUrl("http://127.0.0.1:8000/"));
  TEST_ASSERT_TRUE(!ValidPageUrl("file:///etc/passwd"));
  TEST_ASSERT_TRUE(!ValidPageUrl("http://a b/"));
  TEST_ASSERT_TRUE(!ValidPageUrl("http://a/" + std::string(kMaxUrl, 'x')));
  return 0;
}

int test_sniffs_formats() {
  const auto sniff = [](const std::string &text) {
    return Sniff(reinterpret_cast<const uint8_t *>(text.data()), text.size());
  };
  TEST_ASSERT_TRUE(sniff("\x89PNG\r\n\x1a\nrest") == Format::Png);
  TEST_ASSERT_TRUE(sniff("\xFF\xD8\xFFrest") == Format::Jpeg);
  TEST_ASSERT_TRUE(sniff("GIF89arest") == Format::Gif);
  TEST_ASSERT_TRUE(sniff("RIFF0000WEBPrest") == Format::WebP);
  TEST_ASSERT_TRUE(sniff(std::string("\0\0\1\0\1\0", 6)) == Format::Ico);
  TEST_ASSERT_TRUE(sniff(std::string("\0\0\1\0\0\0", 6)) == Format::Unknown);
  TEST_ASSERT_TRUE(sniff("  <svg xmlns=''/>") == Format::Svg);
  TEST_ASSERT_TRUE(sniff("<?xml version='1.0'?><svg/>") == Format::Svg);
  TEST_ASSERT_TRUE(sniff("<?xml version='1.0'?><rss/>") == Format::Unknown);
  TEST_ASSERT_TRUE(sniff("<!doctype html>") == Format::Unknown);
  TEST_ASSERT_TRUE(sniff("") == Format::Unknown);
  return 0;
}

int test_encodes_png() {
  const Bytes rgba(8 * 8 * 4, 0x7f);
  const Bytes png = EncodePng(8, 8, rgba);
  TEST_ASSERT_TRUE(Sniff(png.data(), png.size()) == Format::Png);
  TEST_ASSERT_EQ_U64(8u, be32(png, 16));
  TEST_ASSERT_EQ_U64(8u, be32(png, 20));
  TEST_ASSERT_TRUE(png_pixels(png, 8) == rgba);
  TEST_ASSERT_TRUE(EncodePng(8, 8, Bytes(3)).empty());
  return 0;
}

int test_an_ico_with_a_png_gives_it_back() {
  const Bytes small = EncodePng(1, 1, Bytes(4, 1));
  const Bytes big = EncodePng(2, 2, Bytes(16, 2));
  Bytes ico = {0, 0, 1, 0, 2, 0};
  const size_t data = 6 + 2 * 16;
  ico.insert(ico.end(), {16, 16, 0, 0, 1, 0, 32, 0});
  put_le(ico, static_cast<uint32_t>(small.size()), 4);
  put_le(ico, static_cast<uint32_t>(data), 4);
  ico.insert(ico.end(), {64, 64, 0, 0, 1, 0, 32, 0});
  put_le(ico, static_cast<uint32_t>(big.size()), 4);
  put_le(ico, static_cast<uint32_t>(data + small.size()), 4);
  ico.insert(ico.end(), small.begin(), small.end());
  ico.insert(ico.end(), big.begin(), big.end());
  TEST_ASSERT_TRUE(IcoToPng(ico.data(), ico.size()) == big);
  return 0;
}

int test_a_bitmap_ico_turns_into_png() {
  /* 2x2, 32-bit, bottom row first: blue, green over red, opaque white */
  const Bytes colours = {0, 0, 255, 255,     255, 255, 255, 255,
                         255, 0, 0, 128,     0, 255, 0, 255};
  const Bytes ico = bmp_ico(2, 32, {}, colours, Bytes(8));
  const Bytes png = IcoToPng(ico.data(), ico.size());
  TEST_ASSERT_TRUE(Sniff(png.data(), png.size()) == Format::Png);
  const Bytes expected = {0, 0, 255, 128,     0, 255, 0, 255,
                          255, 0, 0, 255,     255, 255, 255, 255};
  TEST_ASSERT_TRUE(png_pixels(png, 2) == expected);
  return 0;
}

int test_an_indexed_ico_takes_its_mask() {
  const Bytes palette = {0, 0, 0, 0, 255, 255, 255, 0};
  /* 2x2, 1-bit: rows of 4 bytes, bottom first */
  const Bytes colours = {0x40, 0, 0, 0, 0x80, 0, 0, 0};
  const Bytes mask = {0x80, 0, 0, 0, 0, 0, 0, 0};
  const Bytes ico = bmp_ico(2, 1, palette, colours, mask);
  const Bytes png = IcoToPng(ico.data(), ico.size());
  const Bytes expected = {255, 255, 255, 255, 0, 0, 0, 255,
                          0, 0, 0, 0,         255, 255, 255, 255};
  TEST_ASSERT_TRUE(png_pixels(png, 2) == expected);
  return 0;
}

int test_wild_icos_stay_in_bounds() {
  const Bytes good = bmp_ico(2, 32, {}, Bytes(16, 9), Bytes(8));
  /* every prefix of a good file */
  for (size_t size = 0; size < good.size(); ++size)
    (void)IcoToPng(good.data(), size);
  /* an entry reaching past the end */
  Bytes bad = good;
  bad[14] = 0xff;
  bad[15] = 0xff;
  bad[16] = 0xff;
  bad[17] = 0x7f;
  TEST_ASSERT_TRUE(IcoToPng(bad.data(), bad.size()).empty());
  /* a directory longer than the file */
  bad = good;
  bad[4] = 0xff;
  bad[5] = 0xff;
  TEST_ASSERT_TRUE(IcoToPng(bad.data(), bad.size()).empty());
  /* a size the file cannot hold */
  bad = good;
  bad[22 + 5] = 0x7f;
  TEST_ASSERT_TRUE(IcoToPng(bad.data(), bad.size()).empty());
  /* a colour index past the palette */
  const Bytes palette(4, 0);
  Bytes indexed = bmp_ico(2, 1, palette, Bytes(8, 0xff), Bytes(8));
  indexed[22 + 32] = 1;
  TEST_ASSERT_TRUE(IcoToPng(indexed.data(), indexed.size()).empty());
  return 0;
}

int test_the_letter_of_a_plain_tile() {
  TEST_ASSERT_EQ_U64('E', TileLetter("  example page", "http://b.org/"));
  TEST_ASSERT_EQ_U64('3', TileLetter("3D Viewer", "http://b.org/"));
  TEST_ASSERT_EQ_U64('B', TileLetter("\xE6\x97\xA5\xE6\x9C\xAC",
                                     "https://www.b.org:8/x"));
  TEST_ASSERT_EQ_U64('1', TileLetter("", "http://127.0.0.1:8000/"));
  TEST_ASSERT_EQ_U64('?', TileLetter("", ""));
  const Bytes tile = LetterTile('A', "http://b.org/x");
  TEST_ASSERT_TRUE(Sniff(tile.data(), tile.size()) == Format::Png);
  TEST_ASSERT_EQ_U64(static_cast<uint64_t>(kTileSize), be32(tile, 16));
  TEST_ASSERT_TRUE(LetterTile('A', "http://b.org/y") == tile);
  TEST_ASSERT_TRUE(!LetterTile('?', "http://b.org/").empty());
  return 0;
}

int test_only_icon_sized_pictures_fit() {
  const auto fits = [](const std::string &bytes, Format format) {
    return FitsTile(reinterpret_cast<const uint8_t *>(bytes.data()),
                    bytes.size(), format);
  };
  std::string png("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16);
  TEST_ASSERT_TRUE(fits(png + std::string("\0\0\0\x20\0\0\0\x20", 8),
                        Format::Png));
  TEST_ASSERT_TRUE(!fits(png + std::string("\0\0\x10\0\0\0\x10\0", 8),
                         Format::Png));
  TEST_ASSERT_TRUE(!fits(png, Format::Png));
  TEST_ASSERT_TRUE(fits(std::string("GIF89a\x20\0\x20\0", 10), Format::Gif));
  TEST_ASSERT_TRUE(!fits(std::string("GIF89a\0\x10\0\x10", 10), Format::Gif));
  const std::string jpeg("\xFF\xD8\xFF\xE0\0\x02\xFF\xC0\0\x11\x08", 11);
  TEST_ASSERT_TRUE(fits(jpeg + std::string("\0\x40\0\x40", 4), Format::Jpeg));
  TEST_ASSERT_TRUE(!fits(jpeg + std::string("\x10\0\x10\0", 4), Format::Jpeg));
  std::string webp("RIFF\0\0\0\0WEBPVP8X\x0a\0\0\0\0\0\0\0", 24);
  TEST_ASSERT_TRUE(fits(webp + std::string("\x1f\0\0\x1f\0\0", 6),
                        Format::WebP));
  TEST_ASSERT_TRUE(!fits(webp + std::string("\0\x20\0\0\x20\0", 6),
                         Format::WebP));
  TEST_ASSERT_TRUE(!fits("<svg/>", Format::Svg));
  return 0;
}

} // namespace

extern "C" int test_web_icon_suite(void) {
  int failures = 0;
  failures += onion_test_run("web_icon.finds_the_best_icon",
                             test_finds_the_best_icon);
  failures += onion_test_run("web_icon.odd_html_is_harmless",
                             test_odd_html_is_harmless);
  failures += onion_test_run("web_icon.resolves_links", test_resolves_links);
  failures += onion_test_run("web_icon.valid_values", test_valid_values);
  failures += onion_test_run("web_icon.sniffs_formats", test_sniffs_formats);
  failures += onion_test_run("web_icon.encodes_png", test_encodes_png);
  failures += onion_test_run("web_icon.an_ico_with_a_png_gives_it_back",
                             test_an_ico_with_a_png_gives_it_back);
  failures += onion_test_run("web_icon.a_bitmap_ico_turns_into_png",
                             test_a_bitmap_ico_turns_into_png);
  failures += onion_test_run("web_icon.an_indexed_ico_takes_its_mask",
                             test_an_indexed_ico_takes_its_mask);
  failures += onion_test_run("web_icon.wild_icos_stay_in_bounds",
                             test_wild_icos_stay_in_bounds);
  failures += onion_test_run("web_icon.only_icon_sized_pictures_fit",
                             test_only_icon_sized_pictures_fit);
  failures += onion_test_run("web_icon.the_letter_of_a_plain_tile",
                             test_the_letter_of_a_plain_tile);
  return failures;
}
