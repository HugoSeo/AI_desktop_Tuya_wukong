/**
 * @file ui_record_runtime.h
 * @brief Recording functional-layer API for the view UI runtime
 *
 * Bridges the view dispatch layer to the wukong audio / agent / mode
 * subsystems. The view UI screens never call wukong_* directly; recording
 * actions posted by ui_record.c / ui_record_list.c land in ui_dispatch.c
 * which in turn drives the runtime entry points declared here.
 *
 * Internally maintains the on-disk recording list (JSON in
 * /t5_fs/tmp/record/), opens / closes the recording file, registers the
 * audio-input callback with the AI mode layer, and streams uploaded
 * recordings to the AI agent in fixed-size chunks. Recording duration is
 * tracked independently from the UI label so the persisted duration is
 * the source of truth.
 *
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_RECORD_RUNTIME_H__
#define __UI_RECORD_RUNTIME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "tkl_fs.h"

/* ---------------------------------------------------------------------------
 * Type declarations
 * --------------------------------------------------------------------------- */

/**
 * @brief Reading-card file kind enumeration
 *
 * Identifies which transcribe-result file (transcribe text vs summary text)
 * the reading-card view should target. The on-disk basename is taken from
 * REC_ITEM_T::transcribe_filename or REC_ITEM_T::summary_filename per ADR-0003.
 */
typedef enum {
    UI_REC_FILE_TRANSCRIBE = 0,  /**< raw transcribed text file */
    UI_REC_FILE_SUMMARY    = 1,  /**< summarised text file */
} UI_REC_FILE_KIND_T;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */

/**
 * @brief Enter recording mode and prepare the runtime for a new session
 * @return OPRT_OK on success, error code otherwise
 * @note Saves the current AI device mode, stops any active player and
 *       breaks the chat, switches to AI_DEVICE_MODE_RECORD, registers
 *       the audio-input callback with the AI mode layer and lazily
 *       initializes the on-disk recording list. Safe to call repeatedly:
 *       a second open() before close() reuses the same saved mode.
 */
OPERATE_RET ui_record_runtime_open(VOID_T);

/**
 * @brief Leave recording mode and release per-session resources
 * @return none
 * @note Disables the audio input wakeup, closes any in-progress file
 *       (saved as a list entry if non-empty), aborts an active upload,
 *       stops players and restores the AI device mode that was active
 *       when ui_record_runtime_open() was last called.
 */
VOID_T ui_record_runtime_close(VOID_T);

/**
 * @brief Begin recording: reset elapsed counter and enable the audio input
 * @return OPRT_OK on success, error code otherwise
 * @note Must be paired with a prior ui_record_runtime_open() call. The
 *       audio file is opened lazily on the first captured packet so the
 *       on-disk codec matches what the AI mode layer actually delivers.
 */
OPERATE_RET ui_record_runtime_start(VOID_T);

/**
 * @brief Pause an in-progress recording without closing the file
 * @return none
 * @note Disables the audio input wakeup and accumulates elapsed time so
 *       the persisted duration stays accurate across pause / resume.
 */
VOID_T ui_record_runtime_pause(VOID_T);

/**
 * @brief Resume a paused recording, re-enabling the audio input
 * @return none
 */
VOID_T ui_record_runtime_resume(VOID_T);

/**
 * @brief Stop the current recording, close the file and persist the entry
 * @return none
 * @note When the captured payload is empty the file is silently dropped
 *       (no list entry created). Otherwise the file is fsync-equivalent
 *       closed, a new RECORD_AUDIO_LIST entry is added with the captured
 *       duration in seconds, and the JSON index is rewritten.
 */
VOID_T ui_record_runtime_stop(VOID_T);

/**
 * @brief Read the persisted recording list and push it to the UI screen
 * @return none
 * @note Streams UI_RECORD_ITEM_T entries (id, name, datetime, duration)
 *       to the list module via the replace_begin/push/commit transaction.
 *       Lazily loads the JSON on first access. Safe to call before the
 *       list screen is created.
 */
VOID_T ui_record_runtime_refresh_ui_list(VOID_T);

/**
 * @brief Delete a recording entry by id (removes the file too)
 * @param[in] id record id, as exposed via UI_RECORD_ITEM_T::id
 * @return OPRT_OK on success, OPRT_COM_ERROR when no matching entry
 */
OPERATE_RET ui_record_runtime_delete(INT_T id);

/**
 * @brief Start local playback of a recording entry by id
 * @param[in] id record id
 * @return OPRT_OK on success, OPRT_COM_ERROR when no matching entry
 * @note Stops any background music first, then routes to
 *       wukong_audio_play_local() with the codec deduced from the file
 *       extension (wav / opus).
 */
OPERATE_RET ui_record_runtime_play(INT_T id);

/**
 * @brief Pause the current playback without releasing decoder context
 * @return OPRT_OK on success, error code otherwise
 * @note Forwards to wukong_audio_player_pause(); decoder/file state is
 *       retained so a follow-up ui_record_runtime_play_resume() can
 *       continue from the current offset.
 */
