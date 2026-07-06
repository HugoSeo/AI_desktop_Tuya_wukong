/**
 * @file wukong_playback_ctrl.c
 * @brief Shared playback control for music state, MQTT playcontrol and in-memory playlist
 * @version 0.1
 * @date 2026-03-26
 * @copyright Copyright (c) 2024 Tuya Inc. All Rights Reserved.
 */

/***********************************************************************
 ** INCLUDE                                                           **
 **********************************************************************/
#include <stdio.h>
#include <string.h>

#include "wukong_playback_ctrl.h"

#include "base_event.h"
#include "smart_frame.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_system.h"
#include "tal_thread.h"
#include "tuya_error_code.h"
#include "tuya_iot_internal_api.h"
#include "tuya_list.h"
#include "uni_random.h"
#include "wukong_audio_player.h"

/***********************************************************************
 ** CONSTANT ( MACRO AND ENUM )                                       **
 **********************************************************************/
#define PLAYBACK_PLAYLIST_DEFAULT_MAX      50
#define PLAYBACK_PLAYLIST_HARD_CAP         128
#define PLAYBACK_MUSIC_NAME_MAX_LEN        128
#define PLAYBACK_MUSIC_ARTIST_MAX_LEN      128
#define PLAYBACK_MUSIC_URL_MAX_LEN         512
#define PLAYBACK_BIZ_ID_MAX_LEN            64
#define PLAYBACK_MQTT_DATA_MAX_LEN         256
#define PLAYBACK_MQTT_LARGE_DATA_MAX_LEN   1024
#define PLAYBACK_PENDING_MAX               4
#define PLAYBACK_AUDIO_ID_MAX_LEN          48
#define PLAYBACK_CHANNEL_CODE_MAX_LEN      48
#define PLAYBACK_AUTO_NEXT_STACK_SIZE      8192
#define PLAYBACK_AUTO_NEXT_SETTLE_MS       800
#define PLAYBACK_SEARCH_FIELD_MAX_LEN      128
#define PLAYBACK_FETCH_MORE_TIMEOUT_MS     12000

/***********************************************************************
 ** STRUCT                                                            **
 **********************************************************************/
typedef struct {
    INT_T id;
    CHAR_T song_name[PLAYBACK_MUSIC_NAME_MAX_LEN];
    CHAR_T artist[PLAYBACK_MUSIC_ARTIST_MAX_LEN];
    CHAR_T song_url[PLAYBACK_MUSIC_URL_MAX_LEN];
    CHAR_T audio_id[PLAYBACK_AUDIO_ID_MAX_LEN];
    CHAR_T channel_code[PLAYBACK_CHANNEL_CODE_MAX_LEN];
    LIST_HEAD list_node;
} WUKONG_PLAYBACK_ITEM_T;

typedef struct {
    INT_T num;
    LIST_HEAD list_head;
} WUKONG_PLAYBACK_LIST_T;

typedef struct {
    BOOL_T     used;
    CHAR_T     biz_id[PLAYBACK_BIZ_ID_MAX_LEN];
    SEM_HANDLE sem;
    ty_cJSON  *result;
} WUKONG_PENDING_REQ_T;

typedef struct {
    BOOL_T valid;
    CHAR_T tag[PLAYBACK_SEARCH_FIELD_MAX_LEN];
    CHAR_T keyword[PLAYBACK_SEARCH_FIELD_MAX_LEN];
    CHAR_T name[PLAYBACK_SEARCH_FIELD_MAX_LEN];
    CHAR_T artist[PLAYBACK_SEARCH_FIELD_MAX_LEN];
    CHAR_T album_name[PLAYBACK_SEARCH_FIELD_MAX_LEN];
    CHAR_T lang[16];
    INT_T  limit;
    INT_T  next_offset;
    BOOL_T has_more;
} PLAYBACK_SEARCH_CTX_T;

/***********************************************************************
 ** VARIABLE                                                          **
 **********************************************************************/
STATIC BOOL_T s_playback_inited = FALSE;
STATIC MUTEX_HANDLE s_playback_mutex = NULL;
STATIC WUKONG_MUSIC_PLAYER_T s_music_player = {0};
STATIC WUKONG_PLAYBACK_LIST_T s_playlist = {0};
STATIC CONST WUKONG_PLAYBACK_STORAGE_OPS_T *s_storage_ops = NULL;
STATIC INT_T s_playlist_max_num = PLAYBACK_PLAYLIST_DEFAULT_MAX;
STATIC BOOL_T s_storage_load_in_progress = FALSE;
STATIC WUKONG_PENDING_REQ_T s_pending[PLAYBACK_PENDING_MAX] = {{0}};
STATIC MUTEX_HANDLE s_pending_mutex = NULL;
/* ID of the playlist item currently playing; -1 = none */
STATIC INT_T s_current_play_id = -1;
/* Auto-play-next: when a song finishes, automatically play the next one */
STATIC BOOL_T s_auto_play_next = TRUE;
STATIC BOOL_T s_switching_track = FALSE;
STATIC SEM_HANDLE s_auto_next_sem = NULL;
STATIC THREAD_HANDLE s_auto_next_thread = NULL;
/* User-triggered next: skip settle delay and bypass s_auto_play_next guard */
STATIC BOOL_T s_user_next_pending = FALSE;
/* User-triggered async play of a specific playlist id; -1 = none */
STATIC INT_T s_user_play_id = -1;
STATIC PLAYBACK_SEARCH_CTX_T s_search_ctx = {0};

/***********************************************************************
 ** FUNCTON                                                           **
 **********************************************************************/
STATIC INT_T __playback_playlist_alloc_id_locked(VOID);
STATIC VOID __playback_playlist_clear_locked(VOID);
STATIC BOOL_T __playback_playlist_has_url_locked(CONST CHAR_T *song_url);
STATIC BOOL_T __playback_playlist_has_audio_id_locked(CONST CHAR_T *audio_id);
STATIC VOID __playback_playlist_fifo_evict_locked(VOID);

