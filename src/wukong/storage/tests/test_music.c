/*
 * Unit tests for the "music/cloud" namespace on the storage record layer.
 *
 * Same harness as test_record.c (real wukong_storage_record.c over POSIX
 * tkl_fs, record root redirected to a host temp dir). Verifies that:
 *
 *  - write/read/delete round-trip for ns="music/cloud" — an ns containing "/"
 *    is a single literal byte in the "%s" expansion (no globbing, no path
 *    normalization), so it is simply one more directory level;
 *  - the record actually lands on disk at <root>/tuyaos/music/cloud/
 *    playlist.json — read back through plain stdio, byte-identical;
 *  - the composed path for ns="music/cloud", name="playlist.json" is
 *    byte-identical to the legacy ui_fs path, so OTA-era playlists remain
 *    readable. Path composition is checked against the record layer's
 *    snprintf format (see __rec_entry_path in wukong_storage_record.c):
 *        snprintf(buf, len, "%s/%s/%s", "/sdcard/tuyaos", ns, name)
 *    and the legacy ui_fs_path():
 *        snprintf(dir, "%s/%s",   "/sdcard/tuyaos", "music/cloud");
 *        snprintf(buf, "%s/%s",   dir,              "playlist.json");
 *    Both yield "/sdcard/tuyaos/music/cloud/playlist.json".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wukong_test.h"
#include "wukong_storage.h"

/* Test-controlled stand-in for wukong_storage.c's mount state. */
BOOL_T wukong_storage_ready(VOID)
{
    return TRUE;
}

/* Mirror of the on-device record root (WUKONG_STORAGE_ROOT "/tuyaos" with the
 * SDCARD medium). Kept as a literal here to assert the byte-compatibility
 * contract against the legacy path. */
#define TEST_FS_ROOT                "/sdcard/tuyaos"
#define TEST_MUSIC_NS               "music/cloud"
#define TEST_MUSIC_PLAYLIST_NAME    "playlist.json"
/* Legacy ui_fs pieces (see ui_svc_fs.h: UI_FS_MOUNT / UI_FS_ROOT /
 * UI_FS_MUSIC_CLOUD). */
#define TEST_UI_FS_MOUNT            "/sdcard"
#define TEST_UI_FS_ROOT             TEST_UI_FS_MOUNT "/tuyaos"
#define TEST_UI_FS_MUSIC_CLOUD      "music/cloud"

static char s_root[256];

static void compose_record_path(char *buf, size_t len)
{
    /* Same format string the record layer uses. */
    snprintf(buf, len, "%s/%s/%s", TEST_FS_ROOT, TEST_MUSIC_NS,
             TEST_MUSIC_PLAYLIST_NAME);
}

static void compose_legacy_ui_fs_path(char *buf, size_t len)
{
    /* Two-step composition mirroring ui_svc_fs.c::ui_fs_path(). */
    char dir[160];
    snprintf(dir, sizeof(dir), "%s/%s", TEST_UI_FS_ROOT, TEST_UI_FS_MUSIC_CLOUD);
    snprintf(buf, len, "%s/%s", dir, TEST_MUSIC_PLAYLIST_NAME);
}

