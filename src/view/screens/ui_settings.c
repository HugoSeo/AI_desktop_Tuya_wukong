/**
 * @file ui_settings.c
 * @brief Settings screen for T5AI_BOARD (320x480) — registry-driven
 *
 * The screen is a single scrollable list whose contents are described by
 * a static SETTINGS_ITEM_T s_items[] table. Each entry declares a row
 * type (BUTTON or SWITCH), label text, and either an on_click callback
 * or a get/set pair for the persisted toggle state. Rendering is one
 * for-loop over the table; adding a new setting = one row in the table
 * plus one tiny callback. New row kinds (slider, submenu) extend the
 * enum + render switch case.
 *
 * The destructive "device reset" entry no longer flips the screen into
 * an in-screen confirm sub-view — instead it pops the shared
 * ui_dialog modal with countdown + non-dismissable mask, matching ADR-0004
 * (and unblocking other future destructive settings to reuse the same
 * dialog component).
 *
 * Reset action stays unchanged: tuya_iot_wf_gw_fast_unactive(GWCM_OLD,
 * WF_START_SMART_AP_CONCURRENT). Long-press key reset and mode-driven
 * reset paths are intentionally left untouched — only the settings
 * entry routes through the dialog.
 *
 * @version 2.0
 * @date 2026-05-23
 * @copyright Copyright (c) Tuya Inc.
 */
#include <string.h>
#include "ui_common.h"
#include "ui_settings.h"
#include "ui_dialog.h"
#include "ui_toast.h"
#include "tuya_ws_db.h"
#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
#include "tuya_iot_wifi_api.h"
#endif

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define SETTINGS_CONTENT_Y          UI_TITLE_BAR_H
#define SETTINGS_CONTENT_H          (LV_VER_RES - UI_TITLE_BAR_H)

#define SETTINGS_ROW_W              280
#define SETTINGS_ROW_H              60
#define SETTINGS_ROW_X              ((LV_HOR_RES - SETTINGS_ROW_W) / 2)
#define SETTINGS_ROW_GAP            12
#define SETTINGS_ROW_Y0             16
#define SETTINGS_ROW_RADIUS         16
#define SETTINGS_ROW_BG             0xB8BDDE
#define SETTINGS_ROW_BG_OPA         28
#define SETTINGS_ROW_LBL_PAD        20
#define SETTINGS_ROW_RIGHT_PAD      16

#define SETTINGS_SWITCH_ON_BG       0x34C759   /* iOS-style system green for the "ON" indicator */
#define SETTINGS_SWITCH_OFF_BG      0x55585F   /* track when off */
#define SETTINGS_SWITCH_KNOB        0xFFFFFF
#define SETTINGS_SWITCH_W           50
#define SETTINGS_SWITCH_H           28

/* Confirm-dialog protection knobs (ADR-0004) */
#define SETTINGS_RESET_COUNTDOWN    5

/* KV keys (see CONTEXT.md "AI 相机开关") */
#define KV_AI_CAMERA                "ui_ai_camera"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    SETTINGS_ITEM_BUTTON = 0,
    SETTINGS_ITEM_SWITCH,
} SETTINGS_ITEM_TYPE_E;

typedef struct {
    SETTINGS_ITEM_TYPE_E type;
    const CHAR_T        *label;
    /* Tagged by `type`. C99 anonymous-union initializer style is used in
     * s_items[] below. */
    union {
        VOID_T (*on_click)(VOID_T);                 /* BUTTON */
        struct {
            BOOL_T (*get)(VOID_T);                   /* SWITCH initial state */
            VOID_T (*set)(BOOL_T);                   /* SWITCH on toggle */
        } toggle;
    };
} SETTINGS_ITEM_T;

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *title_bar;
    lv_obj_t *title_lbl;
    lv_obj_t *back_btn;
    lv_obj_t *list_cont;
} SETTINGS_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC SETTINGS_UI_T s_settings = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations — KV helpers
 * --------------------------------------------------------------------------- */
STATIC BOOL_T __ai_camera_get(VOID_T);
STATIC VOID_T __ai_camera_set(BOOL_T on);
#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
STATIC BOOL_T s_p2p_on = FALSE;
STATIC BOOL_T __p2p_get(VOID_T);
STATIC VOID_T __p2p_set(BOOL_T on);
#endif

/* ---------------------------------------------------------------------------
 * Forward declarations — item callbacks
 * --------------------------------------------------------------------------- */
