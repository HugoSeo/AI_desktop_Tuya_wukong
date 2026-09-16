/**
 * @file wukong_storage.h
 * @brief Unified app storage, in two layers:
 *
 *   - Medium layer: compile-time medium selection, the single app-wide mount,
 *     and the root-path macro. Exactly one Kconfig backend is selected per
 *     board: WUKONG_STORAGE_SDCARD / WUKONG_STORAGE_EXT_FLASH /
 *     WUKONG_STORAGE_NONE. Bulk-data consumers compose paths at compile time
 *     (WUKONG_STORAGE_ROOT "/tuyaos/...") and access them directly with
 *     tkl_fs — the medium layer wraps no I/O.
 *
 *   - Record layer (wukong_storage_record.c): (ns, name)-keyed small-record
 *     persistence — wukong_storage_{write,read,delete,free}() — stored at
 *     <WUKONG_STORAGE_ROOT>/tuyaos/<ns>/<name> with atomic writes.
 *
 * When the backend is NONE neither WUKONG_STORAGE_ENABLE nor
 * WUKONG_STORAGE_ROOT is defined; storage-dependent modules must guard
 * themselves with
 * #if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1).
 * (The record layer needs no guard: it degrades by itself — see below.)
 *
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __WUKONG_STORAGE_H__
#define __WUKONG_STORAGE_H__

#include "tuya_cloud_types.h"
#include "tuya_app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Medium layer — compile-time medium selection, the single app-wide mount,
 * the root-path macro and the ready event.
 * ========================================================================== */

#if defined(WUKONG_STORAGE_SDCARD) && (WUKONG_STORAGE_SDCARD == 1)
#define WUKONG_STORAGE_ENABLE   1
#define WUKONG_STORAGE_ROOT     "/sdcard"
#elif defined(WUKONG_STORAGE_EXT_FLASH) && (WUKONG_STORAGE_EXT_FLASH == 1)
#define WUKONG_STORAGE_ENABLE   1
#define WUKONG_STORAGE_ROOT     "/ext-flash"
#endif

/** Published once (base_event) after the volume is mounted and usable.
 *  NB: base_event caps names at EVENT_NAME_MAX_LEN (16). */
#define EVENT_WUKONG_STORAGE_READY  "storage.ready"

/**
 * @brief Kick off the asynchronous mount on WORKQ_SYSTEM and return at once.
 *        NONE backend: no-op returning OPRT_OK (never publishes the event).
 * @return OPRT_OK if the mount work was queued (or NONE); error otherwise.
 */
OPERATE_RET wukong_storage_init(VOID);

/** @brief TRUE iff the volume is mounted and usable. NONE: always FALSE. */
BOOL_T wukong_storage_ready(VOID);

/** @brief WUKONG_STORAGE_ROOT as a runtime string; NULL when backend is NONE. */
CONST CHAR_T *wukong_storage_root(VOID);

/** @brief Reserved for hot-plug / pre-format flows. Unmounts and clears ready. */
OPERATE_RET wukong_storage_umount(VOID);

/**
 * @brief Board hook: format the external flash volume (first boot of a blank
 *        chip). Weak default returns OPRT_NOT_SUPPORTED; boards selecting
 *        WUKONG_STORAGE_EXT_FLASH provide the strong implementation.
 */
OPERATE_RET wukong_storage_port_mkfs(VOID);

/* ==========================================================================
 * Record layer (wukong_storage_record.c) — (ns, name)-keyed small-record
 * persistence for feature modules (e.g. ns "tm" for alarms/reminders, ns
 * "music/cloud" for the cloud playlist). Data lives at
 * <WUKONG_STORAGE_ROOT>/tuyaos/<ns>/<name>; writes are atomic (temp file +
 * rename, with a read-side recovery for the FATFS non-overwriting-rename
 * fallback). No init function: namespace directories are created lazily on
 * first write. Unlike the medium layer's macros this API needs no
 * WUKONG_STORAGE_ENABLE guard — when the medium is unavailable (not mounted
 * yet, or backend NONE) it degrades instead of crashing: write returns an
 * error, read returns OPRT_NOT_FOUND, delete returns OPRT_OK.
 * ========================================================================== */

/**
 * @brief Persist one record (overwrite semantics).
 *
 * @param[in] ns   Namespace (feature) key.
 * @param[in] name Entry name within @p ns.
 * @param[in] data Payload to store.
 * @param[in] len  Payload length in bytes.
 * @return OPRT_OK on success; OPRT_INVALID_PARM on bad args; another error
 *         code (e.g. OPRT_COM_ERROR when the medium is unavailable) otherwise.
 */
OPERATE_RET wukong_storage_write(CONST CHAR_T *ns, CONST CHAR_T *name,
                                 CONST BYTE_T *data, UINT_T len);

