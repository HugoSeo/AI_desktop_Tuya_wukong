/**
 * @file ui_page_contacts.c
 * @brief 联系人页:底部选项卡布局,tab0="联系人"(P2P通话),tab1="发现"(NFC读写)。
 *        P2P 开关关闭时联系人列表仅显示开关提示(含 App 行在内全部隐藏)。
 *        业务仅经 ui_svc_call/ui_svc_nfc(见 RULES.md §6/§8)。
 *        NFC 读写由 service 异步执行,结果在 UI 线程满屏浮层展示。
 */
#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_popup.h"
#include "ui_comp_tabview.h"
#include "ui_svc_call.h"
#include "ui_svc_devinfo.h"
#include "ui_svc_nfc.h"
#include "ui_page_ids.h"
#include "ui_page_contacts.h"
#include "tal_memory.h"
#include "uni_log.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

LV_IMG_DECLARE(icon_back_24_24);

#define CONTACTS_STATUSBAR_H  32
#define CONTACTS_TITLEBAR_H   48
#define CONTACTS_TAB_H        48
#define CONTACTS_ITEM_H       64
#define CONTACTS_NAME_W       176   /* 320 基准宽 - 列表 pad 20x2 - 行 pad 16x2 - 删除键 */
#define CONTACTS_NAME_FULL_W  248   /* App 行无删除键,名称占满行内容宽度 */
#define CONTACTS_DELETE_W     56
#define CONTACTS_DELETE_H     36
#define CONTACTS_BG_NORMAL    0x353740
#define CONTACTS_BG_APP       0x2E4A6E   /* App 行:蓝色强调,与设备行区分 */
#define CONTACTS_HINT         0xB8BDDE
#define CONTACTS_ACCENT       0xF3E55D

/* 列表区终态(P2P_OFF 时连 App 行一起隐藏,其余状态 App 行恒在)。 */
typedef enum {
    CONTACTS_UI_LOADING = 0,   /* 拉取中 / 等链路就绪(就绪后 service 自动重拉) */
    CONTACTS_UI_READY,         /* 拉取成功(含 0 台设备) */
    CONTACTS_UI_FAILED,        /* 拉取失败,可点"刷新"重试 */
    CONTACTS_UI_P2P_OFF,       /* P2P 开关关闭,提示去设置开启 */
} contacts_ui_state_t;

static lv_obj_t            *s_screen      = NULL;
static lv_obj_t            *s_tv          = NULL;  /* tabview */
static lv_obj_t            *s_list        = NULL;
static lv_obj_t            *s_title       = NULL;
static lv_obj_t            *s_refresh_btn = NULL;
static lv_obj_t            *s_nfc_popup   = NULL;   /* NFC 读结果浮层 */
static lv_obj_t            *s_nfc_hint    = NULL;
static lv_obj_t            *s_nfc_read_btn = NULL;
static lv_obj_t            *s_nfc_write_btn = NULL;
/* Contact list: allocated in on_create, released in on_destroy. NULL means the
 * allocation failed — s_contact_cnt then stays 0 and the page degrades to the
 * "no devices" hint instead of failing to open. */
static ui_call_contact_t   *s_contacts    = NULL;
static int                  s_contact_cnt = 0;
static contacts_ui_state_t  s_ui_state    = CONTACTS_UI_LOADING;
static bool                 s_delete_inflight = false;
static bool                 s_nfc_busy = false;

static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void tab_change_cb(lv_event_t *e);
static void refresh_cb(lv_event_t *e);
static void row_cb(lv_event_t *e);
static void delete_cb(lv_event_t *e);
static void on_contact_delete_done(int result);
static void on_contacts_ready(const ui_call_contact_t *list, int count);
static void nfc_read_cb(lv_event_t *e);
static void nfc_write_cb(lv_event_t *e);
static void refresh_nfc_controls(void);

