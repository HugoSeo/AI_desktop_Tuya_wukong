/**
 * @file ui_chat.c
 * @brief Chat screen UI for T5AI_BOARD (320x480)
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include "ui_common.h"
#include "uni_log.h"
#include "tal_image_jpeg_codec.h"
#include "tal_image_scale.h"
#include "tal_memory.h"
#include "tuya_list.h"
#include "wukong_ai_mode.h"
#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
#include "img_utility.h"            /* gif_img_load / gif_img_unload */
#include "tuya_app_gui_fs_path.h"   /* tuya_app_gui_get_picture_full_path */
#endif
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Font / icon declarations
 * --------------------------------------------------------------------------- */

/* External getters provided by tuya_ai_toy module. Mirrors the convention
 * used by ui_control_center.c so that ui modules don't pull in the full
 * tuya_ai_toy.h header which has wider dependencies. */
extern AI_DEVICE_MODE_E   tuya_ai_toy_device_mode_get(VOID);
extern AI_CHAT_SUB_MODE_E tuya_ai_toy_trigger_mode_get(VOID);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define CHAT_AI_BUBBLE_OPA   0
#define CHAT_USER_BUBBLE_BG  0xB8BDDE
#define CHAT_USER_BUBBLE_OPA 28
#define CHAT_BUBBLE_RADIUS   15
#define CHAT_BUBBLE_PAD      12
#define CHAT_ICON_SIZE       48
#define CHAT_TOP_H           50
#define CHAT_TOP_PAD_H       10
#define CHAT_MSG_PAD_H       10
#define CHAT_MSG_PAD_V       6
#define CHAT_LINK_COLOR      0x5B9BD5
#define CHAT_TITLE_TEXT_BUF  48

#define CHAT_ATTACH_BAR_H      60
#define CHAT_ATTACH_THUMB_SIZE 44
#define CHAT_ATTACH_BAR_COLOR  0x2F3036
#define CHAT_ATTACH_BORDER_CLR 0x3E3F44

/* Bubble / label width derived from screen size */
#define CHAT_AI_BUBBLE_RIGHT_PAD  40
#define CHAT_USER_BUBBLE_LEFT_PAD 60
#define CHAT_AI_BUBBLE_MAX_W      (LV_HOR_RES - CHAT_AI_BUBBLE_RIGHT_PAD)
#define CHAT_USER_BUBBLE_MAX_W    (LV_HOR_RES - CHAT_USER_BUBBLE_LEFT_PAD)
#define CHAT_AI_LABEL_MAX_W       (CHAT_AI_BUBBLE_MAX_W - CHAT_BUBBLE_PAD * 2)
#define CHAT_USER_LABEL_MAX_W     (CHAT_USER_BUBBLE_MAX_W - CHAT_BUBBLE_PAD * 2)

/* Streaming upper bound (per single AI reply) */
#define CHAT_STREAM_MAX_LEN  8192

/* Mode-switch / system pending notification text upper bound */
#define CHAT_PENDING_NOTIFY_MAX_LEN  64

/* JPEG dimension sanity bounds */
#define CHAT_IMG_MAX_W (LV_HOR_RES * 4)
#define CHAT_IMG_MAX_H (LV_VER_RES * 4)

/* Layout update throttle for streaming append (in bytes accumulated) */
#define CHAT_STREAM_LAYOUT_THRESHOLD 32

/* Sliding-window cap for visible chat messages (user + AI, mixed) */
#define CHAT_MSG_QUEUE_MAX 10

/* Large image buffer prefer PSRAM when external RAM enabled */
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
#define CHAT_IMG_MALLOC(s) tal_psram_malloc(s)
#define CHAT_IMG_FREE(p)   tal_psram_free(p)
#else
#define CHAT_IMG_MALLOC(s) tal_malloc(s)
#define CHAT_IMG_FREE(p)   tal_free(p)
#endif

#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
/* Stress-test layout (settings toggle, not persisted): the chat screen
 * shrinks msg_container and parks a 320x240 GIF container directly under
 * it (gap = 0), cycling through GIF_*_EMOJ via gif_img_load every
 * CHAT_STRESS_GIF_PERIOD_MS. While ON, the camera attachment bar is
 * suppressed (see ui_chat_set_attachment_jpeg). The timer follows the
 * flag only — it keeps running even when chat is not the active screen,
 * which is the whole point of "stress". */
#define CHAT_STRESS_GIF_W           320
#define CHAT_STRESS_GIF_H           240
#define CHAT_STRESS_GIF_PERIOD_MS   60*1000
#define CHAT_STRESS_MSG_CONT_H      (LV_VER_RES - CHAT_TOP_H - CHAT_STRESS_GIF_H)
#define CHAT_STRESS_GIF_CONT_Y      (CHAT_TOP_H + CHAT_STRESS_MSG_CONT_H)
#endif /* TUYA_DEBUG_STRESS_TESTING */

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    UI_CHAT_LINK_CB cb;
    VOID_T         *cb_arg;
    UINT32_T        arg_len;
} CHAT_LINK_CTX_T;

typedef struct {
    lv_obj_t *row;
    lv_obj_t *bubble;
    lv_obj_t *content;
} CHAT_BUBBLE_T;

/* Sliding-window node: tracks one displayed message row in FIFO order.
 * Uses tuya_list (intrusive doubly-linked list) for chaining. */
typedef struct {
    LIST_HEAD  list_node;
    lv_obj_t  *row;
} CHAT_MSG_NODE_T;

typedef struct {
    lv_obj_t *chat_scr;
    /* top title bar: mode label (left) + ai icon (center) + stat label (right) */
    lv_obj_t *title_bar;
    lv_obj_t *ai_icon;
    lv_obj_t *mode_label;
    lv_obj_t *stat_label;
    /* cache the latest chat state so the label can be re-rendered after the
     * screen is re-created or mode/state is queried lazily. */
    UINT8_T   last_chat_state;
    BOOL_T    last_chat_state_valid;
    lv_obj_t *msg_container;
    /* streaming */
    lv_obj_t *stream_label;
    lv_obj_t *stream_row;
    UINT32_T  stream_len;
    UINT32_T  stream_pending_layout;
    BOOL_T    stream_truncated;
    /* attachment thumbnail bar */
    lv_obj_t *attach_bar;
    lv_obj_t *attach_canvas;
    uint8_t  *attach_buf;
    /* fullscreen image overlay */
    lv_obj_t *image_overlay;
    lv_obj_t *image_canvas;
    uint8_t  *image_buf;
    /* sliding-window queue of message rows (sentinel head; .next = oldest,
     * .prev = newest). q_inited gates one-shot INIT_LIST_HEAD. */
    LIST_HEAD q_list;
    UINT32_T  q_count;
    BOOL_T    q_inited;
#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
    /* Stress-test (settings switch, not persisted). When stress_on flips,
     * msg_container shrinks and stress_gif_cont hosts a cycling GIF. The
     * timer drives the cycle and is gated on the flag, not on visibility. */
    BOOL_T       stress_on;
    lv_obj_t    *stress_gif_cont;
    lv_obj_t    *stress_gif;
    lv_img_dsc_t stress_gif_dsc;
    lv_timer_t  *stress_timer;
    UINT32_T     stress_gif_idx;
#endif /* TUYA_DEBUG_STRESS_TESTING */
} CHAT_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC CHAT_UI_T s_chat = {0};
STATIC lv_style_t s_style_ai_bubble;
STATIC lv_style_t s_style_user_bubble;
STATIC BOOL_T s_chat_styles_inited = FALSE;
STATIC CHAR_T s_chat_pending_notify[CHAT_PENDING_NOTIFY_MAX_LEN] = {0};

