#include "ui_app.h"
#include "ui_port.h"
#include "ui_state.h"
#include "ui_tick.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "tuya_display_hw.h"
#include "tuya_boot_splash.h"
#include "lvgl.h"
#include "tuya_app_config.h"   /* UI_WUKONG_PAGES */
#include "tal_log.h"
#include "tal_memory.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The standard Wukong page set + background services are only compiled when
 * UI_WUKONG_PAGES is on (default). Boards that supply their own root UI via
 * ui_app_register_board_ui() (ROBOT/EVB/EYES) disable it, so the framework
 * core links without the pages/services (and their camera/jpeg/p2p deps). */
#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES
#include "ui_route.h"
#include "ui_page_ids.h"
#include "ui_comp_statusbar.h"
#include "ui_services.h"
#include "ui_svc_devinfo.h"
#include "ui_svc_music.h"
#include "ui_svc_tm.h"
#include "ui_comp_popup.h"
#include "ui_svc_camera.h"
#include "ui_svc_call.h"
#include "ui_svc_nfc.h"
#include "ui_svc_activate.h"
#include "ui_svc_ota.h"
#include "ui_comp_ring_alarm.h"
#include "ui_page_contacts.h"

extern const ui_page_entry_t ui_page_home_entry;
extern const ui_page_entry_t ui_page_chat_entry;
extern const ui_page_entry_t ui_page_pulldown_entry;
extern const ui_page_entry_t ui_page_app_center_entry;
extern const ui_page_entry_t ui_page_settings_entry;
extern const ui_page_entry_t ui_page_about_entry;
extern const ui_page_entry_t ui_page_ota_entry;
extern const ui_page_entry_t ui_page_mode_entry;
extern const ui_page_entry_t ui_page_call_entry;
extern const ui_page_entry_t ui_page_contacts_entry;
extern const ui_page_entry_t ui_page_photo_entry;
extern const ui_page_entry_t ui_page_video_entry;
extern const ui_page_entry_t ui_page_music_entry;
extern const ui_page_entry_t ui_page_music_list_entry;
extern const ui_page_entry_t ui_page_clock_entry;
extern const ui_page_entry_t ui_page_schedule_entry;
extern const ui_page_entry_t ui_page_camera_entry;
extern const ui_page_entry_t ui_page_recording_entry;
extern const ui_page_entry_t ui_page_recording_list_entry;
extern const ui_page_entry_t ui_page_recording_transcribe_entry;
extern const ui_page_entry_t ui_page_files_entry;
extern const ui_page_entry_t ui_page_detection_entry;
extern const ui_page_entry_t ui_page_audio_diag_entry;
extern const ui_page_entry_t ui_page_diag_entry;
extern const ui_page_entry_t ui_page_sys_status_entry;
extern const ui_page_entry_t ui_page_net_status_entry;
extern const ui_page_entry_t ui_page_screen_test_entry;
extern const ui_page_entry_t ui_page_wlan_entry;
extern const ui_page_entry_t ui_page_activation_entry;
extern void ui_page_pulldown_init(void);
extern void ui_page_pulldown_set_gesture_enabled(bool enabled);
extern void tuya_ai_display_ui_register(void);

/* Music keeps playing in the background across most pages; only entering the
 * camera (capture), call, or chat page stops it — those own the audio/mic path
 * or conflict with music. Driven by the router's foreground-change observer. */