/* row index: 0 = App(设备->App);1..cnt = s_contacts[idx-1](设备->设备)。 */
static void make_row(int row_index, const char *name, bool is_app)
{
    lv_obj_t *item = lv_obj_create(s_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_PCT(100), ui_adapt(CONTACTS_ITEM_H));
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(item, ui_adapt(16), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(is_app ? CONTACTS_BG_APP : CONTACTS_BG_NORMAL), 0);
    lv_obj_set_style_pad_hor(item, ui_adapt(16), 0);
    lv_obj_set_style_pad_ver(item, ui_adapt(10), 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(item, row_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)row_index);

    lv_obj_t *nm = lv_label_create(item);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_label_set_text(nm, (name && name[0]) ? name : "-");
    lv_obj_set_width(nm, ui_adapt(is_app ? CONTACTS_NAME_FULL_W : CONTACTS_NAME_W));
    lv_obj_set_style_text_color(nm, is_app ? lv_color_hex(CONTACTS_ACCENT) : lv_color_white(), 0);

    if (!is_app) {
        lv_obj_t *del = ui_comp_btn_create_styled(item, "删除", UI_COMP_BTN_TEXT, NULL);
        lv_obj_set_size(del, ui_adapt(CONTACTS_DELETE_W), ui_adapt(CONTACTS_DELETE_H));
        lv_obj_set_style_text_color(del, UI_COLOR_ERROR, 0);
        lv_obj_clear_flag(del, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(del, delete_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)row_index);

        lv_obj_t *lbl = lv_obj_get_child(del, 0);
        if (lbl) {
            lv_obj_center(lbl);
        }
    }
}

static void add_hint(ui_i18n_key_t key)
{
    lv_obj_t *hint = lv_label_create(s_list);
    lv_label_set_text(hint, ui_i18n_text(key));
    lv_obj_set_style_text_color(hint, lv_color_hex(CONTACTS_HINT), 0);
    lv_obj_set_style_pad_top(hint, ui_adapt(16), 0);
}

static void build_rows(void)
{
    if (!s_list) return;
    lv_obj_clean(s_list);

    /* P2P 开关关闭:整页只留提示,App 行一并隐藏(此时点任何行都拨不出去)。 */
    if (s_ui_state == CONTACTS_UI_P2P_OFF) {
        add_hint(UI_TEXT_CALL_P2P_DISABLED);
        return;
    }

    /* 第一行:呼叫手机 App(设备->App)。不依赖拉取。 */
    make_row(0, ui_i18n_text(UI_TEXT_CALL_CONTACT_APP), true);

    switch (s_ui_state) {
    case CONTACTS_UI_LOADING:
        add_hint(UI_TEXT_LOADING);
        return;
    case CONTACTS_UI_FAILED:
        add_hint(UI_TEXT_CALL_CONTACTS_LOAD_FAILED);
        return;
    case CONTACTS_UI_READY:
    default:
        break;
    }

    /* 已拉回:设备行(设备->设备)。 */
    for (int i = 0; i < s_contact_cnt; i++) {
        make_row(i + 1, s_contacts[i].name, false);
    }
    if (s_contact_cnt == 0) {
        add_hint(UI_TEXT_CALL_NO_DEVICES);
    }
}

/* 页面侧发起一次拉取:先渲染 P2P_OFF/LOADING 终态,再请求 service。 */
static void request_fetch(void)
{
    if (!ui_svc_call_enabled_get()) {
        s_ui_state = CONTACTS_UI_P2P_OFF;
        build_rows();
        return;
    }
    s_ui_state = CONTACTS_UI_LOADING;
    build_rows();
    ui_svc_call_contacts_async(on_contacts_ready);
}

/* UI 线程:拉取结果回调(count<0=失败)。页面若已销毁(s_list==NULL)则丢弃。 */
static void on_contacts_ready(const ui_call_contact_t *list, int count)
{
    if (!s_list) return;
    if (count < 0) {
        s_contact_cnt = 0;
        s_ui_state = CONTACTS_UI_FAILED;
        build_rows();
        return;
    }
    if (count > UI_CALL_CONTACT_MAX) count = UI_CALL_CONTACT_MAX;
    if (s_contacts != NULL && list != NULL && count > 0) {
        memcpy(s_contacts, list, sizeof(*s_contacts) * (size_t)count);
    } else {
        count = 0;   /* no backing store → keep the count consistent with it */
    }
    s_contact_cnt = count;
    s_ui_state = CONTACTS_UI_READY;
    build_rows();
}

