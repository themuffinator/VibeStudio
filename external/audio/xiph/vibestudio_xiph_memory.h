/* VibeStudio integration adapter. Force-include only into the private Xiph
 * C targets, after declaring the upstream types and before other library code.
 * All original source files and their BSD notices remain unchanged. */
#ifndef VIBESTUDIO_XIPH_MEMORY_H
#define VIBESTUDIO_XIPH_MEMORY_H
#include <stddef.h>
#include <ogg/os_types.h>
#ifdef __cplusplus
extern "C" {
#endif
void* vibestudio_xiph_malloc(size_t size);
void* vibestudio_xiph_calloc(size_t count, size_t size);
void* vibestudio_xiph_realloc(void* data, size_t size);
void vibestudio_xiph_free(void* data);
#ifdef __cplusplus
}
#endif
#undef _ogg_malloc
#undef _ogg_calloc
#undef _ogg_realloc
#undef _ogg_free
#define _ogg_malloc vibestudio_xiph_malloc
#define _ogg_calloc vibestudio_xiph_calloc
#define _ogg_realloc vibestudio_xiph_realloc
#define _ogg_free vibestudio_xiph_free
#endif
