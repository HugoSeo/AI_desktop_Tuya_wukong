/**
 * @file wukong_storage_record.c
 * @brief Record layer of the storage module: (ns, name)-keyed small-record
 *        persistence on the app storage volume.
 *
 * Callers address small records by a two-level key — a namespace (@p ns, one
 * per feature, e.g. "tm", "music/cloud") and an entry name (@p name, e.g.
 * "wk_tm_alarms"). Data lives at:
 *
 *     WUKONG_STORAGE_ROOT/tuyaos/<ns>/<name>   e.g. /sdcard/tuyaos/tm/wk_tm_alarms
 *
 * The volume mount is owned by wukong_storage.c; this layer never mounts
 * anything itself — every op checks wukong_storage_ready() and degrades until
 * the asynchronous mount completes. Writes are atomic via a temp file +
 * rename; on filesystems that refuse an overwriting rename (FATFS) the
 * remove-then-rename fallback is not atomic, so wukong_storage_read()
 * recovers from the temp file when the final one is missing (and
 * wukong_storage_delete() drops both names). When the medium is unavailable
 * the layer degrades: writes return an error, reads return OPRT_NOT_FOUND,
 * deletes return OPRT_OK, and nothing crashes.
 *
 * @copyright Copyright (c) Tuya Inc.
 */
#include "wukong_storage.h"

#include <stdio.h>
#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"

#if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1)

#include "tkl_fs.h"

/** App data namespace root (parent of every record namespace). */
#define WUKONG_STORAGE_REC_ROOT     WUKONG_STORAGE_ROOT "/tuyaos"
/** Suffix appended to build the atomic temp file. */
#define WUKONG_STORAGE_REC_TMP_SUF  ".tmp"
/** Max composed path length (dir + name + temp suffix). */
#define WUKONG_STORAGE_REC_PATH_MAX 160

#if defined(WUKONG_STORAGE_HOST_TEST)
/* Host-test-only seam: the real record-layer code runs against a host
 * directory instead of WUKONG_STORAGE_ROOT. Guarded by a macro no firmware
 * build defines — this serves the host unit tests in storage/tests/ only and
 * must never be used by product code. */
STATIC CHAR_T s_rec_root_override[WUKONG_STORAGE_REC_PATH_MAX] = {0};

VOID wukong_storage_test_set_root(CONST CHAR_T *root)
{
    if (root == NULL) {
        s_rec_root_override[0] = '\0';
        return;
    }
    snprintf(s_rec_root_override, sizeof(s_rec_root_override), "%s/tuyaos", root);
}
#endif /* WUKONG_STORAGE_HOST_TEST */

/**
 * @brief Root of the record tree ("<storage root>/tuyaos").
 */
STATIC CONST CHAR_T *__rec_root(VOID)
{
#if defined(WUKONG_STORAGE_HOST_TEST)
    if (s_rec_root_override[0] != '\0') {
        return s_rec_root_override;
    }
#endif
    return WUKONG_STORAGE_REC_ROOT;
}

/**
 * @brief Compose "<record root>/<ns>" into @p buf.
 *
 * @return OPRT_OK; OPRT_BUFFER_NOT_ENOUGH on overflow.
 */
STATIC OPERATE_RET __rec_ns_dir(CHAR_T *buf, UINT_T len, CONST CHAR_T *ns)
{
    INT_T n = snprintf(buf, len, "%s/%s", __rec_root(), ns);
    if (n < 0 || (UINT_T)n >= len) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }
    return OPRT_OK;
}

/**
 * @brief Compose "<record root>/<ns>/<name>" into @p buf.
 *
 * @return OPRT_OK; OPRT_BUFFER_NOT_ENOUGH on overflow.
 */
STATIC OPERATE_RET __rec_entry_path(CHAR_T *buf, UINT_T len, CONST CHAR_T *ns,
                                    CONST CHAR_T *name)
{
    INT_T n = snprintf(buf, len, "%s/%s/%s", __rec_root(), ns, name);
    if (n < 0 || (UINT_T)n >= len) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }
    return OPRT_OK;
}