static void row_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);

    /* P2P 开关关着时不发起呼叫(与呼叫页 dial_cb 同口径)。 */
    if (!ui_svc_call_enabled_get()) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_CALL_P2P_DISABLED), 1500);
        return;
    }

    if (idx == 0) {
        ui_svc_call_set_peer_name(ui_i18n_text(UI_TEXT_CALL_CONTACT_APP));
        ui_svc_call_dial();                            /* 设备->App */
    } else if (idx >= 1 && idx <= s_contact_cnt) {
        ui_svc_call_set_peer_name(s_contacts[idx - 1].name);
        ui_svc_call_dial_device(s_contacts[idx - 1].id); /* 设备->设备 */
    } else {
        return;
    }
    ui_route_push(UI_PAGE_CALL);
}

static void on_contact_delete_done(int result)
{
    bool was_waiting = s_delete_inflight;
    s_delete_inflight = false;
    if (was_waiting) {
        ui_comp_popup_dismiss();
    }

    if (result == UI_SVC_CALL_CONTACT_DELETE_OK) {
        ui_comp_popup_toast("删除成功", 2000);
        if (s_list != NULL) {
            request_fetch();
        }
    } else {
        ui_comp_popup_toast("删除失败", 2000);
    }
}

static void delete_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);

    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (s_delete_inflight) {
        return;
    }
    if (idx < 1 || idx > s_contact_cnt) {
        ui_comp_popup_toast("删除失败", 2000);
        return;
    }

    s_delete_inflight = true;
    ui_comp_popup_show(UI_COMP_POPUP_LOADING, NULL, "删除中...", NULL);
    ui_svc_call_contact_delete_async(s_contacts[idx - 1].id, on_contact_delete_done);
}

/* ---------------------------------------------------------------------------
 * NFC data display popup (read result)
 * -------------------------------------------------------------------------*/

static void nfc_popup_close_cb(lv_event_t *e)
{
    (void)e;
    if (s_nfc_popup) {
        lv_obj_add_flag(s_nfc_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_nfc_popup);
        s_nfc_popup = NULL;
    }
}

static void show_nfc_data_popup(const char *uuid)
{
    /* Dismiss previous popup if any */
    if (s_nfc_popup) {
        lv_obj_add_flag(s_nfc_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_nfc_popup);
        s_nfc_popup = NULL;
    }

    /* Full-screen overlay */
    s_nfc_popup = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_nfc_popup);
    lv_obj_set_size(s_nfc_popup, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_nfc_popup, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_nfc_popup, LV_OPA_70, 0);
    lv_obj_clear_flag(s_nfc_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_nfc_popup, nfc_popup_close_cb, LV_EVENT_CLICKED, NULL);

    /* Center card */
    lv_obj_t *card = lv_obj_create(s_nfc_popup);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, ui_adapt(280), ui_adapt(260));
    lv_obj_set_style_bg_color(card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_pad_all(card, ui_adapt(16), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(card);

    /* Title */
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, ui_i18n_text(UI_TEXT_NFC_READ_RESULT));
    lv_obj_set_style_text_color(title, UI_COLOR_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *text = lv_label_create(card);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text, ui_adapt(248));
    lv_label_set_text(text, (uuid && uuid[0]) ? uuid : ui_i18n_text(UI_TEXT_NFC_EMPTY));
    lv_obj_set_style_text_color(text, UI_COLOR_TEXT, 0);
    lv_obj_align(text, LV_ALIGN_TOP_MID, 0, ui_adapt(28));

    /* Close hint */
    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, ui_i18n_text(UI_TEXT_NFC_TAP_TO_CLOSE));
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SEC, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, ui_adapt(-8));
}

/* ---------------------------------------------------------------------------
 * NFC read / write helpers
 * -------------------------------------------------------------------------*/