STATIC VOID __playback_safe_copy(CHAR_T *dst, UINT_T dst_size, CONST CHAR_T *src)
{
    if ((dst == NULL) || (dst_size == 0)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

STATIC OPERATE_RET __playback_require_init(VOID)
{
    if (s_playback_inited == FALSE || s_playback_mutex == NULL) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief Build JSON array of playlist items; caller must hold s_playback_mutex.
 * @return heap array or NULL on failure
 */
STATIC ty_cJSON *__playback_playlist_serialize_locked(VOID)
{
    ty_cJSON *list = NULL;
    LIST_HEAD *pos = NULL;

    list = ty_cJSON_CreateArray();
    if (list == NULL) {
        return NULL;
    }

    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);
        ty_cJSON *item_json = NULL;

        if (item == NULL) {
            continue;
        }

        item_json = ty_cJSON_CreateObject();
        if (item_json == NULL) {
            ty_cJSON_Delete(list);
            return NULL;
        }

        ty_cJSON_AddNumberToObject(item_json, "id", item->id);
        ty_cJSON_AddStringToObject(item_json, "song_name", item->song_name);
        ty_cJSON_AddStringToObject(item_json, "artist", item->artist);
        ty_cJSON_AddStringToObject(item_json, "song_url", item->song_url);
        ty_cJSON_AddStringToObject(item_json, "audio_id", item->audio_id);
        ty_cJSON_AddStringToObject(item_json, "channel_code", item->channel_code);
        ty_cJSON_AddItemToArray(list, item_json);
    }

    return list;
}

STATIC VOID __playback_persist_and_notify(VOID)
{
    ty_cJSON *arr = NULL;
    OPERATE_RET save_rt = OPRT_OK;

    if (s_storage_ops == NULL || s_storage_ops->save == NULL) {
        return;
    }
    if (s_storage_load_in_progress != FALSE) {
        return;
    }
    if (s_playback_mutex == NULL) {
        return;
    }

    tal_mutex_lock(s_playback_mutex);
    arr = __playback_playlist_serialize_locked();
    tal_mutex_unlock(s_playback_mutex);

    if (arr == NULL) {
        TAL_PR_WARN("[playback] persist: serialize failed");
        return;
    }

    save_rt = s_storage_ops->save(arr);
    if (save_rt != OPRT_OK) {
        TAL_PR_WARN("[playback] persist: save failed rt=%d", save_rt);
    }

    if (s_storage_ops->on_changed != NULL) {
        s_storage_ops->on_changed();
    }

    ty_cJSON_Delete(arr);
}

STATIC BOOL_T __playback_playlist_id_used_locked(INT_T id)
{
    LIST_HEAD *pos = NULL;

    if (id < 0 || id >= s_playlist_max_num) {
        return FALSE;
    }

    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);

        if (item != NULL && item->id == id) {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * @brief Apply loaded JSON array into playlist; caller holds s_playback_mutex; no persist.
 */
STATIC VOID __playback_apply_loaded_json_array_locked(ty_cJSON *arr)
{
    INT_T n = 0;
    INT_T i = 0;

    if (arr == NULL || !ty_cJSON_IsArray(arr)) {
        return;
    }

    n = ty_cJSON_GetArraySize(arr);
    for (i = 0; i < n; i++) {
        ty_cJSON *row = ty_cJSON_GetArrayItem(arr, i);
        ty_cJSON *j_id = NULL;
        ty_cJSON *j_name = NULL;
        ty_cJSON *j_artist = NULL;
        ty_cJSON *j_url = NULL;
        ty_cJSON *j_aid = NULL;
        ty_cJSON *j_ch = NULL;
        CONST CHAR_T *song_name = NULL;
        CONST CHAR_T *artist = NULL;
        CONST CHAR_T *song_url = NULL;
        CONST CHAR_T *audio_id = NULL;
        CONST CHAR_T *channel_code = NULL;
        INT_T prefer_id = -1;
        INT_T music_id = -1;
        BOOL_T has_url = FALSE;
        BOOL_T has_aid = FALSE;
        WUKONG_PLAYBACK_ITEM_T *item = NULL;

        if (row == NULL || !ty_cJSON_IsObject(row)) {
            continue;
        }

        j_id = ty_cJSON_GetObjectItem(row, "id");
        j_name = ty_cJSON_GetObjectItem(row, "song_name");
        j_artist = ty_cJSON_GetObjectItem(row, "artist");
        j_url = ty_cJSON_GetObjectItem(row, "song_url");
        j_aid = ty_cJSON_GetObjectItem(row, "audio_id");
        j_ch = ty_cJSON_GetObjectItem(row, "channel_code");

        song_name = (j_name != NULL && ty_cJSON_IsString(j_name)) ? j_name->valuestring : NULL;
        if (song_name == NULL || song_name[0] == '\0') {
            continue;
        }

        artist = (j_artist != NULL && ty_cJSON_IsString(j_artist)) ? j_artist->valuestring : "";
        song_url = (j_url != NULL && ty_cJSON_IsString(j_url)) ? j_url->valuestring : "";
        audio_id = (j_aid != NULL && ty_cJSON_IsString(j_aid)) ? j_aid->valuestring : "";
        channel_code = (j_ch != NULL && ty_cJSON_IsString(j_ch)) ? j_ch->valuestring : "";

        has_url = (song_url != NULL && song_url[0] != '\0');
        has_aid = (audio_id != NULL && audio_id[0] != '\0');
        if (!has_url && !has_aid) {
            continue;
        }

        if (j_id != NULL && ty_cJSON_IsNumber(j_id)) {
            prefer_id = j_id->valueint;
        } else {
            prefer_id = -1;
        }

        if (prefer_id >= 0 && prefer_id < s_playlist_max_num &&
            __playback_playlist_id_used_locked(prefer_id) == FALSE) {
            music_id = prefer_id;
        } else {
            music_id = __playback_playlist_alloc_id_locked();
        }

        if (music_id < 0) {
            continue;
        }

        if (has_url && __playback_playlist_has_url_locked(song_url) == TRUE) {
            continue;
        }
        if (!has_url && has_aid && __playback_playlist_has_audio_id_locked(audio_id) == TRUE) {
            continue;
        }

        if (s_playlist.num >= s_playlist_max_num) {
            __playback_playlist_fifo_evict_locked();
        }

        item = (WUKONG_PLAYBACK_ITEM_T *)tal_malloc(sizeof(WUKONG_PLAYBACK_ITEM_T));
        if (item == NULL) {
            continue;
        }

        memset(item, 0, sizeof(WUKONG_PLAYBACK_ITEM_T));
        item->id = music_id;
        __playback_safe_copy(item->song_name, SIZEOF(item->song_name), song_name);
        __playback_safe_copy(item->artist, SIZEOF(item->artist), artist);
        if (has_url) {
            __playback_safe_copy(item->song_url, SIZEOF(item->song_url), song_url);
        }
        if (has_aid) {
            __playback_safe_copy(item->audio_id, SIZEOF(item->audio_id), audio_id);
        }
        if (channel_code != NULL && channel_code[0] != '\0') {
            __playback_safe_copy(item->channel_code, SIZEOF(item->channel_code), channel_code);
        }
        tuya_list_add_tail(&item->list_node, &s_playlist.list_head);
        s_playlist.num++;
    }
}

STATIC VOID __playback_storage_load_from_ops(VOID)
{
    ty_cJSON *root = NULL;
    OPERATE_RET ld_rt = OPRT_OK;

    if (s_storage_ops == NULL || s_storage_ops->load == NULL) {
        TAL_PR_DEBUG("[playback] storage load: no ops");
        return;
    }

    ld_rt = s_storage_ops->load(&root);
    if (ld_rt != OPRT_OK) {
        TAL_PR_NOTICE("[playback] storage load: failed rt=%d", ld_rt);
        return;
    }
    if (root == NULL) {
        TAL_PR_DEBUG("[playback] storage load: no data file");
        return;
    }

    tal_mutex_lock(s_playback_mutex);
    s_storage_load_in_progress = TRUE;
    __playback_playlist_clear_locked();
    s_current_play_id = -1;
    __playback_apply_loaded_json_array_locked(root);
    s_storage_load_in_progress = FALSE;
    tal_mutex_unlock(s_playback_mutex);

    ty_cJSON_Delete(root);
    {
        INT_T cnt = 0;
        tal_mutex_lock(s_playback_mutex);
        cnt = s_playlist.num;
        tal_mutex_unlock(s_playback_mutex);
        TAL_PR_NOTICE("[playback] storage load: items=%d", cnt);
    }
}

OPERATE_RET wukong_playback_ctrl_register_storage(CONST WUKONG_PLAYBACK_STORAGE_OPS_T *ops)
{
    if (ops == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (s_playback_inited == TRUE) {
        TAL_PR_WARN("[playback] register_storage after init ignored");
        return OPRT_INIT_MORE_THAN_ONCE;
    }

    s_storage_ops = ops;
    if (ops->max_items > PLAYBACK_PLAYLIST_HARD_CAP) {
        s_playlist_max_num = PLAYBACK_PLAYLIST_HARD_CAP;
    } else if (ops->max_items > 0) {
        s_playlist_max_num = ops->max_items;
    } else {
        s_playlist_max_num = PLAYBACK_PLAYLIST_DEFAULT_MAX;
    }

    TAL_PR_NOTICE("[playback] register_storage max_items=%d", s_playlist_max_num);
    return OPRT_OK;
}

INT_T wukong_playback_ctrl_get_current_play_id(VOID)
{
    INT_T id = -1;

    if (s_playback_mutex == NULL) {
        return -1;
    }

    tal_mutex_lock(s_playback_mutex);
    id = s_current_play_id;
    tal_mutex_unlock(s_playback_mutex);
    return id;
}

STATIC VOID __playback_generate_biz_id(CHAR_T *id, INT_T id_len)
{
    BYTE_T i = 0;
    BYTE_T r[4] = {0};

    if ((id == NULL) || (id_len <= 0)) {
        return;
    }

    for (i = 0; i < SIZEOF(r); i++) {
        r[i] = (BYTE_T)uni_random_range(0xFF);
    }

    snprintf(id, id_len, "%s_%02x%02x%02x%02x", get_gw_cntl()->gw_if.id, r[0], r[1], r[2], r[3]);
}

STATIC VOID __playback_playlist_clear_locked(VOID)
{
    LIST_HEAD *pos = NULL;
    LIST_HEAD *next = NULL;

    tuya_list_for_each_safe(pos, next, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);

        if (item == NULL) {
            continue;
        }

        tuya_list_del(&item->list_node);
        tal_free(item);
    }

    s_playlist.num = 0;
}

STATIC BOOL_T __playback_playlist_has_url_locked(CONST CHAR_T *song_url)
{
    LIST_HEAD *pos = NULL;

    if ((song_url == NULL) || (song_url[0] == '\0')) {
        return FALSE;
    }

    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);

        if (item == NULL) {
            continue;
        }

        if ((item->song_url[0] != '\0') && (strcmp(item->song_url, song_url) == 0)) {
            return TRUE;
        }
    }

    return FALSE;
}

