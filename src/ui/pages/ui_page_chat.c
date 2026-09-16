#include "lvgl.h"
#include "ui_route.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_page_ids.h"
#include "ui_comp_btn.h"
#include "ui_comp_picture.h"
#include "ui_comp_link.h"
#include "ui_svc_picture.h"
#include "ui_svc_dev_ctrl.h"
#include "ui_feature.h"
#include "wukong_ai_mode.h"
#include "tuya_ai_toy.h"
#include "tuya_ai_display.h"
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(icon_ai_icon);
LV_IMG_DECLARE(icon_delete);

/* --- Configuration --- */

#define CHAT_MAX_MESSAGES   6
#define CHAT_MAX_TEXT_LEN   256
#define TITLE_BAR_H         50
#define TITLE_PAD_H         10
#define CHAT_ATTACH_BAR_H   52
#define CHAT_ATTACH_THUMB   40
#define MODE_SWITCH_BAR_H   36
#define BOTTOM_PANEL_PAD_H  4
#define BOTTOM_PANEL_PAD_B  6
#define BOTTOM_PANEL_RADIUS 10

/* Streaming layout throttle: only update scroll every N bytes */
#define STREAM_LAYOUT_THRESHOLD 32

/* --- Page state --- */

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_chat_list = NULL;
static lv_obj_t *s_title_bar = NULL;
static lv_obj_t *s_mode_label = NULL;
static lv_obj_t *s_stat_label = NULL;
static lv_obj_t *s_bottom_shell = NULL;
static lv_obj_t *s_bottom_panel = NULL;
static lv_obj_t *s_attach_bar = NULL;
static lv_obj_t *s_attach_thumb = NULL;
static lv_obj_t *s_attach_label = NULL;
static lv_obj_t *s_attach_divider = NULL;
static lv_obj_t *s_mode_switch_bar = NULL;
static lv_obj_t *s_img_overlay = NULL;   /* full-screen "view image" overlay */
static lv_obj_t *s_img_view = NULL;      /* ui_comp_picture inside the overlay */
static char s_mode_buf[48];

/* Bubble LVGL objects — sliding window */
static lv_obj_t *s_bubbles[CHAT_MAX_MESSAGES];
static uint8_t s_bubble_count;

/* Streaming state */
static lv_obj_t *s_stream_label = NULL;
static uint16_t s_stream_pending_layout;

typedef struct {
    uint8_t       mode;
    ui_i18n_key_t text_key;
} mode_switch_item_t;

static const mode_switch_item_t s_mode_switch_items[] = {
    { AI_DEVICE_MODE_CHAT,      UI_TEXT_MODE_CHAT_SHORT      },
    { AI_DEVICE_MODE_TRANSLATE, UI_TEXT_MODE_TRANSLATE_SHORT },
    { AI_DEVICE_MODE_PICTURE,   UI_TEXT_MODE_PICTURE_SHORT   },
    { AI_DEVICE_MODE_DETECTION, UI_TEXT_MODE_DETECTION_SHORT },
};
#define MODE_SWITCH_COUNT (sizeof(s_mode_switch_items) / sizeof(s_mode_switch_items[0]))

static lv_obj_t *s_mode_switch_btns[MODE_SWITCH_COUNT];
static lv_obj_t *s_mode_switch_labels[MODE_SWITCH_COUNT];

/* --- Mode / status text --- */

static const ui_i18n_key_t s_sub_mode_keys[] = {
    UI_TEXT_MODE_HOLD,
    UI_TEXT_MODE_ONESHOT,
    UI_TEXT_MODE_WAKEUP,
    UI_TEXT_MODE_FREE,
};
#define SUB_MODE_COUNT (sizeof(s_sub_mode_keys) / sizeof(s_sub_mode_keys[0]))