int main(void)
{
    BYTE_T *data = NULL;
    UINT_T len = 0;
    CONST CHAR_T *payload = "[{\"id\":1,\"name\":\"Song A\"}]";
    char record_path[160];
    char legacy_path[160];
    char disk_path[512];

    /* --- point the record tree at a fresh host temp directory --- */
    const char *tmpdir = getenv("TMPDIR");
    snprintf(s_root, sizeof(s_root), "%s/wk_storage_mus_XXXXXX",
             tmpdir ? tmpdir : "/tmp");
    if (mkdtemp(s_root) == NULL) {
        fprintf(stderr, "# mkdtemp failed\n");
        return 1;
    }
    wukong_storage_test_set_root(s_root);

    /* read of a missing music/cloud entry => NOT_FOUND (empty-table contract) */
    data = (BYTE_T *)0x1;
    len = 77;
    EXPECT_ERR(wukong_storage_read(TEST_MUSIC_NS, TEST_MUSIC_PLAYLIST_NAME,
                                   &data, &len), OPRT_NOT_FOUND,
               "missing music/cloud entry reads as OPRT_NOT_FOUND");
    EXPECT_NULL(data, "not-found read clears out-buf to NULL");

    /* write the playlist JSON through the record layer */
    EXPECT_OK(wukong_storage_write(TEST_MUSIC_NS, TEST_MUSIC_PLAYLIST_NAME,
                                   (CONST BYTE_T *)payload,
                                   (UINT_T)strlen(payload)),
              "write to music/cloud should succeed");

    /* read it back — same payload, NUL-terminated past len */
    data = NULL;
    len = 0;
    EXPECT_OK(wukong_storage_read(TEST_MUSIC_NS, TEST_MUSIC_PLAYLIST_NAME,
                                  &data, &len),
              "read-back from music/cloud should succeed");
    EXPECT_NOT_NULL(data, "read returns a heap buffer");
    EXPECT_EQ(len, (UINT_T)strlen(payload),
              "read length matches written length");
    EXPECT(data && memcmp(data, payload, strlen(payload)) == 0,
           "read content matches written content");
    EXPECT(data && data[len] == '\0',
           "read buffer is NUL-terminated past len for JSON parsing");
    wukong_storage_free(data);

    /* --- the record really landed at <root>/tuyaos/music/cloud/playlist.json:
     * read it back through plain stdio, proving the on-disk path rule
     * (including the "/" inside the ns) with the real filesystem --- */
    snprintf(disk_path, sizeof(disk_path), "%s/tuyaos/%s/%s",
             s_root, TEST_MUSIC_NS, TEST_MUSIC_PLAYLIST_NAME);
    {
        char raw[256] = {0};
        FILE *fp = fopen(disk_path, "r");
        EXPECT_NOT_NULL(fp, "on-disk file exists at <root>/tuyaos/music/cloud/playlist.json");
        if (fp) {
            size_t got = fread(raw, 1, sizeof(raw) - 1, fp);
            fclose(fp);
            EXPECT_EQ(got, strlen(payload), "on-disk size matches the payload");
            EXPECT_STR_EQ(raw, payload, "on-disk bytes match the payload");
        }
    }

    /* delete is idempotent */
    EXPECT_OK(wukong_storage_delete(TEST_MUSIC_NS, TEST_MUSIC_PLAYLIST_NAME),
              "delete should succeed");
    EXPECT_OK(wukong_storage_delete(TEST_MUSIC_NS, TEST_MUSIC_PLAYLIST_NAME),
              "deleting a missing entry is idempotent (OK)");
    data = NULL;
    len = 0;
    EXPECT_ERR(wukong_storage_read(TEST_MUSIC_NS, TEST_MUSIC_PLAYLIST_NAME,
                                   &data, &len), OPRT_NOT_FOUND,
               "deleted entry reads as OPRT_NOT_FOUND");

    /* --- Path byte-compatibility: legacy ui_fs path == record-layer path --- */
    compose_record_path(record_path, sizeof(record_path));
    compose_legacy_ui_fs_path(legacy_path, sizeof(legacy_path));
    EXPECT_STR_EQ(record_path, legacy_path,
                  "record path for (music/cloud, playlist.json) matches the "
                  "legacy ui_fs path byte-for-byte");
    EXPECT_STR_EQ(record_path, "/sdcard/tuyaos/music/cloud/playlist.json",
                  "concrete path equals the on-disk legacy path");
    /* Sanity: ns containing "/" does not introduce a double slash. The layer
     * composes ROOT + "/" + ns + "/" + name; ns "music/cloud" embeds one "/",
     * which lands exactly where the legacy path also has one. */
    EXPECT(strstr(record_path, "//") == NULL,
           "record path has no double slash despite ns containing '/'");

    TEST_END();
}