STATIC BOOL_T __playback_playlist_has_audio_id_locked(CONST CHAR_T *audio_id)
{
    LIST_HEAD *pos = NULL;

    if ((audio_id == NULL) || (audio_id[0] == '\0')) {
        return FALSE;
    }

    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);

        if (item == NULL) {
            continue;
        }

        if ((item->audio_id[0] != '\0') && (strcmp(item->audio_id, audio_id) == 0)) {
            return TRUE;
        }
    }

    return FALSE;
}

STATIC INT_T __playback_playlist_get_id_by_url_locked(CONST CHAR_T *song_url)
{
    LIST_HEAD *pos = NULL;

    if ((song_url == NULL) || (song_url[0] == '\0')) {
        return -1;
    }

    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);

        if (item != NULL && item->song_url[0] != '\0' &&
            strcmp(item->song_url, song_url) == 0) {
            return item->id;
        }
    }

    return -1;
}

STATIC INT_T __playback_playlist_get_id_by_audio_id_locked(CONST CHAR_T *audio_id)
{
    LIST_HEAD *pos = NULL;

    if ((audio_id == NULL) || (audio_id[0] == '\0')) {
        return -1;
    }

    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);

        if (item != NULL && item->audio_id[0] != '\0' &&
            strcmp(item->audio_id, audio_id) == 0) {
            return item->id;
        }
    }

    return -1;
}

STATIC INT_T __playback_playlist_alloc_id_locked(VOID)
{
    BOOL_T *used = NULL;
    LIST_HEAD *pos = NULL;
    INT_T id = 0;

    if (s_playlist_max_num <= 0) {
        return -1;
    }

    used = (BOOL_T *)tal_malloc((UINT_T)s_playlist_max_num * sizeof(BOOL_T));
    if (used == NULL) {
        return -1;
    }
    memset(used, 0, (UINT_T)s_playlist_max_num * sizeof(BOOL_T));

    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);

        if (item == NULL) {
            continue;
        }

        if ((item->id >= 0) && (item->id < s_playlist_max_num)) {
            used[item->id] = TRUE;
        }
    }

    for (id = 0; id < s_playlist_max_num; id++) {
        if (used[id] == FALSE) {
            tal_free(used);
            return id;
        }
    }

    tal_free(used);
    return -1;
}

STATIC VOID __playback_playlist_fifo_evict_locked(VOID)
{
    WUKONG_PLAYBACK_ITEM_T *first = NULL;

    if (tuya_list_empty(&s_playlist.list_head)) {
        return;
    }

    first = tuya_list_entry(s_playlist.list_head.next, WUKONG_PLAYBACK_ITEM_T, list_node);
    if (first == NULL) {
        return;
    }

    tuya_list_del(&first->list_node);
    tal_free(first);
    if (s_playlist.num > 0) {
        s_playlist.num--;
    }
}

/**
 * @brief Fetch the next page of music_list using the saved search context,
 *        parse items, add to playlist, and play the first new item.
 * @return OPRT_OK if new songs were fetched and playback started;
 *         OPRT_COM_ERROR if no search context, no more pages, or request failed.
 */
OPERATE_RET wukong_playback_ctrl_fetch_more(VOID)
{
    WUKONG_MUSIC_LIST_PARAM_T param = {0};
    ty_cJSON *result = NULL;
    ty_cJSON *data_node = NULL;
    ty_cJSON *items = NULL;
    ty_cJSON *page_node = NULL;
    INT_T first_id = -1;
    INT_T added = 0;
    OPERATE_RET rt = OPRT_OK;

    tal_mutex_lock(s_playback_mutex);
    if (!s_search_ctx.valid || !s_search_ctx.has_more) {
        tal_mutex_unlock(s_playback_mutex);
        TAL_PR_DEBUG("[playback] fetch_more: no ctx or no more pages");
        return OPRT_COM_ERROR;
    }

    param.tag        = s_search_ctx.tag[0]        ? s_search_ctx.tag        : NULL;
    param.keyword    = s_search_ctx.keyword[0]    ? s_search_ctx.keyword    : NULL;
    param.name       = s_search_ctx.name[0]       ? s_search_ctx.name       : NULL;
    param.artist     = s_search_ctx.artist[0]     ? s_search_ctx.artist     : NULL;
    param.album_name = s_search_ctx.album_name[0] ? s_search_ctx.album_name : NULL;
    param.lang       = s_search_ctx.lang[0]       ? s_search_ctx.lang       : NULL;
    param.offset     = s_search_ctx.next_offset;
    param.limit      = s_search_ctx.limit > 0 ? s_search_ctx.limit : 5;
    tal_mutex_unlock(s_playback_mutex);

    TAL_PR_NOTICE("[playback] fetch_more: offset=%d limit=%d", param.offset, param.limit);

    rt = wukong_playback_ctrl_send_music_list(&param, &result, PLAYBACK_FETCH_MORE_TIMEOUT_MS);
    if (rt != OPRT_OK || result == NULL) {
        TAL_PR_ERR("[playback] fetch_more: music_list failed rt=%d", rt);
        return OPRT_COM_ERROR;
    }

    data_node = ty_cJSON_GetObjectItem(result, "data");
    items = data_node ? ty_cJSON_GetObjectItem(data_node, "items") : NULL;
    if (items == NULL || !ty_cJSON_IsArray(items) || ty_cJSON_GetArraySize(items) == 0) {
        TAL_PR_NOTICE("[playback] fetch_more: empty result");
        ty_cJSON_Delete(result);
        tal_mutex_lock(s_playback_mutex);
        s_search_ctx.has_more = FALSE;
        tal_mutex_unlock(s_playback_mutex);
        return OPRT_COM_ERROR;
    }

    INT_T item_cnt = ty_cJSON_GetArraySize(items);
    for (INT_T i = 0; i < item_cnt; i++) {
        ty_cJSON *row = ty_cJSON_GetArrayItem(items, i);
        if (row == NULL) {
            continue;
        }

        ty_cJSON *j_name   = ty_cJSON_GetObjectItem(row, "name");
        ty_cJSON *j_artist = ty_cJSON_GetObjectItem(row, "artist");
        ty_cJSON *j_aid    = ty_cJSON_GetObjectItem(row, "audioId");
        ty_cJSON *j_ch     = ty_cJSON_GetObjectItem(row, "channelCode");
        ty_cJSON *j_url    = ty_cJSON_GetObjectItem(row, "playUrl");

        CONST CHAR_T *sn = (j_name && ty_cJSON_IsString(j_name)) ? j_name->valuestring : NULL;
        CONST CHAR_T *sa = (j_aid  && ty_cJSON_IsString(j_aid))  ? j_aid->valuestring  : NULL;
        if (sn == NULL || sn[0] == '\0' || sa == NULL || sa[0] == '\0') {
            continue;
        }

        CONST CHAR_T *s_art = NULL;
        if (j_artist && ty_cJSON_IsString(j_artist)) {
            s_art = j_artist->valuestring;
        } else if (j_artist && ty_cJSON_IsArray(j_artist) && ty_cJSON_GetArraySize(j_artist) > 0) {
            ty_cJSON *a0 = ty_cJSON_GetArrayItem(j_artist, 0);
            if (a0 && ty_cJSON_IsString(a0)) {
                s_art = a0->valuestring;
            }
        }
        CONST CHAR_T *s_ch  = (j_ch  && ty_cJSON_IsString(j_ch))  ? j_ch->valuestring  : NULL;
        CONST CHAR_T *s_url = (j_url && ty_cJSON_IsString(j_url) && j_url->valuestring[0] != '\0')
                                  ? j_url->valuestring : NULL;

        INT_T assigned_id = -1;
        if (wukong_playback_playlist_add(sn, s_art, s_url, sa, s_ch, &assigned_id) == OPRT_OK) {
            added++;
            if (first_id < 0) {
                first_id = assigned_id;
            }
        }
    }

    /* Update pagination from response */
    page_node = data_node ? ty_cJSON_GetObjectItem(data_node, "page") : NULL;
    tal_mutex_lock(s_playback_mutex);
    if (page_node != NULL) {
        ty_cJSON *j_more = ty_cJSON_GetObjectItem(page_node, "hasMore");
        s_search_ctx.has_more = (j_more != NULL && j_more->type == ty_cJSON_True);
    } else {
        s_search_ctx.has_more = (item_cnt >= param.limit);
    }
    s_search_ctx.next_offset = param.offset + item_cnt;
    tal_mutex_unlock(s_playback_mutex);

    ty_cJSON_Delete(result);

    TAL_PR_NOTICE("[playback] fetch_more: added %d/%d, next_offset=%d, has_more=%d",
                  added, item_cnt, param.offset + item_cnt, s_search_ctx.has_more);

    if (first_id < 0) {
        return OPRT_COM_ERROR;
    }

    return wukong_playback_playlist_play(first_id);
}

