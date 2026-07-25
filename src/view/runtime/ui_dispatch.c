#include "ui_dispatch.h"

#include "lv_vendor.h"
#include "tal_memory.h"
#include "tuya_ai_toy_camera.h"
#include "tuya_app_config.h"
#include "tuya_ws_db.h"
#include "ui_common.h"
#include "ui_record_runtime.h"
#include "ui_toast.h"
#include "wukong_ai_agent.h"
#include "wukong_ai_mode.h"
#include "wukong_picture.h"
#include "wukong_picture_input.h"

/* tuya_ai_toy_device_mode_get is not exposed in a public header today;
 * desk_event_handle.h borrows it via extern. Mirror that here so the
 * AI camera path can avoid switching mode when already in CHAT. */
extern AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(VOID);

#define DISPATCH_THUMB_SIZE  96
#define DISPATCH_THUMB_MAX_SELECT  30

/* AI camera toggle KV; mirrors ui_settings.c. Read each take_photo so a
 * settings toggle takes effect on the very next shot — no caching. */
#define KV_AI_CAMERA  "ui_ai_camera"

/* Cloud / link connectivity cache, kept in sync from the dispatch
 * handlers __handle_stat_net / online / netcfg below. Read by
 * __action_take_photo to drive the AI camera offline-fallback (ADR-0004
 * "未联网降级"). */
static WUKONG_PICTURE_THUMB_LIST_T s_dispatch_thumb_list = {0};
STATIC BOOL_T s_net_connected = FALSE;

typedef VOID_T (*UI_MSG_HANDLER)(TY_DISPLAY_MSG_T *msg);
typedef OPERATE_RET (*UI_ACTION_HANDLER)(UINT8_T *msg, INT_T len);

typedef struct {
    TY_DISPLAY_TYPE_E type;
    UI_MSG_HANDLER handler;
} UI_MSG_DISPATCH_T;

typedef struct {
    TY_DISPLAY_ACTION_E action;
    UI_ACTION_HANDLER handler;
} UI_ACTION_DISPATCH_T;

STATIC VOID __dispatch_camera_yuv_frame_cb(const CAM_FRAME_T *frame, VOID *ctx)
{
    (VOID)ctx;
    if (NULL == frame) {
        return;
    }

    lv_vendor_disp_lock();
    ui_camera_set_preview_yuv_format(frame->width, frame->height, frame->data, frame->length);
    lv_vendor_disp_unlock();
}

STATIC VOID_T __dispatch_view_image_from_album(VOID_T *arg)
{
    char *pic_name = (char *)arg;

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_by_name(pic_name, &pic) != OPRT_OK) {
        PR_ERR("get picture by name failed: %s", pic_name);
        return;
    }

    if (pic.data && pic.len) {
        ui_chat_disp_image(pic.data, pic.len);
    }

    wukong_picture_free_pic_info(&pic);
}

STATIC VOID_T __dispatch_chat_image(CHAT_MSG_ROLE_TP_E type, char *pic_name)
{
    ui_chat_add_link(type, "查看图片", __dispatch_view_image_from_album, pic_name, strlen(pic_name) + 1);
}

STATIC VOID_T __handle_human_chat(TY_DISPLAY_MSG_T *msg)
{
    ui_chat_add_text(CHAT_MSG_ROLE_USER, (CONST CHAR_T *)msg->data);
}

STATIC VOID_T __handle_ai_chat(TY_DISPLAY_MSG_T *msg)
{
    ui_chat_add_text(CHAT_MSG_ROLE_AI, (CONST CHAR_T *)msg->data);
}

STATIC VOID_T __handle_ai_chat_start(TY_DISPLAY_MSG_T *msg)
{
    ui_chat_stream_begin();
    if (NULL != msg->data) {
        ui_chat_stream_append((CONST CHAR_T *)msg->data);
    }
}

STATIC VOID_T __handle_ai_chat_data(TY_DISPLAY_MSG_T *msg)
{
    ui_chat_stream_append((CONST CHAR_T *)msg->data);
}

STATIC VOID_T __handle_ai_chat_stop(TY_DISPLAY_MSG_T *msg)
{
    (VOID_T)msg;
    ui_chat_stream_end();
}

STATIC VOID_T __handle_ai_image(TY_DISPLAY_MSG_T *msg)
{
    __dispatch_chat_image(CHAT_MSG_ROLE_AI, (char *)msg->data);
}