static const char *get_mode_text(uint8_t device_mode, uint8_t chat_mode)
{
    switch (device_mode) {
    case AI_DEVICE_MODE_CHAT: {
        const char *sub = (chat_mode < SUB_MODE_COUNT)
                          ? ui_i18n_text(s_sub_mode_keys[chat_mode]) : "?";
        snprintf(s_mode_buf, sizeof(s_mode_buf),
                 ui_i18n_text(UI_TEXT_MODE_CHAT_FMT), sub);
        return s_mode_buf;
    }
    case AI_DEVICE_MODE_TRANSLATE: return ui_i18n_text(UI_TEXT_MODE_TRANSLATE);
    case AI_DEVICE_MODE_P2P:       return ui_i18n_text(UI_TEXT_MODE_P2P);
    case AI_DEVICE_MODE_RECORD:    return ui_i18n_text(UI_TEXT_MODE_RECORD);
    case AI_DEVICE_MODE_PICTURE:   return ui_i18n_text(UI_TEXT_MODE_PICTURE);
    case AI_DEVICE_MODE_DETECTION: return ui_i18n_text(UI_TEXT_MODE_DETECTION);
    default: return ui_i18n_text(UI_TEXT_MODE_DEVICE);
    }
}

static const char *get_state_text(uint8_t state)
{
    switch (state) {
    case AI_CHAT_IDLE:    return ui_i18n_text(UI_TEXT_CHAT_IDLE);
    case AI_CHAT_LISTEN:  return ui_i18n_text(UI_TEXT_CHAT_LISTENING);
    case AI_CHAT_UPLOAD:  return ui_i18n_text(UI_TEXT_CHAT_UPLOADING);
    case AI_CHAT_THINK:   return ui_i18n_text(UI_TEXT_CHAT_THINKING);
    case AI_CHAT_SPEAK:   return ui_i18n_text(UI_TEXT_CHAT_SPEAKING);
    default: return "";
    }
}

static void refresh_mode_label(void)
{
    if (!s_mode_label) return;
    lv_label_set_text(s_mode_label, get_mode_text(
        (uint8_t)tuya_ai_toy_device_mode_get(),
        (uint8_t)tuya_ai_toy_trigger_mode_get()));
}

static void refresh_stat_label(void)
{
    if (!s_stat_label) return;
    lv_label_set_text(s_stat_label, get_state_text(ui_state_get_chat()->chat_status));
}

static void refresh_mode_switch(void)
{
    uint8_t current_mode = ui_svc_dev_ctrl_mode_get();

    for (uint8_t i = 0; i < MODE_SWITCH_COUNT; i++) {
        lv_obj_t *btn = s_mode_switch_btns[i];
        lv_obj_t *label = s_mode_switch_labels[i];
        if (!btn || !label) {
            continue;
        }

        bool selected = s_mode_switch_items[i].mode == current_mode;
        lv_obj_set_style_bg_color(btn,
                                  selected ? UI_COLOR_PRIMARY : UI_COLOR_BG_CARD, 0);
        lv_obj_set_style_text_color(label,
                                    selected ? UI_COLOR_TEXT : UI_COLOR_TEXT_SEC, 0);
        lv_label_set_text(label, ui_i18n_text(s_mode_switch_items[i].text_key));
    }
}

static void mode_switch_click_cb(lv_event_t *e)
{
    uint8_t mode = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (mode == ui_svc_dev_ctrl_mode_get()) {
        return;
    }

    ui_svc_dev_ctrl_mode_set(mode);
    /* The old mode page applied the business switch from on_leave().  The
     * selector now stays on CHAT, so schedule that same apply explicitly. */
    ui_svc_dev_ctrl_mode_save();
}

static void create_mode_switch_bar(void)
{
    s_mode_switch_bar = lv_obj_create(s_bottom_panel);
    lv_obj_remove_style_all(s_mode_switch_bar);
    lv_obj_set_size(s_mode_switch_bar, LV_PCT(100), ui_adapt(MODE_SWITCH_BAR_H));
    lv_obj_set_style_bg_color(s_mode_switch_bar, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_mode_switch_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_mode_switch_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_mode_switch_bar, LV_FLEX_FLOW_ROW);

    for (uint8_t i = 0; i < MODE_SWITCH_COUNT; i++) {
        lv_obj_t *btn = lv_obj_create(s_mode_switch_bar);
        lv_obj_remove_style_all(btn);
        lv_obj_set_width(btn, 0);
        lv_obj_set_height(btn, LV_PCT(100));
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, ui_adapt(UI_RADIUS_MD), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_80, LV_STATE_PRESSED);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, mode_switch_click_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s_mode_switch_items[i].mode);
        s_mode_switch_btns[i] = btn;

        lv_obj_t *label = lv_label_create(btn);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        s_mode_switch_labels[i] = label;
    }

    refresh_mode_switch();
}

