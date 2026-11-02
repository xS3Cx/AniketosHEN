#ifndef ANIKETOS_SHELL_AVATAR_CODEC_H
#define ANIKETOS_SHELL_AVATAR_CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

int avatar_codec_should_fit(const char *path);
int avatar_codec_cache_valid(const char *source, const char *cache);
int avatar_codec_fit(const char *source, const char *cache, char *error,
                     unsigned long error_size);

#ifdef __cplusplus
}
#endif

#endif
