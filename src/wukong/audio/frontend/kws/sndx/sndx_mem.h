#ifndef __SNDX_MEM_H__
#define __SNDX_MEM_H__

#ifdef __cplusplus
extern "C" {
#endif

void *sndx_malloc(size_t size);
void *sndx_realloc(void *ptr, size_t size);
void *sndx_calloc(size_t cnt, size_t size);
void sndx_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