/* --- Attachment preview (AI-recognize hand-off from the picture page, ADR-0003) --- */

static void clear_attachment(void)
{
    if (s_attach_thumb) {
        ui_comp_picture_clear(s_attach_thumb);   /* frees the owned RGB565 buffer */
    }
    if (s_attach_bar) {
        lv_obj_add_flag(s_attach_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_attach_divider) {
        lv_obj_add_flag(s_attach_divider, LV_OBJ_FLAG_HIDDEN);
    }
}

static void attach_close_cb(lv_event_t *e)
{
    (void)e;
    /* UI-only dismiss, mirroring the old view: the queued turn input is owned by
     * the agent pipeline and consumed (then CLEAR_ATTACHMENT fires) on send. */
    clear_attachment();
}

/* Pull a pending attachment stashed by the picture page and show it; a no-op when
 * nothing is pending, so it is safe to call on every on_enter. */
static void try_show_attachment(void)
{
    if (!s_attach_bar || !s_attach_thumb) {
        return;
    }
    ui_picture_t att;
    if (!ui_svc_picture_take_pending_attachment(&att)) {
        return;
    }
    if (att.data) {
        /* Ownership transfers in; the widget releases it via the service free fn. */
        ui_comp_picture_set_rgb565(s_attach_thumb, att.width, att.height,
                                 att.data, ui_svc_picture_free_rgb565);
        lv_obj_clear_flag(s_attach_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_attach_divider, LV_OBJ_FLAG_HIDDEN);
    }
}

/* --- "View image" hyperlink for AI-generated pictures (生图模式) ------------
 * The detection / picture-gen pipeline delivers a saved picture filename via
 * TY_DISPLAY_TP_AI_IMAGE. We surface it as a "查看图片" link bubble; tapping it
 * asks ui_svc_picture to decode the named picture off-thread, then shows it in a
 * full-screen overlay (tap anywhere to dismiss). Mirrors the old view module's
 * ui_chat_add_link + ui_chat_disp_image. */

static lv_obj_t *create_bubble(lv_obj_t *parent, bool is_user);
static lv_obj_t *get_bubble_label(lv_obj_t *bubble);
static void      evict_oldest_bubble(void);
static void      scroll_to_bottom(void);

static void img_overlay_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_img_overlay) {
        lv_obj_add_flag(s_img_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_img_view) {
        ui_comp_picture_clear(s_img_view);   /* releases the decoded RGB565 buffer */
    }
}