/**
 * @brief Auto-play-next worker thread. Waits on s_auto_next_sem and plays the
 *        next song from the local playlist, falling back to fetching the next
 *        page via music_list if the local list has been exhausted.
 * @param[in] arg unused
 * @return none
 */
STATIC VOID __auto_next_thread_func(VOID *arg)
{
    (VOID)arg;

    for (;;) {
        if (tal_semaphore_wait(s_auto_next_sem, SEM_WAIT_FOREVER) != OPRT_OK) {
            continue;
        }

        tal_mutex_lock(s_playback_mutex);
        BOOL_T user_next = s_user_next_pending;
        INT_T  user_play = s_user_play_id;
        s_user_next_pending = FALSE;
        s_user_play_id      = -1;
        tal_mutex_unlock(s_playback_mutex);

        /* User-requested play of a specific track — no settle delay needed */
        if (user_play >= 0) {
            TAL_PR_NOTICE("[%s] user-play id:%d", __func__, user_play);
            wukong_playback_playlist_play(user_play);
            continue;
        }

        /* Auto-next: wait for audio pipeline to fully stop before seeking next */
        if (!user_next) {
            tal_system_sleep(PLAYBACK_AUTO_NEXT_SETTLE_MS);
        }

        tal_mutex_lock(s_playback_mutex);
        BOOL_T should_next = user_next || (s_auto_play_next && s_current_play_id >= 0);
        tal_mutex_unlock(s_playback_mutex);
        if (!should_next) {
            continue;
        }

        TAL_PR_NOTICE("[%s] %s triggered", __func__,
                      user_next ? "user-next" : "auto-play-next");

        tal_mutex_lock(s_playback_mutex);
        s_switching_track = TRUE;
        tal_mutex_unlock(s_playback_mutex);

        OPERATE_RET rt = wukong_playback_playlist_next();
        if (rt == OPRT_OK) {
            TAL_PR_NOTICE("[%s] %s: local playlist", __func__,
                          user_next ? "user-next" : "auto-play-next");
            continue;
        }

        TAL_PR_NOTICE("[%s] %s: local end, fetching more via music_list", __func__,
                      user_next ? "user-next" : "auto-play-next");
        rt = wukong_playback_ctrl_fetch_more();
        if (rt == OPRT_OK) {
            TAL_PR_NOTICE("[%s] %s: loaded next page", __func__,
                          user_next ? "user-next" : "auto-play-next");
            continue;
        }

        TAL_PR_NOTICE("[%s] %s: no more pages, playback finished", __func__,
                      user_next ? "user-next" : "auto-play-next");
        tal_mutex_lock(s_playback_mutex);
        s_switching_track = FALSE;
        tal_mutex_unlock(s_playback_mutex);
    }
}

STATIC INT_T __music_player_event_cb(VOID *data)
{
    WUKONG_MUSIC_PLAYER_T *msg = NULL;
    WUKONG_MUSIC_PLAYER_TYPE_E cmd = MUSIC_PLAYER_STATE;

    if (data == NULL) {
        return OPRT_OK;
    }

    if (__playback_require_init() != OPRT_OK) {
        return OPRT_OK;
    }

    msg = (WUKONG_MUSIC_PLAYER_T *)data;
    cmd = msg->cmd;

    BOOL_T trigger_auto_next = FALSE;
    STATIC CONST CHAR_T *state_names[] = {"stopped", "playing", "paused"};

    tal_mutex_lock(s_playback_mutex);
    switch (cmd)
    {
        case MUSIC_PLAYER_STATE:
            s_music_player.state = msg->state;
            TAL_PR_DEBUG("[playback] state -> %s (cur_id:%d switching:%d auto:%d)",
                         (msg->state <= AI_PLAYER_PAUSED) ? state_names[msg->state] : "?",
                         s_current_play_id, s_switching_track, s_auto_play_next);
            if (msg->state == AI_PLAYER_PLAYING) {
                s_switching_track = FALSE;
            } else if (msg->state == AI_PLAYER_STOPPED) {
                if (s_auto_play_next && !s_switching_track &&
                    s_current_play_id >= 0 && s_auto_next_sem != NULL) {
                    trigger_auto_next = TRUE;
                }
            }
            break;
        case MUSIC_PLAYER_DATA:
            TAL_PR_NOTICE("[playback] cloud push: \"%s\" - \"%s\"",
                          msg->song_name, msg->artist);
            __playback_safe_copy(s_music_player.song_name, SIZEOF(s_music_player.song_name), msg->song_name);
            __playback_safe_copy(s_music_player.artist, SIZEOF(s_music_player.artist), msg->artist);
            __playback_safe_copy(s_music_player.song_url, SIZEOF(s_music_player.song_url), msg->song_url);
            s_music_player.state = AI_PLAYER_STOPPED;
            break;
        default:
            break;
    }
    tal_mutex_unlock(s_playback_mutex);

    if (trigger_auto_next) {
        TAL_PR_NOTICE("[playback] song ended, scheduling auto-next");
        tal_semaphore_post(s_auto_next_sem);
    }

    if (cmd == MUSIC_PLAYER_DATA) {
        INT_T added_id = -1;
        OPERATE_RET add_rt = wukong_playback_playlist_add(
            msg->song_name, msg->artist, msg->song_url, NULL, NULL, &added_id);
        if (add_rt != OPRT_OK) {
            TAL_PR_WARN("[playback] auto-add to playlist failed: %d", add_rt);
        } else if (added_id >= 0) {
            TAL_PR_DEBUG("[playback] cloud song added to playlist id:%d", added_id);
            tal_mutex_lock(s_playback_mutex);
            s_current_play_id = added_id;
            tal_mutex_unlock(s_playback_mutex);
        }
    }

    return OPRT_OK;
}