STATIC VOID_T __handle_clear_attachment(TY_DISPLAY_MSG_T *msg)
{
    (VOID_T)msg;
    ui_chat_clear_attachment();
}

STATIC VOID_T __handle_mode_notify(TY_DISPLAY_MSG_T *msg)
{
    if (msg == NULL || msg->data == NULL) {
        return;
    }

    if (ui_nav_current() == UI_SCR_CHAT) {
        ui_chat_add_text(CHAT_MSG_ROLE_AI, (CONST CHAR_T *)msg->data);
    } else {
        ui_chat_set_pending_notify((CONST CHAR_T *)msg->data);
    }

    /* Mode just switched: keep the chat title-bar mode label and the
     * control-center mode entry in sync. ui_chat_refresh_mode is a no-op
     * until the chat screen is lazily created. */
    ui_chat_refresh_mode();
    ui_control_center_refresh_mode();
}

/**
 * @brief Update the chat title-bar interaction state label
 * @param[in] msg display message; msg->data[0] is the AI_CHAT_STATE_E value
 * @return none
 */
STATIC VOID_T __handle_chat_stat(TY_DISPLAY_MSG_T *msg)
{
    if (msg == NULL || msg->data == NULL || msg->len < 1) {
        return;
    }
    ui_chat_set_chat_state(msg->data[0]);
}

/**
 * @brief Handle WiFi signal-level update from the platform
 * @param[in] msg display message; msg->data[0] is the signal level
 *                (0 = disconnected, non-zero = connected/level)
 * @return none
 * @note Mapped to a binary on/off of the home-screen WiFi icon; the view
 *       UI ships only a single wifi icon so finer levels collapse to
 *       "connected".
 */
STATIC VOID_T __handle_stat_net(TY_DISPLAY_MSG_T *msg)
{
    if (msg == NULL || msg->data == NULL || msg->len < 1) {
        return;
    }
    BOOL_T connected = (msg->data[0] != 0);
    s_net_connected = connected;
    ui_home_set_net_state(connected);
}

/**
 * @brief Cloud connection is up: clear netcfg banner and resync chat state
 * @param[in] msg display message (payload unused)
 * @return none
 */
STATIC VOID_T __handle_stat_online(TY_DISPLAY_MSG_T *msg)
{
    (VOID_T)msg;
    s_net_connected = TRUE;
    ui_home_set_net_state(TRUE);
    /* Drop any "进入配网" notify left from a previous unprovisioned cycle
     * and bring the chat title-bar state label back to idle. */
    ui_chat_set_pending_notify(NULL);
    ui_chat_set_chat_state(AI_CHAT_IDLE);
}

/**
 * @brief Device entering low-power / screen-off: park chat state at idle
 * @param[in] msg display message (payload unused)
 * @return none
 * @note tuya_ai_display.c separately requests the screen-saver after
 *       dispatch returns; this handler only resyncs in-RAM UI state.
 */
STATIC VOID_T __handle_stat_sleep(TY_DISPLAY_MSG_T *msg)
{
    (VOID_T)msg;
    ui_chat_set_chat_state(AI_CHAT_IDLE);
}

/**
 * @brief Device is unprovisioned: surface the netcfg prompt to the user
 * @param[in] msg display message (payload unused)
 * @return none
 * @note Mirrors __handle_mode_notify: pushes a bubble onto the chat screen
 *       when active, otherwise stashes a pending notify so the prompt is
 *       shown on the next chat-screen entry. Also forces the home WiFi
 *       icon to the disconnected state for visual consistency.
 */
STATIC VOID_T __handle_stat_netcfg(TY_DISPLAY_MSG_T *msg)
{
    (VOID_T)msg;
    s_net_connected = FALSE;
    ui_home_set_net_state(FALSE);

    CONST CHAR_T *prompt = "进入配网模式，请使用涂鸦智能 App 完成配网";
    if (ui_nav_current() == UI_SCR_CHAT) {
        ui_chat_add_text(CHAT_MSG_ROLE_AI, prompt);
    } else {
        ui_chat_set_pending_notify(prompt);
    }
}

/**
 * @brief Persist the language selection notified by the platform
 * @param[in] msg display message; msg->data[0] is the language code
 *                (0 = zh-CN, non-zero = fallback / en)
 * @return none
 * @note The view UI ships a single Chinese string set today, so this
 *       handler only records the code via log. Once per-language assets
 *       are added, refresh hooks should be wired here.
 */