/* Sub-mode names used to compose CHAT mode display text */
STATIC CONST CHAR_T *s_chat_sub_mode_names[] = {
    "长按", "按键", "唤醒", "自由",
};

/* AI_CHAT_STATE_E -> short Chinese label for the right-side status indicator. */
STATIC CONST CHAR_T *s_chat_state_text[] = {
    [AI_CHAT_INIT]    = "初始化",
    [AI_CHAT_IDLE]    = "待命",
    [AI_CHAT_LISTEN]  = "聆听中",
    [AI_CHAT_UPLOAD]  = "上传中",
    [AI_CHAT_THINK]   = "思考中",
    [AI_CHAT_SPEAK]   = "说话中",
    [AI_CHAT_INVALID] = "",
};

#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
/* Stress-test cycle order: linear 0..19 then wrap. Names live in ui_theme.h
 * as string literals; we cast to (CHAR_T *) at call sites since
 * tuya_app_gui_get_picture_full_path / gif_img_load take non-const char*. */
STATIC CONST CHAR_T *s_chat_stress_gif_names[] = {
    GIF_DEFAULT_EMOJ,
    GIF_HAPPY_EMOJ,
    GIF_CONFUSED_EMOJ,
    GIF_SHY_EMOJ,
    GIF_CRY_EMOJ,
    GIF_ANGRY_EMOJ,
    GIF_SCARED_EMOJ,
    GIF_SURPRISED_EMOJ,
    GIF_DISAPPOINTED_EMOJ,
    GIF_ANNOYED_EMOJ,
    GIF_THINKING_EMOJ,
    GIF_LAUGH_EMOJ,
    GIF_FUNNY_EMOJ,
    GIF_LOVE_EMOJ,
    GIF_EMBARRASSED_EMOJ,
    GIF_BLINK_EMOJ,
    GIF_COOL_EMOJ,
    GIF_RELAXED_EMOJ,
    GIF_DELICIOUS_EMOJ,
    GIF_UNHAPPY_EMOJ,
};
#define CHAT_STRESS_GIF_NUM \
    (sizeof(s_chat_stress_gif_names) / sizeof(s_chat_stress_gif_names[0]))
#endif /* TUYA_DEBUG_STRESS_TESTING */

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __chat_ensure_created(VOID_T);
STATIC VOID_T __chat_ensure_active(VOID_T);
STATIC VOID_T __chat_gesture_cb(lv_event_t *e);
STATIC VOID_T __chat_attach_close_cb(lv_event_t *e);
STATIC VOID_T __chat_queue_init_once(VOID_T);
STATIC VOID_T __chat_msg_row_delete_cb(lv_event_t *e);
STATIC VOID_T __chat_queue_push(lv_obj_t *row);
STATIC CONST CHAR_T *__chat_get_mode_text(VOID_T);
STATIC CONST CHAR_T *__chat_get_stat_text(UINT8_T state);
STATIC VOID_T __chat_apply_mode_label(VOID_T);
STATIC VOID_T __chat_apply_stat_label(VOID_T);
STATIC VOID_T __chat_hide_image_overlay(VOID_T);
#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
STATIC VOID_T __chat_stress_apply_layout(BOOL_T on);
STATIC VOID_T __chat_stress_build_container(VOID_T);
STATIC VOID_T __chat_stress_destroy_container(VOID_T);
STATIC VOID_T __chat_stress_load_current(VOID_T);
STATIC VOID_T __chat_stress_start(VOID_T);
STATIC VOID_T __chat_stress_stop(VOID_T);
STATIC VOID_T __chat_stress_timer_cb(lv_timer_t *tm);
#endif /* TUYA_DEBUG_STRESS_TESTING */

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Build display text for the current device / chat sub-mode
 * @return pointer to a static buffer holding the text
 * @note For CHAT mode the text is "闲聊: <sub-mode>"; otherwise the device
 *       mode name is returned. Static storage is intentional: callers must
 *       not retain the pointer across consecutive invocations.
 */
STATIC CONST CHAR_T *__chat_get_mode_text(VOID_T)
{
    STATIC CHAR_T s_buf[CHAT_TITLE_TEXT_BUF];
    AI_DEVICE_MODE_E cur = tuya_ai_toy_device_mode_get();

    switch (cur) {
    case AI_DEVICE_MODE_CHAT: {
        AI_CHAT_SUB_MODE_E sub = tuya_ai_toy_trigger_mode_get();
        CONST CHAR_T *sub_name = (sub < AI_CHAT_SUB_MAX)
                                 ? s_chat_sub_mode_names[sub] : "未知";
        snprintf(s_buf, sizeof(s_buf), "闲聊: %s", sub_name);
        return s_buf;
    }
    case AI_DEVICE_MODE_TRANSLATE:
        return "翻译模式";
    case AI_DEVICE_MODE_P2P:
        return "P2P模式";
    case AI_DEVICE_MODE_RECORD:
        return "录音模式";
    case AI_DEVICE_MODE_PICTURE:
        return "生图模式";
    case AI_DEVICE_MODE_DETECTION:
        return "侦测模式";
    default:
        return "设备模式";
    }
}

/**
 * @brief Resolve display text for a given AI_CHAT_STATE_E value
 * @param[in] state chat state value (typically from TY_DISPLAY_TP_CHAT_STAT)
 * @return short Chinese label, or empty string when out of range
 */
STATIC CONST CHAR_T *__chat_get_stat_text(UINT8_T state)
{
    UINT32_T n = (UINT32_T)(sizeof(s_chat_state_text) / sizeof(s_chat_state_text[0]));
    if ((UINT32_T)state >= n) {
        return "";
    }
    CONST CHAR_T *text = s_chat_state_text[state];
    return (text != NULL) ? text : "";
}

/**
 * @brief Apply the current device mode text to mode_label (no-op if absent)
 * @return none
 */
STATIC VOID_T __chat_apply_mode_label(VOID_T)
{
    if (s_chat.mode_label == NULL) {
        return;
    }
    lv_label_set_text(s_chat.mode_label, __chat_get_mode_text());
}

/**
 * @brief Apply the cached chat state to stat_label (no-op if absent)
 * @return none
 */
STATIC VOID_T __chat_apply_stat_label(VOID_T)
{
    if (s_chat.stat_label == NULL) {
        return;
    }
    CONST CHAR_T *text = s_chat.last_chat_state_valid
                         ? __chat_get_stat_text(s_chat.last_chat_state)
                         : "";
    lv_label_set_text(s_chat.stat_label, text);
}

/**
 * @brief Lazy one-shot init of the sliding-window list head sentinel
 * @return none
 */
STATIC VOID_T __chat_queue_init_once(VOID_T)
{
    if (s_chat.q_inited) {
        return;
    }
    INIT_LIST_HEAD(&s_chat.q_list);
    s_chat.q_count  = 0;
    s_chat.q_inited = TRUE;
}