OPERATE_RET wukong_playback_ctrl_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_playback_inited == TRUE) {
        return OPRT_INIT_MORE_THAN_ONCE;
    }

    INIT_LIST_HEAD(&s_playlist.list_head);
    s_playlist.num = 0;
    memset(&s_music_player, 0, sizeof(s_music_player));

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_playback_mutex));
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_pending_mutex));

    __playback_storage_load_from_ops();

    TUYA_CALL_ERR_RETURN(tal_semaphore_create_init(&s_auto_next_sem, 0, 1));

    THREAD_CFG_T auto_next_cfg = {0};
    auto_next_cfg.priority = THREAD_PRIO_3;
    auto_next_cfg.stackDepth = PLAYBACK_AUTO_NEXT_STACK_SIZE;
    auto_next_cfg.thrdname = "pb_auto_next";
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(
        &s_auto_next_thread, NULL, NULL,
        __auto_next_thread_func, NULL, &auto_next_cfg));

    s_playback_inited = TRUE;

    TUYA_CALL_ERR_RETURN(ty_subscribe_event(EVENT_MUSIC_PLAYER, "playback_ctrl", __music_player_event_cb, SUBSCRIBE_TYPE_NORMAL));

    TAL_PR_NOTICE("[%s] init success, auto_play_next=%d", __func__, s_auto_play_next);
    return rt;
}

OPERATE_RET wukong_playback_ctrl_get_status(WUKONG_MUSIC_PLAYER_T *out)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(out, OPRT_INVALID_PARM);
    TUYA_CALL_ERR_RETURN(__playback_require_init());

    tal_mutex_lock(s_playback_mutex);
    memcpy(out, &s_music_player, sizeof(WUKONG_MUSIC_PLAYER_T));
    tal_mutex_unlock(s_playback_mutex);

    return rt;
}

OPERATE_RET wukong_playback_ctrl_send_mqtt(CONST CHAR_T *action)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T data[PLAYBACK_MQTT_DATA_MAX_LEN] = {0};
    CHAR_T biz_id[PLAYBACK_BIZ_ID_MAX_LEN] = {0};

    TUYA_CALL_ERR_RETURN(__playback_require_init());
    TUYA_CHECK_NULL_RETURN(action, OPRT_INVALID_PARM);

    if (action[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    __playback_generate_biz_id(biz_id, SIZEOF(biz_id));
    snprintf(data,
             SIZEOF(data),
             "{\"bizId\":\"%s\",\"bizType\":\"SKILL\",\"data\":{\"code\":\"PlayControl\",\"action\":\"%s\",\"id\":0}}",
             biz_id,
             action);

    rt = iot_mqc_send_custom_msg(9000, data, 0, 0, NULL, NULL);
    TUYA_CALL_ERR_RETURN(rt);

    TAL_PR_DEBUG("[%s] send playcontrol action:%s", __func__, action);
    return OPRT_OK;
}

/* ====================================================================== */
/*                     Pending Request Helpers                            */
/* ====================================================================== */

STATIC INT_T __pending_alloc(CONST CHAR_T *biz_id)
{
    INT_T slot = -1;

    tal_mutex_lock(s_pending_mutex);
    for (INT_T i = 0; i < PLAYBACK_PENDING_MAX; i++) {
        if (s_pending[i].used == FALSE) {
            s_pending[i].used = TRUE;
            s_pending[i].result = NULL;
            __playback_safe_copy(s_pending[i].biz_id,
                                 SIZEOF(s_pending[i].biz_id), biz_id);
            if (s_pending[i].sem == NULL) {
                tal_semaphore_create_init(&s_pending[i].sem, 0, 1);
            }
            slot = i;
            break;
        }
    }
    tal_mutex_unlock(s_pending_mutex);

    return slot;
}

STATIC VOID __pending_free(INT_T slot)
{
    if (slot < 0 || slot >= PLAYBACK_PENDING_MAX) {
        return;
    }

    tal_mutex_lock(s_pending_mutex);
    if (s_pending[slot].result) {
        ty_cJSON_Delete(s_pending[slot].result);
        s_pending[slot].result = NULL;
    }
    s_pending[slot].biz_id[0] = '\0';
    s_pending[slot].used = FALSE;
    tal_mutex_unlock(s_pending_mutex);
}

STATIC OPERATE_RET __pending_wait(INT_T slot, INT_T timeout_ms,
                                   ty_cJSON **out_result)
{
    OPERATE_RET rt;

    if (slot < 0 || slot >= PLAYBACK_PENDING_MAX || out_result == NULL) {
        return OPRT_INVALID_PARM;
    }

    rt = tal_semaphore_wait(s_pending[slot].sem, timeout_ms);
    if (rt != OPRT_OK) {
        TAL_PR_WARN("[%s] slot %d timeout (%d ms)", __func__, slot, timeout_ms);
        __pending_free(slot);
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_pending_mutex);
    *out_result = s_pending[slot].result;
    s_pending[slot].result = NULL;
    tal_mutex_unlock(s_pending_mutex);

    __pending_free(slot);
    return OPRT_OK;
}

/* ====================================================================== */
/*                     dispatch_response (called from skills)             */
/* ====================================================================== */

OPERATE_RET wukong_playback_ctrl_dispatch_response(CONST CHAR_T *biz_id,
                                                    CONST ty_cJSON *json)
{
    if (biz_id == NULL || json == NULL || s_pending_mutex == NULL) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(s_pending_mutex);
    for (INT_T i = 0; i < PLAYBACK_PENDING_MAX; i++) {
        if (s_pending[i].used == TRUE &&
            strcmp(s_pending[i].biz_id, biz_id) == 0) {
            s_pending[i].result = ty_cJSON_Duplicate(json, TRUE);
            tal_mutex_unlock(s_pending_mutex);
            tal_semaphore_post(s_pending[i].sem);
            TAL_PR_DEBUG("[%s] matched slot %d biz_id=%s", __func__, i, biz_id);
            return OPRT_OK;
        }
    }
    tal_mutex_unlock(s_pending_mutex);

    return OPRT_COM_ERROR;
}

/* ====================================================================== */
/*               send_music_list / send_refresh_url                       */
/* ====================================================================== */

STATIC VOID __append_str_field(CHAR_T *buf, INT_T buf_len, INT_T *pos,
                                CONST CHAR_T *key, CONST CHAR_T *val)
{
    if (val == NULL || val[0] == '\0') {
        return;
    }
    *pos += snprintf(buf + *pos, buf_len - *pos, ",\"%s\":\"%s\"", key, val);
}

OPERATE_RET wukong_playback_ctrl_send_music_list(
    CONST WUKONG_MUSIC_LIST_PARAM_T *param,
    ty_cJSON **out_result,
    INT_T timeout_ms)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T data[PLAYBACK_MQTT_LARGE_DATA_MAX_LEN] = {0};
    CHAR_T biz_id[PLAYBACK_BIZ_ID_MAX_LEN] = {0};
    INT_T pos = 0;
    INT_T slot = -1;
    INT_T limit_val;
    CHAR_T offset_str[16] = {0};
    CHAR_T limit_str[16] = {0};

    TUYA_CALL_ERR_RETURN(__playback_require_init());
    TUYA_CHECK_NULL_RETURN(param, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(out_result, OPRT_INVALID_PARM);

    __playback_generate_biz_id(biz_id, SIZEOF(biz_id));

    limit_val = (param->limit > 0 && param->limit <= 50)
                    ? param->limit : 20;
    snprintf(offset_str, SIZEOF(offset_str), "%d",
             param->offset >= 0 ? param->offset : 0);
    snprintf(limit_str, SIZEOF(limit_str), "%d", limit_val);

    pos = snprintf(data, SIZEOF(data),
        "{\"bizId\":\"%s\",\"bizType\":\"SKILL\","
        "\"data\":{\"code\":\"PlayControl\","
        "\"action\":\"music_list\","
        "\"offset\":\"%s\",\"limit\":\"%s\",\"id\":\"0\"",
        biz_id, offset_str, limit_str);

    __append_str_field(data, SIZEOF(data), &pos, "tag", param->tag);
    __append_str_field(data, SIZEOF(data), &pos, "name", param->name);
    __append_str_field(data, SIZEOF(data), &pos, "artist", param->artist);
    __append_str_field(data, SIZEOF(data), &pos,
                       "albumName", param->album_name);
    __append_str_field(data, SIZEOF(data), &pos,
                       "keyword", param->keyword);
    __append_str_field(data, SIZEOF(data), &pos, "lang", param->lang);

    pos += snprintf(data + pos, SIZEOF(data) - pos, "}}");

    slot = __pending_alloc("music_list");
    if (slot < 0) {
        TAL_PR_ERR("[%s] no pending slot available", __func__);
        return OPRT_COM_ERROR;
    }

    rt = iot_mqc_send_custom_msg(9000, data, 0, 0, NULL, NULL);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("[%s] mqtt send failed: %d", __func__, rt);
        __pending_free(slot);
        return rt;
    }

    TAL_PR_DEBUG("[%s] sent music_list biz_id=%s", __func__, biz_id);

    rt = __pending_wait(slot, timeout_ms, out_result);
    return rt;
}

