#include "ui_svc_fs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>     /* qsort */
#include <ctype.h>      /* tolower */

#include "tkl_fs.h"
#include "uni_log.h"
#include "ui_app.h"               /* ui_app_async_call — marshal to UI thread */
#include "tal_workq_service.h"    /* WORKQ_SYSTEM, tal_workq_schedule */
#include "tal_memory.h"           /* tal_malloc/free */
#include "base_event.h"

#define UI_FS_PATH_MAX  160

#if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1)

/* Leaf directories of the standard tree; tkl_fs_mkdir_r creates parents, so
 * listing the leaves is enough to materialise the whole structure. */
static const char *const s_tree_dirs[] = {
    UI_FS_PICTURE_ALBUM "/" UI_FS_THUMB_SUBDIR,
    UI_FS_PICTURE_DCIM  "/" UI_FS_THUMB_SUBDIR,
    UI_FS_DATA,
    UI_FS_FONT,
    UI_FS_MUSIC_LOCAL,
    UI_FS_MUSIC_CLOUD,
    UI_FS_RECORDING_TRANSCRIBE,
    UI_FS_VIDEO,
    UI_FS_TMP,
};

/* Top-level directories wiped by ui_fs_format(). */
static const char *const s_top_dirs[] = {
    "picture", UI_FS_DATA, UI_FS_FONT, "music", UI_FS_RECORDING, UI_FS_VIDEO, UI_FS_TMP,
};

static void ensure_tree(void)
{
    char p[UI_FS_PATH_MAX];
    uint32_t i;
    for (i = 0; i < sizeof(s_tree_dirs) / sizeof(s_tree_dirs[0]); i++) {
        snprintf(p, sizeof(p), "%s/%s", UI_FS_ROOT, s_tree_dirs[i]);
        tkl_fs_mkdir_r(p);
    }
}

/* Wipe and recreate tmp/ — called on every (successful) init. */
static void clear_tmp(void)
{
    char p[UI_FS_PATH_MAX];
    snprintf(p, sizeof(p), "%s/%s", UI_FS_ROOT, UI_FS_TMP);
    tkl_fs_remove_r(p);
    tkl_fs_mkdir_r(p);
}

static bool s_ready = false;

/* Called from two places — the base_event callback path and the ui_fs_init()
 * catch-up path below — both of which are made to run on WORKQ_SYSTEM (see
 * ui_fs_init()), so the two invocations are serialized on a single worker
 * thread rather than racing on separate ones. That serialization is what
 * makes the bare s_ready check below sufficient as an idempotency guard, no
 * lock needed. Safe for filesystem I/O, off the UI thread. */
static OPERATE_RET __on_storage_ready(void *data)
{
    (void)data;
    if (s_ready) {
        return OPRT_OK;
    }
    ensure_tree();
    clear_tmp();
    s_ready = true;
    PR_INFO("ui_fs: tree ready under %s", UI_FS_ROOT);
    return OPRT_OK;
}

/* Trampoline: tal_workq_schedule() calls back with void(*)(void*), whereas
 * the event-subscribe callback returns OPERATE_RET — bridge the two so the
 * catch-up path lands on WORKQ_SYSTEM too instead of running inline on
 * whatever thread calls ui_fs_init(). */
static void __catchup_on_workq(void *data)
{
    (void)data;
    __on_storage_ready(NULL);
}

OPERATE_RET ui_fs_init(void)
{
    OPERATE_RET rt = ty_subscribe_event(EVENT_WUKONG_STORAGE_READY, "ui_fs",
                                        __on_storage_ready, SUBSCRIBE_TYPE_NORMAL);
    if (rt != OPRT_OK) {
        return rt;
    }
    /* Storage may already be up (subscribe raced the mount) — catch up. Do
     * NOT call __on_storage_ready() directly here: this function can run on
     * any caller thread while the event callback runs on WORKQ_SYSTEM, and
     * both could pass the s_ready check before either sets it, running
     * ensure_tree()/clear_tmp() concurrently. Scheduling onto WORKQ_SYSTEM
     * instead serializes it against the event callback. */
    if (wukong_storage_ready()) {
        if (tal_workq_schedule(WORKQ_SYSTEM, __catchup_on_workq, NULL) != OPRT_OK) {
            PR_WARN("ui_fs: catch-up schedule failed, tree may not build");
        }
    }
    return OPRT_OK;
}