/**
 * @brief LV_EVENT_DELETE handler for tracked message rows; single source of
 *        truth for keeping the sliding-window queue and LVGL DOM in sync
 * @param[in] e LVGL event whose target is the row being destroyed
 * @return none
 * @note Fired for both internal capacity eviction (lv_obj_del from
 *       __chat_queue_push) and any external lv_obj_del / lv_obj_clean.
 *       MUST clear stream_* before LVGL finalizes row destruction so that
 *       in-flight ui_chat_stream_append calls exit early via NULL guard.
 */
STATIC VOID_T __chat_msg_row_delete_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    if (row == NULL || !s_chat.q_inited) {
        return;
    }

    /* Defense: drop streaming state before the row's children are freed.
     * MUST clear stream_* before LVGL finalizes row destruction. */
    if (row == s_chat.stream_row) {
        s_chat.stream_row             = NULL;
        s_chat.stream_label           = NULL;
        s_chat.stream_len             = 0;
        s_chat.stream_pending_layout  = 0;
        s_chat.stream_truncated       = FALSE;
    }

    /* Unlink and free the corresponding queue node (if any). Linear scan is
     * fine since q_count <= CHAT_MSG_QUEUE_MAX. Not finding a node is valid:
     * push may have skipped this row when tal_malloc failed. */
    LIST_HEAD *pos  = NULL;
    LIST_HEAD *next = NULL;
    tuya_list_for_each_safe(pos, next, &s_chat.q_list) {
        CHAT_MSG_NODE_T *node = tuya_list_entry(pos, CHAT_MSG_NODE_T, list_node);
        if (node->row == row) {
            tuya_list_del(&node->list_node);
            if (s_chat.q_count > 0) {
                s_chat.q_count--;
            }
            tal_free(node);
            return;
        }
    }
}

/**
 * @brief Append a message row to the sliding-window queue and enforce
 *        the CHAT_MSG_QUEUE_MAX cap by evicting the oldest entry
 * @param[in] row LVGL row object to track (NULL is a no-op)
 * @return none
 * @note Registers __chat_msg_row_delete_cb on the row so that ALL future
 *       deletions (capacity eviction, ui_chat_clear's lv_obj_clean, or
 *       any external lv_obj_del) self-heal the queue.
 */
STATIC VOID_T __chat_queue_push(lv_obj_t *row)
{
    if (row == NULL) {
        return;
    }

    __chat_queue_init_once();

    CHAT_MSG_NODE_T *node = tal_malloc(sizeof(CHAT_MSG_NODE_T));
    if (node == NULL) {
        PR_WARN("chat: queue node alloc failed; row not tracked");
        return;
    }
    INIT_LIST_HEAD(&node->list_node);
    node->row = row;

    tuya_list_add_tail(&node->list_node, &s_chat.q_list);
    s_chat.q_count++;

    lv_obj_add_event_cb(row, __chat_msg_row_delete_cb, LV_EVENT_DELETE, NULL);

    /* lv_obj_del fires LV_EVENT_DELETE synchronously, which decrements
     * q_count via the callback above. The loop is guaranteed to terminate
     * within (q_count - CHAT_MSG_QUEUE_MAX) iterations. */
    while (s_chat.q_count > CHAT_MSG_QUEUE_MAX) {
        if (tuya_list_empty(&s_chat.q_list)) {
            break;
        }
        CHAT_MSG_NODE_T *oldest = tuya_list_entry(s_chat.q_list.next,
                                                   CHAT_MSG_NODE_T, list_node);
        lv_obj_del(oldest->row);
    }
}

/**
 * @brief Initialize chat bubble styles (once)
 * @return none
 */
STATIC VOID_T __chat_styles_init(VOID_T)
{
    if (s_chat_styles_inited) {
        return;
    }

    lv_style_init(&s_style_ai_bubble);
    lv_style_set_bg_opa(&s_style_ai_bubble, CHAT_AI_BUBBLE_OPA);
    lv_style_set_text_color(&s_style_ai_bubble, lv_color_white());
    lv_style_set_radius(&s_style_ai_bubble, CHAT_BUBBLE_RADIUS);
    lv_style_set_pad_all(&s_style_ai_bubble, CHAT_BUBBLE_PAD);
    lv_style_set_shadow_width(&s_style_ai_bubble, 0);
    lv_style_set_border_width(&s_style_ai_bubble, 0);

    lv_style_init(&s_style_user_bubble);
    lv_style_set_bg_color(&s_style_user_bubble, lv_color_hex(CHAT_USER_BUBBLE_BG));
    lv_style_set_bg_opa(&s_style_user_bubble, CHAT_USER_BUBBLE_OPA);
    lv_style_set_text_color(&s_style_user_bubble, lv_color_white());
    lv_style_set_radius(&s_style_user_bubble, CHAT_BUBBLE_RADIUS);
    lv_style_set_pad_all(&s_style_user_bubble, CHAT_BUBBLE_PAD);
    lv_style_set_shadow_width(&s_style_user_bubble, 0);
    lv_style_set_border_width(&s_style_user_bubble, 0);

    s_chat_styles_inited = TRUE;
}

/**
 * @brief Create a message row container with bubble
 * @param[in] is_ai TRUE for AI message (left-aligned), FALSE for user (right-aligned)
 * @return CHAT_BUBBLE_T containing row / bubble / content; .row is NULL on failure
 */
