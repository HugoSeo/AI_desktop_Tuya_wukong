/**
 * @file wukong_storage.c
 * @brief The single app-wide external-storage mount (async, on WORKQ_SYSTEM).
 * @copyright Copyright (c) Tuya Inc.
 */
#include "wukong_storage.h"

#include "tal_log.h"
#include "base_event.h"

#if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1)

#include "tkl_fs.h"
#include "tal_system.h"
#include "tal_workq_service.h"

#if defined(WUKONG_STORAGE_SDCARD) && (WUKONG_STORAGE_SDCARD == 1)
#define WUKONG_STORAGE_DEV      DEV_SDCARD
#else
#define WUKONG_STORAGE_DEV      DEV_EXT_FLASH
#endif

/** Mount attempts before giving up for this boot. */
#define WUKONG_STORAGE_MOUNT_TRIES  3
/** Delay between attempts (ms). */
#define WUKONG_STORAGE_RETRY_MS     1000

STATIC volatile BOOL_T s_ready = FALSE;

/* Weak default: boards with external flash override this. */
__attribute__((weak)) OPERATE_RET wukong_storage_port_mkfs(VOID)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief One mount attempt, tolerating an already-mounted point (manufacturing
 *        tests may have mounted it before the app).
 */
STATIC BOOL_T __storage_try_mount(VOID)
{
    if (tkl_fs_mount(WUKONG_STORAGE_ROOT, WUKONG_STORAGE_DEV) == 0) {
        return TRUE;
    }
    TUYA_DIR d = NULL;
    if (tkl_dir_open(WUKONG_STORAGE_ROOT, &d) == 0 && d != NULL) {
        tkl_dir_close(d);
        TAL_PR_WARN("storage: %s already mounted, reusing", WUKONG_STORAGE_ROOT);
        return TRUE;
    }
    return FALSE;
}

STATIC VOID __storage_mount_work(VOID *data)
{
    INT_T attempt;

    (VOID)data;

    for (attempt = 0; attempt < WUKONG_STORAGE_MOUNT_TRIES; attempt++) {
        if (attempt > 0) {
            tal_system_sleep(WUKONG_STORAGE_RETRY_MS);
        }
        if (__storage_try_mount()) {
            s_ready = TRUE;
            TAL_PR_NOTICE("storage: %s mounted (attempt %d)", WUKONG_STORAGE_ROOT, attempt + 1);
            ty_publish_event(EVENT_WUKONG_STORAGE_READY, NULL);
            return;
        }
#if defined(WUKONG_STORAGE_EXT_FLASH) && (WUKONG_STORAGE_EXT_FLASH == 1)
        /* Blank external flash mounts only after a first-time format; the
         * vendor littlefs_mount has no auto-mkfs, so ask the board hook. */
        if (attempt == 0 && wukong_storage_port_mkfs() == OPRT_OK) {
            TAL_PR_NOTICE("storage: ext-flash formatted, remounting");
            continue;
        }
#endif
    }

    TAL_PR_ERR("storage: mount %s failed after %d tries, degrading (no retry this boot)",
               WUKONG_STORAGE_ROOT, WUKONG_STORAGE_MOUNT_TRIES);
}

OPERATE_RET wukong_storage_init(VOID)
{
    if (s_ready) {
        return OPRT_OK;
    }
    return tal_workq_schedule(WORKQ_SYSTEM, __storage_mount_work, NULL);
}

BOOL_T wukong_storage_ready(VOID)
{
    return s_ready;
}

CONST CHAR_T *wukong_storage_root(VOID)
{
    return WUKONG_STORAGE_ROOT;
}

OPERATE_RET wukong_storage_umount(VOID)
{
    if (!s_ready) {
        return OPRT_OK;
    }
    s_ready = FALSE;
    return (tkl_fs_unmount(WUKONG_STORAGE_ROOT) == 0) ? OPRT_OK : OPRT_COM_ERROR;
}

#else /* !WUKONG_STORAGE_ENABLE — NONE backend: inert stubs */

OPERATE_RET wukong_storage_init(VOID)
{
    return OPRT_OK;
}

BOOL_T wukong_storage_ready(VOID)
{
    return FALSE;
}

CONST CHAR_T *wukong_storage_root(VOID)
{
    return NULL;
}

OPERATE_RET wukong_storage_umount(VOID)
{
    return OPRT_OK;
}

#endif /* WUKONG_STORAGE_ENABLE */
