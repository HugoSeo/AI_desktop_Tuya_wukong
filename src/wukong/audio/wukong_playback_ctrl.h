/**
 * @file wukong_playback_ctrl.h
 * @brief Shared playback control module for MQTT and local playlist
 * @version 0.2
 * @date 2026-04-09
 * @copyright Copyright (c) 2024 Tuya Inc. All Rights Reserved.
 */

#ifndef __WUKONG_PLAYBACK_CTRL_H__
#define __WUKONG_PLAYBACK_CTRL_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
#include "wukong_fc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- music_list search parameters ---------- */
typedef struct {
    CONST CHAR_T *tag;
    CONST CHAR_T *name;
    CONST CHAR_T *artist;
    CONST CHAR_T *album_name;
    CONST CHAR_T *keyword;
    INT_T         offset;
    INT_T         limit;
    CONST CHAR_T *lang;
} WUKONG_MUSIC_LIST_PARAM_T;

/* ---------- init / basic ---------- */
OPERATE_RET wukong_playback_ctrl_init(VOID);
OPERATE_RET wukong_playback_ctrl_get_status(WUKONG_MUSIC_PLAYER_T *out);
OPERATE_RET wukong_playback_ctrl_send_mqtt(CONST CHAR_T *action);

/* ---------- async MQTT request/response ---------- */
OPERATE_RET wukong_playback_ctrl_send_music_list(
    CONST WUKONG_MUSIC_LIST_PARAM_T *param,
    ty_cJSON **out_result,
    INT_T timeout_ms);

OPERATE_RET wukong_playback_ctrl_send_refresh_url(
    CONST CHAR_T *audio_ids,
    INT_T bitrate,
    CONST CHAR_T *channel_code,
    ty_cJSON **out_result,
    INT_T timeout_ms);

OPERATE_RET wukong_playback_ctrl_dispatch_response(
    CONST CHAR_T *biz_id,
    CONST ty_cJSON *json);

/* ---------- optional persistent playlist storage ---------- */

/**
 * @brief Optional callbacks to persist the in-memory playlist and notify UI.
 * @note Register via wukong_playback_ctrl_register_storage() before wukong_playback_ctrl_init().
 */
typedef struct {
    /**
     * @brief Persist playlist JSON (array of items, same shape as wukong_playback_playlist_list()).
     * @param[in] playlist_json root array; must not be modified or freed by the callee.
     */
    OPERATE_RET (*save)(CONST ty_cJSON *playlist_json);
    /**
     * @brief Load playlist from storage; on success set *playlist_json to a heap-allocated array
     *        (caller wukong_playback_ctrl_init frees it with ty_cJSON_Delete).
     */
    OPERATE_RET (*load)(ty_cJSON **playlist_json);
    /** @brief Called after save (e.g. refresh UI); may be NULL. */
    VOID (*on_changed)(VOID);
    /** @brief Max playlist items; 0 means default (50). */
    INT_T max_items;
} WUKONG_PLAYBACK_STORAGE_OPS_T;

/**
 * @brief Register playlist storage hooks (must be called before wukong_playback_ctrl_init()).
 * @param[in] ops storage callbacks and max_items; pointer is kept, must remain valid for app lifetime
 * @return OPRT_OK, OPRT_INVALID_PARM, or OPRT_INIT_MORE_THAN_ONCE if init already ran
 */
OPERATE_RET wukong_playback_ctrl_register_storage(CONST WUKONG_PLAYBACK_STORAGE_OPS_T *ops);

/**
 * @brief Return the playlist id currently playing, or -1 if none.
 */
INT_T wukong_playback_ctrl_get_current_play_id(VOID);

/* ---------- playlist ---------- */
OPERATE_RET wukong_playback_playlist_add(CONST CHAR_T *song_name, CONST CHAR_T *artist,
    CONST CHAR_T *song_url, CONST CHAR_T *audio_id, CONST CHAR_T *channel_code,
    INT_T *out_playlist_id);
OPERATE_RET wukong_playback_playlist_remove(INT_T id);
OPERATE_RET wukong_playback_playlist_list(ty_cJSON **out);
OPERATE_RET wukong_playback_playlist_clear(VOID);
OPERATE_RET wukong_playback_playlist_play(INT_T id);