OPERATE_RET wukong_playback_ctrl_send_refresh_url(
    CONST CHAR_T *audio_ids,
    INT_T bitrate,
    CONST CHAR_T *channel_code,
    ty_cJSON **out_result,
    INT_T timeout_ms)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T data[PLAYBACK_MQTT_LARGE_DATA_MAX_LEN] = {0};
    CHAR_T biz_id[PLAYBACK_BIZ_ID_MAX_LEN] = {0};
    CHAR_T bitrate_str[16] = {0};
    INT_T slot = -1;

    TUYA_CALL_ERR_RETURN(__playback_require_init());
    TUYA_CHECK_NULL_RETURN(audio_ids, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(out_result, OPRT_INVALID_PARM);

    __playback_generate_biz_id(biz_id, SIZEOF(biz_id));
    snprintf(bitrate_str, SIZEOF(bitrate_str), "%d",
             bitrate > 0 ? bitrate : 128);

    if (channel_code != NULL && channel_code[0] != '\0') {
        snprintf(data, SIZEOF(data),
            "{\"bizId\":\"%s\",\"bizType\":\"SKILL\","
            "\"data\":{\"code\":\"PlayControl\","
            "\"action\":\"refresh_play_url\","
            "\"audioIds\":\"%s\","
            "\"channelCode\":\"%s\","
            "\"bitrate\":\"%s\","
            "\"id\":\"0\"}}",
            biz_id, audio_ids, channel_code, bitrate_str);
    } else {
        snprintf(data, SIZEOF(data),
            "{\"bizId\":\"%s\",\"bizType\":\"SKILL\","
            "\"data\":{\"code\":\"PlayControl\","
            "\"action\":\"refresh_play_url\","
            "\"audioIds\":\"%s\","
            "\"bitrate\":\"%s\","
            "\"id\":\"0\"}}",
            biz_id, audio_ids, bitrate_str);
    }

    slot = __pending_alloc("refresh_play_url");
    if (slot < 0) {
        TAL_PR_ERR("[%s] no pending slot available", __func__);
        return OPRT_COM_ERROR;
    }

    rt = iot_mqc_send_custom_msg(9000, data, 0, 0, NULL, NULL);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("[%s] mqtt send failed: %d", __func__, rt);
        __pending_free(slot);
        return rt;
    }

    TAL_PR_DEBUG("[%s] sent refresh_play_url biz_id=%s", __func__, biz_id);

    rt = __pending_wait(slot, timeout_ms, out_result);
    return rt;
}

/**
 * @brief Fetch one play URL via refresh_play_url (MQTT). Caller frees not needed; resp deleted here.
 */
STATIC OPERATE_RET __playback_fetch_play_url_mqtt(CONST CHAR_T *audio_id, CONST CHAR_T *channel_code,
                                                 CHAR_T *url_out, UINT_T url_len)
{
    ty_cJSON *resp = NULL;
    ty_cJSON *data = NULL;
    ty_cJSON *items = NULL;
    INT_T n = 0;
    INT_T i = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(audio_id, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(url_out, OPRT_INVALID_PARM);
    if (url_len == 0 || audio_id[0] == '\0') {
        return OPRT_INVALID_PARM;
    }
    url_out[0] = '\0';

    rt = wukong_playback_ctrl_send_refresh_url(
        audio_id, 128,
        (channel_code != NULL && channel_code[0] != '\0') ? channel_code : NULL,
        &resp, 12000);
    if (rt != OPRT_OK || resp == NULL) {
        return (rt != OPRT_OK) ? rt : OPRT_COM_ERROR;
    }

    data = ty_cJSON_GetObjectItem(resp, "data");
    items = data ? ty_cJSON_GetObjectItem(data, "items") : NULL;
    if (items == NULL || !ty_cJSON_IsArray(items)) {
        ty_cJSON_Delete(resp);
        return OPRT_CJSON_PARSE_ERR;
    }

    n = ty_cJSON_GetArraySize(items);
    for (i = 0; i < n; i++) {
        ty_cJSON *row = ty_cJSON_GetArrayItem(items, i);
        ty_cJSON *aid = row ? ty_cJSON_GetObjectItem(row, "audioId") : NULL;
        ty_cJSON *ok = row ? ty_cJSON_GetObjectItem(row, "success") : NULL;
        ty_cJSON *u = row ? ty_cJSON_GetObjectItem(row, "url") : NULL;

        if (aid == NULL || !ty_cJSON_IsString(aid)) {
            continue;
        }
        if (strcmp(aid->valuestring, audio_id) != 0) {
            continue;
        }
        if (ok == NULL || ok->type != ty_cJSON_True) {
            break;
        }
        if (u == NULL || !ty_cJSON_IsString(u) || u->valuestring[0] == '\0') {
            break;
        }
        snprintf(url_out, url_len, "%s", u->valuestring);
        break;
    }

    ty_cJSON_Delete(resp);

    if (url_out[0] == '\0') {
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_playback_playlist_add(CONST CHAR_T *song_name, CONST CHAR_T *artist,
    CONST CHAR_T *song_url, CONST CHAR_T *audio_id, CONST CHAR_T *channel_code,
    INT_T *out_playlist_id)
{
    OPERATE_RET rt = OPRT_OK;
    WUKONG_PLAYBACK_ITEM_T *item = NULL;
    INT_T music_id = -1;
    BOOL_T has_url = (song_url != NULL && song_url[0] != '\0');
    BOOL_T has_aid = (audio_id != NULL && audio_id[0] != '\0');

    if (out_playlist_id != NULL) {
        *out_playlist_id = -1;
    }

    TUYA_CALL_ERR_RETURN(__playback_require_init());
    TUYA_CHECK_NULL_RETURN(song_name, OPRT_INVALID_PARM);
    if (song_name[0] == '\0') {
        return OPRT_INVALID_PARM;
    }
    if (!has_url && !has_aid) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(s_playback_mutex);

    if (has_url && __playback_playlist_has_url_locked(song_url) == TRUE) {
        if (out_playlist_id != NULL) {
            *out_playlist_id = __playback_playlist_get_id_by_url_locked(song_url);
        }
        tal_mutex_unlock(s_playback_mutex);
        return OPRT_OK;
    }
    if (!has_url && has_aid && __playback_playlist_has_audio_id_locked(audio_id) == TRUE) {
        if (out_playlist_id != NULL) {
            *out_playlist_id = __playback_playlist_get_id_by_audio_id_locked(audio_id);
        }
        tal_mutex_unlock(s_playback_mutex);
        return OPRT_OK;
    }

    if (s_playlist.num >= s_playlist_max_num) {
        __playback_playlist_fifo_evict_locked();
    }

    music_id = __playback_playlist_alloc_id_locked();
    if (music_id < 0) {
        tal_mutex_unlock(s_playback_mutex);
        return OPRT_COM_ERROR;
    }

    item = (WUKONG_PLAYBACK_ITEM_T *)tal_malloc(sizeof(WUKONG_PLAYBACK_ITEM_T));
    if (item == NULL) {
        tal_mutex_unlock(s_playback_mutex);
        return OPRT_MALLOC_FAILED;
    }

    memset(item, 0, sizeof(WUKONG_PLAYBACK_ITEM_T));
    item->id = music_id;
    __playback_safe_copy(item->song_name, SIZEOF(item->song_name), song_name);
    __playback_safe_copy(item->artist, SIZEOF(item->artist), artist);
    if (has_url) {
        __playback_safe_copy(item->song_url, SIZEOF(item->song_url), song_url);
    }
    if (has_aid) {
        __playback_safe_copy(item->audio_id, SIZEOF(item->audio_id), audio_id);
    }
    if (channel_code != NULL && channel_code[0] != '\0') {
        __playback_safe_copy(item->channel_code, SIZEOF(item->channel_code), channel_code);
    }
    tuya_list_add_tail(&item->list_node, &s_playlist.list_head);
    s_playlist.num++;

    TAL_PR_DEBUG("[playback] playlist +id:%d \"%s\" - \"%s\" (total:%d)",
                 music_id, song_name, artist ? artist : "", s_playlist.num);

    if (out_playlist_id != NULL) {
        *out_playlist_id = music_id;
    }

    tal_mutex_unlock(s_playback_mutex);

    __playback_persist_and_notify();

    return rt;
}

OPERATE_RET wukong_playback_playlist_remove(INT_T id)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    LIST_HEAD *pos = NULL;
    LIST_HEAD *next = NULL;

    TUYA_CALL_ERR_RETURN(__playback_require_init());

    tal_mutex_lock(s_playback_mutex);
    tuya_list_for_each_safe(pos, next, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);

        if (item == NULL) {
            continue;
        }

        if (item->id != id) {
            continue;
        }

        if (item->id == s_current_play_id) {
            s_current_play_id = -1;
        }

        tuya_list_del(&item->list_node);
        tal_free(item);
        if (s_playlist.num > 0) {
            s_playlist.num--;
        }
        rt = OPRT_OK;
        break;
    }
    tal_mutex_unlock(s_playback_mutex);

    if (rt == OPRT_OK) {
        __playback_persist_and_notify();
    }

    return rt;
}

OPERATE_RET wukong_playback_playlist_list(ty_cJSON **out)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *list = NULL;
    LIST_HEAD *pos = NULL;

    TUYA_CALL_ERR_RETURN(__playback_require_init());
    TUYA_CHECK_NULL_RETURN(out, OPRT_INVALID_PARM);

    list = ty_cJSON_CreateArray();
    TUYA_CHECK_NULL_RETURN(list, OPRT_MALLOC_FAILED);

    tal_mutex_lock(s_playback_mutex);
    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);
        ty_cJSON *item_json = NULL;

        if (item == NULL) {
            continue;
        }

        item_json = ty_cJSON_CreateObject();
        if (item_json == NULL) {
            tal_mutex_unlock(s_playback_mutex);
            ty_cJSON_Delete(list);
            return OPRT_MALLOC_FAILED;
        }

        ty_cJSON_AddNumberToObject(item_json, "id", item->id);
        ty_cJSON_AddStringToObject(item_json, "song_name", item->song_name);
        ty_cJSON_AddStringToObject(item_json, "artist", item->artist);
        ty_cJSON_AddStringToObject(item_json, "song_url", item->song_url);
        ty_cJSON_AddStringToObject(item_json, "audio_id", item->audio_id);
        ty_cJSON_AddStringToObject(item_json, "channel_code", item->channel_code);
        ty_cJSON_AddItemToArray(list, item_json);
    }
    tal_mutex_unlock(s_playback_mutex);

    *out = list;
    return rt;
}