/**
 * @brief Load one record into a freshly allocated buffer.
 *
 * On success @p *data points to a heap buffer of @p *len + 1 bytes: the first
 * @p *len bytes are the payload, byte [len] is an extra '\0' (NOT counted in
 * @p *len) so text payloads (JSON) can be used as C strings directly. Release
 * it with wukong_storage_free().
 *
 * @param[in]  ns   Namespace key.
 * @param[in]  name Entry name within @p ns.
 * @param[out] data Receives the allocated buffer.
 * @param[out] len  Receives the payload length in bytes.
 * @return OPRT_OK when the record was read; OPRT_NOT_FOUND when it does not
 *         exist (or the medium is unavailable) — @p *data is set to NULL and
 *         @p *len to 0, treat as an empty table; OPRT_INVALID_PARM on bad
 *         args; another error code on real I/O failures.
 */
OPERATE_RET wukong_storage_read(CONST CHAR_T *ns, CONST CHAR_T *name,
                                BYTE_T **data, UINT_T *len);

/**
 * @brief Delete one record. Best-effort and idempotent: deleting a missing
 *        record (or with the medium unavailable) still returns OPRT_OK.
 *
 * @param[in] ns   Namespace key.
 * @param[in] name Entry name within @p ns.
 * @return OPRT_OK on success; OPRT_INVALID_PARM on bad args.
 */
OPERATE_RET wukong_storage_delete(CONST CHAR_T *ns, CONST CHAR_T *name);

/**
 * @brief Append one chunk to a record (parent namespace directories are
 *        created as needed).
 *
 * NOT atomic (unlike wukong_storage_write): a power cut may leave a partial
 * tail, so line-oriented readers must skip a trailing partial record. There is
 * no temp-file stage either — appends land in the final file directly.
 *
 * @param[in] ns   Namespace key (multi-level allowed, e.g. "claw/session").
 * @param[in] name Entry name within @p ns. Must be a leaf file name — no '/':
 *                 subdirectories are expressed through @p ns so the directory
 *                 chain is always created in full.
 * @param[in] data Payload chunk to append.
 * @param[in] len  Chunk length in bytes (> 0).
 * @return OPRT_OK on success; OPRT_INVALID_PARM on bad args (including a '/'
 *         in @p name); OPRT_COM_ERROR when the medium is unavailable or the
 *         write fails.
 */
OPERATE_RET wukong_storage_append(CONST CHAR_T *ns, CONST CHAR_T *name,
                                  CONST BYTE_T *data, UINT_T len);

/**
 * @brief List the regular files directly under a namespace directory.
 *
 * @param[in]  ns    Namespace key (multi-level allowed).
 * @param[out] names Receives a heap array of heap name strings; NULL when
 *                   @p *count is 0. Release with wukong_storage_free_list().
 * @param[out] count Receives the number of entries.
 * @return OPRT_OK — a missing directory or an unavailable medium yields an
 *         empty list; OPRT_INVALID_PARM on bad args.
 */
OPERATE_RET wukong_storage_list(CONST CHAR_T *ns, CHAR_T ***names, UINT_T *count);

/**
 * @brief List the first-level subdirectories directly under a namespace
 *        directory (regular files are excluded). Used by consumers whose
 *        entries are themselves directories, e.g. the skill catalog's
 *        claw/skills/<id>/ layout.
 *
 * @param[in]  ns    Namespace key (multi-level allowed).
 * @param[out] names Receives a heap array of heap name strings (subdirectory
 *                   names only); NULL when @p *count is 0. Release with
 *                   wukong_storage_free_list().
 * @param[out] count Receives the number of entries.
 * @return OPRT_OK — a missing directory or an unavailable medium yields an
 *         empty list; OPRT_INVALID_PARM on bad args.
 */
OPERATE_RET wukong_storage_list_dirs(CONST CHAR_T *ns, CHAR_T ***names, UINT_T *count);

/**
 * @brief Release a name array returned by wukong_storage_list(). NULL-safe.
 *
 * @param[in] names Array from wukong_storage_list(), or NULL.
 * @param[in] count Entry count reported alongside @p names.
 */
VOID wukong_storage_free_list(CHAR_T **names, UINT_T count);

/**
 * @brief Release a buffer returned by wukong_storage_read(). NULL-safe.
 *
 * @param[in] data Buffer from wukong_storage_read(), or NULL.
 */
VOID wukong_storage_free(BYTE_T *data);

#if defined(WUKONG_STORAGE_HOST_TEST)
/**
 * @brief Host-test-only seam: redirect the record tree to "<root>/tuyaos" so
 *        the real record-layer implementation runs against a host temp
 *        directory. NULL restores the default. Never compiled into firmware
 *        (no firmware build defines WUKONG_STORAGE_HOST_TEST); product code
 *        must not use it.
 */
VOID wukong_storage_test_set_root(CONST CHAR_T *root);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_STORAGE_H__ */
