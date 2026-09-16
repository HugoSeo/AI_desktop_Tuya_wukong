#include "tkl_memory.h"
#include "sndx_mem.h"

#define SNDX_USE_PSRAM

#ifdef SNDX_USE_PSRAM

void *sndx_malloc(size_t size)
{
	return tkl_system_psram_malloc(size);
}

void *sndx_realloc(void *ptr, size_t size)
{
	return tkl_system_psram_realloc(ptr, size);
}

void *sndx_calloc(size_t cnt, size_t size)
{
	return tkl_system_psram_calloc(cnt, size);
}

void sndx_free(void *ptr)
{
	tkl_system_psram_free(ptr);
}

#else

void *sndx_malloc(size_t size)
{
	return tkl_system_malloc(size);
}

void *sndx_realloc(void *ptr, size_t size)
{
	return tkl_system_realloc(ptr, size);
}

void *sndx_calloc(size_t cnt, size_t size)
{
	return tkl_system_calloc(cnt, size);
}

void sndx_free(void *ptr)
{
	tkl_system_free(ptr);
}

#endif