STATIC VOID_T __on_click_device_reset(VOID_T);
STATIC VOID_T __on_dialog_reset_confirm(VOID_T);
STATIC VOID_T __on_dialog_reset_cancel(VOID_T);

/* ---------------------------------------------------------------------------
 * Items registry (single source of truth for the settings list)
 * --------------------------------------------------------------------------- */
STATIC const SETTINGS_ITEM_T s_items[] = {
    {
        .type  = SETTINGS_ITEM_SWITCH,
        .label = "AI 相机",
        .toggle = { __ai_camera_get, __ai_camera_set },
    },
#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
    {
        .type  = SETTINGS_ITEM_SWITCH,
        .label = "P2P",
        .toggle = { __p2p_get, __p2p_set },
    },
#endif
#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
    {
        /* Stress-test toggle: NOT persisted to KV (every cold boot starts
         * OFF). State + GIF cycle + layout live in ui_chat.c; settings is
         * just a thin UI projection — see ui_chat_set/get_stress_test. */
        .type  = SETTINGS_ITEM_SWITCH,
        .label = "压力测试",
        .toggle = { ui_chat_get_stress_test, ui_chat_set_stress_test },
    },
#endif
    {
        .type     = SETTINGS_ITEM_BUTTON,
        .label    = "设备重置",
        .on_click = __on_click_device_reset,
    },
};
#define SETTINGS_ITEMS_NUM  (sizeof(s_items) / sizeof(s_items[0]))

/* ---------------------------------------------------------------------------
 * Forward declarations — internal builders / events
 * --------------------------------------------------------------------------- */
STATIC VOID_T setup_scr_settings(VOID_T);
STATIC VOID_T __settings_build_title_bar(VOID_T);
STATIC VOID_T __settings_build_list(VOID_T);
STATIC VOID_T __settings_build_row_button(lv_obj_t *parent,
                                          const SETTINGS_ITEM_T *item,
                                          INT_T y);
STATIC VOID_T __settings_build_row_switch(lv_obj_t *parent,
                                          const SETTINGS_ITEM_T *item,
                                          INT_T y);
STATIC VOID_T __settings_back_cb(lv_event_t *e);
STATIC VOID_T __settings_button_row_cb(lv_event_t *e);
STATIC VOID_T __settings_switch_changed_cb(lv_event_t *e);

/* ---------------------------------------------------------------------------
 * KV helpers (direct wd_common_* per ADR-0004 "no wrapper layer")
 * --------------------------------------------------------------------------- */

/**
 * @brief Read AI camera toggle from KV; default OFF on miss / invalid.
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
 * @brief Persist AI camera toggle to KV.
 */
STATIC VOID_T __ai_camera_set(BOOL_T on)
{
    BYTE_T      v  = on ? 1 : 0;
    OPERATE_RET rt = wd_common_write(KV_AI_CAMERA, (CONST BYTE_T *)&v, sizeof(v));
    if (rt != OPRT_OK) {
        PR_ERR("settings: ai_camera kv write failed, rt=%d", rt);
    }
}

#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
STATIC BOOL_T __p2p_get(VOID_T)
{
    return FALSE;
}

STATIC VOID_T __p2p_set(BOOL_T on)
{
    PR_DEBUG("settings: p2p -> %s", on ? "ON" : "OFF");
    if (!on) {
        return;
    }

    s_p2p_on = TRUE;
    tuya_p2p_app_start();
    TUYA_IPC_call_init();
}
#endif

/* ---------------------------------------------------------------------------
 * Item callbacks — Device reset
 * --------------------------------------------------------------------------- */

STATIC VOID_T __on_dialog_reset_cancel(VOID_T)
{
    PR_DEBUG("settings: reset cancelled");
    ui_dialog_hide();
}

STATIC VOID_T __on_dialog_reset_confirm(VOID_T)
{
    PR_INFO("settings: device reset confirmed");
    ui_dialog_hide();
#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
    tuya_iot_wf_gw_fast_unactive(GWCM_OLD, WF_START_SMART_AP_CONCURRENT);
#endif
}

STATIC VOID_T __on_click_device_reset(VOID_T)
{
    UI_DIALOG_CFG_T cfg = {
        .title         = "确认重置设备",
        .body          = "重置后将断开当前 Wi-Fi 配网并重新进入配网模式。",
        .button_count  = 2,
        .buttons       = {
            {
                .text       = "取消",
                .is_primary = FALSE,
                .on_click   = __on_dialog_reset_cancel,
            },
            {
                .text       = "确认",
                .is_primary = TRUE,
                .on_click   = __on_dialog_reset_confirm,
            },
        },
        .countdown_sec = SETTINGS_RESET_COUNTDOWN,
        .dismissable   = FALSE,
        .on_dismiss    = NULL,
    };
    ui_dialog_show(&cfg);
}

