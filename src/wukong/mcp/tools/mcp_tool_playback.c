/**
 * @file mcp_tool_playback.c
 * @brief MCP tools for music playback: control, status, search, playlist list/clear
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_tool_playback.h"
#include "wukong_ai_mcp.h"
#include "wukong_playback_ctrl.h"

#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "wukong_ai_agent.h"

/* ========================================================================== */
/*                      Tool: device_playback_control                         */
/* ========================================================================== */

typedef struct {
    CONST CHAR_T *name;
    WUKONG_AI_EVENT_TYPE_E event;
} PLAYBACK_ACTION_MAP_T;

STATIC CONST PLAYBACK_ACTION_MAP_T s_event_action_map[] = {
    { "resume",          WUKONG_AI_EVENT_PLAY_CTL_RESUME },
    { "pause",           WUKONG_AI_EVENT_PLAY_CTL_PAUSE },
    { "replay",          WUKONG_AI_EVENT_PLAY_CTL_REPLAY },
    { "single_loop",     WUKONG_AI_EVENT_PLAY_CTL_SINGLE_LOOP },
    { "sequential_loop", WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL_LOOP },
    { "no_loop",         WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL },
};

#define EVENT_ACTION_MAP_SIZE  (sizeof(s_event_action_map) / sizeof(s_event_action_map[0]))

/*
 * Small-memory builds use a bounded MAX_MQ_MESSAGE; cap music_list page size.
 * Play URLs are not prefetched here — resolved on device_playlist_play via refresh_play_url.
 */
#define MUSIC_SEARCH_LIST_LIMIT_CAP        5

STATIC OPERATE_RET __playback_control(CONST CHAR_T *name, CONST ty_cJSON *args,
                                       ty_cJSON **out_content, BOOL_T *is_error,
                                       VOID *user_data)
{
    CONST CHAR_T *action = NULL;
    ty_cJSON *j;

    (VOID)name;
    (VOID)user_data;

    if (args) {
        j = ty_cJSON_GetObjectItem(args, "action");
        if (j && ty_cJSON_IsString(j))
            action = j->valuestring;
    }

    if (!action) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Missing action parameter"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    TAL_PR_DEBUG("playback control action: %s", action);

    /* Event group: resume / pause / replay / loop modes */
    for (UINT_T i = 0; i < EVENT_ACTION_MAP_SIZE; i++) {
        if (strcmp(action, s_event_action_map[i].name) == 0) {
            wukong_ai_event_notify(s_event_action_map[i].event, NULL);

            *out_content = ty_cJSON_CreateArray();
            if (*out_content) {
                ty_cJSON_AddItemToArray(*out_content,
                    mcp_content_make_text("true"));
            }
            return OPRT_OK;
        }
    }

    /* next / prev: local playlist navigation, fetch more pages on boundary */
    if (strcmp(action, "next") == 0 || strcmp(action, "prev") == 0) {
        BOOL_T is_next = (strcmp(action, "next") == 0);
        OPERATE_RET rt = is_next ? wukong_playback_playlist_next()
                                 : wukong_playback_playlist_prev();

        if (rt != OPRT_OK && is_next) {
            TAL_PR_NOTICE("playlist next: local end, fetching more via music_list");
            rt = wukong_playback_ctrl_fetch_more();
        }

        if (rt == OPRT_OK) {
            WUKONG_MUSIC_PLAYER_T cur = {0};
            wukong_playback_ctrl_get_status(&cur);

            ty_cJSON *res = ty_cJSON_CreateObject();
            if (res) {
                ty_cJSON_AddBoolToObject(res, "ok", TRUE);
                ty_cJSON_AddStringToObject(res, "song_name", cur.song_name);
                ty_cJSON_AddStringToObject(res, "artist", cur.artist);
            }
            CHAR_T *out = res ? ty_cJSON_PrintUnformatted(res) : NULL;
            if (res) {
                ty_cJSON_Delete(res);
            }

            *out_content = ty_cJSON_CreateArray();
            if (*out_content) {
                ty_cJSON_AddItemToArray(*out_content,
                    mcp_content_make_text(out ? out : "{\"ok\":true}"));
            }
            if (out) {
                ty_cJSON_FreeBuffer(out);
            }
            return OPRT_OK;
        }

        *out_content = ty_cJSON_CreateArray();
        if (*out_content) {
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text(is_next
                    ? "{\"ok\":false,\"reason\":\"no more songs\"}"
                    : "{\"ok\":false,\"reason\":\"already at beginning\"}"));
        }
        return OPRT_OK;
    }

    /* Auto-play-next toggle */
    if (strcmp(action, "auto_play_next_on") == 0 ||
        strcmp(action, "auto_play_next_off") == 0) {
        BOOL_T enable = (strcmp(action, "auto_play_next_on") == 0);
        wukong_playback_ctrl_set_auto_next(enable);

        *out_content = ty_cJSON_CreateArray();
        if (*out_content) {
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text(enable ? "{\"auto_play_next\":true}"
                                             : "{\"auto_play_next\":false}"));
        }
        return OPRT_OK;
    }

    *out_content = ty_cJSON_CreateArray();
    if (*out_content) {
        ty_cJSON_AddItemToArray(*out_content,
            mcp_content_make_text("Unknown action"));
    }
    *is_error = TRUE;
    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_playback_status                         */