/* Decode result for a "查看图片" tap (UI thread, registered as the view cb). */
static void on_picture_view(const ui_picture_t *picture)
{
    if (!s_img_overlay || !s_img_view || picture == NULL) {
        if (picture && picture->data) {
            ui_svc_picture_free_rgb565(picture->data);
        }
        return;
    }
    if (picture->data) {
        /* Ownership transfers in; freed on clear/replace/delete via the free fn. */
        ui_comp_picture_set_rgb565(s_img_view, picture->width, picture->height,
                                 picture->data, ui_svc_picture_free_rgb565);
        lv_obj_clear_flag(s_img_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void view_image_link_cb(void *arg)
{
    const char *pic_name = (const char *)arg;
    if (pic_name && pic_name[0]) {
        ui_svc_picture_view_request(pic_name);   /* async → on_picture_view */
    }
}

static void chat_append_image_link(const char *pic_name)
{
    /* 图片解码/预览属 CAMERA 簇（ui_svc_picture）；关掉时不挂死 link。 */
    if (!ui_feature_available(UI_FEATURE_ID_CAMERA)) {
        return;
    }

    if (!s_chat_list || !pic_name || !pic_name[0]) {
        return;
    }

    while (s_bubble_count >= CHAT_MAX_MESSAGES) {
        evict_oldest_bubble();
    }

    lv_obj_t *bubble = create_bubble(s_chat_list, false);
    lv_obj_t *label = get_bubble_label(bubble);
    if (label) {
        /* Replace the empty default text label with the hyperlink widget. */
        lv_obj_t *box = lv_obj_get_parent(label);
        lv_obj_del(label);
        ui_comp_link_create(box, ui_i18n_text(UI_TEXT_PICTURE_VIEW), view_image_link_cb,
                            pic_name, (uint32_t)strlen(pic_name) + 1);
    }

    s_bubbles[s_bubble_count++] = bubble;
    scroll_to_bottom();
}

/* --- Bubble creation --- */

static lv_obj_t *create_bubble(lv_obj_t *parent, bool is_user)
{
    lv_obj_t *bubble = lv_obj_create(parent);
    lv_obj_remove_style_all(bubble);
    lv_obj_set_width(bubble, LV_PCT(100));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(bubble, ui_adapt(2), 0);

    lv_obj_t *box = lv_obj_create(bubble);
    lv_obj_remove_style_all(box);
    lv_obj_set_width(box, LV_PCT(75));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(box, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_pad_all(box, ui_adapt(UI_SPACE_SM), 0);

    if (is_user) {
        lv_obj_set_align(box, LV_ALIGN_RIGHT_MID);
        lv_obj_set_style_bg_color(box, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_align(box, LV_ALIGN_LEFT_MID);
        lv_obj_set_style_bg_color(box, UI_COLOR_BG_CARD, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    }

    lv_obj_t *label = lv_label_create(box);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label,
        is_user ? lv_color_white() : UI_COLOR_TEXT, 0);
    lv_label_set_text(label, "");

    return bubble;
}

static lv_obj_t *get_bubble_label(lv_obj_t *bubble)
{
    lv_obj_t *box = lv_obj_get_child(bubble, 0);
    if (!box) return NULL;
    return lv_obj_get_child(box, 0);
}

/* --- Sliding window: evict oldest bubble from LVGL --- */

static void evict_oldest_bubble(void)
{
    if (s_bubble_count == 0) return;
    /* If the bubble being evicted contains the active streaming label, drop the
     * dangling reference first: lv_obj_del frees the whole bubble subtree
     * (label included), and a later AI_CHAT_DATA would otherwise write into
     * freed memory via s_stream_label. */
    if (s_stream_label) {
        for (lv_obj_t *p = s_stream_label; p; p = lv_obj_get_parent(p)) {
            if (p == s_bubbles[0]) {
                s_stream_label = NULL;
                s_stream_pending_layout = 0;
                break;
            }
        }
    }
    lv_obj_del(s_bubbles[0]);
    s_bubble_count--;
    memmove(&s_bubbles[0], &s_bubbles[1], s_bubble_count * sizeof(lv_obj_t *));
    s_bubbles[s_bubble_count] = NULL;
}

static void scroll_to_bottom(void)
{
    lv_obj_scroll_to_y(s_chat_list, LV_COORD_MAX, LV_ANIM_ON);
}

/* --- Chat operations --- */

static void chat_append_ai_stop(void);

static void chat_append_user(const char *text, uint16_t len)
{
    if (!s_chat_list) return;

    /* A user utterance starts a new turn. If an AI reply is still streaming
     * (barge-in during playback), finalize it here so later NLG data opens a
     * new bubble below this ASR bubble instead of appending to the old one. */
    chat_append_ai_stop();

    while (s_bubble_count >= CHAT_MAX_MESSAGES) {
        evict_oldest_bubble();
    }

    lv_obj_t *bubble = create_bubble(s_chat_list, true);
    lv_obj_t *label = get_bubble_label(bubble);
    if (label) {
        char buf[CHAT_MAX_TEXT_LEN];
        uint16_t copy_len = len < sizeof(buf) - 1 ? len : (uint16_t)(sizeof(buf) - 1);
        memcpy(buf, text, copy_len);
        buf[copy_len] = '\0';
        lv_label_set_text(label, buf);
    }

    s_bubbles[s_bubble_count++] = bubble;
    scroll_to_bottom();
}

static void chat_append_ai_data(const char *text, uint16_t len);

static void chat_append_ai_start(const char *text, uint16_t len)
{
    if (!s_chat_list) return;

    while (s_bubble_count >= CHAT_MAX_MESSAGES) {
        evict_oldest_bubble();
    }

    lv_obj_t *bubble = create_bubble(s_chat_list, false);
    lv_obj_t *label = get_bubble_label(bubble);
    if (label) {
        lv_label_set_text(label, "");
    }

    s_bubbles[s_bubble_count++] = bubble;
    s_stream_label = label;
    s_stream_pending_layout = 0;
    scroll_to_bottom();

    if (text && len > 0) {
        chat_append_ai_data(text, len);
    }
}

static void chat_append_ai_data(const char *text, uint16_t len)
{
    if (!text || len == 0) return;
    if (!s_stream_label) {
        chat_append_ai_start(NULL, 0);
        if (!s_stream_label) return;
    }

    if (text[len] == '\0') {
        lv_label_ins_text(s_stream_label, LV_LABEL_POS_LAST, text);
    } else {
        char buf[256];
        uint16_t copy_len = len < sizeof(buf) - 1 ? len : (uint16_t)(sizeof(buf) - 1);
        while (copy_len > 0 && (text[copy_len] & 0xC0) == 0x80) {
            copy_len--;
        }
        if (copy_len > 0 && (text[copy_len - 1] & 0x80) != 0) {
            uint16_t start = copy_len - 1;
            while (start > 0 && (text[start] & 0xC0) == 0x80) start--;
            int expected = 1;
            uint8_t lead = (uint8_t)text[start];
            if ((lead & 0xE0) == 0xC0) expected = 2;
            else if ((lead & 0xF0) == 0xE0) expected = 3;
            else if ((lead & 0xF8) == 0xF0) expected = 4;
            if (copy_len - start < expected) copy_len = start;
        }
        memcpy(buf, text, copy_len);
        buf[copy_len] = '\0';
        lv_label_ins_text(s_stream_label, LV_LABEL_POS_LAST, buf);
    }

    s_stream_pending_layout += len;

    if (s_stream_pending_layout >= STREAM_LAYOUT_THRESHOLD) {
        s_stream_pending_layout = 0;
        lv_obj_update_layout(s_chat_list);
        scroll_to_bottom();
    }
}

static void chat_append_ai_stop(void)
{
    if (!s_stream_label) return;

    /* Final layout flush */
    if (s_stream_pending_layout > 0) {
        lv_obj_update_layout(s_chat_list);
        scroll_to_bottom();
    }

    s_stream_label = NULL;
    s_stream_pending_layout = 0;
}

/* --- Page lifecycle --- */

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT) {
        ui_route_reset(UI_PAGE_HOME);
    }
}

static void on_create(void *parent)
{
    (void)parent;
    memset(s_bubbles, 0, sizeof(s_bubbles));
    s_bubble_count = 0;
    s_stream_label = NULL;
    s_stream_pending_layout = 0;

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_screen, UI_COLOR_TEXT, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(32), 0);

    /* Title bar */
    s_title_bar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_title_bar);
    lv_obj_set_size(s_title_bar, LV_PCT(100), ui_adapt(TITLE_BAR_H));
    lv_obj_set_style_bg_opa(s_title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ai_icon = lv_img_create(s_title_bar);
    lv_img_set_src(ai_icon, &icon_ai_icon);
    lv_obj_align(ai_icon, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(ai_icon, LV_OBJ_FLAG_CLICKABLE);

    s_mode_label = lv_label_create(s_title_bar);
    lv_obj_set_style_text_color(s_mode_label, UI_COLOR_TEXT, 0);
    lv_obj_align(s_mode_label, LV_ALIGN_LEFT_MID, ui_adapt(TITLE_PAD_H), 0);

    s_stat_label = lv_label_create(s_title_bar);
    lv_obj_set_style_text_color(s_stat_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(s_stat_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_stat_label, LV_ALIGN_RIGHT_MID, -ui_adapt(TITLE_PAD_H), 0);

    /* Chat list area */
    s_chat_list = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_chat_list);
    lv_obj_set_width(s_chat_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_chat_list, 1);
    lv_obj_set_style_bg_opa(s_chat_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(s_chat_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_chat_list, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_hor(s_chat_list, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_ver(s_chat_list, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_scroll_dir(s_chat_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_chat_list, LV_SCROLLBAR_MODE_OFF);

    /* Bottom shell supplies screen-edge breathing room.  The inner panel owns
     * both attachment and mode rows so they read as one rounded control. */
    s_bottom_shell = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_bottom_shell);
    lv_obj_set_width(s_bottom_shell, LV_PCT(100));
    lv_obj_set_height(s_bottom_shell, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(s_bottom_shell, ui_adapt(BOTTOM_PANEL_PAD_H), 0);
    lv_obj_set_style_pad_bottom(s_bottom_shell, ui_adapt(BOTTOM_PANEL_PAD_B), 0);
    lv_obj_clear_flag(s_bottom_shell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_bottom_shell, LV_FLEX_FLOW_COLUMN);

    s_bottom_panel = lv_obj_create(s_bottom_shell);
    lv_obj_remove_style_all(s_bottom_panel);
    lv_obj_set_width(s_bottom_panel, LV_PCT(100));
    lv_obj_set_height(s_bottom_panel, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_bottom_panel, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_bottom_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_bottom_panel, ui_adapt(BOTTOM_PANEL_RADIUS), 0);
    lv_obj_set_style_clip_corner(s_bottom_panel, true, 0);
    lv_obj_set_flex_flow(s_bottom_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_bottom_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Attachment row (hidden from flex layout until a picture is present). */
    s_attach_bar = lv_obj_create(s_bottom_panel);
    lv_obj_remove_style_all(s_attach_bar);
    lv_obj_set_size(s_attach_bar, LV_PCT(100), ui_adapt(CHAT_ATTACH_BAR_H));
    lv_obj_set_style_bg_color(s_attach_bar, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_attach_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_attach_bar, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_clear_flag(s_attach_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_attach_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_attach_bar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *thumb_box = lv_obj_create(s_attach_bar);
    lv_obj_remove_style_all(thumb_box);
    lv_obj_set_size(thumb_box, ui_adapt(CHAT_ATTACH_THUMB),
                    ui_adapt(CHAT_ATTACH_THUMB));
    lv_obj_set_style_radius(thumb_box, ui_adapt(6), 0);
    lv_obj_set_style_clip_corner(thumb_box, true, 0);
    lv_obj_clear_flag(thumb_box, LV_OBJ_FLAG_SCROLLABLE);

    s_attach_thumb = ui_comp_picture_create(thumb_box);
    lv_obj_clear_flag(s_attach_thumb, LV_OBJ_FLAG_CLICKABLE);

    s_attach_label = lv_label_create(s_attach_bar);
    lv_obj_set_width(s_attach_label, 0);
    lv_obj_set_flex_grow(s_attach_label, 1);
    lv_obj_set_style_text_align(s_attach_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_attach_label, UI_COLOR_TEXT_SEC, 0);
    lv_label_set_text(s_attach_label, ui_i18n_text(UI_TEXT_CHAT_ATTACHMENT));

    lv_obj_t *attach_close = ui_comp_btn_icon_create(s_attach_bar, &icon_delete, attach_close_cb);
    lv_obj_set_size(attach_close, ui_adapt(CHAT_ATTACH_THUMB),
                    ui_adapt(CHAT_ATTACH_THUMB));
    lv_obj_center(lv_obj_get_child(attach_close, 0));

    lv_obj_add_flag(s_attach_bar, LV_OBJ_FLAG_HIDDEN);

    s_attach_divider = lv_obj_create(s_bottom_panel);
    lv_obj_remove_style_all(s_attach_divider);
    lv_obj_set_size(s_attach_divider, LV_PCT(100), ui_adapt(1));
    lv_obj_set_style_bg_color(s_attach_divider, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_bg_opa(s_attach_divider, LV_OPA_20, 0);
    lv_obj_clear_flag(s_attach_divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_attach_divider, LV_OBJ_FLAG_HIDDEN);

    create_mode_switch_bar();

    /* Full-screen "view image" overlay (生图模式): floats above the chat —
     * IGNORE_LAYOUT keeps it out of the column flex flow. Hidden until a
     * "查看图片" link is tapped; tapping the overlay dismisses it. */
    s_img_overlay = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_img_overlay);
    lv_obj_set_size(s_img_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_img_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(s_img_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_img_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_img_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_img_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_img_overlay, img_overlay_click_cb, LV_EVENT_CLICKED, NULL);

    s_img_view = ui_comp_picture_create(s_img_overlay);   /* centred, native size */
    lv_obj_clear_flag(s_img_view, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(s_img_overlay, LV_OBJ_FLAG_HIDDEN);

    ui_svc_picture_set_view_cb(on_picture_view);

    /* Gesture: right-swipe to return home */
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);

}

static void on_enter(uint32_t dirty)
{
    /* Mode/trigger + chat-status live in CHAT; both labels are also i18n, so a
     * language change (SETTINGS) must re-resolve them too. Entry sets all bits,
     * so this also covers the first paint. Gating avoids re-setting the labels
     * (an LVGL redraw, no built-in dedup) on every per-second SYSTEM tick. */
    if (dirty & ((1u << UI_STATE_GROUP_CHAT) | (1u << UI_STATE_GROUP_SETTINGS))) {
        refresh_mode_label();
        refresh_stat_label();
        refresh_mode_switch();
        if (s_attach_label) {
            lv_label_set_text(s_attach_label, ui_i18n_text(UI_TEXT_CHAT_ATTACHMENT));
        }
    }
    /* Entry only: pick up an AI-recognize attachment hand-off if one is pending. */
    if (ui_dirty_is_enter(dirty)) {
        try_show_attachment();
    }
}

static void on_leave(void)
{
}

static void on_destroy(void)
{
    ui_svc_picture_set_view_cb(NULL);   /* late decode results become no-ops */

    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    memset(s_bubbles, 0, sizeof(s_bubbles));
    s_bubble_count = 0;
    s_stream_label = NULL;
    s_stream_pending_layout = 0;
    s_screen = NULL;
    s_chat_list = NULL;
    s_title_bar = NULL;
    s_mode_label = NULL;
    s_stat_label = NULL;
    s_bottom_shell = NULL;
    s_bottom_panel = NULL;
    s_attach_bar = NULL;
    s_attach_thumb = NULL;
    s_attach_label = NULL;
    s_attach_divider = NULL;
    s_mode_switch_bar = NULL;
    memset(s_mode_switch_btns, 0, sizeof(s_mode_switch_btns));
    memset(s_mode_switch_labels, 0, sizeof(s_mode_switch_labels));
    s_img_overlay = NULL;
    s_img_view = NULL;
}

const ui_page_entry_t ui_page_chat_entry = {
    .id = UI_PAGE_CHAT,
    .name = "chat",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};

/* Message handler called from tuya_ai_display_stub.c */
void ui_page_chat_on_msg(const uint8_t *msg, int len, int display_tp)
{
    if (display_tp == TY_DISPLAY_TP_CHAT_MODE ||
        display_tp == TY_DISPLAY_TP_MODE_NOTIFY) {
        if (s_mode_label) refresh_mode_label();
        return;
    }

    if (!s_screen) return;

    switch (display_tp) {
    case TY_DISPLAY_TP_HUMAN_CHAT:
        chat_append_user((const char *)msg, (uint16_t)len);
        break;
    case TY_DISPLAY_TP_AI_CHAT_START:
        chat_append_ai_start((const char *)msg, (uint16_t)len);
        refresh_stat_label();
        break;
    case TY_DISPLAY_TP_AI_CHAT_DATA:
        chat_append_ai_data((const char *)msg, (uint16_t)len);
        break;
    case TY_DISPLAY_TP_AI_CHAT_STOP:
        chat_append_ai_stop();
        refresh_stat_label();
        break;
    case TY_DISPLAY_TP_STAT_LISTEN:
    case TY_DISPLAY_TP_STAT_SPEAK:
    case TY_DISPLAY_TP_STAT_IDLE:
    case TY_DISPLAY_TP_CHAT_STAT:
        refresh_stat_label();
        break;
    case TY_DISPLAY_TP_CLEAR_ATTACHMENT:
        clear_attachment();
        break;
    case TY_DISPLAY_TP_AI_IMAGE: {
        /* msg carries the saved picture filename; copy it null-terminated. */
        char name[UI_PICTURE_NAME_MAX + 1];
        int n = (!msg || len < 0) ? 0 : (len > UI_PICTURE_NAME_MAX ? UI_PICTURE_NAME_MAX : len);
        if (n > 0) {
            memcpy(name, msg, (size_t)n);
        }
        name[n] = '\0';
        chat_append_image_link(name);
        break;
    }
    default:
        break;
    }
}
