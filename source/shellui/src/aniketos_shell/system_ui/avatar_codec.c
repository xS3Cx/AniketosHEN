#include "aniketos_shell/system_ui/avatar_codec.h"

#include <onion/fs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR_NO_SIMD
#include "stb_image_resize2.h"

#define MSF_GIF_IMPL
#include "msf_gif.h"

enum {
  AVATAR_CODEC_MAX_INPUT = 8 * 1024 * 1024,
  AVATAR_CODEC_MAX_FRAMES = 120,
  AVATAR_CODEC_MAX_BYTES = 110 * 1024,
  AVATAR_CODEC_MAX_DIMENSION = 4096,
  AVATAR_CODEC_MAX_PIXELS = 16 * 1024 * 1024,
  AVATAR_CODEC_PATH_SIZE = 4096
};

static void set_error(char *error, unsigned long error_size, const char *text) {
  if (!error || error_size == 0)
    return;
  (void)snprintf(error, (size_t)error_size, "%s", text);
}

static int read_file(const char *path, unsigned char **out, size_t *out_size) {
  FILE *file = fopen(path, "rb");
  long length = 0;
  unsigned char *data = NULL;
  if (!file)
    return -1;
  if (fseek(file, 0, SEEK_END) == 0)
    length = ftell(file);
  if (length > 0 && length <= AVATAR_CODEC_MAX_INPUT &&
      fseek(file, 0, SEEK_SET) == 0)
    data = (unsigned char *)malloc((size_t)length);
  if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
    free(data);
    data = NULL;
  }
  fclose(file);
  if (!data)
    return -1;
  *out = data;
  *out_size = (size_t)length;
  return 0;
}

static int file_info(const char *path, long long *size, long long *mtime) {
  struct stat info;
  if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
    return -1;
  if (size)
    *size = (long long)info.st_size;
  if (mtime)
    *mtime = (long long)info.st_mtime;
  return 0;
}

static int meta_path_of(const char *cache, char *path) {
  return snprintf(path, AVATAR_CODEC_PATH_SIZE, "%s.meta", cache) <
         AVATAR_CODEC_PATH_SIZE;
}

static int meta_matches(const char *cache, long long source_size,
                        long long source_mtime) {
  char meta_path[AVATAR_CODEC_PATH_SIZE];
  char tag[32];
  long long size = 0;
  long long mtime = 0;
  FILE *file = NULL;
  int count = 0;
  if (!meta_path_of(cache, meta_path))
    return 0;
  file = fopen(meta_path, "r");
  if (!file)
    return 0;
  count = fscanf(file, "%31s %lld %lld", tag, &size, &mtime);
  fclose(file);
  return count == 3 && strcmp(tag, "aniketos-avatar") == 0 &&
         size == source_size && mtime == source_mtime;
}

int avatar_codec_should_fit(const char *path) {
  unsigned char header[10];
  unsigned width = 0;
  unsigned height = 0;
  long long size = 0;
  size_t got = 0;
  FILE *file = NULL;
  if (!path)
    return 0;
  if (file_info(path, &size, NULL) != 0)
    return 0;
  file = fopen(path, "rb");
  if (!file)
    return 0;
  got = fread(header, 1, sizeof(header), file);
  fclose(file);
  if (got < sizeof(header))
    return 0;
  if (memcmp(header, "GIF87a", 6) != 0 && memcmp(header, "GIF89a", 6) != 0)
    return 0;
  width = (unsigned)header[6] | ((unsigned)header[7] << 8);
  height = (unsigned)header[8] | ((unsigned)header[9] << 8);
  if (width > 256 || height > 256)
    return 1;
  return size > 140000;
}

int avatar_codec_cache_valid(const char *source, const char *cache) {
  long long cache_size = 0;
  long long cache_mtime = 0;
  long long source_size = 0;
  long long source_mtime = 0;
  if (!source || !cache)
    return 0;
  if (file_info(cache, &cache_size, &cache_mtime) != 0 || cache_size <= 0)
    return 0;
  if (file_info(source, &source_size, &source_mtime) != 0)
    return 0;
  if (cache_mtime < source_mtime)
    return 0;
  return meta_matches(cache, source_size, source_mtime);
}

static MsfGifResult encode_target(const unsigned char *frames, int width,
                                  int height, int sample_count,
                                  const int *sample_index,
                                  const int *sample_delay, int target) {
  const int side = width < height ? width : height;
  const int crop_x = (width - side) / 2;
  const int crop_y = (height - side) / 2;
  const size_t row_bytes = (size_t)side * 4u;
  unsigned char *crop = (unsigned char *)malloc(row_bytes * (size_t)side);
  unsigned char *resized =
      (unsigned char *)malloc((size_t)target * (size_t)target * 4u);
  MsfGifState state = {0};
  MsfGifResult result = {0};
  int ok = 1;
  msf_gif_alpha_threshold = 128;
  if (!crop || !resized || !msf_gif_begin(&state, target, target)) {
    free(crop);
    free(resized);
    return result;
  }
  for (int i = 0; ok && i < sample_count; ++i) {
    const unsigned char *frame =
        frames + (size_t)sample_index[i] * (size_t)width * (size_t)height * 4u;
    for (int y = 0; y < side; ++y) {
      memcpy(crop + (size_t)y * row_bytes,
             frame +
                 ((size_t)(crop_y + y) * (size_t)width + (size_t)crop_x) * 4u,
             row_bytes);
    }
    ok = stbir_resize_uint8_srgb(crop, side, side, 0, resized, target, target,
                                 0, STBIR_RGBA) &&
         msf_gif_frame(&state, resized, sample_delay[i], 8, 0);
  }
  free(crop);
  free(resized);
  result = msf_gif_end(&state);
  if (!ok || !result.data || result.dataSize == 0) {
    msf_gif_free(result);
    result = (MsfGifResult){0};
  }
  return result;
}