static const char *nfc_error_text(ui_svc_nfc_result_t result, bool write)
{
    switch (result) {
    case UI_SVC_NFC_DISABLED:
        return ui_i18n_text(UI_TEXT_NFC_DISABLED);
    case UI_SVC_NFC_NOT_READY:
    case UI_SVC_NFC_BUSY:
        return ui_i18n_text(UI_TEXT_NFC_INITIALIZING);
    case UI_SVC_NFC_NO_CARD:
        return ui_i18n_text(UI_TEXT_NFC_NO_CARD);
    case UI_SVC_NFC_UNSUPPORTED_CARD:
        return ui_i18n_text(UI_TEXT_NFC_UNSUPPORTED_CARD);
    case UI_SVC_NFC_FAILED:
    default:
        return ui_i18n_text(write ? UI_TEXT_NFC_WRITE_FAILED : UI_TEXT_NFC_READ_FAILED);
    }
}

static void nfc_read_done(ui_svc_nfc_result_t result, const char *uuid)
{
    if (!s_screen) {
        return;
    }
    s_nfc_busy = false;
    if (ui_route_current() != UI_PAGE_CONTACTS) {
        return;
    }
    refresh_nfc_controls();

    if (result == UI_SVC_NFC_OK) {
        show_nfc_data_popup(uuid);
    } else {
        ui_comp_popup_toast(nfc_error_text(result, false), 2000);
    }
}

static void nfc_write_done(ui_svc_nfc_result_t result, const char *uuid)
{
    (void)uuid;
    if (!s_screen) {
        return;
    }
    s_nfc_busy = false;
    if (ui_route_current() != UI_PAGE_CONTACTS) {
        return;
    }
    refresh_nfc_controls();
    ui_comp_popup_toast(result == UI_SVC_NFC_OK
                            ? ui_i18n_text(UI_TEXT_NFC_WRITE_SUCCESS)
                            : nfc_error_text(result, true),
                        2000);
}

static void nfc_read_cb(lv_event_t *e)
{
    (void)e;
    if (s_nfc_busy) {
        return;
    }
    s_nfc_busy = true;
    refresh_nfc_controls();
    ui_svc_nfc_read_async(nfc_read_done);
}

static void nfc_write_cb(lv_event_t *e)
{
    (void)e;
    if (s_nfc_busy) {
        return;
    }

    const char *uuid = ui_svc_devinfo_uuid();
    if (uuid == NULL || uuid[0] == '\0' || strcmp(uuid, "--") == 0) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_NFC_WRITE_FAILED), 2000);
        return;
    }
    s_nfc_busy = true;
    refresh_nfc_controls();
    ui_svc_nfc_write_uuid_async(uuid, nfc_write_done);
}