/**
 * @brief Compose "<entry path><WUKONG_STORAGE_REC_TMP_SUF>" into @p buf.
 *
 * @return OPRT_OK; OPRT_BUFFER_NOT_ENOUGH on overflow.
 */
STATIC OPERATE_RET __rec_tmp_path(CHAR_T *buf, UINT_T len, CONST CHAR_T *path)
{
    INT_T n = snprintf(buf, len, "%s%s", path, WUKONG_STORAGE_REC_TMP_SUF);
    if (n < 0 || (UINT_T)n >= len) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_storage_write(CONST CHAR_T *ns, CONST CHAR_T *name,
                                 CONST BYTE_T *data, UINT_T len)
{
    CHAR_T dir[WUKONG_STORAGE_REC_PATH_MAX];
    CHAR_T path[WUKONG_STORAGE_REC_PATH_MAX];
    CHAR_T tmp[WUKONG_STORAGE_REC_PATH_MAX];
    TUYA_FILE fp = NULL;
    INT_T written;
    OPERATE_RET rt;

    if (ns == NULL || name == NULL || (data == NULL && len > 0)) {
        return OPRT_INVALID_PARM;
    }
    if (!wukong_storage_ready()) {
        return OPRT_COM_ERROR;
    }

    /* Ensure the namespace directory exists before writing into it. */
    if (__rec_ns_dir(dir, sizeof(dir), ns) != OPRT_OK) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }
    tkl_fs_mkdir_r(dir);

    rt = __rec_entry_path(path, sizeof(path), ns, name);
    if (rt != OPRT_OK) {
        return rt;
    }
    rt = __rec_tmp_path(tmp, sizeof(tmp), path);
    if (rt != OPRT_OK) {
        return rt;
    }

    /* Write the full payload to a temp file, flush, then atomically swap. */
    fp = tkl_fopen(tmp, "w");
    if (fp == NULL) {
        TAL_PR_ERR("storage rec -> open temp '%s' failed", tmp);
        return OPRT_COM_ERROR;
    }

    written = (len > 0) ? tkl_fwrite((VOID_T *)data, (INT_T)len, fp) : 0;
    (VOID)tkl_fsync(tkl_fileno(fp));
    tkl_fclose(fp);

    if (written != (INT_T)len) {
        TAL_PR_ERR("storage rec -> short write %d/%u to '%s'", written, len, tmp);
        (VOID)tkl_fs_remove(tmp);
        return OPRT_COM_ERROR;
    }

    /* rename(temp -> final). Some embedded filesystems (FATFS) refuse to
     * rename onto an existing target, so fall back to remove-then-rename.
     *
     * The fallback opens a non-atomic window: after remove(path) and before
     * the second rename lands, neither file is at its final name. If the
     * second rename fails (or power drops inside the window) the fully
     * written payload still exists at the temp name, so it is deliberately
     * NOT removed here — wukong_storage_read() falls back to the temp file
     * when the final one is missing, which closes the window on the read
     * side. */
    if (tkl_fs_rename(tmp, path) != 0) {
        (VOID)tkl_fs_remove(path);
        if (tkl_fs_rename(tmp, path) != 0) {
            TAL_PR_ERR("storage rec -> rename '%s' -> '%s' failed (payload "
                       "preserved at temp name)", tmp, path);
            return OPRT_COM_ERROR;
        }
    }

    return OPRT_OK;
}