OPERATE_RET ui_fs_format(void)
{
    char p[UI_FS_PATH_MAX];
    uint32_t i;

    if (!s_ready) {
        PR_ERR("ui_fs: format before successful mount");
        return OPRT_COM_ERROR;
    }

    for (i = 0; i < sizeof(s_top_dirs) / sizeof(s_top_dirs[0]); i++) {
        snprintf(p, sizeof(p), "%s/%s", UI_FS_ROOT, s_top_dirs[i]);
        tkl_fs_remove_r(p);
    }
    ensure_tree();

    PR_INFO("ui_fs: formatted (wiped + rebuilt standard tree)");
    return OPRT_OK;
}

bool ui_fs_ready(void)
{
    return s_ready;
}

const char *ui_fs_root(void)
{
    return UI_FS_ROOT;
}

const char *ui_fs_mount(void)
{
    return UI_FS_MOUNT;
}

OPERATE_RET ui_fs_path(char *buf, size_t len, const char *rel, const char *name)
{
    char dir[UI_FS_PATH_MAX];
    int n;

    if (buf == NULL || len == 0 || rel == NULL) {
        return OPRT_INVALID_PARM;
    }

    n = snprintf(dir, sizeof(dir), "%s/%s", UI_FS_ROOT, rel);
    if (n < 0 || (size_t)n >= sizeof(dir)) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }
    tkl_fs_mkdir_r(dir);   /* ensure the directory part exists */

    if (name != NULL && name[0] != '\0') {
        n = snprintf(buf, len, "%s/%s", dir, name);
    } else {
        n = snprintf(buf, len, "%s", dir);
    }
    if (n < 0 || (size_t)n >= len) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }
    return OPRT_OK;
}

static OPERATE_RET __remove(const char *rel, const char *name, bool app_root)
{
    char path[UI_FS_PATH_MAX];
    const char *root = app_root ? UI_FS_ROOT : UI_FS_MOUNT;
    int n;

    if (name == NULL || name[0] == '\0') {
        return OPRT_INVALID_PARM;
    }
    if (!s_ready) {
        PR_ERR("ui_fs: remove before successful mount");
        return OPRT_COM_ERROR;
    }

    if (rel == NULL || rel[0] == '\0') {
        n = snprintf(path, sizeof(path), "%s/%s", root, name);
    } else {
        n = snprintf(path, sizeof(path), "%s/%s/%s", root, rel, name);
    }
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }

    if (tkl_fs_remove(path) != 0) {
        PR_ERR("ui_fs: remove %s failed", path);
        return OPRT_COM_ERROR;
    }
    PR_INFO("ui_fs: removed %s", path);
    return OPRT_OK;
}

OPERATE_RET ui_fs_remove(const char *rel, const char *name)
{
    return __remove(rel, name, false);
}

OPERATE_RET ui_fs_remove_app(const char *rel, const char *name)
{
    return __remove(rel, name, true);
}

