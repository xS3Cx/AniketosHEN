#ifndef ANIKETOS_SHELL_AVATAR_CODEC_H
#define ANIKETOS_SHELL_AVATAR_CODEC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int avatar_codec_should_fit(const char *path);
int avatar_codec_cache_valid(const char *source, const char *cache);
int avatar_codec_fit(const char *source, const char *cache, char *error,
                     unsigned long error_size);
/* Whether the GIF in data is small enough to decode whole. In
   account_avatar.cpp, as the check is C++ and this file is not. */
int avatar_codec_decode_fits(const unsigned char *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