/* ---------------------------------------------------------------------------
 * UI builders
 * --------------------------------------------------------------------------- */

STATIC VOID_T __settings_build_title_bar(VOID_T)
{
    s_settings.title_bar = lv_obj_create(s_settings.scr);
    lv_obj_remove_style_all(s_settings.title_bar);
    lv_obj_set_pos(s_settings.title_bar, 0, 0);
    lv_obj_set_size(s_settings.title_bar, LV_HOR_RES, UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_settings.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_settings.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_settings.back_btn = lv_btn_create(s_settings.title_bar);
    lv_obj_remove_style_all(s_settings.back_btn);
    lv_obj_set_size(s_settings.back_btn, UI_TITLE_BTN_W, UI_TITLE_BAR_H);
    lv_obj_set_pos(s_settings.back_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_settings.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_settings.back_btn, __settings_back_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_img_create(s_settings.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_set_size(back_icon, UI_TITLE_ICON_SIZE, UI_TITLE_ICON_SIZE);
    lv_obj_center(back_icon);

    s_settings.title_lbl = lv_label_create(s_settings.title_bar);
    lv_label_set_text(s_settings.title_lbl, "设置");
    lv_obj_set_style_text_font(s_settings.title_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_settings.title_lbl, lv_color_white(), 0);
    lv_obj_center(s_settings.title_lbl);
}

STATIC VOID_T __settings_build_row_button(lv_obj_t *parent,
                                          const SETTINGS_ITEM_T *item,
                                          INT_T y)
{
    lv_obj_t *row = lv_btn_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, SETTINGS_ROW_W, SETTINGS_ROW_H);
    lv_obj_set_pos(row, SETTINGS_ROW_X, y);
    lv_obj_set_style_radius(row, SETTINGS_ROW_RADIUS, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(SETTINGS_ROW_BG), 0);
    lv_obj_set_style_bg_opa(row, SETTINGS_ROW_BG_OPA, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    /* Item index is encoded into user_data so the shared cb can
     * dispatch back to s_items[i].on_click. */
    lv_obj_add_event_cb(row, __settings_button_row_cb,
                        LV_EVENT_CLICKED,
                        (VOID_T *)(uintptr_t)(item - s_items));

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, item->label);
    lv_obj_set_style_text_font(lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, SETTINGS_ROW_LBL_PAD, 0);

    lv_obj_t *arrow = lv_img_create(row);
    LV_IMG_DECLARE(icon_arrow_yellow);
    lv_img_set_src(arrow, &icon_arrow_yellow);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -SETTINGS_ROW_RIGHT_PAD, 0);
    lv_obj_clear_flag(arrow, LV_OBJ_FLAG_CLICKABLE);
}

STATIC VOID_T __settings_build_row_switch(lv_obj_t *parent,
                                          const SETTINGS_ITEM_T *item,
                                          INT_T y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, SETTINGS_ROW_W, SETTINGS_ROW_H);
    lv_obj_set_pos(row, SETTINGS_ROW_X, y);
    lv_obj_set_style_radius(row, SETTINGS_ROW_RADIUS, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(SETTINGS_ROW_BG), 0);
    lv_obj_set_style_bg_opa(row, SETTINGS_ROW_BG_OPA, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, item->label);
    lv_obj_set_style_text_font(lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, SETTINGS_ROW_LBL_PAD, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, SETTINGS_SWITCH_W, SETTINGS_SWITCH_H);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -SETTINGS_ROW_RIGHT_PAD, 0);
    /* Track + indicator + knob colors per ADR-0004 (armed-yellow accent
     * to share visual language with the reset confirm armed state). */
    lv_obj_set_style_bg_color(sw,
                              lv_color_hex(SETTINGS_SWITCH_OFF_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw,
                              lv_color_hex(SETTINGS_SWITCH_ON_BG),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw,
                              lv_color_hex(SETTINGS_SWITCH_KNOB),
                              LV_PART_KNOB);

    /* Initial state from the item's getter; cb writes via setter. */
    if (item->toggle.get != NULL && item->toggle.get()) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, __settings_switch_changed_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (VOID_T *)(uintptr_t)(item - s_items));
}

