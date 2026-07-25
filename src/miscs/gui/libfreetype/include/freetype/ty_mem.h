#ifndef _TY_MEM_H_
#define _TY_MEM_H_
#include <stddef.h>

void *ty_malloc(size_t size);
void *ty_realloc(void *ptr, size_t size);
void ty_free(void *ptr);
void* ty_calloc(int num, unsigned int size);
#endif