static void refresh_nfc_controls(void)
{
    if (!s_nfc_hint || !s_nfc_read_btn || !s_nfc_write_btn) {
        return;
    }

    ui_comp_btn_set_text(s_nfc_read_btn, ui_i18n_text(UI_TEXT_NFC_READ));
    ui_comp_btn_set_text(s_nfc_write_btn, ui_i18n_text(UI_TEXT_NFC_WRITE));

    if (!ui_svc_nfc_available()) {
        lv_label_set_text(s_nfc_hint, ui_i18n_text(UI_TEXT_FEATURE_UNAVAILABLE));
    } else if (!ui_svc_nfc_switch_get()) {
        lv_label_set_text(s_nfc_hint, ui_i18n_text(UI_TEXT_NFC_DISABLED));
    } else if (!ui_svc_nfc_ready()) {
        lv_label_set_text(s_nfc_hint, ui_i18n_text(UI_TEXT_NFC_INITIALIZING));
    } else {
        lv_obj_add_flag(s_nfc_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_nfc_read_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_nfc_write_btn, LV_OBJ_FLAG_HIDDEN);
        ui_comp_btn_set_enabled(s_nfc_read_btn, !s_nfc_busy);
        ui_comp_btn_set_enabled(s_nfc_write_btn, !s_nfc_busy);
        return;
    }

    lv_obj_clear_flag(s_nfc_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_nfc_read_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_nfc_write_btn, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------------------
 * Page lifecycle
 * -------------------------------------------------------------------------*/

static void on_create(void *parent)
{
    (void)parent;

    /* Contact rows are only needed while this page lives; allocate on entry and
     * release in on_destroy. Failure is non-fatal — see the declaration. */
    s_contacts = tal_malloc(sizeof(*s_contacts) * UI_CALL_CONTACT_MAX);
    if (s_contacts) {
        memset(s_contacts, 0, sizeof(*s_contacts) * UI_CALL_CONTACT_MAX);
    } else {
        PR_ERR("contacts: alloc %d bytes failed, list disabled",
               (int)(sizeof(*s_contacts) * UI_CALL_CONTACT_MAX));
    }

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(CONTACTS_STATUSBAR_H), 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);

    /* --- Shared title bar --- */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(CONTACTS_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_CALL_CONTACTS_TITLE));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    s_refresh_btn = ui_comp_btn_create_styled(titlebar, ui_i18n_text(UI_TEXT_REFRESH),
                                              UI_COMP_BTN_TEXT, refresh_cb);
    lv_obj_align(s_refresh_btn, LV_ALIGN_RIGHT_MID, -ui_adapt(8), 0);

    /* --- Shared styled native tabview: tabs at bottom --- */
    const ui_comp_tabview_props_t tabview_props = {
        .tab_pos = LV_DIR_BOTTOM,
        .tab_size = ui_adapt(CONTACTS_TAB_H),
    };
    s_tv = ui_comp_tabview_create(s_screen, &tabview_props);
    lv_obj_set_width(s_tv, LV_PCT(100));
    lv_obj_set_flex_grow(s_tv, 1);
    lv_obj_add_event_cb(s_tv, tab_change_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* --- Tab 0: 联系人 --- */
    lv_obj_t *tab_contacts = lv_tabview_add_tab(s_tv, ui_i18n_text(UI_TEXT_CALL_CONTACTS_TITLE));
    lv_obj_set_flex_flow(tab_contacts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab_contacts, 0, 0);

    /* Contact list */
    s_list = lv_obj_create(tab_contacts);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(s_list, ui_adapt(20), 0);
    lv_obj_set_style_pad_ver(s_list, ui_adapt(12), 0);
    lv_obj_set_style_pad_row(s_list, ui_adapt(10), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    /* --- Tab 1: 发现 --- */
    lv_obj_t *tab_discover = lv_tabview_add_tab(s_tv, ui_i18n_text(UI_TEXT_CONTACTS_TAB_DISCOVER));
    lv_obj_set_flex_flow(tab_discover, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab_discover,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab_discover, ui_adapt(24), 0);
    lv_obj_set_style_pad_row(tab_discover, ui_adapt(20), 0);

    s_nfc_hint = lv_label_create(tab_discover);
    lv_obj_set_width(s_nfc_hint, ui_adapt(240));
    lv_obj_set_style_text_align(s_nfc_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_nfc_hint, UI_COLOR_TEXT_SEC, 0);

    /* NFC Read button */
    s_nfc_read_btn = ui_comp_btn_create_styled(tab_discover,
                                                ui_i18n_text(UI_TEXT_NFC_READ),
                                                UI_COMP_BTN_PRIMARY, nfc_read_cb);
    lv_obj_set_width(s_nfc_read_btn, ui_adapt(200));
    lv_obj_set_height(s_nfc_read_btn, ui_adapt(56));
    /* create_styled does not lay out the label; centre it inside the button. */
    lv_obj_t *lbl_read = lv_obj_get_child(s_nfc_read_btn, 0);
    if (lbl_read) {
        lv_obj_center(lbl_read);
    }

    /* NFC Write button */
    s_nfc_write_btn = ui_comp_btn_create_styled(tab_discover,
                                                 ui_i18n_text(UI_TEXT_NFC_WRITE),
                                                 UI_COMP_BTN_PRIMARY, nfc_write_cb);
    lv_obj_set_width(s_nfc_write_btn, ui_adapt(200));
    lv_obj_set_height(s_nfc_write_btn, ui_adapt(56));
    /* create_styled does not lay out the label; centre it inside the button. */
    lv_obj_t *lbl_write = lv_obj_get_child(s_nfc_write_btn, 0);
    if (lbl_write) {
        lv_obj_center(lbl_write);
    }

    s_nfc_busy = false;
    refresh_nfc_controls();
    s_contact_cnt = 0;
    request_fetch();
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        /* Rename tabs in case language changed */
        if (s_tv) {
            lv_tabview_rename_tab(s_tv, 0, ui_i18n_text(UI_TEXT_CALL_CONTACTS_TITLE));
            lv_tabview_rename_tab(s_tv, 1, ui_i18n_text(UI_TEXT_CONTACTS_TAB_DISCOVER));
        }
        /* Update shared title bar to match active tab */
        if (s_title && s_tv) {
            uint16_t active = lv_tabview_get_tab_act(s_tv);
            if (active == 0) {
                lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_CALL_CONTACTS_TITLE));
                if (s_refresh_btn) lv_obj_clear_flag(s_refresh_btn, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_CONTACTS_TAB_DISCOVER));
                if (s_refresh_btn) lv_obj_add_flag(s_refresh_btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (s_refresh_btn) {
            ui_comp_btn_set_text(s_refresh_btn, ui_i18n_text(UI_TEXT_REFRESH));
        }
        refresh_nfc_controls();
        build_rows();
    }
}

static void on_leave(void)
{
    if (s_nfc_popup) {
        lv_obj_add_flag(s_nfc_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_nfc_popup);
        s_nfc_popup = NULL;
    }
}

static void on_destroy(void)
{
    ui_svc_call_contacts_async(NULL);   /* 注销回调:迟到结果在 service 侧丢弃 */
    if (s_delete_inflight) {
        ui_comp_popup_dismiss();
    }
    s_delete_inflight = false;

    /* Dismiss NFC popup if open */
    if (s_nfc_popup) {
        lv_obj_add_flag(s_nfc_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_nfc_popup);
        s_nfc_popup = NULL;
    }

    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL; s_tv = NULL; s_list = NULL;
        s_title = NULL; s_refresh_btn = NULL;
        s_nfc_hint = NULL; s_nfc_read_btn = NULL; s_nfc_write_btn = NULL;
    }
    /* Release the contact list together with its count. The fetch callback was
     * unregistered above and is guarded by s_list (nulled here), so nothing can
     * reach s_contacts after this point. */
    tal_free(s_contacts);
    s_contacts    = NULL;
    s_contact_cnt = 0;
    s_ui_state = CONTACTS_UI_LOADING;
    s_nfc_busy = false;
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_route_pop();
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        ui_route_pop();
    }
}

/* 选项卡切换时同步更新共享标题栏文字,刷新按钮仅在联系人tab可见。 */
static void tab_change_cb(lv_event_t *e)
{
    (void)e;
    if (!s_tv || !s_title) return;
    uint16_t active = lv_tabview_get_tab_act(s_tv);
    if (active == 0) {
        lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_CALL_CONTACTS_TITLE));
        if (s_refresh_btn) lv_obj_clear_flag(s_refresh_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_CONTACTS_TAB_DISCOVER));
        if (s_refresh_btn) lv_obj_add_flag(s_refresh_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/* "刷新":重走一次拉取(P2P 开关态在 request_fetch 里判定)。
 * service 侧对在途拉取做防抖,连点不会堆积请求。 */
static void refresh_cb(lv_event_t *e)
{
    (void)e;
    request_fetch();
}

bool ui_page_contacts_is_discover_active(void)
{
    return ui_route_current() == UI_PAGE_CONTACTS &&
           s_tv != NULL &&
           lv_tabview_get_tab_act(s_tv) == 1;
}

const ui_page_entry_t ui_page_contacts_entry = {
    .id = UI_PAGE_CONTACTS,
    .name = "contacts",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