STATIC VOID_T __settings_build_list(VOID_T)
{
    s_settings.list_cont = lv_obj_create(s_settings.scr);
    lv_obj_remove_style_all(s_settings.list_cont);
    lv_obj_set_pos(s_settings.list_cont, 0, SETTINGS_CONTENT_Y);
    lv_obj_set_size(s_settings.list_cont, LV_HOR_RES, SETTINGS_CONTENT_H);
    lv_obj_set_style_bg_opa(s_settings.list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_settings.list_cont, 0, 0);
    lv_obj_set_scrollbar_mode(s_settings.list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_settings.list_cont, LV_OBJ_FLAG_SCROLLABLE);

    INT_T y = SETTINGS_ROW_Y0;
    for (UINT_T i = 0; i < SETTINGS_ITEMS_NUM; i++) {
        const SETTINGS_ITEM_T *item = &s_items[i];
        switch (item->type) {
        case SETTINGS_ITEM_BUTTON:
            __settings_build_row_button(s_settings.list_cont, item, y);
            break;
        case SETTINGS_ITEM_SWITCH:
            __settings_build_row_switch(s_settings.list_cont, item, y);
            break;
        default:
            PR_WARN("settings: unknown item type %d at idx %u",
                    (int)item->type, i);
            break;
        }
        y += SETTINGS_ROW_H + SETTINGS_ROW_GAP;
    }
}

STATIC VOID_T setup_scr_settings(VOID_T)
{
    if (s_settings.scr != NULL) {
        return;
    }

    memset(&s_settings, 0, sizeof(s_settings));

    s_settings.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_settings.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_settings.scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_bg_opa(s_settings.scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_settings.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_settings.scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_settings.scr, LV_OBJ_FLAG_SCROLLABLE);

    __settings_build_title_bar();
    __settings_build_list();

    ui_control_center_register_gesture(s_settings.scr);

    lv_obj_update_layout(s_settings.scr);
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

VOID_T ui_settings_show(VOID_T)
{
    if (s_settings.scr == NULL) {
        setup_scr_settings();
    }

    /* Drop any in-flight reset dialog from a previous visit. */
    if (ui_dialog_is_visible()) {
        ui_dialog_hide();
    }

    if (lv_scr_act() != s_settings.scr) {
        lv_scr_load(s_settings.scr);
    }
}

VOID_T ui_settings_hide(VOID_T)
{
    /* Tear down the modal so the next entry is clean; settings rows
     * stay built (lazy-init pattern shared with other screens). */
    if (ui_dialog_is_visible()) {
        ui_dialog_hide();
    }
}

lv_obj_t *ui_settings_get_scr(VOID_T)
{
    return s_settings.scr;
}

BOOL_T ui_settings_p2p_get(VOID_T)
{
#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
    return s_p2p_on;
#else
    return FALSE;
#endif
}

/* ---------------------------------------------------------------------------
 * Event handlers
 * --------------------------------------------------------------------------- */

STATIC VOID_T __settings_back_cb(lv_event_t *e)
{
    (VOID_T)e;

    /* If the reset dialog is up, back routes to dialog dismiss only when
     * dismissable; otherwise it stays put (mirrors mask-click semantics).
     * Reset dialog has dismissable=FALSE, so back here just no-ops in that
     * case — user must drive the dialog with its own buttons. */
    if (ui_dialog_is_visible()) {
        return;
    }

    PR_DEBUG("settings: back");
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_SETTINGS);
}

STATIC VOID_T __settings_button_row_cb(lv_event_t *e)
{
    UINT_T idx = (UINT_T)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= SETTINGS_ITEMS_NUM) {
        return;
    }
    const SETTINGS_ITEM_T *item = &s_items[idx];
    if (item->type != SETTINGS_ITEM_BUTTON) {
        return;
    }
    if (item->on_click != NULL) {
        item->on_click();
    }
}

STATIC VOID_T __settings_switch_changed_cb(lv_event_t *e)
{
    UINT_T idx = (UINT_T)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= SETTINGS_ITEMS_NUM) {
        return;
    }
    const SETTINGS_ITEM_T *item = &s_items[idx];
    if (item->type != SETTINGS_ITEM_SWITCH || item->toggle.set == NULL) {
        return;
    }
    lv_obj_t *sw = lv_event_get_target(e);
    BOOL_T on = lv_obj_has_state(sw, LV_STATE_CHECKED) ? TRUE : FALSE;
#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
    if (item->toggle.get == __p2p_get && s_p2p_on == TRUE && on == FALSE) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
        ui_toast_show("请重启设备");
        return;
    }
#endif
    PR_DEBUG("settings: switch '%s' -> %s",
             item->label, on ? "ON" : "OFF");
    item->toggle.set(on);
}
