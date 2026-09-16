#ifndef __TAL_MEMORY_H__
#define __TAL_MEMORY_H__

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define tal_malloc malloc
#define tal_calloc calloc
#define tal_free   free

/* mm_strdup: additive. Previously only provided ad-hoc per-suite via a
 * generated tmp header (e.g. provider/claw/tests/run_test.sh); hoisted here
 * so any host test that #includes this shared stub gets it directly. */
static inline char *mm_strdup(const char *s) { return s ? strdup(s) : NULL; }

/* Free-heap query (declared in the SDK's tal_memory.h). Impl provided per-suite. */
int32_t tal_system_get_free_heap_size(void);

#endif