OPERATE_RET wukong_playback_playlist_clear(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(__playback_require_init());

    tal_mutex_lock(s_playback_mutex);
    __playback_playlist_clear_locked();
    s_current_play_id = -1;
    tal_mutex_unlock(s_playback_mutex);

    __playback_persist_and_notify();

    return rt;
}

OPERATE_RET wukong_playback_playlist_play(INT_T id)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    LIST_HEAD *pos = NULL;
    CHAR_T song_name[PLAYBACK_MUSIC_NAME_MAX_LEN] = {0};
    CHAR_T artist[PLAYBACK_MUSIC_ARTIST_MAX_LEN] = {0};
    CHAR_T song_url[PLAYBACK_MUSIC_URL_MAX_LEN] = {0};
    CHAR_T audio_id[PLAYBACK_AUDIO_ID_MAX_LEN] = {0};
    CHAR_T channel_code[PLAYBACK_CHANNEL_CODE_MAX_LEN] = {0};
    WUKONG_PLAYBACK_ITEM_T *found = NULL;
    WUKONG_AI_MUSIC_T music = {0};
    WUKONG_AI_MUSIC_SRC_T music_src = {0};

    TUYA_CALL_ERR_RETURN(__playback_require_init());

    tal_mutex_lock(s_playback_mutex);
    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        WUKONG_PLAYBACK_ITEM_T *item = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);

        if (item == NULL) {
            continue;
        }

        if (item->id != id) {
            continue;
        }

        found = item;
        __playback_safe_copy(song_name, SIZEOF(song_name), item->song_name);
        __playback_safe_copy(artist, SIZEOF(artist), item->artist);
        __playback_safe_copy(song_url, SIZEOF(song_url), item->song_url);
        __playback_safe_copy(audio_id, SIZEOF(audio_id), item->audio_id);
        __playback_safe_copy(channel_code, SIZEOF(channel_code), item->channel_code);
        rt = OPRT_OK;
        break;
    }
    tal_mutex_unlock(s_playback_mutex);

    TUYA_CALL_ERR_RETURN(rt);

    if (song_url[0] == '\0') {
        if (audio_id[0] == '\0') {
            return OPRT_INVALID_PARM;
        }
        rt = __playback_fetch_play_url_mqtt(audio_id, channel_code, song_url, SIZEOF(song_url));
        TUYA_CALL_ERR_RETURN(rt);
        tal_mutex_lock(s_playback_mutex);
        if (found != NULL) {
            __playback_safe_copy(found->song_url, SIZEOF(found->song_url), song_url);
        }
        tal_mutex_unlock(s_playback_mutex);
    }

    snprintf(music.action, SIZEOF(music.action), "%s", "play");
    music.src_cnt = 1;
    music.src_array = &music_src;

    music_src.url = song_url;
    music_src.song_name = song_name;
    music_src.artist = artist;
    music_src.format = AI_AUDIO_CODEC_MP3;

    TAL_PR_NOTICE("[playback] >>> play id:%d name:\"%s\" artist:\"%s\" aid:%s",
                  id, song_name, artist, audio_id[0] ? audio_id : "-");

    tal_mutex_lock(s_playback_mutex);
    s_switching_track = TRUE;
    tal_mutex_unlock(s_playback_mutex);

    rt = wukong_audio_play_music(&music);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("[playback] play failed id:%d rt:%d", id, rt);
        tal_mutex_lock(s_playback_mutex);
        s_switching_track = FALSE;
        tal_mutex_unlock(s_playback_mutex);
        return rt;
    }

    tal_mutex_lock(s_playback_mutex);
    __playback_safe_copy(s_music_player.song_name, SIZEOF(s_music_player.song_name), song_name);
    __playback_safe_copy(s_music_player.artist, SIZEOF(s_music_player.artist), artist);
    __playback_safe_copy(s_music_player.song_url, SIZEOF(s_music_player.song_url), song_url);
    s_music_player.state = AI_PLAYER_STOPPED;
    s_current_play_id = id;
    tal_mutex_unlock(s_playback_mutex);

    /* Notify UI: publish MUSIC_PLAYER_DATA so desk_func_music.c updates song labels.
     * Cloud-pushed songs already go through ty_publish_event; local playlist plays
     * do not, leaving the UI cache stale and the screen showing empty song info. */
    {
        WUKONG_MUSIC_PLAYER_T ui_msg = {0};
        ui_msg.cmd = MUSIC_PLAYER_DATA;
        __playback_safe_copy(ui_msg.song_name, SIZEOF(ui_msg.song_name), song_name);
        __playback_safe_copy(ui_msg.artist,    SIZEOF(ui_msg.artist),    artist);
        __playback_safe_copy(ui_msg.song_url,  SIZEOF(ui_msg.song_url),  song_url);
        ty_publish_event(EVENT_MUSIC_PLAYER, &ui_msg);
    }

    return OPRT_OK;
}

