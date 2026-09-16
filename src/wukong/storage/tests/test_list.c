/*
 * Unit tests for wukong_storage_list_dirs() (wukong_storage_record.c): lists
 * only the first-level subdirectories of a namespace — used by wukong_skill's
 * per-skill-directory catalog scan (claw/skills/<id>/SKILL.md). Regular files
 * sitting directly under the namespace, and a missing namespace directory,
 * must not show up in the result.
 *
 * Same harness style as test_record.c: the REAL record layer runs against a
 * host temp directory via the WUKONG_STORAGE_HOST_TEST seam, with
 * wukong_storage_ready() supplied locally (always TRUE — this suite does not
 * exercise the medium-unavailable path, already covered by test_record.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wukong_test.h"
#include "wukong_storage.h"

static BOOL_T s_ready = TRUE;
BOOL_T wukong_storage_ready(VOID)
{
    return s_ready;
}

static char s_root[256];

static int has_name(CHAR_T **names, UINT_T count, const char *name)
{
    UINT_T i;
    for (i = 0; i < count; i++) {
        if (names[i] != NULL && strcmp(names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    CHAR_T **names = NULL;
    UINT_T count = 0;
    const char *tmpdir = getenv("TMPDIR");

    snprintf(s_root, sizeof(s_root), "%s/wk_storage_list_XXXXXX",
             tmpdir ? tmpdir : "/tmp");
    if (mkdtemp(s_root) == NULL) {
        fprintf(stderr, "# mkdtemp failed\n");
        return 1;
    }
    wukong_storage_test_set_root(s_root);

    /* missing namespace directory -> empty list, still OPRT_OK */
    EXPECT_OK(wukong_storage_list_dirs("claw/skills", &names, &count),
              "missing namespace dir -> OK");
    EXPECT_EQ(count, 0, "missing namespace dir -> empty list");
    EXPECT_NULL(names, "missing namespace dir -> NULL array");
    wukong_storage_free_list(names, count);

    /* invalid params */
    EXPECT_ERR(wukong_storage_list_dirs(NULL, &names, &count),
               OPRT_INVALID_PARM, "NULL ns rejected");
    EXPECT_ERR(wukong_storage_list_dirs("claw/skills", NULL, &count),
               OPRT_INVALID_PARM, "NULL names out-param rejected");
    EXPECT_ERR(wukong_storage_list_dirs("claw/skills", &names, NULL),
               OPRT_INVALID_PARM, "NULL count out-param rejected");

    /* seed claw/skills/a/ and claw/skills/b/ (created lazily by a write into
     * each) plus a stray regular file directly under claw/skills/ */
    EXPECT_OK(wukong_storage_write("claw/skills/a", "SKILL.md",
                                   (CONST BYTE_T *)"a", 1),
              "seed claw/skills/a/SKILL.md");
    EXPECT_OK(wukong_storage_write("claw/skills/b", "SKILL.md",
                                   (CONST BYTE_T *)"b", 1),
              "seed claw/skills/b/SKILL.md");
    EXPECT_OK(wukong_storage_write("claw/skills", "loose.txt",
                                   (CONST BYTE_T *)"x", 1),
              "seed a stray regular file directly under claw/skills");

    names = NULL;
    count = 0;
    EXPECT_OK(wukong_storage_list_dirs("claw/skills", &names, &count),
              "list_dirs on a populated namespace -> OK");
    EXPECT_EQ(count, 2, "exactly the two subdirectories, no more");
    EXPECT(has_name(names, count, "a"), "contains 'a'");
    EXPECT(has_name(names, count, "b"), "contains 'b'");
    EXPECT(!has_name(names, count, "loose.txt"),
           "the regular file is excluded");
    wukong_storage_free_list(names, count);

    TEST_END();
}
