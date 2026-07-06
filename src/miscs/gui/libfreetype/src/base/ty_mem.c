
#include "freetype/ty_mem.h"
#include "tkl_memory.h"

void *ty_malloc(size_t size)
{
    return tkl_system_psram_malloc(size);
}

void *ty_realloc(void *ptr, size_t size)
{
    return tkl_system_psram_realloc(ptr, size);
}

void ty_free(void *ptr)
{
    tkl_system_psram_free(ptr);
}

void* ty_calloc(int num, unsigned int size)
{
    return tkl_system_psram_calloc(num, size);
}