static void on_route_change(ui_page_id_t cur)
{
    ui_svc_ota_state_t ota_state = ui_svc_ota_get_state();
    if ((ota_state == UI_SVC_OTA_VERIFYING ||
         ota_state == UI_SVC_OTA_INSTALLING) && cur != UI_PAGE_OTA) {
        ui_route_reset(UI_PAGE_OTA);
        return;
    }

    /* Immersive pages hide the statusbar and place controls at y=0. Disable
     * the top-layer pulldown gesture zone so it cannot cover their back button. */
    ui_page_pulldown_set_gesture_enabled(cur != UI_PAGE_WLAN &&
                                         cur != UI_PAGE_CAMERA &&
                                         cur != UI_PAGE_SCREEN_TEST &&
                                         cur != UI_PAGE_OTA);

    if (cur != UI_PAGE_CAMERA && cur != UI_PAGE_CALL &&
        cur != UI_PAGE_CHAT   && cur != UI_PAGE_RECORDING) {
        return;
    }
#if defined(UI_FEATURE_MUSIC) && UI_FEATURE_MUSIC
    /* Only hit the backend when something is actually playing/paused. */
    ui_music_status_t st;
    ui_svc_music_get_status(&st);
    if (st.state != AI_PLAYER_STOPPED) {
        ui_svc_music_stop();
    }
#endif
}

/* An OTA opens its dedicated page once. During the downloadable phase the user
 * may leave it and follow progress from the pulldown panel. At 98% the SDK has
 * entered verify/apply, so the page becomes mandatory again until reboot. */
static bool s_ota_auto_opened = false;

static void on_ota_change(bool upgrading)
{
    (void)upgrading;
    ui_svc_ota_state_t state = ui_svc_ota_get_state();
    bool first_open = (state == UI_SVC_OTA_PREPARING ||
                       state == UI_SVC_OTA_DOWNLOADING) && !s_ota_auto_opened;
    bool mandatory = state == UI_SVC_OTA_VERIFYING ||
                     state == UI_SVC_OTA_INSTALLING;
    bool failure_notice = state == UI_SVC_OTA_FAILED;

    if (mandatory && ui_route_current() != UI_PAGE_OTA) {
        /* Installation must not be dropped because the route stack happened to
         * be full; collapse back to HOME and put OTA on top. */
        ui_route_reset(UI_PAGE_OTA);
    } else if ((first_open || failure_notice) && ui_route_current() != UI_PAGE_OTA) {
        if (ui_route_current() == UI_PAGE_PULLDOWN) {
            ui_route_replace(UI_PAGE_OTA);
        } else {
            ui_route_push(UI_PAGE_OTA);
        }
    }

    if (state == UI_SVC_OTA_PREPARING ||
        state == UI_SVC_OTA_DOWNLOADING ||
        state == UI_SVC_OTA_VERIFYING ||
        state == UI_SVC_OTA_INSTALLING) {
        s_ota_auto_opened = true;
    } else if (state == UI_SVC_OTA_FAILED) {
        s_ota_auto_opened = false;
    }
}

/* UI 线程:开机自检——摄像头初始化失败时提示(影响 P2P 视频通话)。
 * ui_app_init 末尾经 ui_app_async_call 触发,此时 ui_port_start 已执行、LVGL 就绪。 */
static void boot_camera_check_cb(void *p) {
    (void)p;
    if (!ui_svc_camera_available()) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_CAMERA_UNAVAILABLE), 2500);
    }
}

#if defined(UI_FEATURE_TIME) && UI_FEATURE_TIME
/* Wires the ring-alarm widget's Stop action back to the timer service.
 * Widgets may not call services directly (RULES §6), so ui_app bridges it. */
static void on_ring_stop(ui_ring_kind_t kind, const char *id)
{
    if (kind == UI_RING_KIND_ALARM && id && id[0]) {
        ui_svc_tm_alarm_ack(id);
    }
}

/* Timer/alarm fire events arrive already marshalled to the UI thread by
 * ui_svc_tm, so we can drive the global ringing overlay directly here. */
static void on_tm_fire(ui_svc_tm_fire_event_t event, const char *id, const char *message)
{
    switch (event) {
        case UI_SVC_TM_FIRE_ALARM:          ui_comp_ring_alarm_show(UI_RING_KIND_ALARM, id, message, on_ring_stop); break;
        case UI_SVC_TM_FIRE_REMINDER:       ui_comp_ring_alarm_show(UI_RING_KIND_REMINDER, id, message, on_ring_stop); break;
        case UI_SVC_TM_FIRE_COUNTDOWN_DONE: ui_comp_ring_alarm_show(UI_RING_KIND_COUNTDOWN, "", "", on_ring_stop); break;
        case UI_SVC_TM_FIRE_POMODORO_PHASE: /* in-page only; no global overlay */ break;
        default: break;
    }
}
#endif /* UI_FEATURE_TIME */
#endif /* UI_WUKONG_PAGES */

