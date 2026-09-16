#include "ui_svc_music.h"
#include "ui_app.h"            /* ui_app_async_call — marshal to UI thread */
#include <string.h>

#include "base_event.h"        /* ty_subscribe_event */
#include "tal_workq_service.h" /* WORKQ_SYSTEM, tal_workq_schedule */
#include "wukong_fc.h"  /* EVENT_MUSIC_PLAYER/BREAK, WUKONG_MUSIC_PLAYER_T */
#include "wukong_playback_ctrl.h"
#include "wukong_audio_player.h"
#include "wukong_storage.h"    /* record layer — persists the cloud playlist */
#include "uni_log.h"

#define MUSIC_EVT_SUBSCRIBER  "ui_svc_music"

/* Playlist cap; aligns with the backend PLAYBACK_PLAYLIST_HARD_CAP (private)
 * and the music list page's MLIST_MAX_ROWS. register_storage clamps to the
 * backend hard cap regardless. */
#define MUSIC_PLAYLIST_MAX    128

/* ---------------------------------------------------------------------------
 * Persistent playlist storage (cloud playlist) — routed through the wukong
 * store layer at ns "music/cloud", entry "playlist.json". With the default FS
 * backend the store composes the exact same on-disk path the legacy ui_fs
 * implementation used (/sdcard/tuyaos/music/cloud/playlist.json), so an OTA
 * keeps reading the previously-saved file. Registered with the playback
 * controller in ui_svc_music_init() (after the store is mounted), which
 * triggers a reload from this storage. save/load run synchronously on the
 * caller's thread.
 * --------------------------------------------------------------------------- */
#define MUSIC_STORE_NS         "music/cloud"
#define MUSIC_PLAYLIST_FILE    "playlist.json"

