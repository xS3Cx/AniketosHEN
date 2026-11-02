#include "aniketos_shell/avatar_codec.h"

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
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return -1;
  }
  length = ftell(file);
  if (length <= 0 || length > AVATAR_CODEC_MAX_INPUT) {
    fclose(file);
    return -1;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return -1;
  }
  data = (unsigned char *)malloc((size_t)length);
  if (!data) {
    fclose(file);
    return -1;
  }
  if (fread(data, 1, (size_t)length, file) != (size_t)length) {
    free(data);
    fclose(file);
    return -1;
  }
  fclose(file);
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

static int meta_matches(const char *source, const char *cache) {
  char meta_path[AVATAR_CODEC_PATH_SIZE];
  char tag[32];
  long long size = 0;
  long long mtime = 0;
  long long source_size = 0;
  long long source_mtime = 0;
  FILE *file = NULL;
  int count = 0;
  if (snprintf(meta_path, sizeof(meta_path), "%s.meta", cache) >=
      (int)sizeof(meta_path))
    return 0;
  file = fopen(meta_path, "r");
  if (!file)
    return 0;
  count = fscanf(file, "%31s %lld %lld", tag, &size, &mtime);
  fclose(file);
  if (count != 3 || strcmp(tag, "aniketos-avatar") != 0)
    return 0;
  if (file_info(source, &source_size, &source_mtime) != 0)
    return 0;
  return size == source_size && mtime == source_mtime;
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
  return meta_matches(source, cache);
}

static int encode_target(const unsigned char *frames, int width, int height,
                         int sample_count, const int *sample_index,
                         const int *sample_delay, int target,
                         unsigned char **out_data, size_t *out_size) {
  const int side = width < height ? width : height;
  const int crop_x = (width - side) / 2;
  const int crop_y = (height - side) / 2;
  const size_t row_bytes = (size_t)side * 4u;
  unsigned char *crop = (unsigned char *)malloc(row_bytes * (size_t)side);
  unsigned char *resized =
      (unsigned char *)malloc((size_t)target * (size_t)target * 4u);
  MsfGifState state;
  MsfGifResult result;
  if (!crop || !resized) {
    free(crop);
    free(resized);
    return -1;
  }
  memset(&state, 0, sizeof(state));
  msf_gif_alpha_threshold = 128;
  if (!msf_gif_begin(&state, target, target)) {
    free(crop);
    free(resized);
    return -1;
  }
  for (int i = 0; i < sample_count; ++i) {
    const unsigned char *frame =
        frames + (size_t)sample_index[i] * (size_t)width * (size_t)height * 4u;
    for (int y = 0; y < side; ++y) {
      memcpy(crop + (size_t)y * row_bytes,
             frame +
                 ((size_t)(crop_y + y) * (size_t)width + (size_t)crop_x) * 4u,
             row_bytes);
    }
    if (!stbir_resize_uint8_srgb(crop, side, side, 0, resized, target, target,
                                 0, STBIR_RGBA)) {
      msf_gif_free(msf_gif_end(&state));
      free(crop);
      free(resized);
      return -1;
    }
    if (!msf_gif_frame(&state, resized, sample_delay[i], 8, 0)) {
      msf_gif_free(msf_gif_end(&state));
      free(crop);
      free(resized);
      return -1;
    }
  }
  free(crop);
  free(resized);
  result = msf_gif_end(&state);
  if (!result.data || result.dataSize == 0) {
    msf_gif_free(result);
    return -1;
  }
  *out_data = (unsigned char *)result.data;
  *out_size = result.dataSize;
  return 0;
}

static int write_atomic(const char *path, const unsigned char *data,
                        size_t size) {
  char tmp_path[AVATAR_CODEC_PATH_SIZE];
  FILE *file = NULL;
  int ok = 1;
  if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) >=
      (int)sizeof(tmp_path))
    return -1;
  file = fopen(tmp_path, "wb");
  if (!file)
    return -1;
  if (fwrite(data, 1, size, file) != size)
    ok = 0;
  if (fflush(file) != 0)
    ok = 0;
  if (fclose(file) != 0)
    ok = 0;
  if (!ok) {
    remove(tmp_path);
    return -1;
  }
  if (rename(tmp_path, path) != 0) {
    remove(tmp_path);
    return -1;
  }
  return 0;
}

