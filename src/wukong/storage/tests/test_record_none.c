/*
 * Unit tests for the record layer's NONE branch (wukong_storage_record.c
 * compiled with no WUKONG_STORAGE_* macro, i.e. WUKONG_STORAGE_ENABLE
 * undefined — the storage-less board configuration).
 *
 * Verifies the inert stubs link without tkl_fs / wukong_storage.c and honour
 * the degradation contract: write errors, read reports OPRT_NOT_FOUND with
 * cleared out-params, delete is a no-op success, free is NULL-safe.
 */
#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "wukong_storage.h"

int main(void)
{
    BYTE_T *data = NULL;
    UINT_T len = 0;

    /* invalid params still rejected explicitly */
    EXPECT_ERR(wukong_storage_write(NULL, "k", (CONST BYTE_T *)"x", 1),
               OPRT_INVALID_PARM, "write with NULL ns rejected");
    EXPECT_ERR(wukong_storage_read("tm", "k", NULL, &len),
               OPRT_INVALID_PARM, "read with NULL out-buf rejected");
    EXPECT_ERR(wukong_storage_delete("tm", NULL),
               OPRT_INVALID_PARM, "delete with NULL name rejected");

    /* degradation contract */
    EXPECT_NE(wukong_storage_write("tm", "k", (CONST BYTE_T *)"x", 1), OPRT_OK,
              "write returns an error (nothing persists on NONE boards)");
    data = (BYTE_T *)0x1; /* poison: read must clear it */
    len = 77;
    EXPECT_ERR(wukong_storage_read("tm", "k", &data, &len), OPRT_NOT_FOUND,
               "read always reports OPRT_NOT_FOUND (empty table)");
    EXPECT_NULL(data, "read clears out-buf to NULL");
    EXPECT_EQ(len, 0, "read clears out-len to 0");
    EXPECT_OK(wukong_storage_delete("tm", "k"),
              "delete is a no-op success (idempotent)");
    EXPECT_OK(wukong_storage_delete("tm", "k"),
              "repeated delete still returns OK");
    wukong_storage_free(NULL); /* NULL-safe, must not crash */
    EXPECT(1, "wukong_storage_free(NULL) is safe");

    TEST_END();
}
