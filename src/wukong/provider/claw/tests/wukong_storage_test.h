#pragma once
/* Test reset for suites built with -DWUKONG_STORAGE_SDCARD -DWUKONG_STORAGE_HOST_TEST:
 * wipe the host temp volume and point the real record layer at it via the
 * wukong_storage_test_set_root() seam (same approach as storage/tests). Call
 * before the first storage access of every case. */
#include <stdlib.h>
#include "wukong_storage.h"

#define WK_TEST_STORAGE_ROOT "/tmp/wukong_claw_store_test"

static inline void wukong_storage_test_reset(void)
{
    int rc = system("rm -rf " WK_TEST_STORAGE_ROOT);
    (void)rc;
    wukong_storage_test_set_root(WK_TEST_STORAGE_ROOT);
}