/* ========================================================================== */

STATIC OPERATE_RET __playback_status(CONST CHAR_T *name, CONST ty_cJSON *args,
                                      ty_cJSON **out_content, BOOL_T *is_error,
                                      VOID *user_data)
{
    WUKONG_MUSIC_PLAYER_T status = {0};
    CONST CHAR_T *state_str = "stopped";
    ty_cJSON *result = NULL;
    CHAR_T *json_str = NULL;

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    wukong_playback_ctrl_get_status(&status);

    if (status.state == AI_PLAYER_PLAYING)
        state_str = "playing";
    else if (status.state == AI_PLAYER_PAUSED)
        state_str = "paused";

    result = ty_cJSON_CreateObject();
    if (!result)
        return OPRT_MALLOC_FAILED;

    ty_cJSON_AddStringToObject(result, "song_name", status.song_name);
    ty_cJSON_AddStringToObject(result, "artist", status.artist);
    ty_cJSON_AddStringToObject(result, "state", state_str);
    ty_cJSON_AddBoolToObject(result, "auto_play_next",
                             wukong_playback_ctrl_get_auto_next());

    json_str = ty_cJSON_PrintUnformatted(result);
    ty_cJSON_Delete(result);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content,
            mcp_content_make_text(json_str ? json_str : "{}"));
    if (json_str)
        ty_cJSON_FreeBuffer(json_str);

    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_music_search                            */
/* ========================================================================== */

