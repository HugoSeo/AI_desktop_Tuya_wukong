#ifndef __UI_SVC_FS_H__
#define __UI_SVC_FS_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "tuya_cloud_types.h"
#include "wukong_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Unified app filesystem. Mount ownership belongs to wukong_storage (see
 * wukong_storage.h) — this service only owns the app data namespace UI_FS_ROOT
 * ("<mount>/tuyaos") on top of it: ui_fs_init() arms a listener for
 * EVENT_WUKONG_STORAGE_READY and, once the volume is mounted, builds the
 * standard directory tree and clears tmp/. No fallback if the backend is NONE
 * or the mount never comes up — ui_fs_ready() then just stays false.
 * The UI_FS_ROOT layout (picture / data / font / music / recording / video / tmp) is
 * documented in src/ui/README.md (ui_svc_fs section).
 *
 * Two distinct roots, by purpose:
 *   - UI_FS_ROOT  — where the app reads/writes its own data (ui_fs_path()).
 *   - UI_FS_MOUNT — the whole mounted volume; what the file browser walks
 *                   (ui_fs_list_async() is relative to this), so user files
 *                   sitting next to tuyaos/ on the card are visible too.
 * Both macros are only defined when WUKONG_STORAGE_ENABLE is set; with backend
 * NONE neither exists and all APIs below degrade to inert stubs.
 * --------------------------------------------------------------------------- */

#if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1)
#define UI_FS_MOUNT                 WUKONG_STORAGE_ROOT    /* device mount root */
#define UI_FS_ROOT                  UI_FS_MOUNT "/tuyaos"  /* app data namespace */
#endif

/* Directory-listing limits. */
#define UI_FS_NAME_MAX              128   /* max leaf-name length kept per entry */
#define UI_FS_LIST_MAX              256   /* max entries returned for one directory */

/* Standard sub-directories (relative to UI_FS_ROOT). */
#define UI_FS_PICTURE_ALBUM         "picture/album"
#define UI_FS_PICTURE_DCIM          "picture/DCIM"
#define UI_FS_THUMB_SUBDIR          "thumb"           /* under album/DCIM */
#define UI_FS_MUSIC_LOCAL           "music/local"
#define UI_FS_MUSIC_CLOUD           "music/cloud"
#define UI_FS_RECORDING             "recording"
#define UI_FS_RECORDING_TRANSCRIBE  "recording/transcribe"
#define UI_FS_VIDEO                 "video"
#define UI_FS_FONT                  "font"
#define UI_FS_DATA                  "data"            /* user downloads — never auto-cleared */
#define UI_FS_TMP                   "tmp"             /* scratch — cleared on every ui_fs_init */

/**
 * @brief Arm the unified app filesystem: once wukong_storage reports the
 *        volume mounted (EVENT_WUKONG_STORAGE_READY), ensure the standard
 *        directory tree under UI_FS_ROOT and clear tmp/. Returns immediately;
 *        ui_fs_ready() flips to true only after the tree is in place.
 *        Storage backend NONE: inert, ui_fs_ready() stays false.
 */
OPERATE_RET ui_fs_init(void);

/**
 * @brief Logical format: recursively delete everything under UI_FS_ROOT, then
 *        rebuild the standard directory tree. Manual/destructive — wipes data/
 *        too. Requires a prior successful ui_fs_init().
 */
OPERATE_RET ui_fs_format(void);

/** @brief TRUE iff the device is currently mounted. */
bool ui_fs_ready(void);

/** @brief The app data namespace root, e.g. "/sdcard/tuyaos". */
const char *ui_fs_root(void);

/** @brief The device mount root, e.g. "/sdcard" (parent of UI_FS_ROOT). */
const char *ui_fs_mount(void);

/**
 * @brief Compose "UI_FS_ROOT/<rel>/<name>" into @buf, creating the directory
 *        part (UI_FS_ROOT/<rel>) recursively. @name may be NULL/"" to get just
 *        the directory path.
 * @return OPRT_OK; OPRT_INVALID_PARM on bad args; OPRT_BUFFER_NOT_ENOUGH if it
 *         would overflow @buf.
 */
OPERATE_RET ui_fs_path(char *buf, size_t len, const char *rel, const char *name);

/** Remove UI_FS_MOUNT/<rel>/<name>; used by the whole-card file browser. */
OPERATE_RET ui_fs_remove(const char *rel, const char *name);

/**
 * @brief Remove UI_FS_ROOT/<rel>/<name> instead of UI_FS_MOUNT/<rel>/<name>.
 *        Use this for app-owned media returned by ui_fs_list_app_async().
 */
OPERATE_RET ui_fs_remove_app(const char *rel, const char *name);

/**
 * @brief Check whether the direct child UI_FS_ROOT/<rel>/<name> exists.
 * @param[out] exists Set to true when the path exists, false otherwise.
 * @note Performs synchronous directory I/O; call from a worker thread.
 */
OPERATE_RET ui_fs_app_exists(const char *rel, const char *name, bool *exists);

/* ---------------------------------------------------------------------------
 * Directory listing (async). One directory level, non-recursive. Enumeration
 * runs on WORKQ_SYSTEM; the result is marshalled back to the UI thread before
 * the callback fires (same contract as the other ui_svc_* data services), so
 * the callback may touch LVGL directly.
 * --------------------------------------------------------------------------- */

/** A single directory entry. */
typedef struct {
    char     name[UI_FS_NAME_MAX];  /* leaf name, no path */
    BOOL_T   is_dir;
    uint64_t size;                  /* bytes; 0 for directories */
} ui_fs_entry_t;

/**
 * @brief Listing result callback (runs on the UI thread).
 * @param rt      OPRT_OK on success; otherwise @entries is NULL and @count 0.
 * @param entries Array of @count entries, valid ONLY for the callback's
 *                duration — copy what you need before returning.
 * @param count   Number of entries.
 * @param seq     The @seq passed to ui_fs_list_async (for latest-wins dropping).
 * @param user    The @user pointer passed to ui_fs_list_async.
 */
typedef void (*ui_fs_list_cb_t)(OPERATE_RET rt, const ui_fs_entry_t *entries,
                                uint16_t count, uint32_t seq, void *user);

/**
 * @brief List the direct children of UI_FS_MOUNT/<rel> off the UI thread.
 *        Entries are sorted directories-first, then by name (case-insensitive).
 *        "." and ".." are skipped; at most UI_FS_LIST_MAX entries are returned.
 * @param rel  Path relative to the device mount root UI_FS_MOUNT; "" for the
 *             mount root itself (the whole card).
 * @param seq  Caller sequence number, echoed back to @cb for latest-wins.
 * @param cb   Result callback (UI thread). Required.
 * @param user Opaque pointer echoed back to @cb.
 * @return OPRT_OK if the request was queued (cb will fire later); otherwise a
 *         synchronous failure and @cb is NOT called.
 */
OPERATE_RET ui_fs_list_async(const char *rel, uint32_t seq,
                             ui_fs_list_cb_t cb, void *user);

/**
 * @brief List UI_FS_ROOT/<rel> instead of the whole mounted volume.
 *        This is the preferred API for app-owned media such as UI_FS_VIDEO.
 *        The threading, sorting and callback-lifetime contract is identical to
 *        ui_fs_list_async().
 */
OPERATE_RET ui_fs_list_app_async(const char *rel, uint32_t seq,
                                 ui_fs_list_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* __UI_SVC_FS_H__ */