STATIC CHAT_BUBBLE_T __chat_create_bubble(BOOL_T is_ai)
{
    CHAT_BUBBLE_T result = {0};

    lv_obj_t *msg_row = lv_obj_create(s_chat.msg_container);
    if (msg_row == NULL) {
        return result;
    }
    lv_obj_remove_style_all(msg_row);
    lv_obj_set_size(msg_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(msg_row, CHAT_MSG_PAD_V, 0);
    lv_obj_set_flex_flow(msg_row, LV_FLEX_FLOW_ROW);
    /* Single-column left-aligned layout: AI and USER both align to the start.
     * Visual differentiation is done by bubble background style (see
     * __chat_styles_init). USER bubble keeps a slightly narrower max width
     * for a subtle visual hierarchy. */
    lv_obj_set_flex_align(msg_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(msg_row, CHAT_MSG_PAD_H, 0);

    lv_coord_t bubble_max_w = is_ai ? CHAT_AI_BUBBLE_MAX_W : CHAT_USER_BUBBLE_MAX_W;
    lv_coord_t label_max_w  = is_ai ? CHAT_AI_LABEL_MAX_W  : CHAT_USER_LABEL_MAX_W;

    lv_obj_t *bubble = lv_obj_create(msg_row);
    lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, bubble_max_w, 0);
    lv_obj_add_style(bubble, is_ai ? &s_style_ai_bubble : &s_style_user_bubble, 0);
    lv_obj_set_scrollbar_mode(bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(bubble, LV_DIR_NONE);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *content = lv_obj_create(bubble);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, LV_SIZE_CONTENT);
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(content, label_max_w, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content, LV_DIR_NONE);

    result.row     = msg_row;
    result.bubble  = bubble;
    result.content = content;

    /* Track this row in the FIFO sliding window; may evict the oldest row */
    __chat_queue_push(msg_row);

    return result;
}

/**
 * @brief Create a wrapped text label inside an existing bubble content container
 * @param[in] content bubble content container returned by __chat_create_bubble
 * @param[in] is_ai TRUE if the label belongs to an AI bubble (affects max width)
 * @param[in] text label text (must NOT be NULL)
 * @return label object, or NULL on failure
 * @note Uses measure-then-resize: short text keeps a compact bubble (LV_SIZE_CONTENT),
 *       long text gets a fixed width + LV_LABEL_LONG_WRAP. This is required because
 *       LVGL v8 sets LV_TEXT_FLAG_FIT when label width is LV_SIZE_CONTENT, which
 *       prevents wrapping regardless of style_max_width.
 */
STATIC lv_obj_t *__chat_make_text_label(lv_obj_t *content, BOOL_T is_ai, CONST CHAR_T *text)
{
    lv_obj_t *label = lv_label_create(content);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_update_layout(label);

    lv_coord_t max_w = is_ai ? CHAT_AI_LABEL_MAX_W : CHAT_USER_LABEL_MAX_W;
    if (lv_obj_get_width(label) > max_w) {
        lv_obj_set_width(label, max_w);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    return label;
}

/**
 * @brief Free link context when the label object is deleted
 * @param[in] e LVGL event carrying CHAT_LINK_CTX_T pointer in user data
 * @return none
 */
STATIC VOID_T __chat_link_delete_cb(lv_event_t *e)
{
    CHAT_LINK_CTX_T *ctx = (CHAT_LINK_CTX_T *)lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    if (ctx->cb_arg) {
        tal_free(ctx->cb_arg);
        ctx->cb_arg = NULL;
    }
    tal_free(ctx);
}

/**
 * @brief Event handler for hyperlink click
 * @param[in] e LVGL event carrying CHAT_LINK_CTX_T pointer in user data
 * @return none
 * @note Copies cb / cb_arg to local stack before invoking, so the callback
 *       may safely trigger label deletion (which frees ctx).
 */
STATIC VOID_T __chat_link_click_cb(lv_event_t *e)
{
    CHAT_LINK_CTX_T *ctx = (CHAT_LINK_CTX_T *)lv_event_get_user_data(e);
    if (ctx == NULL || ctx->cb == NULL) {
        return;
    }
    UI_CHAT_LINK_CB cb  = ctx->cb;
    VOID_T         *arg = ctx->cb_arg;
    cb(arg);
}

/**
 * @brief Add a clickable hyperlink to the chat
 * @param[in] type message role type (AI or user)
 * @param[in] text display text for the link
 * @param[in] cb callback invoked when the link is clicked
 * @param[in] cb_arg argument data to copy (can be NULL if arg_len is 0)
 * @param[in] arg_len size in bytes of cb_arg data to copy
 * @return none
 */
VOID_T ui_chat_add_link(CHAT_MSG_ROLE_TP_E type, CONST CHAR_T *text, UI_CHAT_LINK_CB cb,
                        CONST VOID_T *cb_arg, UINT32_T arg_len)
{
    if (text == NULL || cb == NULL) {
        return;
    }

    __chat_ensure_active();

    BOOL_T is_ai = (type == CHAT_MSG_ROLE_AI);
    CHAT_BUBBLE_T b = __chat_create_bubble(is_ai);
    if (b.row == NULL) {
        return;
    }

    lv_obj_t *label = __chat_make_text_label(b.content, is_ai, text);
    if (label == NULL) {
        return;
    }
    lv_obj_set_style_text_color(label, lv_color_hex(CHAT_LINK_COLOR), 0);
    lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_UNDERLINE, 0);

    CHAT_LINK_CTX_T *ctx = tal_malloc(sizeof(CHAT_LINK_CTX_T));
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->cb      = cb;
    ctx->arg_len = arg_len;

    if (cb_arg && arg_len > 0) {
        ctx->cb_arg = tal_malloc(arg_len);
        if (ctx->cb_arg == NULL) {
            tal_free(ctx);
            return;
        }
        memcpy(ctx->cb_arg, cb_arg, arg_len);
    }

    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, __chat_link_click_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(label, __chat_link_delete_cb, LV_EVENT_DELETE, ctx);

    lv_obj_scroll_to_view(b.row, LV_ANIM_ON);
    lv_obj_update_layout(s_chat.msg_container);
}

STATIC VOID_T __chat_hide_image_overlay(VOID_T)
{
    if (s_chat.image_overlay) {
        lv_obj_add_flag(s_chat.image_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_chat.image_buf) {
        CHAT_IMG_FREE(s_chat.image_buf);
        s_chat.image_buf = NULL;
    }
}

STATIC VOID_T __chat_image_click_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    __chat_hide_image_overlay();
}

/**
 * @brief Display a JPEG image fullscreen over the chat screen, click to dismiss
 * @param[in] jpeg_data JPEG image data
 * @param[in] jpeg_len JPEG data length in bytes
 * @return none
 */
VOID_T ui_chat_disp_image(CONST UINT8_T *jpeg_data, UINT32_T jpeg_len)
{
    if (jpeg_data == NULL || jpeg_len == 0) {
        return;
    }

    __chat_ensure_active();
    if (s_chat.image_overlay == NULL || s_chat.image_canvas == NULL) {
        PR_ERR("chat: image overlay not initialized");
        return;
    }

    TAL_IMAGE_JPEG_INFO_T info = {0};
    if (tal_image_jpeg_get_info(jpeg_data, jpeg_len, &info) != OPRT_OK) {
        PR_ERR("chat: jpeg get info failed");
        return;
    }

    if (info.width == 0 || info.height == 0 ||
        info.width > CHAT_IMG_MAX_W || info.height > CHAT_IMG_MAX_H) {
        PR_ERR("chat: jpeg dimension invalid: %u x %u", info.width, info.height);
        return;
    }

    /* Compute size in 64-bit to detect overflow before downcasting.
     * After dimension sanity check above this should never trip, but kept
     * as defense in depth. */
    uint64_t rgb565_size_64 = (uint64_t)info.width * (uint64_t)info.height * 2u;
    if (rgb565_size_64 > 0xFFFFFFFFULL) {
        PR_ERR("chat: rgb565 size overflow: %u x %u", info.width, info.height);
        return;
    }
    uint32_t rgb565_size = (uint32_t)rgb565_size_64;

    uint8_t *rgb565_buf = CHAT_IMG_MALLOC(rgb565_size);
    if (rgb565_buf == NULL) {
        PR_ERR("chat: malloc rgb565 buf failed, size=%u", rgb565_size);
        return;
    }

    TAL_IMAGE_JPEG_OUTPUT_T out = {0};
    out.out_buf      = rgb565_buf;
    out.out_buf_size = rgb565_size;
    out.out_width    = info.width;
    out.out_height   = info.height;

    if (tal_image_jpeg_decode_rgb565(jpeg_data, jpeg_len, &out) != OPRT_OK) {
        PR_ERR("chat: jpeg decode rgb565 failed");
        CHAT_IMG_FREE(rgb565_buf);
        return;
    }

    /* Swap buffer references first, then free the old one to avoid a brief
     * window where the canvas still references freed memory. */
    uint8_t *old_buf = s_chat.image_buf;
    s_chat.image_buf = rgb565_buf;
    lv_canvas_set_buffer(s_chat.image_canvas, rgb565_buf,
                         info.width, info.height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(s_chat.image_canvas);
    lv_obj_clear_flag(s_chat.image_overlay, LV_OBJ_FLAG_HIDDEN);
    if (old_buf) {
        CHAT_IMG_FREE(old_buf);
    }
}

/**
 * @brief Close button callback for attachment bar
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __chat_attach_close_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_chat_clear_attachment();
}

/**
 * @brief Set a JPEG image as pending attachment thumbnail at chat bottom
 * @param[in] jpeg_data JPEG image data
 * @param[in] jpeg_len JPEG data length in bytes
 * @return none
 */
VOID_T ui_chat_set_attachment_jpeg(CONST UINT8_T *jpeg_data, UINT32_T jpeg_len)
{
    if (jpeg_data == NULL || jpeg_len == 0) {
        return;
    }

    __chat_ensure_active();

#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
    /* Stress-test owns the bottom 240 px; suppress attachment workflow
     * entirely until the user turns the switch off. */
    if (s_chat.stress_on) {
        PR_DEBUG("chat: stress on, attachment suppressed");
        return;
    }
#endif

    if (s_chat.attach_bar == NULL || s_chat.attach_canvas == NULL) {
        return;
    }

    TAL_IMAGE_JPEG_SCALE_IN_T in = {0};
    in.method     = TAL_IMAGE_SCALE_MTH_BILINEAR;
    in.mode       = TAL_IMAGE_SCALE_MODE_SIZE;
    in.data       = (uint8_t *)jpeg_data;
    in.size       = jpeg_len;
    in.out_width  = CHAT_ATTACH_THUMB_SIZE;
    in.out_height = CHAT_ATTACH_THUMB_SIZE;

    TAL_IMAGE_SCALE_OUT_T out = {0};
    if (tal_image_jpeg_scale_rgb565(&in, &out) != OPRT_OK) {
        PR_ERR("chat: attach thumbnail scale failed");
        return;
    }

    /* Swap then free, mirroring the safe pattern used for image overlay */
    uint8_t *old_buf = s_chat.attach_buf;
    s_chat.attach_buf = out.buf;
    lv_canvas_set_buffer(s_chat.attach_canvas, s_chat.attach_buf,
                         out.width, out.height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_invalidate(s_chat.attach_canvas);
    if (old_buf) {
        tal_image_scale_buf_free(&(TAL_IMAGE_SCALE_OUT_T){.buf = old_buf});
    }

    lv_obj_clear_flag(s_chat.attach_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_chat.msg_container, LV_VER_RES - CHAT_TOP_H - CHAT_ATTACH_BAR_H);
    lv_obj_update_layout(s_chat.msg_container);
}

/**
 * @brief Clear the pending attachment and restore chat layout
 * @return none
 */
VOID_T ui_chat_clear_attachment(VOID_T)
{
    if (s_chat.attach_bar) {
        lv_obj_add_flag(s_chat.attach_bar, LV_OBJ_FLAG_HIDDEN);
    }
    /* NOTE: do NOT call lv_canvas_set_buffer(attach_canvas, NULL, ...) here.
     * LVGL 8.3.9 asserts `buf != NULL` (lv_canvas.c:69), which froze the
     * gui_main_task when the stress-test toggle first hit this path with
     * no prior attachment ever set. The canvas stays inside the hidden
     * attach_bar so LVGL never renders from it; the next
     * ui_chat_set_attachment_jpeg() will rebind via lv_canvas_set_buffer
     * with a fresh buffer. Mirrors __chat_hide_image_overlay's pattern. */
    if (s_chat.attach_buf) {
        tal_image_scale_buf_free(&(TAL_IMAGE_SCALE_OUT_T){.buf = s_chat.attach_buf});
        s_chat.attach_buf = NULL;
    }
    if (s_chat.msg_container) {
#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
        /* Stress-test layout keeps msg_container at the shrunk height; the
         * normal layout restores full height. */
        INT_T h = s_chat.stress_on ? CHAT_STRESS_MSG_CONT_H
                                   : (LV_VER_RES - CHAT_TOP_H);
        lv_obj_set_height(s_chat.msg_container, h);
#else
        lv_obj_set_height(s_chat.msg_container, LV_VER_RES - CHAT_TOP_H);
#endif
    }
}

/**
 * @brief Start a new AI streaming text message (creates bubble, ready for append)
 * @return none
 */
VOID_T ui_chat_stream_begin(VOID_T)
{
    __chat_ensure_active();

    s_chat.stream_len             = 0;
    s_chat.stream_pending_layout  = 0;
    s_chat.stream_truncated       = FALSE;

    CHAT_BUBBLE_T b = __chat_create_bubble(TRUE);
    if (b.row == NULL) {
        return;
    }

    /* Streaming label cannot use __chat_make_text_label: its measure-then-resize
     * path needs a non-empty initial text. We fix the width upfront so subsequent
     * lv_label_ins_text() calls wrap correctly. Keeps the original stream-bubble
     * "always full AI width" look (see ui_chat optimize plan). */
    s_chat.stream_label = lv_label_create(b.content);
    if (s_chat.stream_label) {
        lv_obj_set_width(s_chat.stream_label, CHAT_AI_LABEL_MAX_W);
        lv_label_set_long_mode(s_chat.stream_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_chat.stream_label, "");
    }
    s_chat.stream_row = b.row;
}

/**
 * @brief Append text chunk to the current AI streaming message
 * @param[in] chunk text fragment to append (NUL-terminated)
 * @return none
 * @note Does NOT switch to chat screen if user has navigated away during the
 *       stream; only ensures chat objects exist. Layout updates are throttled
 *       to avoid O(n^2) cost when many small chunks arrive.
 */
VOID_T ui_chat_stream_append(CONST CHAR_T *chunk)
{
    if (chunk == NULL) {
        return;
    }
    /* Lazy-create chat objects, but do NOT force-switch to chat screen
     * (so user can leave the chat during a long streaming reply). */
    __chat_ensure_created();
    if (s_chat.stream_label == NULL) {
        return;
    }

    UINT32_T chunk_len = strnlen(chunk, CHAT_STREAM_MAX_LEN);
    if (chunk_len == 0) {
        return;
    }

    if (s_chat.stream_len >= CHAT_STREAM_MAX_LEN) {
        if (!s_chat.stream_truncated) {
            PR_WARN("chat: stream truncated at %u bytes", s_chat.stream_len);
            s_chat.stream_truncated = TRUE;
        }
        return;
    }

    UINT32_T room = CHAT_STREAM_MAX_LEN - s_chat.stream_len;
    if (chunk_len > room) {
        chunk_len = room;
        if (!s_chat.stream_truncated) {
            PR_WARN("chat: stream truncated at %u bytes", CHAT_STREAM_MAX_LEN);
            s_chat.stream_truncated = TRUE;
        }
    }

    /* Insert at end without re-uploading the whole text every chunk. */
    if (chunk_len == strlen(chunk)) {
        lv_label_ins_text(s_chat.stream_label, LV_LABEL_POS_LAST, chunk);
    } else {
        /* Truncated chunk: build a NUL-terminated copy on stack (small) */
        CHAR_T tmp[64];
        UINT32_T copy = chunk_len < sizeof(tmp) - 1 ? chunk_len : sizeof(tmp) - 1;
        memcpy(tmp, chunk, copy);
        tmp[copy] = '\0';
        lv_label_ins_text(s_chat.stream_label, LV_LABEL_POS_LAST, tmp);
        chunk_len = copy;
    }

    s_chat.stream_len            += chunk_len;
    s_chat.stream_pending_layout += chunk_len;

    /* Throttle layout / scroll updates: only every CHAT_STREAM_LAYOUT_THRESHOLD bytes */
    if (s_chat.stream_pending_layout >= CHAT_STREAM_LAYOUT_THRESHOLD) {
        s_chat.stream_pending_layout = 0;
        lv_obj_update_layout(s_chat.msg_container);
        if (s_chat.stream_row) {
            lv_obj_scroll_to_view(s_chat.stream_row, LV_ANIM_ON);
        }
    }
}

/**
 * @brief End the current AI streaming message, flush pending layout
 * @return none
 */
VOID_T ui_chat_stream_end(VOID_T)
{
    if (s_chat.stream_label && s_chat.stream_pending_layout > 0) {
        lv_obj_update_layout(s_chat.msg_container);
        if (s_chat.stream_row) {
            lv_obj_scroll_to_view(s_chat.stream_row, LV_ANIM_ON);
        }
    }
    s_chat.stream_label            = NULL;
    s_chat.stream_row              = NULL;
    s_chat.stream_len              = 0;
    s_chat.stream_pending_layout   = 0;
    s_chat.stream_truncated        = FALSE;
}

/**
 * @brief Add a text message to the chat
 * @param[in] role message role (AI or user)
 * @param[in] text message text string
 * @return none
 */
VOID_T ui_chat_add_text(CHAT_MSG_ROLE_TP_E role, CONST CHAR_T *text)
{
    if (text == NULL) {
        return;
    }

    __chat_ensure_active();

    BOOL_T is_ai = (role == CHAT_MSG_ROLE_AI);
    CHAT_BUBBLE_T b = __chat_create_bubble(is_ai);
    if (b.row == NULL) {
        return;
    }

    if (__chat_make_text_label(b.content, is_ai, text) == NULL) {
        return;
    }

    lv_obj_scroll_to_view(b.row, LV_ANIM_ON);
    lv_obj_update_layout(s_chat.msg_container);
}

/**
 * @brief Set a pending notification text to be flushed onto chat as an AI bubble
 * @param[in] msg notification text (NULL clears the pending buffer)
 * @return none
 * @note Only the latest notification is kept; safe to call from any thread that
 *       holds the LVGL display lock. Does NOT auto-navigate to chat; pair with
 *       ui_nav_replace(UI_SCR_CHAT) or wait until ui_chat_show() flushes it.
 */
VOID_T ui_chat_set_pending_notify(CONST CHAR_T *msg)
{
    if (msg == NULL) {
        s_chat_pending_notify[0] = '\0';
        return;
    }
    snprintf(s_chat_pending_notify, sizeof(s_chat_pending_notify), "%s", msg);
}

/**
 * @brief Flush pending notification text to chat as an AI bubble
 * @return none
 * @note No-op when pending buffer is empty or chat screen is not yet created.
 *       Called automatically at the end of ui_chat_show().
 */
VOID_T ui_chat_flush_pending_notify(VOID_T)
{
    if (s_chat_pending_notify[0] == '\0' || s_chat.chat_scr == NULL) {
        return;
    }
    ui_chat_add_text(CHAT_MSG_ROLE_AI, s_chat_pending_notify);
    s_chat_pending_notify[0] = '\0';
}

/**
 * @brief Clear all messages, pending attachment and any open image overlay
 * @return none
 */
VOID_T ui_chat_clear(VOID_T)
{
    if (s_chat.msg_container) {
        lv_obj_clean(s_chat.msg_container);
    }
    s_chat.stream_label           = NULL;
    s_chat.stream_row             = NULL;
    s_chat.stream_len             = 0;
    s_chat.stream_pending_layout  = 0;
    s_chat.stream_truncated       = FALSE;

    ui_chat_clear_attachment();
    __chat_hide_image_overlay();
}

/**
 * @brief Create chat screen objects (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_chat(VOID_T)
{
    if (s_chat.chat_scr) {
        return;
    }

    __chat_styles_init();

    s_chat.chat_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_chat.chat_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_chat.chat_scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_pad_all(s_chat.chat_scr, 0, 0);
    lv_obj_set_style_text_font(s_chat.chat_scr, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_chat.chat_scr, lv_color_white(), 0);
    lv_obj_set_scrollbar_mode(s_chat.chat_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_chat.chat_scr, LV_DIR_NONE);

    /* ---- Title bar: mode (left) | ai icon (center) | state (right) ---- */
    s_chat.title_bar = lv_obj_create(s_chat.chat_scr);
    lv_obj_remove_style_all(s_chat.title_bar);
    lv_obj_set_size(s_chat.title_bar, LV_HOR_RES, CHAT_TOP_H);
    lv_obj_set_pos(s_chat.title_bar, 0, 0);
    lv_obj_set_style_bg_opa(s_chat.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_chat.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_chat.ai_icon = lv_img_create(s_chat.title_bar);
    lv_img_set_src(s_chat.ai_icon, &icon_ai_icon);
    lv_obj_align(s_chat.ai_icon, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chat.ai_icon, LV_OBJ_FLAG_CLICKABLE);

    s_chat.mode_label = lv_label_create(s_chat.title_bar);
    lv_label_set_text(s_chat.mode_label, __chat_get_mode_text());
    lv_obj_set_style_text_color(s_chat.mode_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_chat.mode_label,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_align(s_chat.mode_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_chat.mode_label, LV_ALIGN_LEFT_MID, CHAT_TOP_PAD_H, 0);

    s_chat.stat_label = lv_label_create(s_chat.title_bar);
    lv_label_set_text(s_chat.stat_label,
                      s_chat.last_chat_state_valid
                      ? __chat_get_stat_text(s_chat.last_chat_state)
                      : "");
    lv_obj_set_style_text_color(s_chat.stat_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_chat.stat_label,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_align(s_chat.stat_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_chat.stat_label, LV_ALIGN_RIGHT_MID, -CHAT_TOP_PAD_H, 0);

    s_chat.msg_container = lv_obj_create(s_chat.chat_scr);
    lv_obj_set_size(s_chat.msg_container, LV_HOR_RES, LV_VER_RES - CHAT_TOP_H);
    lv_obj_set_pos(s_chat.msg_container, 0, CHAT_TOP_H);
    lv_obj_set_flex_flow(s_chat.msg_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(s_chat.msg_container, 0, 0);
    lv_obj_set_style_bg_opa(s_chat.msg_container, 0, 0);
    lv_obj_set_style_pad_ver(s_chat.msg_container, 8, 0);
    lv_obj_set_style_pad_hor(s_chat.msg_container, CHAT_MSG_PAD_H, 0);
    lv_obj_set_scroll_dir(s_chat.msg_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_chat.msg_container, LV_SCROLLBAR_MODE_OFF);

    s_chat.stream_label            = NULL;
    s_chat.stream_row              = NULL;
    s_chat.stream_len              = 0;
    s_chat.stream_pending_layout   = 0;
    s_chat.stream_truncated        = FALSE;

    /* ---- Bottom attachment bar (hidden by default) ---- */
    s_chat.attach_bar = lv_obj_create(s_chat.chat_scr);
    lv_obj_remove_style_all(s_chat.attach_bar);
    lv_obj_set_size(s_chat.attach_bar, LV_HOR_RES, CHAT_ATTACH_BAR_H);
    lv_obj_align(s_chat.attach_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_chat.attach_bar, lv_color_hex(CHAT_ATTACH_BAR_COLOR), 0);
    lv_obj_set_style_bg_opa(s_chat.attach_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_chat.attach_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(s_chat.attach_bar, 1, 0);
    lv_obj_set_style_border_color(s_chat.attach_bar, lv_color_hex(CHAT_ATTACH_BORDER_CLR), 0);
    lv_obj_set_scrollbar_mode(s_chat.attach_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_chat.attach_bar, LV_OBJ_FLAG_HIDDEN);

    /* Thumbnail container (rounded clip) */
    lv_obj_t *thumb_cont = lv_obj_create(s_chat.attach_bar);
    lv_obj_remove_style_all(thumb_cont);
    lv_obj_set_size(thumb_cont, CHAT_ATTACH_THUMB_SIZE, CHAT_ATTACH_THUMB_SIZE);
    lv_obj_align(thumb_cont, LV_ALIGN_LEFT_MID, CHAT_MSG_PAD_H, 0);
    lv_obj_set_style_radius(thumb_cont, 8, 0);
    lv_obj_set_style_clip_corner(thumb_cont, true, 0);
    lv_obj_set_style_bg_color(thumb_cont, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(thumb_cont, LV_OPA_COVER, 0);

    s_chat.attach_canvas = lv_canvas_create(thumb_cont);
    lv_obj_set_pos(s_chat.attach_canvas, 0, 0);
    lv_obj_set_size(s_chat.attach_canvas, CHAT_ATTACH_THUMB_SIZE, CHAT_ATTACH_THUMB_SIZE);
    lv_obj_set_style_border_width(s_chat.attach_canvas, 0, 0);

    /* "x" close badge on top-right of thumbnail */
    lv_obj_t *close_btn = lv_btn_create(s_chat.attach_bar);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 18, 18);
    lv_obj_set_pos(close_btn,
                   CHAT_MSG_PAD_H + CHAT_ATTACH_THUMB_SIZE - 12,
                   (CHAT_ATTACH_BAR_H - CHAT_ATTACH_THUMB_SIZE) / 2 - 6);
    lv_obj_set_style_radius(close_btn, 9, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(close_btn, 8);
    lv_obj_add_event_cb(close_btn, __chat_attach_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "\xC3\x97");  /* × */
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);

    s_chat.attach_buf = NULL;

    s_chat.image_overlay = lv_obj_create(s_chat.chat_scr);
    lv_obj_remove_style_all(s_chat.image_overlay);
    lv_obj_set_size(s_chat.image_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_chat.image_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_chat.image_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_chat.image_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_chat.image_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_chat.image_overlay, __chat_image_click_cb, LV_EVENT_CLICKED, NULL);

    s_chat.image_canvas = lv_canvas_create(s_chat.image_overlay);
    lv_obj_center(s_chat.image_canvas);
    s_chat.image_buf = NULL;

    ui_control_center_register_gesture(s_chat.chat_scr);
    lv_obj_add_event_cb(s_chat.chat_scr, __chat_gesture_cb, LV_EVENT_GESTURE, NULL);

#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
    /* If the stress switch was flipped ON before the chat screen existed,
     * apply the alternate layout now so the first show is correct. */
    if (s_chat.stress_on) {
        __chat_stress_apply_layout(TRUE);
    }
#endif

    lv_obj_update_layout(s_chat.chat_scr);
}

/**
 * @brief Chat screen gesture callback, swipe-right to return to home page
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __chat_gesture_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_RIGHT) {
        ui_nav_to(UI_SCR_HOME);
    }
}

/**
 * @brief Ensure the chat screen objects exist (lazy create), without switching
 * @return none
 */
STATIC VOID_T __chat_ensure_created(VOID_T)
{
    if (s_chat.chat_scr == NULL) {
        setup_scr_chat();
    }
}

/**
 * @brief Ensure the chat screen exists AND is the currently active screen
 * @return none
 * @note Triggers ui_nav_to(UI_SCR_CHAT) when not active. Use this for
 *       user-facing additions; for streaming append use __chat_ensure_created
 *       to avoid stealing focus from the user.
 */
STATIC VOID_T __chat_ensure_active(VOID_T)
{
    __chat_ensure_created();
    if (s_chat.chat_scr && lv_scr_act() != s_chat.chat_scr) {
        ui_nav_to(UI_SCR_CHAT);
    }
}

/**
 * @brief Refresh the device-mode label in the chat title bar
 * @return none
 * @note Safe to call before the chat screen is created (no-op until then).
 *       Typical caller: mode notify / device-mode switch events.
 */
VOID_T ui_chat_refresh_mode(VOID_T)
{
    __chat_apply_mode_label();
}

/**
 * @brief Set and refresh the interaction-state label in the chat title bar
 * @param[in] state AI_CHAT_STATE_E value (0=INIT, 1=IDLE, 2=LISTEN,
 *                  3=UPLOAD, 4=THINK, 5=SPEAK)
 * @return none
 * @note Caches the value so the label can be re-rendered after lazy chat
 *       creation. Safe to call before the chat screen is created.
 */
VOID_T ui_chat_set_chat_state(UINT8_T state)
{
    s_chat.last_chat_state       = state;
    s_chat.last_chat_state_valid = TRUE;
    __chat_apply_stat_label();
}

/**
 * @brief Show the chat screen (called by ui_nav, does NOT push to nav stack)
 * @return none
 */
VOID_T ui_chat_show(VOID_T)
{
    if (s_chat.chat_scr == NULL) {
        setup_scr_chat();
    }
    if (lv_scr_act() != s_chat.chat_scr) {
        lv_scr_load(s_chat.chat_scr);
    }
    __chat_hide_image_overlay();
    __chat_apply_mode_label();
    __chat_apply_stat_label();
    ui_chat_flush_pending_notify();
}

/**
 * @brief Hide chat screen and return to previous screen
 * @param[in] target_scr screen to switch to (NULL to stay)
 * @return none
 */
VOID_T ui_chat_hide(lv_obj_t *target_scr)
{
    if (target_scr && s_chat.chat_scr && lv_scr_act() == s_chat.chat_scr) {
        lv_scr_load(target_scr);
    }
}

/**
 * @brief Get the chat screen object
 * @return chat screen pointer, NULL if not created
 */
lv_obj_t *ui_chat_get_scr(VOID_T)
{
    return s_chat.chat_scr;
}

#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
/* ---------------------------------------------------------------------------
 * Stress-test (settings switch, not persisted to KV)
 * --------------------------------------------------------------------------- */

/**
 * @brief Build the 320x240 GIF container as a child of chat_scr at y=240
 * @return none
 * @note Idempotent. Pushes image_overlay back to the foreground so that
 *       fullscreen JPEG preview still wins when both are visible.
 */
STATIC VOID_T __chat_stress_build_container(VOID_T)
{
    if (s_chat.stress_gif_cont != NULL || s_chat.chat_scr == NULL) {
        return;
    }

    s_chat.stress_gif_cont = lv_obj_create(s_chat.chat_scr);
    lv_obj_remove_style_all(s_chat.stress_gif_cont);
    lv_obj_set_size(s_chat.stress_gif_cont,
                    CHAT_STRESS_GIF_W, CHAT_STRESS_GIF_H);
    lv_obj_set_pos(s_chat.stress_gif_cont, 0, CHAT_STRESS_GIF_CONT_Y);
    lv_obj_set_style_bg_color(s_chat.stress_gif_cont,
                              lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_bg_opa(s_chat.stress_gif_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_chat.stress_gif_cont, 0, 0);
    lv_obj_set_style_border_width(s_chat.stress_gif_cont, 0, 0);
    lv_obj_clear_flag(s_chat.stress_gif_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_chat.stress_gif_cont, LV_OBJ_FLAG_CLICKABLE);

    s_chat.stress_gif = lv_gif_create(s_chat.stress_gif_cont);
    lv_obj_center(s_chat.stress_gif);
    lv_obj_clear_flag(s_chat.stress_gif, LV_OBJ_FLAG_CLICKABLE);

    /* Keep the fullscreen image overlay on top so JPEG preview still
     * occludes the GIF container when triggered. */
    if (s_chat.image_overlay != NULL) {
        lv_obj_move_foreground(s_chat.image_overlay);
    }

    memset(&s_chat.stress_gif_dsc, 0, sizeof(s_chat.stress_gif_dsc));
    s_chat.stress_gif_idx = 0;
}

/**
 * @brief Tear down the GIF container (lv_gif inside is freed by cascade)
 * @return none
 */
STATIC VOID_T __chat_stress_destroy_container(VOID_T)
{
    if (s_chat.stress_gif_cont != NULL) {
        lv_obj_del(s_chat.stress_gif_cont);
        s_chat.stress_gif_cont = NULL;
        s_chat.stress_gif      = NULL;
    }
}

/**
 * @brief Unload the previous GIF and load the one at s_chat.stress_gif_idx
 * @return none
 * @note Mirrors the unload-then-load-then-set_src pattern from desk_home.c.
 *       On load failure: log and leave the previous frame on screen; the
 *       next tick will advance the index and try a different file so a
 *       single bad GIF does not freeze the cycle.
 */
STATIC VOID_T __chat_stress_load_current(VOID_T)
{
    if (s_chat.stress_gif == NULL) {
        return;
    }

    if (s_chat.stress_gif_dsc.data != NULL) {
        gif_img_unload(&s_chat.stress_gif_dsc);
        memset(&s_chat.stress_gif_dsc, 0, sizeof(s_chat.stress_gif_dsc));
    }

    CHAR_T *name = (CHAR_T *)s_chat_stress_gif_names[s_chat.stress_gif_idx];
    CHAR_T *path = tuya_app_gui_get_picture_full_path(name);
    if (path == NULL) {
        PR_WARN("chat: stress gif path null for %s", name);
        return;
    }

    if (gif_img_load(path, &s_chat.stress_gif_dsc) != OPRT_OK) {
        PR_WARN("chat: stress gif load failed: %s", name);
        memset(&s_chat.stress_gif_dsc, 0, sizeof(s_chat.stress_gif_dsc));
        return;
    }

    lv_gif_set_src(s_chat.stress_gif, &s_chat.stress_gif_dsc);
}

/**
 * @brief LVGL timer callback: advance to the next GIF and reload
 * @param[in] tm timer handle (unused)
 * @return none
 */
STATIC VOID_T __chat_stress_timer_cb(lv_timer_t *tm)
{
    LV_UNUSED(tm);
    s_chat.stress_gif_idx = (s_chat.stress_gif_idx + 1) % CHAT_STRESS_GIF_NUM;
    __chat_stress_load_current();
}

/**
 * @brief Load the first GIF immediately and arm the cycling timer
 * @return none
 */
STATIC VOID_T __chat_stress_start(VOID_T)
{
    s_chat.stress_gif_idx = 0;
    __chat_stress_load_current();
    if (s_chat.stress_timer == NULL) {
        s_chat.stress_timer = lv_timer_create(__chat_stress_timer_cb,
                                              CHAT_STRESS_GIF_PERIOD_MS,
                                              NULL);
    }
}

/**
 * @brief Stop the cycling timer and free the currently loaded GIF
 * @return none
 */
STATIC VOID_T __chat_stress_stop(VOID_T)
{
    if (s_chat.stress_timer != NULL) {
        lv_timer_del(s_chat.stress_timer);
        s_chat.stress_timer = NULL;
    }
    if (s_chat.stress_gif != NULL) {
        lv_gif_set_src(s_chat.stress_gif, NULL);
    }
    if (s_chat.stress_gif_dsc.data != NULL) {
        gif_img_unload(&s_chat.stress_gif_dsc);
        memset(&s_chat.stress_gif_dsc, 0, sizeof(s_chat.stress_gif_dsc));
    }
}

/**
 * @brief Apply stress layout to an already-built chat screen
 * @param[in] on TRUE = shrink msg_container, build GIF container, start timer
 *               FALSE = stop timer, destroy container, restore msg_container
 * @return none
 * @note Callers MUST ensure s_chat.chat_scr != NULL before invoking.
 *       Attach bar is force-hidden on ON; on OFF it stays hidden — the user
 *       must re-capture an attachment to bring it back (mutual-exclusion
 *       policy, see ui_chat_set_attachment_jpeg's stress guard).
 */
STATIC VOID_T __chat_stress_apply_layout(BOOL_T on)
{
    if (s_chat.msg_container == NULL) {
        return;
    }

    if (on) {
        if (s_chat.attach_bar != NULL) {
            lv_obj_add_flag(s_chat.attach_bar, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_height(s_chat.msg_container, CHAT_STRESS_MSG_CONT_H);
        __chat_stress_build_container();
        __chat_stress_start();
    } else {
        __chat_stress_stop();
        __chat_stress_destroy_container();
        lv_obj_set_height(s_chat.msg_container, LV_VER_RES - CHAT_TOP_H);
    }

    lv_obj_update_layout(s_chat.msg_container);
}

VOID_T ui_chat_set_stress_test(BOOL_T on)
{
    if (s_chat.stress_on == on) {
        return;
    }
    s_chat.stress_on = on;
    PR_INFO("chat: stress_test -> %s", on ? "ON" : "OFF");

    /* Mutual-exclusion with the camera attach bar: turning stress ON
     * drops any pending attachment so the GIF container owns the bottom
     * 240 px unambiguously. Only invoke the clear path when there is an
     * actual buffer to release; otherwise skip it entirely so we don't
     * touch attach_canvas without need (defense-in-depth even though the
     * underlying lv_canvas_set_buffer(NULL) assert has been removed). */
    if (on && s_chat.attach_buf != NULL) {
        ui_chat_clear_attachment();
    }

    /* Apply side effects only when the chat screen exists; otherwise
     * setup_scr_chat will pick up the flag at first creation. */
    if (s_chat.chat_scr != NULL) {
        __chat_stress_apply_layout(on);
    }
}

BOOL_T ui_chat_get_stress_test(VOID_T)
{
    return s_chat.stress_on;
}
#endif /* TUYA_DEBUG_STRESS_TESTING */