STATIC OPERATE_RET __music_search(CONST CHAR_T *name, CONST ty_cJSON *args,
                                   ty_cJSON **out_content, BOOL_T *is_error,
                                   VOID *user_data)
{
    WUKONG_MUSIC_LIST_PARAM_T param = {0};
    ty_cJSON *result = NULL;
    ty_cJSON *data_node = NULL;
    ty_cJSON *items = NULL;
    CHAR_T *json_str = NULL;
    ty_cJSON *j;

    (VOID)name;
    (VOID)user_data;

    /* Parse optional parameters */
    if (args) {
        j = ty_cJSON_GetObjectItem(args, "keyword");
        if (j && ty_cJSON_IsString(j)) param.keyword = j->valuestring;

        j = ty_cJSON_GetObjectItem(args, "tag");
        if (j && ty_cJSON_IsString(j)) param.tag = j->valuestring;

        j = ty_cJSON_GetObjectItem(args, "name");
        if (j && ty_cJSON_IsString(j)) param.name = j->valuestring;

        j = ty_cJSON_GetObjectItem(args, "artist");
        if (j && ty_cJSON_IsString(j)) param.artist = j->valuestring;

        j = ty_cJSON_GetObjectItem(args, "album_name");
        if (j && ty_cJSON_IsString(j)) param.album_name = j->valuestring;

        j = ty_cJSON_GetObjectItem(args, "offset");
        if (j && ty_cJSON_IsNumber(j)) param.offset = j->valueint;

        j = ty_cJSON_GetObjectItem(args, "limit");
        if (j && ty_cJSON_IsNumber(j)) param.limit = j->valueint;
    }

    if (param.limit <= 0) {
        param.limit = 5;
    }
    if (param.limit > MUSIC_SEARCH_LIST_LIMIT_CAP) {
        param.limit = MUSIC_SEARCH_LIST_LIMIT_CAP;
    }

    TAL_PR_DEBUG("music_search: keyword=%s tag=%s artist=%s",
                 param.keyword ? param.keyword : "",
                 param.tag ? param.tag : "",
                 param.artist ? param.artist : "");

    /* Send music_list request and wait for cloud response */
    OPERATE_RET rt = wukong_playback_ctrl_send_music_list(
        &param, &result, 8000);

    if (rt != OPRT_OK || !result) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Music search timed out or failed"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    /* Extract data.items from response */
    data_node = ty_cJSON_GetObjectItem(result, "data");
    items = data_node ? ty_cJSON_GetObjectItem(data_node, "items") : NULL;

    if (!items || !ty_cJSON_IsArray(items) ||
        ty_cJSON_GetArraySize(items) == 0) {
        ty_cJSON_Delete(result);
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("[]"));
        return OPRT_OK;
    }

    /* Auto-populate local playlist and build slim summary for AI */
    INT_T item_cnt = ty_cJSON_GetArraySize(items);
    INT_T added = 0;
    INT_T first_id = -1;
    CONST CHAR_T *first_name = NULL;
    CONST CHAR_T *first_artist_str = NULL;

    ty_cJSON *songs_arr = ty_cJSON_CreateArray();

    for (INT_T i = 0; i < item_cnt; i++) {
        ty_cJSON *row = ty_cJSON_GetArrayItem(items, i);
        if (row == NULL) {
            continue;
        }

        ty_cJSON *j_name = ty_cJSON_GetObjectItem(row, "name");
        ty_cJSON *j_artist = ty_cJSON_GetObjectItem(row, "artist");
        ty_cJSON *j_aid = ty_cJSON_GetObjectItem(row, "audioId");
        ty_cJSON *j_ch = ty_cJSON_GetObjectItem(row, "channelCode");
        ty_cJSON *j_url = ty_cJSON_GetObjectItem(row, "playUrl");

        CONST CHAR_T *s_name = (j_name && ty_cJSON_IsString(j_name))
                                    ? j_name->valuestring : NULL;
        CONST CHAR_T *s_aid = (j_aid && ty_cJSON_IsString(j_aid))
                                    ? j_aid->valuestring : NULL;
        if (s_name == NULL || s_name[0] == '\0') {
            continue;
        }
        if (s_aid == NULL || s_aid[0] == '\0') {
            continue;
        }

        CONST CHAR_T *s_artist = NULL;
        if (j_artist && ty_cJSON_IsString(j_artist)) {
            s_artist = j_artist->valuestring;
        } else if (j_artist && ty_cJSON_IsArray(j_artist) &&
                   ty_cJSON_GetArraySize(j_artist) > 0) {
            ty_cJSON *first = ty_cJSON_GetArrayItem(j_artist, 0);
            if (first && ty_cJSON_IsString(first)) {
                s_artist = first->valuestring;
            }
        }

        CONST CHAR_T *s_ch = (j_ch && ty_cJSON_IsString(j_ch))
                                    ? j_ch->valuestring : NULL;
        CONST CHAR_T *s_url = (j_url && ty_cJSON_IsString(j_url) &&
                                j_url->valuestring[0] != '\0')
                                    ? j_url->valuestring : NULL;

        INT_T assigned_id = -1;
        OPERATE_RET add_rt = wukong_playback_playlist_add(
            s_name, s_artist, s_url, s_aid, s_ch, &assigned_id);
        if (add_rt == OPRT_OK) {
            added++;
            if (first_id < 0) {
                first_id = assigned_id;
                first_name = s_name;
                first_artist_str = s_artist;
            }
        }

        if (songs_arr) {
            ty_cJSON *song = ty_cJSON_CreateObject();
            if (song) {
                ty_cJSON_AddStringToObject(song, "name", s_name);
                ty_cJSON_AddStringToObject(song, "artist", s_artist ? s_artist : "");
                ty_cJSON_AddItemToArray(songs_arr, song);
            }
        }
    }
    TAL_PR_NOTICE("music_search: auto-added %d/%d items to playlist", added, item_cnt);

    /* Parse pagination and save search context for auto-next */
    ty_cJSON *page_node = data_node ? ty_cJSON_GetObjectItem(data_node, "page") : NULL;
    BOOL_T has_more = FALSE;
    INT_T next_offset = param.offset + item_cnt;
    if (page_node != NULL) {
        ty_cJSON *j_more = ty_cJSON_GetObjectItem(page_node, "hasMore");
        has_more = (j_more != NULL && j_more->type == ty_cJSON_True);
        ty_cJSON *j_ofs = ty_cJSON_GetObjectItem(page_node, "offset");
        ty_cJSON *j_lim = ty_cJSON_GetObjectItem(page_node, "limit");
        if (j_ofs && ty_cJSON_IsNumber(j_ofs) && j_lim && ty_cJSON_IsNumber(j_lim)) {
            next_offset = j_ofs->valueint + j_lim->valueint;
        }
    } else {
        has_more = (item_cnt >= param.limit);
    }
    wukong_playback_ctrl_save_search_ctx(&param, next_offset, has_more);

    /* Auto-play the first song */
    BOOL_T playing = FALSE;
    if (first_id >= 0) {
        OPERATE_RET play_rt = wukong_playback_playlist_play(first_id);
        playing = (play_rt == OPRT_OK);
        TAL_PR_NOTICE("music_search: auto-play id=%d rt=%d", first_id, play_rt);
    }

    ty_cJSON_Delete(result);

    /* Build slim summary JSON for AI */
    ty_cJSON *summary = ty_cJSON_CreateObject();
    if (summary) {
        ty_cJSON_AddNumberToObject(summary, "added", added);
        ty_cJSON_AddNumberToObject(summary, "total", item_cnt);
        if (playing && first_name) {
            ty_cJSON *np = ty_cJSON_CreateObject();
            if (np) {
                ty_cJSON_AddStringToObject(np, "name", first_name);
                ty_cJSON_AddStringToObject(np, "artist",
                    first_artist_str ? first_artist_str : "");
                ty_cJSON_AddItemToObject(summary, "now_playing", np);
            }
        }
        if (songs_arr) {
            ty_cJSON_AddItemToObject(summary, "songs", songs_arr);
            songs_arr = NULL;
        }
    }
    if (songs_arr) {
        ty_cJSON_Delete(songs_arr);
    }

    json_str = summary ? ty_cJSON_PrintUnformatted(summary) : NULL;
    if (summary) {
        ty_cJSON_Delete(summary);
    }

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content,
            mcp_content_make_text(json_str ? json_str : "{}"));
    if (json_str)
        ty_cJSON_FreeBuffer(json_str);

    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_playlist_list                           */
