/*
 * POSIX-backed host-test double for the tkl_fs adapter (see stubs/tkl_fs.h).
 *
 * Lets the REAL storage record layer (wukong_storage_record.c) run unmodified
 * against a host directory: the storage/tests suites point the record root at
 * a temp dir via wukong_storage_test_set_root() and every tkl_* call below
 * maps 1:1 onto the host filesystem. Return conventions follow the vendor
 * adapter: 0 on success / non-zero on failure for the path ops, byte counts
 * for read/write, negative for a missing file in tkl_fgetsize().
 */
#include "tkl_fs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

INT_T tkl_fs_mkdir_r(CONST CHAR_T *path)
{
    CHAR_T buf[512];
    size_t i;
    size_t n;

    if (path == NULL) {
        return -1;
    }
    n = strlen(path);
    if (n == 0 || n >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, path, n + 1);

    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            CHAR_T saved = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0777) != 0 && errno != EEXIST) {
                return -1;
            }
            buf[i] = saved;
        }
    }
    return 0;
}

INT_T tkl_fs_remove(CONST CHAR_T *path)
{
    return (remove(path) == 0) ? 0 : -1;
}

INT_T tkl_fs_rename(CONST CHAR_T *path_old, CONST CHAR_T *path_new)
{
    return (rename(path_old, path_new) == 0) ? 0 : -1;
}

TUYA_FILE tkl_fopen(CONST CHAR_T *path, CONST CHAR_T *mode)
{
    return (TUYA_FILE)fopen(path, mode);
}

INT_T tkl_fclose(TUYA_FILE file)
{
    return fclose((FILE *)file);
}

INT_T tkl_fread(VOID_T *buf, INT_T bytes, TUYA_FILE file)
{
    return (INT_T)fread(buf, 1, (size_t)bytes, (FILE *)file);
}

INT_T tkl_fwrite(VOID_T *buf, INT_T bytes, TUYA_FILE file)
{
    size_t written = fwrite(buf, 1, (size_t)bytes, (FILE *)file);
    /* The record layer fsyncs by fd right after this call; flush the stdio
     * buffer here so that fsync (and tkl_fgetsize on the closed file) sees
     * everything. */
    (void)fflush((FILE *)file);
    return (INT_T)written;
}

INT_T tkl_fsync(INT_T fd)
{
    return fsync(fd);
}

INT_T tkl_fgetsize(CONST CHAR_T *filepath)
{
    struct stat st;

    if (filepath == NULL || stat(filepath, &st) != 0) {
        return -1;
    }
    return (INT_T)st.st_size;
}

INT_T tkl_fileno(TUYA_FILE file)
{
    return fileno((FILE *)file);
}

/* --- directory enumeration (POSIX-backed), used by claw_store_list --- */
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    DIR *d;
    struct dirent *ent;
    char base[512];
} STUB_TKL_DIR_T;

INT_T tkl_dir_open(CONST CHAR_T *path, TUYA_DIR *dir)
{
    if (!path || !dir) {
        return -1;
    }
    STUB_TKL_DIR_T *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return -1;
    }
    ctx->d = opendir(path);
    if (!ctx->d) {
        free(ctx);
        *dir = NULL;
        return -1;
    }
    snprintf(ctx->base, sizeof(ctx->base), "%s", path);
    *dir = ctx;
    return 0;
}

INT_T tkl_dir_read(TUYA_DIR dir, TUYA_FILEINFO *info)
{
    STUB_TKL_DIR_T *ctx = (STUB_TKL_DIR_T *)dir;
    if (!ctx || !info) {
        return -1;
    }
    do {
        ctx->ent = readdir(ctx->d);
    } while (ctx->ent && (strcmp(ctx->ent->d_name, ".") == 0 ||
                          strcmp(ctx->ent->d_name, "..") == 0));
    *info = ctx->ent ? (TUYA_FILEINFO)ctx : NULL;
    return 0;
}

INT_T tkl_dir_name(TUYA_FILEINFO info, CONST CHAR_T **name)
{
    STUB_TKL_DIR_T *ctx = (STUB_TKL_DIR_T *)info;
    if (!ctx || !ctx->ent || !name) {
        return -1;
    }
    *name = ctx->ent->d_name;
    return 0;
}

INT_T tkl_dir_is_regular(TUYA_FILEINFO info, BOOL_T *is_regular)
{
    STUB_TKL_DIR_T *ctx = (STUB_TKL_DIR_T *)info;
    char path[768];
    struct stat st;
    if (!ctx || !ctx->ent || !is_regular) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s/%s", ctx->base, ctx->ent->d_name);
    *is_regular = (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? TRUE : FALSE;
    return 0;
}

INT_T tkl_dir_is_directory(TUYA_FILEINFO info, BOOL_T *is_dir)
{
    STUB_TKL_DIR_T *ctx = (STUB_TKL_DIR_T *)info;
    char path[768];
    struct stat st;
    if (!ctx || !ctx->ent || !is_dir) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s/%s", ctx->base, ctx->ent->d_name);
    *is_dir = (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? TRUE : FALSE;
    return 0;
}

INT_T tkl_dir_close(TUYA_DIR dir)
{
    STUB_TKL_DIR_T *ctx = (STUB_TKL_DIR_T *)dir;
    if (!ctx) {
        return -1;
    }
    closedir(ctx->d);
    free(ctx);
    return 0;
}