OPERATE_RET wukong_storage_read(CONST CHAR_T *ns, CONST CHAR_T *name,
                                BYTE_T **data, UINT_T *len)
{
    CHAR_T path[WUKONG_STORAGE_REC_PATH_MAX];
    CHAR_T tmp[WUKONG_STORAGE_REC_PATH_MAX];
    CONST CHAR_T *src = path;
    TUYA_FILE fp = NULL;
    INT_T size;
    INT_T got;
    BYTE_T *buf;

    if (ns == NULL || name == NULL || data == NULL || len == NULL) {
        return OPRT_INVALID_PARM;
    }
    *data = NULL;
    *len = 0;

    /* Not mounted => treat as "no data" so callers start from an empty table. */
    if (!wukong_storage_ready()) {
        return OPRT_NOT_FOUND;
    }

    if (__rec_entry_path(path, sizeof(path), ns, name) != OPRT_OK) {
        return OPRT_NOT_FOUND;
    }

    size = tkl_fgetsize(path);
    if (size <= 0) {
        /* Final file missing or empty. A write may have died inside the FATFS
         * remove-then-rename window (see wukong_storage_write) — in that case
         * the fully written payload still sits at the temp name
         * (wukong_storage_write only enters that window after a complete
         * write + fsync of the temp file), so recover from it instead of
         * reporting "no data". */
        if (__rec_tmp_path(tmp, sizeof(tmp), path) != OPRT_OK) {
            return OPRT_NOT_FOUND;
        }
        size = tkl_fgetsize(tmp);
        if (size <= 0) {
            /* Missing or empty file — nothing to load. */
            return OPRT_NOT_FOUND;
        }
        TAL_PR_WARN("storage rec -> '%s' missing, recovering from temp file", path);
        /* Promote the orphan back to its final name so later reads and writes
         * see a normal state; read the temp name in place if that fails. */
        if (tkl_fs_rename(tmp, path) != 0) {
            src = tmp;
        }
    }

    fp = tkl_fopen(src, "r");
    if (fp == NULL) {
        return OPRT_NOT_FOUND;
    }

    /* +1 so text payloads (JSON) are always NUL-terminated for the caller. */
    buf = (BYTE_T *)tal_malloc((SIZE_T)size + 1);
    if (buf == NULL) {
        tkl_fclose(fp);
        return OPRT_MALLOC_FAILED;
    }

    got = tkl_fread((VOID_T *)buf, size, fp);
    tkl_fclose(fp);

    if (got <= 0) {
        tal_free(buf);
        return OPRT_COM_ERROR;
    }

    buf[got] = '\0';
    *data = buf;
    *len = (UINT_T)got;
    return OPRT_OK;
}

OPERATE_RET wukong_storage_delete(CONST CHAR_T *ns, CONST CHAR_T *name)
{
    CHAR_T path[WUKONG_STORAGE_REC_PATH_MAX];
    CHAR_T tmp[WUKONG_STORAGE_REC_PATH_MAX];

    if (ns == NULL || name == NULL) {
        return OPRT_INVALID_PARM;
    }
    /* Nothing persisted when the medium is unavailable — treat as done. */
    if (!wukong_storage_ready()) {
        return OPRT_OK;
    }

    if (__rec_entry_path(path, sizeof(path), ns, name) != OPRT_OK) {
        return OPRT_OK;
    }
    (VOID)tkl_fs_remove(path);
    /* Also drop any temp-file orphan, or wukong_storage_read()'s recovery
     * path would resurrect the deleted entry from it. */
    if (__rec_tmp_path(tmp, sizeof(tmp), path) == OPRT_OK) {
        (VOID)tkl_fs_remove(tmp);
    }
    return OPRT_OK;
}