/* ========================================================================== */

/**
 * @brief List all items in the device playlist (slim: name + artist only)
 * @param[in] name tool name
 * @param[in] args tool arguments (unused)
 * @param[out] out_content MCP response content
 * @param[out] is_error error flag
 * @param[in] user_data user context (unused)
 * @return OPRT_OK
 */
STATIC OPERATE_RET __playlist_list(CONST CHAR_T *name, CONST ty_cJSON *args,
                                    ty_cJSON **out_content, BOOL_T *is_error,
                                    VOID *user_data)
{
    ty_cJSON *json = NULL;
    OPERATE_RET rt;

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    rt = wukong_playback_playlist_list(&json);
    if (rt != OPRT_OK || !json) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Failed to retrieve playlist"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    /* Build slim list: only name + artist */
    INT_T cnt = ty_cJSON_GetArraySize(json);
    ty_cJSON *slim = ty_cJSON_CreateArray();
    for (INT_T i = 0; i < cnt; i++) {
        ty_cJSON *item = ty_cJSON_GetArrayItem(json, i);
        if (!item) {
            continue;
        }
        ty_cJSON *entry = ty_cJSON_CreateObject();
        if (!entry) {
            continue;
        }
        ty_cJSON *jn = ty_cJSON_GetObjectItem(item, "song_name");
        ty_cJSON *ja = ty_cJSON_GetObjectItem(item, "artist");
        ty_cJSON_AddStringToObject(entry, "name",
            (jn && ty_cJSON_IsString(jn)) ? jn->valuestring : "");
        ty_cJSON_AddStringToObject(entry, "artist",
            (ja && ty_cJSON_IsString(ja)) ? ja->valuestring : "");
        ty_cJSON_AddItemToArray(slim, entry);
    }
    ty_cJSON_Delete(json);

    CHAR_T *json_str = slim ? ty_cJSON_PrintUnformatted(slim) : NULL;
    if (slim) {
        ty_cJSON_Delete(slim);
    }

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content,
            mcp_content_make_text(json_str ? json_str : "[]"));
    if (json_str)
        ty_cJSON_FreeBuffer(json_str);

    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_playlist_clear                          */