/**
 * @brief Find the playlist item adjacent to s_current_play_id and play it.
 * @param[in] forward TRUE = next, FALSE = prev
 * @return OPRT_OK if navigation and playback started successfully;
 *         OPRT_COM_ERROR if the list is empty, current position is unknown,
 *         or there is no adjacent item in the requested direction.
 * @note Must NOT be called with s_playback_mutex held.
 */
STATIC OPERATE_RET __playback_playlist_navigate(BOOL_T forward)
{
    OPERATE_RET rt = OPRT_OK;
    LIST_HEAD *pos = NULL;
    LIST_HEAD *adj_node = NULL;
    WUKONG_PLAYBACK_ITEM_T *edge = NULL;
    WUKONG_PLAYBACK_ITEM_T *cur = NULL;
    WUKONG_PLAYBACK_ITEM_T *adj = NULL;
    INT_T target_id = -1;

    TUYA_CALL_ERR_RETURN(__playback_require_init());

    tal_mutex_lock(s_playback_mutex);

    if (tuya_list_empty(&s_playlist.list_head)) {
        tal_mutex_unlock(s_playback_mutex);
        return OPRT_COM_ERROR;
    }

    /* If nothing has been played yet, start from first or last. */
    if (s_current_play_id < 0) {
        if (forward) {
            edge = tuya_list_entry(s_playlist.list_head.next,
                                   WUKONG_PLAYBACK_ITEM_T, list_node);
        } else {
            edge = tuya_list_entry(s_playlist.list_head.prev,
                                   WUKONG_PLAYBACK_ITEM_T, list_node);
        }
        if (edge != NULL) {
            target_id = edge->id;
        }
        tal_mutex_unlock(s_playback_mutex);
        if (target_id < 0) {
            return OPRT_COM_ERROR;
        }
        return wukong_playback_playlist_play(target_id);
    }

    /* Walk the list to find the current item, then step one position. */
    tuya_list_for_each(pos, &s_playlist.list_head)
    {
        cur = tuya_list_entry(pos, WUKONG_PLAYBACK_ITEM_T, list_node);
        if (cur == NULL || cur->id != s_current_play_id) {
            continue;
        }

        /* Found current item — check for adjacent item. */
        adj_node = forward ? pos->next : pos->prev;
        if (adj_node == &s_playlist.list_head) {
            tal_mutex_unlock(s_playback_mutex);
            TAL_PR_NOTICE("[playback] %s: end of playlist (cur id:%d \"%s\")",
                          forward ? "next" : "prev",
                          cur->id, cur->song_name);
            return OPRT_COM_ERROR;
        }

        adj = tuya_list_entry(adj_node, WUKONG_PLAYBACK_ITEM_T, list_node);
        if (adj != NULL) {
            target_id = adj->id;
        }
        break;
    }

    tal_mutex_unlock(s_playback_mutex);

    if (target_id < 0) {
        /* Current item not found in list (evicted?). */
        return OPRT_COM_ERROR;
    }

    return wukong_playback_playlist_play(target_id);
}

/**
 * @brief Play the next item in the local playlist.
 * @return OPRT_OK if next item found and playback started;
 *         OPRT_COM_ERROR if at the end of the list or list is empty.
 */
OPERATE_RET wukong_playback_playlist_next(VOID)
{
    return __playback_playlist_navigate(TRUE);
}

/**
 * @brief Play the previous item in the local playlist.
 * @return OPRT_OK if previous item found and playback started;
 *         OPRT_COM_ERROR if at the beginning of the list or list is empty.
 */
OPERATE_RET wukong_playback_playlist_prev(VOID)
{
    return __playback_playlist_navigate(FALSE);
}

/**
 * @brief Enable or disable auto-play-next
 * @param[in] enable TRUE to enable, FALSE to disable
 * @return none
 */
VOID wukong_playback_ctrl_set_auto_next(BOOL_T enable)
{
    if (s_playback_mutex != NULL) {
        tal_mutex_lock(s_playback_mutex);
    }
    s_auto_play_next = enable;
    if (s_playback_mutex != NULL) {
        tal_mutex_unlock(s_playback_mutex);
    }

    TAL_PR_NOTICE("[%s] auto_play_next=%d", __func__, enable);
}

/**
 * @brief Query whether auto-play-next is currently enabled
 * @return TRUE if enabled, FALSE otherwise
 */
BOOL_T wukong_playback_ctrl_get_auto_next(VOID)
{
    BOOL_T val;

    if (s_playback_mutex != NULL) {
        tal_mutex_lock(s_playback_mutex);
    }
    val = s_auto_play_next;
    if (s_playback_mutex != NULL) {
        tal_mutex_unlock(s_playback_mutex);
    }

    return val;
}

/**
 * @brief Non-blocking "next" for UI callbacks.
 *        Tries the local playlist first; if exhausted, schedules fetch_more
 *        on the background worker thread and returns immediately.
 * @return OPRT_OK always (fetch result delivered via EVENT_MUSIC_PLAYER).
 */
OPERATE_RET wukong_playback_ctrl_next(VOID)
{
    OPERATE_RET rt;

    TUYA_CALL_ERR_RETURN(__playback_require_init());

    rt = wukong_playback_playlist_next();
    if (rt == OPRT_OK) {
        return OPRT_OK;
    }

    /* Local playlist exhausted — schedule fetch_more on the worker thread */
    if (s_auto_next_sem == NULL) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_playback_mutex);
    s_user_next_pending = TRUE;
    tal_mutex_unlock(s_playback_mutex);

    tal_semaphore_post(s_auto_next_sem);

    TAL_PR_NOTICE("[playback] user next: local end, fetch_more scheduled");
    return OPRT_OK;
}

/**
 * @brief Non-blocking play of a specific playlist item for UI callbacks.
 *        Dispatches wukong_playback_playlist_play(id) to the background
 *        worker thread to avoid blocking the LVGL task during URL refresh.
 * @param[in] id  playlist item id
 * @return OPRT_OK if request was queued; OPRT_COM_ERROR if module not ready.
 */
OPERATE_RET wukong_playback_playlist_play_async(INT_T id)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(__playback_require_init());

    if (s_auto_next_sem == NULL) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_playback_mutex);
    s_user_play_id = id;
    tal_mutex_unlock(s_playback_mutex);

    tal_semaphore_post(s_auto_next_sem);

    TAL_PR_NOTICE("[playback] play_async id:%d scheduled", id);
    return OPRT_OK;
}

/**
 * @brief Save the search context from a music_list request so that
 *        auto-play-next can fetch more pages with the same parameters.
 * @param[in] param    search parameters used for the request
 * @param[in] next_offset  offset for the next page (current offset + items returned)
 * @param[in] has_more TRUE if the cloud indicated more pages are available
 * @return none
 */
VOID wukong_playback_ctrl_save_search_ctx(CONST WUKONG_MUSIC_LIST_PARAM_T *param,
                                           INT_T next_offset,
                                           BOOL_T has_more)
{
    if (param == NULL) {
        return;
    }

    tal_mutex_lock(s_playback_mutex);
    memset(&s_search_ctx, 0, sizeof(s_search_ctx));
    __playback_safe_copy(s_search_ctx.tag,        SIZEOF(s_search_ctx.tag),        param->tag);
    __playback_safe_copy(s_search_ctx.keyword,    SIZEOF(s_search_ctx.keyword),    param->keyword);
    __playback_safe_copy(s_search_ctx.name,       SIZEOF(s_search_ctx.name),       param->name);
    __playback_safe_copy(s_search_ctx.artist,     SIZEOF(s_search_ctx.artist),     param->artist);
    __playback_safe_copy(s_search_ctx.album_name, SIZEOF(s_search_ctx.album_name), param->album_name);
    __playback_safe_copy(s_search_ctx.lang,       SIZEOF(s_search_ctx.lang),       param->lang);
    s_search_ctx.limit       = param->limit > 0 ? param->limit : 5;
    s_search_ctx.next_offset = next_offset;
    s_search_ctx.has_more    = has_more;
    s_search_ctx.valid       = TRUE;
    tal_mutex_unlock(s_playback_mutex);

    TAL_PR_DEBUG("[playback] search ctx saved: next_offset=%d has_more=%d",
                 next_offset, has_more);
}