/**
 * @brief Play the next item in the local playlist relative to the last played item.
 * @return OPRT_OK on success; OPRT_COM_ERROR if at the end or list is empty.
 * @note The caller should fall back to the cloud (MQTT next) on OPRT_COM_ERROR.
 */
OPERATE_RET wukong_playback_playlist_next(VOID);

/**
 * @brief Play the previous item in the local playlist relative to the last played item.
 * @return OPRT_OK on success; OPRT_COM_ERROR if at the beginning or list is empty.
 * @note The caller should fall back to the cloud (MQTT prev) on OPRT_COM_ERROR.
 */
OPERATE_RET wukong_playback_playlist_prev(VOID);

/* ---------- auto play next ---------- */

/**
 * @brief Enable or disable auto-play-next. When enabled, the next song in the
 *        playlist is played automatically after the current one finishes.
 * @param[in] enable TRUE to enable, FALSE to disable
 * @return none
 */
VOID wukong_playback_ctrl_set_auto_next(BOOL_T enable);

/**
 * @brief Query whether auto-play-next is currently enabled.
 * @return TRUE if enabled, FALSE otherwise
 */
BOOL_T wukong_playback_ctrl_get_auto_next(VOID);

/* ---------- play mode ---------- */
typedef enum {
    WK_PLAY_MODE_SEQUENCE = 0,  /* 顺序：本地末尾走 fetch_more（默认，旧行为） */
    WK_PLAY_MODE_LIST_LOOP,     /* 列表循环：本地末尾回到第一首 */
    WK_PLAY_MODE_SINGLE_LOOP,   /* 单曲循环：重播当前曲目 */
    WK_PLAY_MODE_SHUFFLE,       /* 随机 */
    WK_PLAY_MODE_MAX
} WK_PLAY_MODE_E;

/**
 * @brief Set playback mode (affects auto-next behaviour). Held in memory, not persisted.
 * @param[in] mode one of WK_PLAY_MODE_E
 * @return none
 */
VOID wukong_playback_ctrl_set_play_mode(WK_PLAY_MODE_E mode);

/**
 * @brief Get current playback mode.
 * @return current WK_PLAY_MODE_E
 */
WK_PLAY_MODE_E wukong_playback_ctrl_get_play_mode(VOID);

/**
 * @brief Save the search context from a music_list request so that
 *        auto-play-next can fetch more pages with the same parameters.
 * @param[in] param        search parameters used for the request
 * @param[in] next_offset  offset for the next page
 * @param[in] has_more     TRUE if the cloud indicated more pages are available
 * @return none
 */
VOID wukong_playback_ctrl_save_search_ctx(CONST WUKONG_MUSIC_LIST_PARAM_T *param,
                                           INT_T next_offset,
                                           BOOL_T has_more);

/**
 * @brief Fetch the next page of songs using the saved search context.
 *        Sends music_list with incremented offset, adds results to the
 *        playlist, and starts playing the first new item.
 * @return OPRT_OK if new songs were fetched and playback started;
 *         OPRT_COM_ERROR if no search context, no more pages, or request failed.
 */
OPERATE_RET wukong_playback_ctrl_fetch_more(VOID);

/**
 * @brief Non-blocking next for UI callbacks.
 *        Fully asynchronous: navigation runs on the background worker thread
 *        (never blocks the caller on URL refresh); falls back to fetch_more
 *        at the end of the local playlist.
 * @return OPRT_OK if queued; result delivered later via EVENT_MUSIC_PLAYER.
 */
OPERATE_RET wukong_playback_ctrl_next(VOID);

/**
 * @brief Non-blocking prev for UI callbacks.
 *        Dispatches playlist_prev to the background worker thread (never
 *        blocks the caller on URL refresh). At the beginning of the playlist
 *        the request is dropped and the current track keeps playing.
 * @return OPRT_OK if queued; result delivered later via EVENT_MUSIC_PLAYER.
 */
OPERATE_RET wukong_playback_ctrl_prev(VOID);

/**
 * @brief Non-blocking play of a specific playlist item for UI callbacks.
 *        Dispatches wukong_playback_playlist_play(id) to the background
 *        worker thread so the LVGL task is never blocked during URL refresh.
 * @param[in] id  playlist item id returned by wukong_playback_playlist_list()
 * @return OPRT_OK if queued; OPRT_COM_ERROR if module not ready.
 */
OPERATE_RET wukong_playback_playlist_play_async(INT_T id);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_PLAYBACK_CTRL_H__ */
