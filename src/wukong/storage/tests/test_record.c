/*
 * Unit tests for the storage record layer (wukong_storage_record.c), running
 * the REAL implementation against a host temp directory:
 *
 *  - compiled with -DWUKONG_STORAGE_SDCARD=1 so the real filesystem code path
 *    is built (not the NONE stubs), and -DWUKONG_STORAGE_HOST_TEST=1 so the
 *    record root can be redirected via wukong_storage_test_set_root();
 *  - tkl_fs is mapped 1:1 onto POSIX by tests_common/stub_tkl_fs.c;
 *  - wukong_storage_ready() (normally wukong_storage.c, firmware-only) is
 *    provided here as a toggle so the medium-unavailable degradation contract
 *    can be exercised too.
 *
 * Covers: write/read round-trip, OPRT_NOT_FOUND + out-param clearing,
 * the '\0' byte past len, overwrite semantics, idempotent delete, the atomic
 * write artifacts (final file present, temp file gone), the .tmp recovery
 * path (interrupted FATFS remove-then-rename fallback), and that delete drops
 * the temp name too so a deleted entry cannot be resurrected.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wukong_test.h"
#include "wukong_storage.h"

/* Test-controlled stand-in for wukong_storage.c's mount state. */
static BOOL_T s_ready = FALSE;
BOOL_T wukong_storage_ready(VOID)
{
    return s_ready;
}

static char s_root[256];

/* Compose the on-disk path of a record, mirroring the documented rule
 * <root>/tuyaos/<ns>/<name> (wukong_storage_test_set_root() points the
 * "tuyaos" tree under s_root). */
static void entry_path(char *buf, size_t len, const char *ns,
                       const char *name, const char *suffix)
{
    snprintf(buf, len, "%s/tuyaos/%s/%s%s", s_root, ns, name, suffix);
}

static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