/* Board UI provider 注册（单 slot：每次构建只编入一个 board）。注册后 override 默认
 * wukong 路径；不注册则 ui_app_init 走默认完整路径。 */
static ui_board_init_fn s_board_init = NULL;
static ui_board_msg_fn  s_board_msg  = NULL;
static volatile bool    s_ui_app_ready = false;
static char             s_nfc_confirm_uuid[33]; /* UI-thread only, matches active prompt */

typedef struct {
    char uuid[33];
} nfc_detect_event_t;

void ui_app_register_board_ui(ui_board_init_fn init, ui_board_msg_fn msg_handler)
{
    s_board_init = init;
    s_board_msg  = msg_handler;
}

/* 适配 shim（仅 board 专属 handler 用）：总线回调签名 (u8*,len,type) →
 * 组装 TY_DISPLAY_MSG_T → 持 LVGL 锁转发 board handler。
 * 默认 wukong 的 ui_display_msg_handler 自持锁、不经此 shim。 */
static OPERATE_RET ui_board_bus_cb(UINT8_T *data, INT_T len, TY_DISPLAY_TYPE_E tp)
{
    if (s_board_msg) {
        TY_DISPLAY_MSG_T m = { .type = tp, .len = (UINT_T)len, .data = data };
        ui_port_lock();
        s_board_msg(&m);
        ui_port_unlock();
    }
    return OPRT_OK;
}

void ui_app_init(void)
{
    tuya_boot_splash_stop();   /* 停掉开机动画线程,避免与 LVGL 并发 flush */
    uint16_t hor = tuya_display_hw_get_width();
    uint16_t ver = tuya_display_hw_get_height();

    ui_port_cfg_t port_cfg = {
        .disp_handle = tuya_display_hw_get_handle(),
        .hor_res = hor,
        .ver_res = ver,
    };
    ui_port_init(&port_cfg);

    ui_adaptive_init(hor, ver);
    ui_theme_init();
    ui_i18n_init(UI_LANG_ZH_CN);
    ui_state_init();
    ui_tick_init();

    tuya_ai_display_init();   /* 从 tuya_app_main.c 收口：UI 框架内部总线 */

    if (s_board_init) {
        s_board_init();          /* board 专属 UI（EYES/ROBOT/EVB），override 默认路径 */
    }
#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES
    else {
        ui_app_wukong_init();    /* 默认：完整 wukong 路径（T5AI_BOARD/DESKTOP，无需注册）*/
    }
#endif

    ui_port_start();
    s_ui_app_ready = true;
    tuya_display_hw_backlight_open();

    /* 消息处理器注册放在 port_start 之后，与原版顺序一致（T5AI_BOARD 零行为变化）*/
    if (s_board_init) {
        if (s_board_msg) {
            tuya_ai_display_msg_handler_register(ui_board_bus_cb);  /* 加锁 shim */
        }
    }
#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES
    else {
        tuya_ai_display_ui_register();   /* wukong 消息注册 + online 同步（stub，不动）*/
        ui_app_async_call(boot_camera_check_cb, NULL);  /* 开机自检:摄像头不可用则提示 */
    }
#endif
}

#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES && defined(UI_FEATURE_CALL) && UI_FEATURE_CALL
static void nfc_add_contact_result_ui_cb(void *data)
{
    bool ok = ((uintptr_t)data != 0);
    ui_comp_popup_toast(ui_i18n_text(ok ? UI_TEXT_NFC_ADD_CONTACT_SUCCESS
                                       : UI_TEXT_NFC_ADD_CONTACT_FAILED),
                        2000);
}