/* The largest size that stays under the limit; no data when none does. */
static MsfGifResult encode_within_limit(const unsigned char *frames, int width,
                                        int height, int sample_count,
                                        const int *sample_index,
                                        const int *sample_delay) {
  static const int targets[] = {256, 192, 128};
  MsfGifResult encoded = {0};
  for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
    encoded = encode_target(frames, width, height, sample_count, sample_index,
                            sample_delay, targets[i]);
    if (encoded.dataSize <= AVATAR_CODEC_MAX_BYTES && encoded.data)
      return encoded;
    msf_gif_free(encoded);
  }
  return (MsfGifResult){0};
}

static bool save_file(const char *path, const void *data, size_t size) {
  if (!write_file_atomic(path, data, size))
    return false;
  chmod(path, 0644); /* write_file_atomic leaves 0777 */
  return true;
}

static int write_meta(const char *source, const char *cache) {
  char meta_path[AVATAR_CODEC_PATH_SIZE];
  char text[64];
  long long size = 0;
  long long mtime = 0;
  if (file_info(source, &size, &mtime) != 0 || !meta_path_of(cache, meta_path))
    return -1;
  const int length =
      snprintf(text, sizeof(text), "aniketos-avatar %lld %lld\n", size, mtime);
  return save_file(meta_path, text, (size_t)length) ? 0 : -1;
}

/* Spreads the frames over at most AVATAR_CODEC_MAX_FRAMES samples, each
   lasting as long as the frames it stands for. */
static int sample_frames(const int *delays, int frames, int *sample_index,
                         int *sample_delay) {
  const int sample_count =
      frames < AVATAR_CODEC_MAX_FRAMES ? frames : AVATAR_CODEC_MAX_FRAMES;
  for (int i = 0; i < sample_count; ++i) {
    const int begin = (int)((long long)i * frames / sample_count);
    const int end = (int)((long long)(i + 1) * frames / sample_count);
    long long total = 0;
    int centiseconds = 0;
    for (int j = begin; j < end; ++j)
      total += delays[j];
    centiseconds = (int)(total / 10);
    if (centiseconds < 2)
      centiseconds = 2;
    if (centiseconds > 65535)
      centiseconds = 65535;
    sample_index[i] = begin;
    sample_delay[i] = centiseconds;
  }
  return sample_count;
}

int avatar_codec_fit(const char *source, const char *cache, char *error,
                     unsigned long error_size) {
  unsigned char *data = NULL;
  unsigned char *pixels = NULL;
  int *delays = NULL;
  int sample_index[AVATAR_CODEC_MAX_FRAMES];
  int sample_delay[AVATAR_CODEC_MAX_FRAMES];
  MsfGifResult encoded;
  size_t data_size = 0;
  int width = 0;
  int height = 0;
  int frames = 0;
  int comp = 0;
  int info_width = 0;
  int info_height = 0;
  int info_comp = 0;
  int sample_count = 0;
  int result = -1;
  if (!source || !cache) {
    set_error(error, error_size, "invalid arguments");
    return -1;
  }
  if (read_file(source, &data, &data_size) != 0) {
    set_error(error, error_size, "read failed");
    return -1;
  }
  if (!stbi_info_from_memory(data, (int)data_size, &info_width, &info_height,
                             &info_comp) ||
      info_width <= 0 || info_height <= 0 ||
      info_width > AVATAR_CODEC_MAX_DIMENSION ||
      info_height > AVATAR_CODEC_MAX_DIMENSION ||
      (long long)info_width * (long long)info_height >
          AVATAR_CODEC_MAX_PIXELS) {
    free(data);
    set_error(error, error_size, "dimensions rejected");
    return -1;
  }
  if (!avatar_codec_decode_fits(data, data_size)) {
    free(data);
    set_error(error, error_size, "too many frames");
    return -1;
  }
  pixels = stbi_load_gif_from_memory(data, (int)data_size, &delays, &width,
                                     &height, &frames, &comp, 4);
  free(data);
  if (!pixels || !delays || frames <= 0 || width <= 0 || height <= 0) {
    stbi_image_free(pixels);
    free(delays);
    set_error(error, error_size, "decode failed");
    return -1;
  }
  sample_count = sample_frames(delays, frames, sample_index, sample_delay);
  encoded = encode_within_limit(pixels, width, height, sample_count,
                                sample_index, sample_delay);
  if (!encoded.data) {
    set_error(error, error_size, "output too large");
  } else if (!save_file(cache, encoded.data, encoded.dataSize)) {
    set_error(error, error_size, "write failed");
  } else if (write_meta(source, cache) != 0) {
    remove(cache);
    set_error(error, error_size, "meta failed");
  } else {
    result = 0;
  }
  msf_gif_free(encoded);
  stbi_image_free(pixels);
  free(delays);
  return result;
}