STATIC VOID_T __handle_language(TY_DISPLAY_MSG_T *msg)
{
    if (msg == NULL || msg->data == NULL || msg->len < 1) {
        return;
    }
    PR_DEBUG("display: language=%u", msg->data[0]);
}

STATIC OPERATE_RET __action_open_camera(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    OPERATE_RET rt = tuya_ai_toy_camera_subscribe(CAM_STREAM_YUV422, CAM_CONSUMER_UI_PREVIEW,
                                                   __dispatch_camera_yuv_frame_cb, NULL);
    if (rt == OPRT_NOT_SUPPORTED) {
        PR_WARN("camera open: not supported");
        return rt;
    }
    PR_DEBUG("camera open");
    ui_nav_to(UI_SCR_CAMERA);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_close_camera(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    OPERATE_RET rt = tuya_ai_toy_camera_unsubscribe(CAM_STREAM_YUV422, CAM_CONSUMER_UI_PREVIEW);
    if (rt == OPRT_NOT_SUPPORTED) {
        PR_WARN("camera close: not supported");
        return rt;
    }
    PR_DEBUG("camera close");
    ui_nav_back();
    return OPRT_OK;
}

STATIC OPERATE_RET __action_open_album(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    ui_nav_to(UI_SCR_ALBUM);
    wukong_picture_open_album();
    uint32_t count = wukong_picture_get_count();
    PR_DEBUG("album open: count=%d, seek to last", count);
    if (count > 0) {
        ui_album_set_empty_state(FALSE);
        wukong_picture_seek_to_photo(count);
        WUKONG_PICTURE_INFO_T pic = {0};
        if (wukong_picture_get_next(&pic) == OPRT_OK && pic.data && pic.len) {
            ui_album_set_jpeg_photo(pic.width, pic.height, pic.data, pic.len);
        }
        wukong_picture_free_pic_info(&pic);
    } else {
        ui_album_set_empty_state(TRUE);
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __action_close_album(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("album close");
    wukong_picture_close_album();
    ui_nav_back();
    return OPRT_OK;
}

STATIC OPERATE_RET __action_album_view_next(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_next(&pic) == OPRT_OK && pic.data && pic.len) {
        ui_album_set_jpeg_photo(pic.width, pic.height, pic.data, pic.len);
    }
    wukong_picture_free_pic_info(&pic);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_album_view_prev(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_prev(&pic) == OPRT_OK && pic.data && pic.len) {
        ui_album_set_jpeg_photo(pic.width, pic.height, pic.data, pic.len);
    }
    wukong_picture_free_pic_info(&pic);
    return OPRT_OK;
}

/**
 * @brief Read AI camera toggle from KV; default OFF on miss.
 *
 * Read on every shot rather than caching: keeps the settings toggle's
 * "next shot picks up immediately" contract (ADR-0004 A) and avoids
 * cross-screen state. KV reads are cheap; user can't tap the shutter
 * fast enough to make this measurable.
 */
STATIC BOOL_T __ai_camera_get(VOID_T)
{
    BYTE_T *value = NULL;
    UINT_T  len   = 0;
    BOOL_T  on    = FALSE;

    if (wd_common_read(KV_AI_CAMERA, &value, &len) == OPRT_OK && value != NULL) {
        if (len >= 1) {
            on = (value[0] != 0);
        }
        wd_common_free_data(value);
    }
    return on;
}

/**
 * @brief AI-camera ON path: send the JPEG to wukong_ai_agent and jump
 *        to the chat screen for the streamed reply.
 *
 * Mirrors desk_func_camera.c (T5AI_BOARD_DESKTOP) so the two boards stay
 * behaviour-identical (ADR-0004 C): switch to CHAT mode if not already,
 * pin the agent scene, run a single input transaction (start → image →
 * text → stop), then ui_nav_to(UI_SCR_CHAT). The send_text prompt is
 * identical to desktop's literal — any future change must update both
 * boards together (ADR-0004 "为什么 · send_text prompt").
 */
STATIC VOID_T __take_photo_send_ai(BYTE_T *jpeg, UINT_T jpeg_len)
{
    if (tuya_ai_toy_device_mode_get() != AI_DEVICE_MODE_CHAT) {
        PR_INFO("take photo ai: device mode -> CHAT");
        wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);
    }
    wukong_ai_agent_set_scene(AI_AGENT_SCODE_CHAT);
    wukong_ai_agent_input_start(TRUE);
    wukong_ai_agent_send_image(jpeg, jpeg_len);
    wukong_ai_agent_send_text("请解释刚刚上传的图片内容，请勿触发 MCP 技能。");
    wukong_ai_agent_input_stop();
    ui_nav_to(UI_SCR_CHAT);
}

STATIC OPERATE_RET __action_take_photo(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    BYTE_T *jpeg = NULL;
    UINT_T jpeg_len = 0;
    OPERATE_RET rt = tuya_ai_toy_camera_snapshot(CAM_SNAP_JPEG, &jpeg, &jpeg_len, 3000);
    if (rt == OPRT_NOT_SUPPORTED) {
        PR_WARN("take photo: not supported");
        return rt;
    }
    if (rt != OPRT_OK || jpeg == NULL || jpeg_len == 0) {
        PR_WARN("take photo: snapshot failed, rt=%d", rt);
        if (jpeg != NULL) {
            tal_psram_free(jpeg);
        }
        return (rt != OPRT_OK) ? rt : OPRT_COM_ERROR;
    }

    /* Always preserve to local album + thumbnail — ADR-0004 D says even
     * the offline AI-camera fallback keeps the user's shot. */
    char name[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};
    wukong_picture_save_to_album((uint8_t *)jpeg, (uint32_t)jpeg_len, name);
    ui_camera_set_thumbnail_jpeg((uint8_t *)jpeg, (uint32_t)jpeg_len);

    if (__ai_camera_get()) {
        if (s_net_connected) {
            __take_photo_send_ai(jpeg, jpeg_len);
        } else {
            PR_WARN("take photo: AI camera ON but offline; local-only");
            ui_toast_show("未联网，AI 解释失败");
        }
    }

    /* Snapshot buffer is psram_malloc'd inside tuya_ai_toy_camera_snapshot;
     * save_to_album / set_thumbnail / send_image are all deep-copy paths
     * (image_album mem backend, RGB565 thumb generator, agent send queue),
     * so freeing here is safe and closes a long-standing leak. */
    tal_psram_free(jpeg);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_album_delete(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    if (wukong_picture_delete_current() != OPRT_OK) {
        return OPRT_OK;
    }
    if (wukong_picture_get_count() == 0) {
        ui_album_set_empty_state(TRUE);
        ui_camera_clear_thumbnail();
        return OPRT_OK;
    }

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_next(&pic) == OPRT_OK && pic.data && pic.len) {
        ui_album_set_jpeg_photo(pic.width, pic.height, pic.data, pic.len);
        ui_camera_set_thumbnail_jpeg(pic.data, pic.len);
    }
    wukong_picture_free_pic_info(&pic);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_open_album_grid(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("album: all photos grid");
    ui_nav_to(UI_SCR_ALBUM_GRID);
    wukong_picture_free_thumb_list(&s_dispatch_thumb_list);
    wukong_picture_get_thumb_list(DISPATCH_THUMB_SIZE, DISPATCH_THUMB_SIZE, &s_dispatch_thumb_list);
    ui_album_grid_set_thumbs(&s_dispatch_thumb_list);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_close_album_grid(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("album grid: close");
    ui_nav_back();
    wukong_picture_free_thumb_list(&s_dispatch_thumb_list);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_album_batch_delete(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    CONST CHAR_T *names[DISPATCH_THUMB_MAX_SELECT];
    UINT32_T del_count = ui_album_grid_get_pending_delete_names(names, DISPATCH_THUMB_MAX_SELECT);
    PR_DEBUG("album grid: batch delete %u photos", del_count);
    if (del_count > 0) {
        wukong_picture_delete_batch(names, del_count);
    }
    if (del_count >= s_dispatch_thumb_list.count) {
        ui_camera_clear_thumbnail();
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __action_open_device_mode(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("device mode open");
    ui_nav_to(UI_SCR_DEVICE_MODE);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_close_device_mode(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("device mode close");
    ui_nav_back();
    return OPRT_OK;
}

/**
 * @brief Open the main record screen and enter recording mode
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 * @note Defers session bring-up (mode switch, audio cb registration,
 *       list init) to the runtime so the dispatch layer stays a thin
 *       router.
 */
STATIC OPERATE_RET __action_open_record(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("record open");
    OPERATE_RET rt = ui_record_runtime_open();
    ui_nav_to(UI_SCR_RECORD);
    return rt;
}

/**
 * @brief Close the main record screen and tear down the recording session
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_close_record(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("record close");
    ui_record_runtime_close();
    ui_nav_back();
    return OPRT_OK;
}

/**
 * @brief Open the record list screen, refreshing the on-disk list snapshot
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_open_record_list(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("record_list open");
    ui_record_runtime_refresh_ui_list();
    ui_nav_to(UI_SCR_RECORD_LIST);
    return OPRT_OK;
}

/**
 * @brief Close the record list screen
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK
 */
STATIC OPERATE_RET __action_close_record_list(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("record_list close");
    ui_nav_back();
    return OPRT_OK;
}

/**
 * @brief Begin a new recording capture
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_record_start(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("record start");
    return ui_record_runtime_start();
}

/**
 * @brief Pause an in-progress capture without closing the file
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK
 */
STATIC OPERATE_RET __action_record_pause(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("record pause");
    ui_record_runtime_pause();
    return OPRT_OK;
}

/**
 * @brief Resume a paused capture
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK
 */
STATIC OPERATE_RET __action_record_resume(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("record resume");
    ui_record_runtime_resume();
    return OPRT_OK;
}

/**
 * @brief Stop the active capture and persist the audio file
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK
 * @note Refreshes the UI list snapshot after persistence so the user
 *       sees the new entry immediately on next navigation to the list.
 */
STATIC OPERATE_RET __action_record_stop(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("record stop");
    ui_record_runtime_stop();
    ui_record_runtime_refresh_ui_list();
    return OPRT_OK;
}

/**
 * @brief Start local playback of the record entry identified by id
 * @param[in] msg payload; expected to carry an INT_T id when len >= sizeof(INT_T)
 * @param[in] len payload length in bytes
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_record_play(UINT8_T *msg, INT_T len)
{
    INT_T id = -1;
    if (msg != NULL && len >= (INT_T)sizeof(INT_T)) {
        memcpy(&id, msg, sizeof(INT_T));
    }
    PR_DEBUG("record play id=%d", id);
    return ui_record_runtime_play(id);
}

/**
 * @brief Delete the record entry identified by id (file + JSON entry)
 * @param[in] msg payload; expected to carry an INT_T id when len >= sizeof(INT_T)
 * @param[in] len payload length in bytes
 * @return OPRT_OK on success
 * @note Refreshes the UI list snapshot post-delete so the row disappears
 *       on the next navigation to the list view.
 */
STATIC OPERATE_RET __action_record_delete(UINT8_T *msg, INT_T len)
{
    INT_T id = -1;
    OPERATE_RET rt = OPRT_OK;

    if (msg != NULL && len >= (INT_T)sizeof(INT_T)) {
        memcpy(&id, msg, sizeof(INT_T));
    }
    PR_DEBUG("record delete id=%d", id);
    rt = ui_record_runtime_delete(id);
    ui_record_runtime_refresh_ui_list();
    return rt;
}

/**
 * @brief Upload the record entry identified by id to the AI agent
 * @param[in] msg payload; expected to carry an INT_T id when len >= sizeof(INT_T)
 * @param[in] len payload length in bytes
 * @return OPRT_OK when the chunked upload starts successfully
 */
STATIC OPERATE_RET __action_record_upload(UINT8_T *msg, INT_T len)
{
    INT_T id = -1;
    if (msg != NULL && len >= (INT_T)sizeof(INT_T)) {
        memcpy(&id, msg, sizeof(INT_T));
    }
    PR_DEBUG("record upload id=%d", id);
    return ui_record_runtime_upload(id);
}

/**
 * @brief Pause the current playback (UI -> audio bridge)
 * @param[in] msg unused; payload is empty for this action
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_record_play_pause(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;
    PR_DEBUG("record play pause");
    return ui_record_runtime_play_pause();
}

/**
 * @brief Resume a paused playback (UI -> audio bridge)
 * @param[in] msg unused; payload is empty for this action
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_record_play_resume(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;
    PR_DEBUG("record play resume");
    return ui_record_runtime_play_resume();
}

/**
 * @brief Hard-stop the current playback (UI -> audio bridge)
 * @param[in] msg unused; payload is empty for this action
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_record_play_stop(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;
    PR_DEBUG("record play stop");
    return ui_record_runtime_play_stop();
}

/**
 * @brief Open the music screen
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_open_music(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("music open");
    ui_nav_to(UI_SCR_MUSIC);
    return OPRT_OK;
}

/**
 * @brief Close the music screen and pop nav stack
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_close_music(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("music close");
    ui_nav_back();
    return OPRT_OK;
}

/**
 * @brief Open the call screen via ui_nav (control-center "通话" entry)
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_open_call(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    if (!ui_settings_p2p_get()) {
        PR_WARN("call open blocked: p2p disabled");
        ui_toast_show("请开启P2P");
        return OPRT_OK;
    }

    PR_DEBUG("call open");
    ui_nav_to(UI_SCR_CALL);
    return OPRT_OK;
}

/**
 * @brief Close the call screen and pop nav stack (ui_call_hide handles hangup)`
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_close_call(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("call close");
    ui_nav_back();
    return OPRT_OK;
}

/**
 * @brief Open the detection list screen via ui_nav (control-center "侦测" entry)
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_open_detection(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("detection open");
    ui_nav_to(UI_SCR_DETECTION);
    return OPRT_OK;
}

/**
 * @brief Close the detection list screen and pop nav stack
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_close_detection(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("detection close");
    ui_nav_back();
    return OPRT_OK;
}

/**
 * @brief Open the settings screen via ui_nav (control-center "设置" entry)
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_open_settings(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("settings open");
    ui_nav_to(UI_SCR_SETTINGS);
    return OPRT_OK;
}

/**
 * @brief Close the settings screen and pop nav stack
 * @param[in] msg unused
 * @param[in] len unused
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __action_close_settings(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("settings close");
    ui_nav_back();
    return OPRT_OK;
}

STATIC OPERATE_RET __action_album_ai_recognize(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    char cur_name[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};
    if (wukong_picture_get_current_name(cur_name) != OPRT_OK) {
        PR_ERR("album AI: no current picture");
        return OPRT_OK;
    }

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_by_name(cur_name, &pic) != OPRT_OK || pic.data == NULL) {
        PR_ERR("album AI: get picture failed: %s", cur_name);
        wukong_picture_free_pic_info(&pic);
        return OPRT_OK;
    }

    PR_DEBUG("album AI: attach %s to chat", cur_name);
    wukong_picture_input_add_from_album(cur_name, NULL);
    wukong_picture_close_album();
    ui_nav_back();
    /* If the album was opened from the camera screen, fold the camera
     * preview as well. The screen guard implies camera is enabled, so the
     * unsubscribe call always reaches the real implementation here. */
    if (ui_nav_current() == UI_SCR_CAMERA) {
        (VOID_T)tuya_ai_toy_camera_unsubscribe(CAM_STREAM_YUV422, CAM_CONSUMER_UI_PREVIEW);
        ui_nav_back();
    }
    ui_chat_set_attachment_jpeg(pic.data, pic.len);
    wukong_picture_free_pic_info(&pic);
    return OPRT_OK;
}

STATIC CONST UI_MSG_DISPATCH_T s_msg_dispatch_table[] = {
    { TY_DISPLAY_TP_HUMAN_CHAT,       __handle_human_chat },
    { TY_DISPLAY_TP_AI_CHAT,          __handle_ai_chat },
    { TY_DISPLAY_TP_AI_CHAT_START,    __handle_ai_chat_start },
    { TY_DISPLAY_TP_AI_CHAT_DATA,     __handle_ai_chat_data },
    { TY_DISPLAY_TP_AI_CHAT_STOP,     __handle_ai_chat_stop },
    { TY_DISPLAY_TP_AI_IMAGE,         __handle_ai_image },
    { TY_DISPLAY_TP_CLEAR_ATTACHMENT, __handle_clear_attachment },
    { TY_DISPLAY_TP_MODE_NOTIFY,      __handle_mode_notify },
    { TY_DISPLAY_TP_CHAT_STAT,        __handle_chat_stat },
    { TY_DISPLAY_TP_STAT_NET,         __handle_stat_net },
    { TY_DISPLAY_TP_STAT_ONLINE,      __handle_stat_online },
    { TY_DISPLAY_TP_STAT_SLEEP,       __handle_stat_sleep },
    { TY_DISPLAY_TP_STAT_NETCFG,      __handle_stat_netcfg },
    { TY_DISPLAY_TP_LANGUAGE,         __handle_language },
};

STATIC CONST UI_ACTION_DISPATCH_T s_action_dispatch_table[] = {
    { TY_DISP_ACT_OPEN_ALBUM,         __action_open_album },
    { TY_DISP_ACT_CLOSE_ALBUM,        __action_close_album },
    { TY_DISP_ACT_ALBUM_VIEW_NEXT_PIC, __action_album_view_next },
    { TY_DISP_ACT_ALBUM_VIEW_PREV_PIC, __action_album_view_prev },
    { TY_DISP_ACT_ALBUM_DELETE_PIC,   __action_album_delete },
    { TY_DISP_ACT_OPEN_ALBUM_GRID,    __action_open_album_grid },
    { TY_DISP_ACT_CLOSE_ALBUM_GRID,   __action_close_album_grid },
    { TY_DISP_ACT_ALBUM_BATCH_DELETE, __action_album_batch_delete },
    { TY_DISP_ACT_OPEN_DEVICE_MODE,   __action_open_device_mode },
    { TY_DISP_ACT_CLOSE_DEVICE_MODE,  __action_close_device_mode },
    { TY_DISP_ACT_ALBUM_AI_RECOGNIZE, __action_album_ai_recognize },
    { TY_DISP_ACT_OPEN_CAMERA,        __action_open_camera },
    { TY_DISP_ACT_CLOSE_CAMERA,       __action_close_camera },
    { TY_DISP_ACT_TAKE_PHOTO,         __action_take_photo },
    { TY_DISP_ACT_OPEN_RECORD,        __action_open_record },
    { TY_DISP_ACT_CLOSE_RECORD,       __action_close_record },
    { TY_DISP_ACT_OPEN_RECORD_LIST,   __action_open_record_list },
    { TY_DISP_ACT_CLOSE_RECORD_LIST,  __action_close_record_list },
    { TY_DISP_ACT_RECORD_START,       __action_record_start },
    { TY_DISP_ACT_RECORD_PAUSE,       __action_record_pause },
    { TY_DISP_ACT_RECORD_RESUME,      __action_record_resume },
    { TY_DISP_ACT_RECORD_STOP,        __action_record_stop },
    { TY_DISP_ACT_RECORD_PLAY,        __action_record_play },
    { TY_DISP_ACT_RECORD_DELETE,      __action_record_delete },
    { TY_DISP_ACT_RECORD_UPLOAD,      __action_record_upload },
    { TY_DISP_ACT_RECORD_PLAY_PAUSE,  __action_record_play_pause },
    { TY_DISP_ACT_RECORD_PLAY_RESUME, __action_record_play_resume },
    { TY_DISP_ACT_RECORD_PLAY_STOP,   __action_record_play_stop },
    { TY_DISP_ACT_OPEN_MUSIC,         __action_open_music },
    { TY_DISP_ACT_CLOSE_MUSIC,        __action_close_music },
    { TY_DISP_ACT_OPEN_CALL,          __action_open_call },
    { TY_DISP_ACT_CLOSE_CALL,         __action_close_call },
    { TY_DISP_ACT_OPEN_DETECTION,     __action_open_detection },
    { TY_DISP_ACT_CLOSE_DETECTION,    __action_close_detection },
    { TY_DISP_ACT_OPEN_SETTINGS,      __action_open_settings },
    { TY_DISP_ACT_CLOSE_SETTINGS,     __action_close_settings },
};

VOID_T ui_dispatch_msg(TY_DISPLAY_MSG_T *msg)
{
    UINT32_T i;

    if (NULL == msg) {
        return;
    }

    for (i = 0; i < sizeof(s_msg_dispatch_table) / sizeof(s_msg_dispatch_table[0]); i++) {
        if (s_msg_dispatch_table[i].type == msg->type) {
            s_msg_dispatch_table[i].handler(msg);
            return;
        }
    }
}

OPERATE_RET ui_dispatch_action(TY_DISPLAY_ACTION_E action, UINT8_T *msg, INT_T len)
{
    UINT32_T i;

    lv_vendor_disp_lock();

    for (i = 0; i < sizeof(s_action_dispatch_table) / sizeof(s_action_dispatch_table[0]); i++) {
        if (s_action_dispatch_table[i].action == action) {
            OPERATE_RET rt = s_action_dispatch_table[i].handler(msg, len);
            lv_vendor_disp_unlock();
            return rt;
        }
    }

    lv_vendor_disp_unlock();
    return OPRT_OK;
}