static void nfc_add_contact_result_cb(int result)
{
    /* The popup widget dismisses the confirm overlay after invoking its callback.
     * Defer even immediate service failures so the result toast survives. */
    lv_async_call(nfc_add_contact_result_ui_cb,
                  (void *)(uintptr_t)(result == UI_SVC_CALL_CONTACT_APPLY_OK));
}

static void nfc_add_contact_confirm_cb(bool confirmed)
{
    TAL_PR_NOTICE("NFC add contact popup: %s", confirmed ? "confirm" : "cancel");

    if (!confirmed) {
        s_nfc_confirm_uuid[0] = '\0';
        return;
    }

    if (s_nfc_confirm_uuid[0] == '\0') {
        lv_async_call(nfc_add_contact_result_ui_cb, (void *)(uintptr_t)false);
        return;
    }

    ui_svc_call_contact_apply_uuid_async(s_nfc_confirm_uuid,
                                         nfc_add_contact_result_cb);
    s_nfc_confirm_uuid[0] = '\0';
}
#endif

static void nfc_card_detected_ui_cb(void *data)
{
    nfc_detect_event_t *event = (nfc_detect_event_t *)data;
    char uuid[33] = {0};
    if (event != NULL) {
        snprintf(uuid, sizeof(uuid), "%s", event->uuid);
        tal_free(event);
    }

#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES && defined(UI_FEATURE_CALL) && UI_FEATURE_CALL
    if (!ui_svc_nfc_available() || !ui_svc_nfc_switch_get()) {
        return;
    }

    bool uuid_valid = uuid[0] != '\0';
    for (size_t i = 0; uuid_valid && uuid[i] != '\0'; i++) {
        unsigned char c = (unsigned char)uuid[i];
        if (c < 0x20 || c > 0x7e) {
            uuid_valid = false;
        }
    }
    if (!uuid_valid) {
        TAL_PR_NOTICE("NFC card notification ignored: UUID payload is empty or invalid");
        if (ui_page_contacts_is_discover_active()) {
            ui_comp_popup_toast(ui_i18n_text(UI_TEXT_NFC_READ_FAILED), 2000);
        }
        return;
    }

    const char *self_uuid = ui_svc_devinfo_uuid();
    if (self_uuid != NULL && self_uuid[0] != '\0' &&
        strcmp(uuid, self_uuid) == 0) {
        TAL_PR_NOTICE("NFC card detected self UUID, skip popup: card UUID=%s local UUID=%s",
                      uuid, self_uuid);
        return;
    }

    if (ui_page_contacts_is_discover_active()) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_NFC_CARD_DETECTED), 2000);
        return;
    }

    char msg[96];
    snprintf(msg, sizeof(msg), "%s\nUUID: %s",
             ui_i18n_text(UI_TEXT_NFC_ADD_CONTACT_CONFIRM),
             uuid);

    snprintf(s_nfc_confirm_uuid, sizeof(s_nfc_confirm_uuid), "%s", uuid);

    ui_comp_popup_show(UI_COMP_POPUP_CONFIRM, NULL,
                       msg,
                       nfc_add_contact_confirm_cb);
#else
    TAL_PR_NOTICE("NFC card UI notification ignored: contacts UI is not enabled");
#endif
}

void ui_app_notify_nfc_card_detected(const char *uuid)
{
    if (!s_ui_app_ready) {
        TAL_PR_NOTICE("NFC card UI notification ignored: UI is not ready");
        return;
    }
    nfc_detect_event_t *event = (nfc_detect_event_t *)tal_malloc(sizeof(*event));
    if (event == NULL) {
        TAL_PR_ERR("NFC card UI notification dropped: alloc failed");
        return;
    }
    memset(event, 0, sizeof(*event));
    if (uuid != NULL) {
        snprintf(event->uuid, sizeof(event->uuid), "%s", uuid);
    }
    ui_app_async_call(nfc_card_detected_ui_cb, event);
}