OPERATE_RET ui_record_runtime_play_pause(VOID_T);

/**
 * @brief Resume the playback that was suspended by ui_record_runtime_play_pause()
 * @return OPRT_OK on success, error code otherwise
 */
OPERATE_RET ui_record_runtime_play_resume(VOID_T);

/**
 * @brief Stop the current playback and release the BG audio player
 * @return OPRT_OK on success, error code otherwise
 * @note Used when the playback view is dismissed so the audio thread
 *       does not keep consuming the file in the background.
 */
OPERATE_RET ui_record_runtime_play_stop(VOID_T);

/**
 * @brief Upload a recording entry to the AI agent in fixed-size chunks
 * @param[in] id record id
 * @return OPRT_OK when the upload pipeline starts successfully
 * @note Drives an LVGL timer that reads from disk and forwards chunks to
 *       wukong_ai_agent_send_file(). Progress is pushed back to the UI
 *       through ui_record_list_set_upload_progress(). On completion a
 *       follow-up text prompt is sent so the agent transcribes / summarizes
 *       the upload.
 */
OPERATE_RET ui_record_runtime_upload(INT_T id);

/**
 * @brief Debug-print every field of a recording entry to PR_DEBUG
 * @param[in] id record id
 * @return none
 * @note No-op when the list is not yet initialized or no entry matches the
 *       given id. Intended to be called from UI tap-to-expand handlers so
 *       the full REC_ITEM_T (id / name / len / duration / create_time /
 *       md5 hex) is visible in logs without bouncing through the JSON.
 */
VOID_T ui_record_runtime_dump_info(INT_T id);

/**
 * @brief Spawn the transcribe-result poll thread (idempotent)
 * @return none
 * @note Background worker: every 5s POSTs the list of in-flight md5s
 *       (transcribe_status==0 && md5 non-zero) to
 *       m.wearable.audio.device.transcribe.result and applies the result
 *       back to REC_ITEM_T::transcribe_status. On status=1 it synchronously
 *       downloads the transcribe + summary text files via tuya_ai_http_dld_file
 *       and persists the basenames to record_list.json. Fires-and-forgets:
 *       no stop API, runs until power-off. Wrap the call site in
 *       #if ENABLE_AI_MODE_RECORD per ADR-0003.
 */
VOID_T ui_record_runtime_poll_start(VOID_T);

/**
 * @brief Check whether the entry has the requested transcribe-result file
 * @param[in] id   record id, as exposed via UI_RECORD_ITEM_T::id
 * @param[in] kind UI_REC_FILE_TRANSCRIBE or UI_REC_FILE_SUMMARY
 * @return TRUE iff the matching basename slot in REC_ITEM_T is non-empty
 * @note  Performs a list lookup under the runtime mutex; safe to call from
 *        the LVGL thread. Does NOT touch the filesystem.
 */
BOOL_T ui_record_runtime_has_file(INT_T id, UI_REC_FILE_KIND_T kind);

/**
 * @brief Open a transcribe-result file for streaming read
 * @param[in]  id       record id
 * @param[in]  kind     which file to open
 * @param[out] out_fp   receives the opened TUYA_FILE handle on success
 * @param[out] out_size receives the file size in bytes on success
 * @return OPRT_OK on success; OPRT_INVALID_PARM on bad args; OPRT_COM_ERROR
 *         when the entry has no such file or the kernel open failed
 * @note  Caller must release the returned handle with
 *        ui_record_runtime_close_file(). The basename is resolved while
 *        holding the runtime mutex, then released before the kernel call
 *        so a slow tkl_fopen() never blocks the list.
 */
OPERATE_RET ui_record_runtime_open_file(INT_T id,
                                        UI_REC_FILE_KIND_T kind,
                                        TUYA_FILE *out_fp,
                                        UINT32_T *out_size);

/**
 * @brief Read [offset, offset+size) from a previously opened reading file
 * @param[in]  fp     handle returned by ui_record_runtime_open_file()
 * @param[in]  offset byte offset from start-of-file
 * @param[out] buf    destination buffer, at least @p size bytes
 * @param[in]  size   number of bytes to read
 * @return number of bytes actually read (0 = EOF, negative = error)
 * @note  Uses tkl_fseek + tkl_fread; not safe to call concurrently on the
 *        same handle. Designed for the LVGL paginator which only ever has
 *        one reading session active at a time.
 */
INT_T ui_record_runtime_read_at(TUYA_FILE fp,
                                UINT32_T offset,
                                CHAR_T *buf,
                                UINT32_T size);

/**
 * @brief Close a handle returned by ui_record_runtime_open_file()
 * @param[in] fp handle to close (NULL is a no-op)
 * @return none
 */
VOID_T ui_record_runtime_close_file(TUYA_FILE fp);

#ifdef __cplusplus
}
#endif
#endif /* __UI_RECORD_RUNTIME_H__ */