OPERATE_RET ui_fs_app_exists(const char *rel, const char *name, bool *exists)
{
    char dir[UI_FS_PATH_MAX];
    TUYA_DIR d = NULL;
    int n;

    if (!exists || !name || name[0] == '\0' || strchr(name, '/') ||
        strchr(name, '\\')) {
        return OPRT_INVALID_PARM;
    }
    *exists = false;
    if (!s_ready) return OPRT_COM_ERROR;

    if (!rel || rel[0] == '\0') {
        n = snprintf(dir, sizeof(dir), "%s", UI_FS_ROOT);
    } else {
        n = snprintf(dir, sizeof(dir), "%s/%s", UI_FS_ROOT, rel);
    }
    if (n < 0 || (size_t)n >= sizeof(dir)) return OPRT_BUFFER_NOT_ENOUGH;
    if (tkl_dir_open(dir, &d) != 0 || !d) return OPRT_COM_ERROR;

    for (;;) {
        TUYA_FILEINFO info;
        const char *entry_name = NULL;

        if (tkl_dir_read(d, &info) != 0) break;
        if (tkl_dir_name(info, &entry_name) != 0 || !entry_name) {
            tkl_dir_close(d);
            return OPRT_COM_ERROR;
        }
        if (strcmp(entry_name, name) == 0) {
            *exists = true;
            break;
        }
    }

    tkl_dir_close(d);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Directory listing (async)
 * -------------------------------------------------------------------------*/

typedef struct {
    char            rel[UI_FS_PATH_MAX];
    uint32_t        seq;
    ui_fs_list_cb_t cb;
    void           *user;
    bool            app_root;
} fs_list_req_t;

typedef struct {
    OPERATE_RET     rt;
    uint32_t        seq;
    uint16_t        count;
    ui_fs_entry_t  *entries;   /* tal_malloc'd (NULL when count == 0) */
    ui_fs_list_cb_t cb;
    void           *user;
} fs_list_res_t;

static int ci_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Directories first, then case-insensitive name order. */
static int entry_cmp(const void *p1, const void *p2)
{
    const ui_fs_entry_t *a = (const ui_fs_entry_t *)p1;
    const ui_fs_entry_t *b = (const ui_fs_entry_t *)p2;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return ci_cmp(a->name, b->name);
}

/* UI thread: hand the result to the page, then release it. */
static void __notify_listed(void *p)
{
    fs_list_res_t *res = (fs_list_res_t *)p;
    if (res->cb) {
        res->cb(res->rt, res->entries, res->count, res->seq, res->user);
    }
    if (res->entries) tal_free(res->entries);
    tal_free(res);
}

/* WORKQ_SYSTEM: enumerate one directory level, marshal back to the UI thread. */
static void __list_work(void *data)
{
    fs_list_req_t *req = (fs_list_req_t *)data;

    fs_list_res_t *res = (fs_list_res_t *)tal_malloc(sizeof(*res));
    if (res == NULL) {
        tal_free(req);
        return;
    }
    memset(res, 0, sizeof(*res));
    res->seq  = req->seq;
    res->cb   = req->cb;
    res->user = req->user;
    res->rt   = OPRT_COM_ERROR;

    /* The file browser uses the whole mount; app media services use the
     * managed UI_FS_ROOT namespace. Both share one async enumeration path. */
    char dir[UI_FS_PATH_MAX];
    int n;
    const char *root = req->app_root ? UI_FS_ROOT : UI_FS_MOUNT;
    if (req->rel[0] == '\0') {
        n = snprintf(dir, sizeof(dir), "%s", root);
    } else {
        n = snprintf(dir, sizeof(dir), "%s/%s", root, req->rel);
    }

    TUYA_DIR d = NULL;
    if (!s_ready || n < 0 || (size_t)n >= sizeof(dir) ||
        tkl_dir_open(dir, &d) != 0 || d == NULL) {
        ui_app_async_call(__notify_listed, res);   /* res->rt stays COM_ERROR */
        tal_free(req);
        return;
    }

    ui_fs_entry_t *arr = (ui_fs_entry_t *)tal_malloc(sizeof(ui_fs_entry_t) * UI_FS_LIST_MAX);
    if (arr == NULL) {
        tkl_dir_close(d);
        ui_app_async_call(__notify_listed, res);   /* res->rt stays COM_ERROR */
        tal_free(req);
        return;
    }
    uint16_t count = 0;
    {
        TUYA_FILEINFO info;
        while (count < UI_FS_LIST_MAX && tkl_dir_read(d, &info) == 0) {
            const char *nm = NULL;
            if (tkl_dir_name(info, &nm) != 0 || nm == NULL) {
                break;   /* avoid spinning if a node has no resolvable name */
            }
            /* skip "." and ".." */
            if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) {
                continue;
            }

            BOOL_T is_dir = FALSE;
            tkl_dir_is_directory(info, &is_dir);

            ui_fs_entry_t *e = &arr[count];
            memset(e, 0, sizeof(*e));
            strncpy(e->name, nm, UI_FS_NAME_MAX - 1);
            e->name[UI_FS_NAME_MAX - 1] = '\0';
            e->is_dir = is_dir;
            e->size   = 0;

            if (!is_dir) {
                char full[UI_FS_PATH_MAX];
                int m = snprintf(full, sizeof(full), "%s/%s", dir, e->name);
                if (m > 0 && (size_t)m < sizeof(full)) {
                    INT_T sz = tkl_fgetsize(full);
                    if (sz > 0) e->size = (uint64_t)sz;
                }
            }
            count++;
        }
    }
    tkl_dir_close(d);

    if (count > 1) {
        qsort(arr, count, sizeof(ui_fs_entry_t), entry_cmp);
    }

    res->rt      = OPRT_OK;
    res->entries = arr;
    res->count   = count;

    ui_app_async_call(__notify_listed, res);
    tal_free(req);
}