/* ========================================================================== */

/**
 * @brief Clear all items from the device playlist
 * @param[in] name tool name
 * @param[in] args tool arguments (unused)
 * @param[out] out_content MCP response content
 * @param[out] is_error error flag (unused)
 * @param[in] user_data user context (unused)
 * @return OPRT_OK
 */
STATIC OPERATE_RET __playlist_clear(CONST CHAR_T *name, CONST ty_cJSON *args,
                                     ty_cJSON **out_content, BOOL_T *is_error,
                                     VOID *user_data)
{
    (VOID)name;
    (VOID)args;
    (VOID)user_data;
    (VOID)is_error;

    wukong_playback_playlist_clear();

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content,
            mcp_content_make_text("true"));

    return OPRT_OK;
}

/* ========================================================================== */
/*                              Init                                          */
/* ========================================================================== */

/**
 * @brief Register all playback and playlist MCP tools
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET mcp_tool_playback_init(VOID)
{
    OPERATE_RET rt;

    rt = MCP_TOOL_ADD(
        "device_playback_control",
        "Control music/story playback on the device.\n"
        "- 'next' / 'prev': Skip to next/previous song. Returns JSON with ok, "
        "song_name and artist of the new track.\n"
        "- 'pause' / 'resume': Pause or resume the currently playing audio.\n"
        "- 'replay': Restart the current track from the beginning.\n"
        "- 'single_loop' / 'sequential_loop' / 'no_loop': Change the loop mode.\n"
        "- 'auto_play_next_on' / 'auto_play_next_off': Enable or disable automatic "
        "playback of the next song when the current one finishes.",
        __playback_control, NULL,
        MCP_SCHEMA_STR("action",
            "Playback action: "
            "'next' = next song, "
            "'prev' = previous song, "
            "'resume' = resume playback, "
            "'pause' = pause playback, "
            "'replay' = restart current track, "
            "'single_loop' = repeat current track, "
            "'sequential_loop' = loop entire playlist, "
            "'no_loop' = play sequentially without looping, "
            "'auto_play_next_on' = enable auto-play next, "
            "'auto_play_next_off' = disable auto-play next")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = MCP_TOOL_ADD(
        "device_playback_status",
        "Get current playback status.\n"
        "Returns JSON with song_name, artist, state (playing/paused/stopped) "
        "and auto_play_next (true/false).",
        __playback_status, NULL
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = MCP_TOOL_ADD(
        "device_music_search",
        "Search cloud music, auto-add all results to playlist, and auto-play the first song.\n"
        "One call does everything: search + enqueue + play. No need to call add or play separately.\n"
        "Returns JSON: added, total, now_playing (name + artist), songs array (name + artist).",
        __music_search, NULL,
        MCP_SCHEMA_STR_OPT("keyword", "General search keyword"),
        MCP_SCHEMA_STR_OPT("tag", "Music category tag, e.g. 'children','pop'"),
        MCP_SCHEMA_STR_OPT("name", "Song title"),
        MCP_SCHEMA_STR_OPT("artist", "Artist name"),
        MCP_SCHEMA_STR_OPT("album_name", "Album name"),
        MCP_SCHEMA_INT_OPT("offset", "Page offset, default 0"),
        MCP_SCHEMA_INT_OPT_RANGE("limit", "Page size, default 5, max 5", 1, MUSIC_SEARCH_LIST_LIMIT_CAP)
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = MCP_TOOL_ADD(
        "device_playlist_list",
        "List all songs in the device playlist.\n"
        "Returns JSON array of {name, artist}.",
        __playlist_list, NULL
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = MCP_TOOL_ADD(
        "device_playlist_clear",
        "Clear the entire device playlist. Use before searching a different genre.",
        __playlist_clear, NULL
    );

    return rt;
}