OPERATE_RET wukong_storage_append(CONST CHAR_T *ns, CONST CHAR_T *name,
                                  CONST BYTE_T *data, UINT_T len)
{
    CHAR_T dir[WUKONG_STORAGE_REC_PATH_MAX];
    CHAR_T path[WUKONG_STORAGE_REC_PATH_MAX];
    TUYA_FILE fp = NULL;
    INT_T written;
    OPERATE_RET rt;

    /* name must be a leaf file name: a '/' inside it would address a
     * directory level nobody creates (only the ns chain is mkdir'ed). */
    if (ns == NULL || name == NULL || name[0] == '\0' ||
        strchr(name, '/') != NULL || data == NULL || len == 0) {
        return OPRT_INVALID_PARM;
    }
    if (!wukong_storage_ready()) {
        return OPRT_COM_ERROR;
    }

    if (__rec_ns_dir(dir, sizeof(dir), ns) != OPRT_OK) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }
    tkl_fs_mkdir_r(dir);

    rt = __rec_entry_path(path, sizeof(path), ns, name);
    if (rt != OPRT_OK) {
        return rt;
    }

    fp = tkl_fopen(path, "a");
    if (fp == NULL) {
        TAL_PR_ERR("storage rec -> open append '%s' failed", path);
        return OPRT_COM_ERROR;
    }

    written = tkl_fwrite((VOID_T *)data, (INT_T)len, fp);
    (VOID)tkl_fsync(tkl_fileno(fp));
    tkl_fclose(fp);

    if (written != (INT_T)len) {
        TAL_PR_ERR("storage rec -> short append %d/%u to '%s'", written, len, path);
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_storage_list(CONST CHAR_T *ns, CHAR_T ***names, UINT_T *count)
{
    CHAR_T dir[WUKONG_STORAGE_REC_PATH_MAX];
    TUYA_DIR d = NULL;
    TUYA_FILEINFO info = NULL;
    CHAR_T **arr = NULL;
    UINT_T cap = 0;
    UINT_T cnt = 0;

    if (ns == NULL || names == NULL || count == NULL) {
        return OPRT_INVALID_PARM;
    }
    *names = NULL;
    *count = 0;

    if (!wukong_storage_ready()) {
        return OPRT_OK;   /* medium unavailable -> empty list */
    }
    if (__rec_ns_dir(dir, sizeof(dir), ns) != OPRT_OK) {
        return OPRT_OK;
    }
    if (tkl_dir_open(dir, &d) != 0 || d == NULL) {
        return OPRT_OK;   /* no such dir -> empty list */
    }

    while (tkl_dir_read(d, &info) == 0 && info != NULL) {
        BOOL_T reg = FALSE;
        CONST CHAR_T *name = NULL;
        CHAR_T *dup;

        tkl_dir_is_regular(info, &reg);
        tkl_dir_name(info, &name);
        if (!reg || name == NULL || name[0] == '\0') {
            continue;
        }
        if (cnt == cap) {
            UINT_T ncap = cap ? cap * 2 : 8;
            CHAR_T **grown = (CHAR_T **)tal_malloc(ncap * sizeof(CHAR_T *));
            if (grown == NULL) {
                break;
            }
            if (arr != NULL) {
                memcpy(grown, arr, cnt * sizeof(CHAR_T *));
                tal_free(arr);
            }
            arr = grown;
            cap = ncap;
        }
        dup = (CHAR_T *)tal_malloc(strlen(name) + 1);
        if (dup == NULL) {
            break;
        }
        strcpy(dup, name);
        arr[cnt++] = dup;
    }
    tkl_dir_close(d);

    *names = arr;
    *count = cnt;
    return OPRT_OK;
}

OPERATE_RET wukong_storage_list_dirs(CONST CHAR_T *ns, CHAR_T ***names, UINT_T *count)
{
    CHAR_T dir[WUKONG_STORAGE_REC_PATH_MAX];
    TUYA_DIR d = NULL;
    TUYA_FILEINFO info = NULL;
    CHAR_T **arr = NULL;
    UINT_T cap = 0;
    UINT_T cnt = 0;

    if (ns == NULL || names == NULL || count == NULL) {
        return OPRT_INVALID_PARM;
    }
    *names = NULL;
    *count = 0;

    if (!wukong_storage_ready()) {
        return OPRT_OK;   /* medium unavailable -> empty list */
    }
    if (__rec_ns_dir(dir, sizeof(dir), ns) != OPRT_OK) {
        return OPRT_OK;
    }
    if (tkl_dir_open(dir, &d) != 0 || d == NULL) {
        return OPRT_OK;   /* no such dir -> empty list */
    }

    while (tkl_dir_read(d, &info) == 0 && info != NULL) {
        BOOL_T is_dir = FALSE;
        CONST CHAR_T *name = NULL;
        CHAR_T *dup;

        tkl_dir_is_directory(info, &is_dir);
        tkl_dir_name(info, &name);
        if (!is_dir || name == NULL || name[0] == '\0') {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        if (cnt == cap) {
            UINT_T ncap = cap ? cap * 2 : 8;
            CHAR_T **grown = (CHAR_T **)tal_malloc(ncap * sizeof(CHAR_T *));
            if (grown == NULL) {
                break;
            }
            if (arr != NULL) {
                memcpy(grown, arr, cnt * sizeof(CHAR_T *));
                tal_free(arr);
            }
            arr = grown;
            cap = ncap;
        }
        dup = (CHAR_T *)tal_malloc(strlen(name) + 1);
        if (dup == NULL) {
            break;
        }
        strcpy(dup, name);
        arr[cnt++] = dup;
    }
    tkl_dir_close(d);

    *names = arr;
    *count = cnt;
    return OPRT_OK;
}

#else /* !WUKONG_STORAGE_ENABLE — NONE backend: inert record stubs so
       * storage-less boards still link. Same degradation contract as an
       * unmounted volume: write errors, read reports "no data", delete is a
       * no-op success, and nothing crashes. */

OPERATE_RET wukong_storage_write(CONST CHAR_T *ns, CONST CHAR_T *name,
                                 CONST BYTE_T *data, UINT_T len)
{
    if (ns == NULL || name == NULL || (data == NULL && len > 0)) {
        return OPRT_INVALID_PARM;
    }
    return OPRT_COM_ERROR;
}

OPERATE_RET wukong_storage_read(CONST CHAR_T *ns, CONST CHAR_T *name,
                                BYTE_T **data, UINT_T *len)
{
    if (ns == NULL || name == NULL || data == NULL || len == NULL) {
        return OPRT_INVALID_PARM;
    }
    *data = NULL;
    *len = 0;
    return OPRT_NOT_FOUND;
}

OPERATE_RET wukong_storage_delete(CONST CHAR_T *ns, CONST CHAR_T *name)
{
    if (ns == NULL || name == NULL) {
        return OPRT_INVALID_PARM;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_storage_append(CONST CHAR_T *ns, CONST CHAR_T *name,
                                  CONST BYTE_T *data, UINT_T len)
{
    if (ns == NULL || name == NULL || name[0] == '\0' ||
        strchr(name, '/') != NULL || data == NULL || len == 0) {
        return OPRT_INVALID_PARM;
    }
    return OPRT_COM_ERROR;
}

OPERATE_RET wukong_storage_list(CONST CHAR_T *ns, CHAR_T ***names, UINT_T *count)
{
    if (ns == NULL || names == NULL || count == NULL) {
        return OPRT_INVALID_PARM;
    }
    *names = NULL;
    *count = 0;
    return OPRT_OK;
}

OPERATE_RET wukong_storage_list_dirs(CONST CHAR_T *ns, CHAR_T ***names, UINT_T *count)
{
    if (ns == NULL || names == NULL || count == NULL) {
        return OPRT_INVALID_PARM;
    }
    *names = NULL;
    *count = 0;
    return OPRT_OK;
}

#endif /* WUKONG_STORAGE_ENABLE */

VOID wukong_storage_free(BYTE_T *data)
{
    if (data != NULL) {
        tal_free(data);
    }
}

VOID wukong_storage_free_list(CHAR_T **names, UINT_T count)
{
    UINT_T i;

    if (names == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        tal_free(names[i]);
    }
    tal_free(names);
}