#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES
/* 默认 wukong 完整页面集路径 —— 内容/顺序与改动前 ui_app_init 一致（不含消息注册）*/
void ui_app_wukong_init(void)
{
    ui_route_init(UI_PAGE_HOME);
    ui_route_register(&ui_page_home_entry);
    ui_route_register(&ui_page_chat_entry);
    ui_route_register(&ui_page_pulldown_entry);
    ui_route_register(&ui_page_app_center_entry);
    ui_route_register(&ui_page_settings_entry);
    ui_route_register(&ui_page_about_entry);
    ui_route_register(&ui_page_ota_entry);
    ui_route_register(&ui_page_mode_entry);
    ui_route_register(&ui_page_wlan_entry);
    ui_route_register(&ui_page_activation_entry);
#if defined(UI_FEATURE_CALL) && UI_FEATURE_CALL
    ui_route_register(&ui_page_call_entry);
    ui_route_register(&ui_page_contacts_entry);
#endif
#if defined(UI_FEATURE_CAMERA) && UI_FEATURE_CAMERA
    ui_route_register(&ui_page_photo_entry);
    ui_route_register(&ui_page_camera_entry);
#endif
#if defined(UI_FEATURE_VIDEO) && UI_FEATURE_VIDEO
    ui_route_register(&ui_page_video_entry);
#endif
#if defined(UI_FEATURE_MUSIC) && UI_FEATURE_MUSIC
    ui_route_register(&ui_page_music_entry);
    ui_route_register(&ui_page_music_list_entry);
#endif
#if defined(UI_FEATURE_TIME) && UI_FEATURE_TIME
    ui_route_register(&ui_page_clock_entry);
    ui_route_register(&ui_page_schedule_entry);
#endif
#if defined(UI_FEATURE_RECORDING) && UI_FEATURE_RECORDING
    ui_route_register(&ui_page_recording_entry);
    ui_route_register(&ui_page_recording_list_entry);
    ui_route_register(&ui_page_recording_transcribe_entry);
#endif
#if defined(UI_FEATURE_FILES) && UI_FEATURE_FILES
    ui_route_register(&ui_page_files_entry);
#endif
#if defined(UI_FEATURE_DETECTION) && UI_FEATURE_DETECTION
    ui_route_register(&ui_page_detection_entry);
#endif
#if defined(UI_FEATURE_AUDIO_DIAG) && UI_FEATURE_AUDIO_DIAG
    ui_route_register(&ui_page_audio_diag_entry);
#endif
    ui_route_register(&ui_page_diag_entry);
    ui_route_register(&ui_page_sys_status_entry);
    ui_route_register(&ui_page_net_status_entry);
    ui_route_register(&ui_page_screen_test_entry);

    ui_comp_statusbar_init();
    ui_page_pulldown_init();
    ui_services_init();
#if defined(UI_FEATURE_TIME) && UI_FEATURE_TIME
    ui_svc_tm_set_fire_cb(on_tm_fire);   /* after ui_services_init -> ui_svc_tm_init */
#endif

    ui_route_set_change_cb(on_route_change);
    ui_route_push(UI_PAGE_HOME);

    /* 开机动画已在 ui_app_init 首行阻塞播完。未激活设备在 HOME 之上叠一层
     * 单页激活向导（语言 → 激活方式 → [WLAN 中转] → 二维码），向导终点
     * 通过 push(HOME) 收回到主页。 */
    if (!ui_svc_activate_is_activated()) {
        ui_route_push(UI_PAGE_ACTIVATION);
    }

    /* Register only after the root/activation route is seeded. Catch up from
     * the cached state in case OTA started while the UI was initializing. */
    ui_svc_ota_set_global_cb(on_ota_change);
    on_ota_change(ui_svc_ota_is_upgrading());
}
#endif /* UI_WUKONG_PAGES */

void ui_app_async_call(ui_app_async_cb_t cb, void *data)
{
    if (!cb) return;
    ui_port_lock();
    lv_async_call((lv_async_cb_t)cb, data);
    ui_port_unlock();
}