static int write_meta(const char *source, const char *cache) {
  char meta_path[AVATAR_CODEC_PATH_SIZE];
  char tmp_path[AVATAR_CODEC_PATH_SIZE];
  long long size = 0;
  long long mtime = 0;
  FILE *file = NULL;
  int ok = 1;
  if (file_info(source, &size, &mtime) != 0)
    return -1;
  if (snprintf(meta_path, sizeof(meta_path), "%s.meta", cache) >=
      (int)sizeof(meta_path))
    return -1;
  if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", meta_path) >=
      (int)sizeof(tmp_path))
    return -1;
  file = fopen(tmp_path, "wb");
  if (!file)
    return -1;
  if (fprintf(file, "aniketos-avatar %lld %lld\n", size, mtime) <= 0)
    ok = 0;
  if (fflush(file) != 0)
    ok = 0;
  if (fclose(file) != 0)
    ok = 0;
  if (!ok) {
    remove(tmp_path);
    return -1;
  }
  if (rename(tmp_path, meta_path) != 0) {
    remove(tmp_path);
    return -1;
  }
  return 0;
}

int avatar_codec_fit(const char *source, const char *cache, char *error,
                     unsigned long error_size) {
  static const int targets[] = {256, 192, 128};
  unsigned char *data = NULL;
  unsigned char *pixels = NULL;
  unsigned char *encoded = NULL;
  unsigned char *chosen_data = NULL;
  int *delays = NULL;
  int *sample_index = NULL;
  int *sample_delay = NULL;
  size_t data_size = 0;
  size_t encoded_size = 0;
  size_t chosen_size = 0;
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
  pixels = stbi_load_gif_from_memory(data, (int)data_size, &delays, &width,
                                     &height, &frames, &comp, 4);
  free(data);
  data = NULL;
  if (!pixels || !delays || frames <= 0 || width <= 0 || height <= 0) {
    stbi_image_free(pixels);
    free(delays);
    set_error(error, error_size, "decode failed");
    return -1;
  }
  sample_count = frames < AVATAR_CODEC_MAX_FRAMES ? frames
                                                  : AVATAR_CODEC_MAX_FRAMES;
  sample_index = (int *)malloc((size_t)sample_count * sizeof(int));
  sample_delay = (int *)malloc((size_t)sample_count * sizeof(int));
  if (!sample_index || !sample_delay) {
    set_error(error, error_size, "out of memory");
    goto cleanup;
  }
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
  for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
    if (encode_target(pixels, width, height, sample_count, sample_index,
                      sample_delay, targets[i], &encoded, &encoded_size) != 0) {
      encoded = NULL;
      continue;
    }
    if (encoded_size <= AVATAR_CODEC_MAX_BYTES) {
      chosen_data = encoded;
      chosen_size = encoded_size;
      encoded = NULL;
      break;
    }
    msf_gif_free((MsfGifResult){encoded, encoded_size, encoded_size, NULL});
    encoded = NULL;
  }
  if (!chosen_data) {
    set_error(error, error_size, "output too large");
    goto cleanup;
  }
  if (write_atomic(cache, chosen_data, chosen_size) != 0) {
    set_error(error, error_size, "write failed");
    goto cleanup;
  }
  if (write_meta(source, cache) != 0) {
    remove(cache);
    set_error(error, error_size, "meta failed");
    goto cleanup;
  }
  result = 0;
cleanup:
  if (chosen_data)
    msf_gif_free(
        (MsfGifResult){chosen_data, chosen_size, chosen_size, NULL});
  if (encoded)
    msf_gif_free((MsfGifResult){encoded, encoded_size, encoded_size, NULL});
  stbi_image_free(pixels);
  free(delays);
  free(sample_index);
  free(sample_delay);
  return result;
}
