/* Host tests for reading an avatar file's format and size (no PS5/Mono). */
#include "test_harness.h"

#include "aniketos_shell/system_ui/avatar_image.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace AvatarImage;

namespace {

using Bytes = std::vector<uint8_t>;

void put(Bytes &out, const std::string &text) {
  out.insert(out.end(), text.begin(), text.end());
}

void put_le(Bytes &out, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i)
    out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

/* A GIF with a 4-colour global table, one comment and `frames` frames, each
   with a 2-colour local table when `local_table`. */
Bytes gif(int width, int height, int frames, bool local_table = false) {
  Bytes out;
  put(out, "GIF89a");
  put_le(out, width, 2);
  put_le(out, height, 2);
  out.push_back(0x81); /* global table of 4 entries */
  out.push_back(0);
  out.push_back(0);
  out.insert(out.end(), 12, 0);
  out.insert(out.end(), {0x21, 0xFE, 2, 'h', 'i', 0});
  for (int frame = 0; frame < frames; ++frame) {
    out.push_back(0x2C);
    out.insert(out.end(), 8, 0);
    out.push_back(local_table ? 0x80 : 0);
    if (local_table)
      out.insert(out.end(), 6, 0);
    out.insert(out.end(), {2, 3, 1, 2, 3, 0});
  }
  out.push_back(0x3B);
  return out;
}

void chunk(Bytes &out, const char *name, const Bytes &payload) {
  put(out, name);
  put_le(out, static_cast<uint32_t>(payload.size()), 4);
  out.insert(out.end(), payload.begin(), payload.end());
  if (payload.size() & 1)
    out.push_back(0);
}

Bytes webp(int width, int height, int frames) {
  Bytes body;
  put(body, "WEBP");
  Bytes header(4, 0);
  put_le(header, width - 1, 3);
  put_le(header, height - 1, 3);
  chunk(body, "VP8X", header);
  chunk(body, "ANIM", Bytes(6, 0));
  for (int frame = 0; frame < frames; ++frame)
    chunk(body, "ANMF", Bytes(17, 0));
  Bytes out;
  put(out, "RIFF");
  put_le(out, static_cast<uint32_t>(body.size()), 4);
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

Info inspect(const Bytes &bytes) {
  return Inspect(bytes.data(), bytes.size());
}


int test_gif_size_and_frames() {
  const Info info = inspect(gif(256, 128, 90));
  TEST_ASSERT_TRUE(info.format == Format::Gif);
  TEST_ASSERT_EQ_INT(256, info.width);
  TEST_ASSERT_EQ_INT(128, info.height);
  TEST_ASSERT_EQ_INT(90, info.frames);
  TEST_ASSERT_TRUE(Check(info) == Problem::None);
  return 0;
}

int test_gif_local_colour_tables_are_skipped() {
  TEST_ASSERT_EQ_INT(7, inspect(gif(16, 16, 7, true)).frames);
  return 0;
}

int test_gif_at_the_limits_is_fine_and_past_them_is_not() {
  TEST_ASSERT_TRUE(Check(inspect(gif(300, 300, 150))) == Problem::None);
  TEST_ASSERT_TRUE(Check(inspect(gif(301, 300, 2))) == Problem::TooLarge);
  TEST_ASSERT_TRUE(Check(inspect(gif(300, 300, 151))) == Problem::TooManyFrames);
  return 0;
}

int test_a_still_gif_has_no_size_limit() {
  TEST_ASSERT_TRUE(Check(inspect(gif(1920, 1080, 1))) == Problem::None);
  return 0;
}

int test_a_cut_gif_keeps_the_frames_it_has() {
  Bytes bytes = gif(32, 32, 5);
  bytes.resize(bytes.size() - 8);
  TEST_ASSERT_EQ_INT(4, inspect(bytes).frames);
  return 0;
}

int test_a_gif_without_frames_is_no_picture() {
  Bytes bytes = gif(32, 32, 0);
  bytes.resize(13);
  TEST_ASSERT_TRUE(Check(inspect(bytes)) == Problem::NotPicture);
  return 0;
}

int test_a_wild_block_length_stays_in_bounds() {
  Bytes bytes = gif(32, 32, 1);
  bytes[bytes.size() - 6] = 0xFF;
  (void)inspect(bytes);
  bytes[10] = 0x87; /* colour table past the end */
  TEST_ASSERT_TRUE(Check(inspect(bytes)) == Problem::NotPicture);
  return 0;
}

int test_webp_size_and_frames() {
  const Info info = inspect(webp(498, 498, 40));
  TEST_ASSERT_TRUE(info.format == Format::WebP);
  TEST_ASSERT_EQ_INT(498, info.width);
  TEST_ASSERT_EQ_INT(498, info.height);
  TEST_ASSERT_EQ_INT(40, info.frames);
  TEST_ASSERT_TRUE(Check(info) == Problem::TooLarge);
  return 0;
}

int test_a_wild_webp_chunk_length_stays_in_bounds() {
  Bytes bytes = webp(16, 16, 2);
  bytes[16] = bytes[17] = bytes[18] = bytes[19] = 0xFF;
  TEST_ASSERT_TRUE(inspect(bytes).format == Format::WebP);
  return 0;
}

int test_formats_by_their_bytes() {
  const Bytes png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  const Bytes jpeg = {0xFF, 0xD8, 0xFF, 0xE0};
  const Bytes text = {'<', 'h', 't', 'm', 'l', '>', ' ', ' ', ' ', ' '};
  TEST_ASSERT_TRUE(inspect(png).format == Format::Png);
  TEST_ASSERT_TRUE(inspect(jpeg).format == Format::Jpeg);
  TEST_ASSERT_TRUE(Check(inspect(text)) == Problem::NotPicture);
  TEST_ASSERT_TRUE(Check(inspect({})) == Problem::NotPicture);
  return 0;
}

} // namespace

extern "C" int test_avatar_image_suite(void) {
  int failures = 0;
  failures += onion_test_run("avatar_image.gif_size_and_frames",
                             test_gif_size_and_frames);
  failures += onion_test_run("avatar_image.gif_local_colour_tables_are_skipped",
                             test_gif_local_colour_tables_are_skipped);
  failures += onion_test_run("avatar_image.gif_at_the_limits_is_fine_and_past_them_is_not",
                             test_gif_at_the_limits_is_fine_and_past_them_is_not);
  failures += onion_test_run("avatar_image.a_still_gif_has_no_size_limit",
                             test_a_still_gif_has_no_size_limit);
  failures += onion_test_run("avatar_image.a_cut_gif_keeps_the_frames_it_has",
                             test_a_cut_gif_keeps_the_frames_it_has);
  failures += onion_test_run("avatar_image.a_gif_without_frames_is_no_picture",
                             test_a_gif_without_frames_is_no_picture);
  failures += onion_test_run("avatar_image.a_wild_block_length_stays_in_bounds",
                             test_a_wild_block_length_stays_in_bounds);
  failures += onion_test_run("avatar_image.webp_size_and_frames",
                             test_webp_size_and_frames);
  failures += onion_test_run("avatar_image.a_wild_webp_chunk_length_stays_in_bounds",
                             test_a_wild_webp_chunk_length_stays_in_bounds);
  failures += onion_test_run("avatar_image.formats_by_their_bytes",
                             test_formats_by_their_bytes);
  return failures;
}