static OPERATE_RET __list_async(const char *rel, uint32_t seq,
                                ui_fs_list_cb_t cb, void *user, bool app_root)
{
    if (cb == NULL || rel == NULL) {
        return OPRT_INVALID_PARM;
    }

    fs_list_req_t *req = (fs_list_req_t *)tal_malloc(sizeof(*req));
    if (req == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    memset(req, 0, sizeof(*req));
    strncpy(req->rel, rel, sizeof(req->rel) - 1);
    req->rel[sizeof(req->rel) - 1] = '\0';
    req->seq  = seq;
    req->cb   = cb;
    req->user = user;
    req->app_root = app_root;

    OPERATE_RET rt = tal_workq_schedule(WORKQ_SYSTEM, __list_work, req);
    if (rt != OPRT_OK) {
        tal_free(req);
    }
    return rt;
}

OPERATE_RET ui_fs_list_async(const char *rel, uint32_t seq,
                             ui_fs_list_cb_t cb, void *user)
{
    return __list_async(rel, seq, cb, user, false);
}

OPERATE_RET ui_fs_list_app_async(const char *rel, uint32_t seq,
                                 ui_fs_list_cb_t cb, void *user)
{
    return __list_async(rel, seq, cb, user, true);
}

#else /* storage backend NONE — inert stubs, keep consumers linking */

OPERATE_RET ui_fs_init(void)                          { return OPRT_OK; }
OPERATE_RET ui_fs_format(void)                        { return OPRT_NOT_SUPPORTED; }
bool ui_fs_ready(void)                                { return false; }
const char *ui_fs_root(void)                          { return NULL; }
const char *ui_fs_mount(void)                         { return NULL; }
OPERATE_RET ui_fs_path(char *buf, size_t len, const char *rel, const char *name)
{
    (void)buf; (void)len; (void)rel; (void)name;
    return OPRT_NOT_SUPPORTED;
}
OPERATE_RET ui_fs_remove(const char *rel, const char *name)
{
    (void)rel; (void)name;
    return OPRT_NOT_SUPPORTED;
}
OPERATE_RET ui_fs_remove_app(const char *rel, const char *name)
{
    (void)rel; (void)name;
    return OPRT_NOT_SUPPORTED;
}
OPERATE_RET ui_fs_app_exists(const char *rel, const char *name, bool *exists)
{
    (void)rel; (void)name;
    if (exists) *exists = false;
    return OPRT_NOT_SUPPORTED;
}
OPERATE_RET ui_fs_list_async(const char *rel, uint32_t seq,
                             ui_fs_list_cb_t cb, void *user)
{
    (void)rel; (void)seq; (void)cb; (void)user;
    return OPRT_NOT_SUPPORTED;
}
OPERATE_RET ui_fs_list_app_async(const char *rel, uint32_t seq,
                                 ui_fs_list_cb_t cb, void *user)
{
    (void)rel; (void)seq; (void)cb; (void)user;
    return OPRT_NOT_SUPPORTED;
}

#endif /* WUKONG_STORAGE_ENABLE */