static OPERATE_RET music_playlist_save(CONST ty_cJSON *playlist_json)
{
    char       *json_str = NULL;
    int         write_size = 0;
    OPERATE_RET rt;

    if (playlist_json == NULL) {
        return OPRT_INVALID_PARM;
    }

    json_str = ty_cJSON_PrintUnformatted((ty_cJSON *)playlist_json);
    if (json_str == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    write_size = (int)strlen(json_str);

    rt = wukong_storage_write(MUSIC_STORE_NS, MUSIC_PLAYLIST_FILE,
                            (CONST BYTE_T *)json_str, (UINT_T)write_size);
    if (rt != OPRT_OK) {
        PR_ERR("music storage: write failed rt=%d", rt);
    }
    ty_cJSON_FreeBuffer(json_str);
    return rt;
}

static OPERATE_RET music_playlist_load(ty_cJSON **playlist_json)
{
    ty_cJSON  *parsed = NULL;
    ty_cJSON  *list = NULL;
    BYTE_T    *buf = NULL;
    UINT_T     len = 0;
    OPERATE_RET rt;

    if (playlist_json == NULL) {
        return OPRT_INVALID_PARM;
    }
    *playlist_json = NULL;

    /* OPRT_NOT_FOUND covers "no saved playlist yet" AND "medium unavailable"
     * (e.g. no SD card); both map to the empty-table contract. */
    rt = wukong_storage_read(MUSIC_STORE_NS, MUSIC_PLAYLIST_FILE, &buf, &len);
    if (rt == OPRT_NOT_FOUND) {
        return OPRT_OK;
    }
    if (rt != OPRT_OK) {
        PR_ERR("music storage: read failed rt=%d", rt);
        return rt;
    }

    /* The record layer NUL-terminates the buffer one byte past @len, so the
     * JSON parser can treat it as a C string. */
    parsed = ty_cJSON_Parse((CONST char *)buf);
    wukong_storage_free(buf);
    if (parsed == NULL) {
        return OPRT_CJSON_PARSE_ERR;
    }

    /* Accept a bare array, or an object wrapping a "list" array. The backend
     * takes ownership of *playlist_json and frees it with ty_cJSON_Delete. */
    if (ty_cJSON_IsArray(parsed)) {
        *playlist_json = parsed;
        return OPRT_OK;
    }
    if (ty_cJSON_IsObject(parsed)) {
        list = ty_cJSON_GetObjectItem(parsed, "list");
        if (list != NULL && ty_cJSON_IsArray(list)) {
            *playlist_json = ty_cJSON_Duplicate(list, TRUE);
            ty_cJSON_Delete(parsed);
            return (*playlist_json != NULL) ? OPRT_OK : OPRT_MALLOC_FAILED;
        }
    }
    ty_cJSON_Delete(parsed);
    return OPRT_CJSON_PARSE_ERR;
}

/* on_changed is NULL for v1: the music list page re-fetches on open / player
 * events, so no extra UI poke is needed when the persisted list changes. */
static CONST WUKONG_PLAYBACK_STORAGE_OPS_T s_music_store_ops = {
    .save       = music_playlist_save,
    .load       = music_playlist_load,
    .on_changed = NULL,
    .max_items  = MUSIC_PLAYLIST_MAX,
};

static ui_svc_music_cb_t s_cb      = NULL;
static ui_music_status_t s_status  = { .state = AI_PLAYER_STOPPED };
static bool              s_inited  = false;

/* ---- UI-thread side: notify page ---------------------------------------- */
static void notify_ui_cb(void *unused)
{
    (void)unused;
    if (s_cb) {
        s_cb(&s_status);
    }
}

static void post_notify(void)
{
    ui_app_async_call(notify_ui_cb, NULL);
}

/* ---- event-thread callbacks --------------------------------------------- */
static INT_T music_player_evt_cb(VOID_T *data)
{
    if (data == NULL) {
        return OPRT_OK;
    }
    WUKONG_MUSIC_PLAYER_T *msg = (WUKONG_MUSIC_PLAYER_T *)data;
    switch (msg->cmd) {
        case MUSIC_PLAYER_STATE:
            s_status.state = msg->state;
            break;
        case MUSIC_PLAYER_DATA:
            strncpy(s_status.song_name, msg->song_name, sizeof(s_status.song_name) - 1);
            s_status.song_name[sizeof(s_status.song_name) - 1] = '\0';
            strncpy(s_status.artist, msg->artist, sizeof(s_status.artist) - 1);
            s_status.artist[sizeof(s_status.artist) - 1] = '\0';
            break;
        default:
            break;
    }
    post_notify();
    return OPRT_OK;
}

static INT_T music_break_evt_cb(VOID_T *data)
{
    (void)data;
    /* EVENT_MUSIC_BREAK is only published by wukong_audio_player_stop(), which
     * hard-stops the background player AND clears its engine playlist. Reflect
     * that as STOPPED, not PAUSED: a PAUSED cache makes the play button take the
     * resume() path, which is a no-op on a stopped+cleared engine, so playback
     * could then only be restarted via "next". */
    s_status.state = AI_PLAYER_STOPPED;
    post_notify();
    return OPRT_OK;
}

/* ---- public API --------------------------------------------------------- */
void ui_svc_music_init(void)
{
    if (s_inited) {
        return;
    }

    /* Register the store-backed playlist storage. The external volume mounts
     * asynchronously (wukong_storage, EVENT_WUKONG_STORAGE_READY), so with an
     * FS-backed music namespace wukong_playback_ctrl defers its one-shot
     * playlist load until storage.ready; non-FS backends (KV/UF/CUSTOM) load
     * immediately. Either way the persisted cloud playlist comes from
     * <root>/tuyaos/music/cloud/playlist.json — the same path the legacy
     * ui_fs implementation wrote. */
    wukong_playback_ctrl_register_storage(&s_music_store_ops);

    /* Seed cache from current backend status (a song may already be playing). */
    WUKONG_MUSIC_PLAYER_T cur = {0};
    if (wukong_playback_ctrl_get_status(&cur) == OPRT_OK) {
        s_status.state = cur.state;
        strncpy(s_status.song_name, cur.song_name, sizeof(s_status.song_name) - 1);
        s_status.song_name[sizeof(s_status.song_name) - 1] = '\0';
        strncpy(s_status.artist, cur.artist, sizeof(s_status.artist) - 1);
        s_status.artist[sizeof(s_status.artist) - 1] = '\0';
    }

    if (ty_subscribe_event(EVENT_MUSIC_PLAYER, MUSIC_EVT_SUBSCRIBER,
                           music_player_evt_cb, SUBSCRIBE_TYPE_NORMAL) != OPRT_OK) {
        PR_ERR("music svc: subscribe EVENT_MUSIC_PLAYER failed");
    }
    if (ty_subscribe_event(EVENT_MUSIC_BREAK, MUSIC_EVT_SUBSCRIBER,
                           music_break_evt_cb, SUBSCRIBE_TYPE_NORMAL) != OPRT_OK) {
        PR_ERR("music svc: subscribe EVENT_MUSIC_BREAK failed");
    }

    /* Spec: UI default is list-loop (backend default stays sequence). */
    wukong_playback_ctrl_set_play_mode(WK_PLAY_MODE_LIST_LOOP);

    s_inited = true;
}

void ui_svc_music_set_cb(ui_svc_music_cb_t cb)
{
    s_cb = cb;
}

void ui_svc_music_get_status(ui_music_status_t *out)
{
    if (out) {
        *out = s_status;
    }
}

/* Shared "start playing again" path for the play/pause button and page
 * autoplay. A bare wukong_audio_player_resume() only works when the engine is
 * genuinely PAUSED; after a USER stop (mode switch / chat break / photo) the
 * background player is STOPPED with a cleared engine playlist, so resume() is a
 * no-op there and the track must be restarted from the retained playlist id.
 * Caller must have already handled/excluded the PLAYING case. */
static void music_resume_or_restart(void)
{
    if (s_status.state == AI_PLAYER_PAUSED) {
        wukong_audio_player_resume();
        return;
    }

    /* STOPPED: restart the current track, else the first playlist item.
     * play_async hands the (possibly 12s) URL refresh to the backend worker so
     * the UI thread never blocks. */
    int id = wukong_playback_ctrl_get_current_play_id();
    if (id < 0) {
        ty_cJSON *list = NULL;
        if (wukong_playback_playlist_list(&list) == OPRT_OK && list != NULL) {
            ty_cJSON *first = ty_cJSON_GetArrayItem(list, 0);
            ty_cJSON *jid = first ? ty_cJSON_GetObjectItem(first, "id") : NULL;
            if (jid && ty_cJSON_IsNumber(jid)) {
                id = jid->valueint;
            }
            ty_cJSON_Delete(list);
        }
    }

    if (id >= 0) {
        wukong_playback_playlist_play_async(id);
    } else {
        /* Empty playlist: ctrl_next's worker fetch_more falls back to a default
         * cloud browse, pulling and playing the first recommended song. */
        wukong_playback_ctrl_next();
    }
}

void ui_svc_music_play_pause(void)
{
    if (s_status.state == AI_PLAYER_PLAYING) {
        wukong_audio_player_pause();
    } else {
        /* Resuming from the music UI implies normal playback — restore the
         * auto-advance that ui_svc_music_stop() turned off on navigate-away. */
        wukong_playback_ctrl_set_auto_next(TRUE);
        music_resume_or_restart();
    }
}

void ui_svc_music_next(void) { wukong_playback_ctrl_set_auto_next(TRUE); wukong_playback_ctrl_next(); }
void ui_svc_music_prev(void) { wukong_playback_ctrl_set_auto_next(TRUE); wukong_playback_ctrl_prev(); }

/* Stop only the background (music) player; the foreground TTS player is left
 * alone. The player fires a STOPPED event, which the subscription turns into a
 * status-callback update — no need to mutate s_status here.
 *
 * Disable auto-play-next FIRST: a bare player stop emits the same STOPPED event
 * as a natural song-end, and the playback controller would otherwise schedule
 * auto-next and start the next track. The setter takes the playback mutex that
 * the event handler also holds when it reads s_auto_play_next, so the disable is
 * committed before the (async) STOPPED event is processed. The music-page play
 * entry points below re-enable it. */
void ui_svc_music_stop(void)
{
    wukong_playback_ctrl_set_auto_next(FALSE);
    wukong_audio_player_stop(AI_PLAYER_BG);
}

void ui_svc_music_autoplay(void)
{
    /* Page (re)entry: restore auto-advance in case a previous navigate-away
     * disabled it via ui_svc_music_stop(). Set before the early-return so a
     * still-PLAYING session also keeps a consistent auto-next state. */
    wukong_playback_ctrl_set_auto_next(TRUE);

    if (s_status.state == AI_PLAYER_PLAYING) {
        return;
    }

    music_resume_or_restart();
}

OPERATE_RET ui_svc_music_list(ty_cJSON **out) { return wukong_playback_playlist_list(out); }

/* ---- async playlist fetch ------------------------------------------------ */
static ui_svc_music_list_cb_t s_list_cb = NULL;   /* latest-wins single slot */

/* UI thread: hand the fetched list to the registered receiver (ownership
 * transfers); with no receiver left, free it so nothing leaks. */
static void list_deliver_ui_cb(void *data)
{
    ty_cJSON *list = (ty_cJSON *)data;
    if (s_list_cb) {
        s_list_cb(list);
    } else if (list) {
        ty_cJSON_Delete(list);
    }
}

/* WORKQ_SYSTEM: the playlist fetch contends on the playback mutex (held for
 * seconds during URL refresh) — keep it off the UI thread. */
static void list_fetch_work_cb(void *data)
{
    (void)data;
    ty_cJSON *list = NULL;
    if (wukong_playback_playlist_list(&list) != OPRT_OK) {
        list = NULL;
    }
    ui_app_async_call(list_deliver_ui_cb, list);
}

void ui_svc_music_list_async(ui_svc_music_list_cb_t cb)
{
    s_list_cb = cb;
    if (tal_workq_schedule(WORKQ_SYSTEM, list_fetch_work_cb, NULL) != OPRT_OK) {
        PR_ERR("music svc: schedule list fetch failed");
        ui_app_async_call(list_deliver_ui_cb, NULL);   /* deliver empty so the page settles */
    }
}
void ui_svc_music_play_id(int id)             { wukong_playback_ctrl_set_auto_next(TRUE); wukong_playback_playlist_play_async(id); }
void ui_svc_music_remove_id(int id)           { wukong_playback_playlist_remove(id); }
int  ui_svc_music_current_id(void)            { return wukong_playback_ctrl_get_current_play_id(); }

ui_music_mode_t ui_svc_music_mode_get(void)
{
    return (ui_music_mode_t)wukong_playback_ctrl_get_play_mode();
}

void ui_svc_music_mode_set(ui_music_mode_t mode)
{
    if (mode < UI_MUSIC_MODE_MAX) {
        wukong_playback_ctrl_set_play_mode((WK_PLAY_MODE_E)mode);
    }
}

int ui_svc_music_progress_percent(void)
{
    UINT_T off = 0, len = 0;
    if (wukong_audio_player_get_progress(&off, &len) != OPRT_OK) {
        return -1;
    }
    if (len == 0) {
        return -1;
    }
    if (off > len) {
        off = len;
    }
    return (int)(((uint64_t)off * 100u) / len);
}