int main(void)
{
    BYTE_T *data = NULL;
    UINT_T len = 0;
    CONST CHAR_T *payload = "{\"alarms\":[]}";
    CONST CHAR_T *payload2 = "{\"alarms\":[{\"id\":\"a1\"}]}";
    char path[512];
    char tmp[512];

    /* --- point the record tree at a fresh host temp directory --- */
    const char *tmpdir = getenv("TMPDIR");
    snprintf(s_root, sizeof(s_root), "%s/wk_storage_rec_XXXXXX",
             tmpdir ? tmpdir : "/tmp");
    if (mkdtemp(s_root) == NULL) {
        fprintf(stderr, "# mkdtemp failed\n");
        return 1;
    }
    wukong_storage_test_set_root(s_root);

    /* --- medium unavailable: degrade, never crash --- */
    s_ready = FALSE;
    EXPECT_NE(wukong_storage_write("tm", "k", (CONST BYTE_T *)"x", 1), OPRT_OK,
              "write with medium unavailable fails");
    data = (BYTE_T *)0x1; /* poison: read must clear it */
    len = 77;
    EXPECT_ERR(wukong_storage_read("tm", "k", &data, &len), OPRT_NOT_FOUND,
               "read with medium unavailable reports not-found (empty)");
    EXPECT_NULL(data, "unavailable-medium read clears out-buf to NULL");
    EXPECT_EQ(len, 0, "unavailable-medium read clears out-len to 0");
    EXPECT_OK(wukong_storage_delete("tm", "k"),
              "delete with medium unavailable is a no-op success");

    /* --- medium ready from here on --- */
    s_ready = TRUE;

    /* invalid params */
    EXPECT_ERR(wukong_storage_write(NULL, "k", (CONST BYTE_T *)"x", 1),
               OPRT_INVALID_PARM, "write with NULL ns rejected");
    EXPECT_ERR(wukong_storage_write("tm", "k", NULL, 1),
               OPRT_INVALID_PARM, "write with NULL data and len>0 rejected");
    EXPECT_ERR(wukong_storage_read("tm", NULL, &data, &len),
               OPRT_INVALID_PARM, "read with NULL name rejected");
    EXPECT_ERR(wukong_storage_read("tm", "k", NULL, &len),
               OPRT_INVALID_PARM, "read with NULL out-buf rejected");
    EXPECT_ERR(wukong_storage_delete(NULL, "k"),
               OPRT_INVALID_PARM, "delete with NULL ns rejected");

    /* missing entry => NOT_FOUND, out-params cleared */
    data = (BYTE_T *)0x1;
    len = 77;
    EXPECT_ERR(wukong_storage_read("tm", "k1", &data, &len), OPRT_NOT_FOUND,
               "missing entry reads as OPRT_NOT_FOUND");
    EXPECT_NULL(data, "not-found read clears out-buf to NULL");
    EXPECT_EQ(len, 0, "not-found read clears out-len to 0");

    /* write then read back (ns dir created lazily — no init call anywhere) */
    EXPECT_OK(wukong_storage_write("tm", "k1", (CONST BYTE_T *)payload,
                                   (UINT_T)strlen(payload)),
              "write should succeed");
    data = NULL;
    len = 0;
    EXPECT_OK(wukong_storage_read("tm", "k1", &data, &len),
              "read back should succeed");
    EXPECT_NOT_NULL(data, "read returns a heap buffer");
    EXPECT_EQ(len, (UINT_T)strlen(payload), "read length matches written length");
    EXPECT(data && memcmp(data, payload, strlen(payload)) == 0,
           "read content matches written content");
    EXPECT(data && data[len] == '\0', "read buffer is NUL-terminated past len");
    wukong_storage_free(data);
    wukong_storage_free(NULL); /* NULL-safe */

    /* atomic write artifacts: final file on disk, temp file gone */
    entry_path(path, sizeof(path), "tm", "k1", "");
    entry_path(tmp, sizeof(tmp), "tm", "k1", ".tmp");
    EXPECT(file_exists(path), "final file exists at <root>/tuyaos/<ns>/<name>");
    EXPECT(!file_exists(tmp), "temp file is gone after a successful write");

    /* overwrite semantics */
    EXPECT_OK(wukong_storage_write("tm", "k1", (CONST BYTE_T *)payload2,
                                   (UINT_T)strlen(payload2)),
              "second write to the same entry should succeed");
    data = NULL;
    len = 0;
    EXPECT_OK(wukong_storage_read("tm", "k1", &data, &len),
              "read back after overwrite should succeed");
    EXPECT_EQ(len, (UINT_T)strlen(payload2),
              "overwrite replaced (not appended) the payload");
    EXPECT(data && memcmp(data, payload2, strlen(payload2)) == 0,
           "read content matches the second write");
    wukong_storage_free(data);

    /* delete then read back => NOT_FOUND; delete again => still OK */
    EXPECT_OK(wukong_storage_delete("tm", "k1"), "delete should succeed");
    data = NULL;
    len = 0;
    EXPECT_ERR(wukong_storage_read("tm", "k1", &data, &len), OPRT_NOT_FOUND,
               "deleted entry reads as OPRT_NOT_FOUND");
    EXPECT_OK(wukong_storage_delete("tm", "k1"),
              "deleting a missing entry is idempotent (OK)");

    /* --- .tmp recovery: a write that died inside the FATFS
     * remove-then-rename window leaves the full payload at the temp name and
     * nothing at the final name; read must recover it and promote the orphan
     * back to its final name --- */
    EXPECT_OK(wukong_storage_write("tm", "k2", (CONST BYTE_T *)payload,
                                   (UINT_T)strlen(payload)),
              "write the entry that will lose its final name");
    entry_path(path, sizeof(path), "tm", "k2", "");
    entry_path(tmp, sizeof(tmp), "tm", "k2", ".tmp");
    EXPECT(rename(path, tmp) == 0,
           "fabricate the interrupted-fallback state (payload only at .tmp)");
    data = NULL;
    len = 0;
    EXPECT_OK(wukong_storage_read("tm", "k2", &data, &len),
              "read recovers the payload from the temp file");
    EXPECT_EQ(len, (UINT_T)strlen(payload), "recovered length matches");
    EXPECT(data && memcmp(data, payload, strlen(payload)) == 0,
           "recovered content matches");
    EXPECT(data && data[len] == '\0', "recovered buffer is NUL-terminated");
    wukong_storage_free(data);
    EXPECT(file_exists(path), "recovery promoted the orphan back to the final name");
    EXPECT(!file_exists(tmp), "temp file is gone after recovery");

    /* --- delete drops BOTH names, so a deleted entry cannot be resurrected
     * through the recovery path --- */
    EXPECT_OK(wukong_storage_write("tm", "k3", (CONST BYTE_T *)payload,
                                   (UINT_T)strlen(payload)),
              "write the entry to be deleted");
    entry_path(path, sizeof(path), "tm", "k3", "");
    entry_path(tmp, sizeof(tmp), "tm", "k3", ".tmp");
    {
        /* plant a temp-file orphan next to the final file */
        FILE *fp = fopen(tmp, "w");
        EXPECT_NOT_NULL(fp, "plant a temp-file orphan");
        if (fp) {
            fwrite(payload, 1, strlen(payload), fp);
            fclose(fp);
        }
    }
    EXPECT_OK(wukong_storage_delete("tm", "k3"), "delete should succeed");
    EXPECT(!file_exists(path), "delete removed the final file");
    EXPECT(!file_exists(tmp), "delete removed the temp file too");
    data = NULL;
    len = 0;
    EXPECT_ERR(wukong_storage_read("tm", "k3", &data, &len), OPRT_NOT_FOUND,
               "deleted entry is not resurrected from the temp file");

    TEST_END();
}
