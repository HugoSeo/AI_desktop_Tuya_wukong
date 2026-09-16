/**
 * @file tuya_ai_display_stub.c
 * @brief New UI framework's display message handler registration
 *
 * This file bridges the generic display dispatch layer (miscs/display/)
 * and the new UI framework's page system. It registers a handler that
 * acquires the LVGL lock and forwards messages to the active page.
 */

#include "tuya_app_config.h"   /* UI_WUKONG_PAGES */

/* This stub is the standard Wukong UI's display-message handler — it routes
 * business messages to the wukong pages (chat/detection). It is only needed
 * when the Wukong page set is built; board-UI boards use their own handler via
 * ui_app_register_board_ui(), so the whole file compiles empty when off. */
#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES

#include "tuya_ai_display.h"
#include "uni_log.h"
#include "wukong_ai_mode.h"
#include "tuya_ai_toy.h"
#include "ui_port.h"
#include "ui_state.h"
#include "ui_route.h"
#include "ui_page_ids.h"

extern void ui_page_chat_on_msg(const uint8_t *msg, int len, int display_tp);
#if defined(UI_FEATURE_DETECTION) && UI_FEATURE_DETECTION
extern void ui_page_detection_on_msg(const uint8_t *msg, int len, int display_tp);
#endif

static void update_chat_state(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp)
{
    uint8_t old_status = ui_state_get_chat()->chat_status;

    switch (display_tp) {
    case TY_DISPLAY_TP_STAT_LISTEN:
        ui_state_set_chat_status(AI_CHAT_LISTEN);
        break;
    case TY_DISPLAY_TP_STAT_SPEAK:
    case TY_DISPLAY_TP_AI_CHAT_START:
        ui_state_set_chat_status(AI_CHAT_SPEAK);
        break;
    case TY_DISPLAY_TP_STAT_IDLE:
    case TY_DISPLAY_TP_AI_CHAT_STOP:
        ui_state_set_chat_status(AI_CHAT_IDLE);
        break;
    case TY_DISPLAY_TP_CHAT_STAT:
        if (msg && len >= 1) {
            ui_state_set_chat_status(msg[0]);
        }
        break;
    case TY_DISPLAY_TP_CHAT_MODE:
    case TY_DISPLAY_TP_MODE_NOTIFY:
        ui_state_set_device_mode((uint8_t)tuya_ai_toy_device_mode_get());
        ui_state_set_chat_sub_mode((uint8_t)tuya_ai_toy_trigger_mode_get());
        break;
    default:
        break;
    }

    uint8_t new_status = ui_state_get_chat()->chat_status;
    if (old_status <= AI_CHAT_IDLE && new_status > AI_CHAT_IDLE) {
        if (ui_route_current() == UI_PAGE_HOME) {
            ui_route_push(UI_PAGE_CHAT);
        }
    }
}

static OPERATE_RET ui_display_msg_handler(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp)
{
    PR_INFO("ui display: display_tp = %d\n", display_tp);
    switch (display_tp) {
    case TY_DISPLAY_TP_HUMAN_CHAT:
    case TY_DISPLAY_TP_AI_CHAT_START:
    case TY_DISPLAY_TP_AI_CHAT_DATA:
    case TY_DISPLAY_TP_AI_CHAT_STOP:
    case TY_DISPLAY_TP_EMOJI:
    case TY_DISPLAY_TP_STAT_LISTEN:
    case TY_DISPLAY_TP_STAT_SPEAK:
    case TY_DISPLAY_TP_STAT_IDLE:
    case TY_DISPLAY_TP_CHAT_MODE:
    case TY_DISPLAY_TP_MODE_NOTIFY:
    case TY_DISPLAY_TP_CHAT_STAT:
    case TY_DISPLAY_TP_CLEAR_ATTACHMENT:
        ui_port_lock();
        update_chat_state(msg, len, display_tp);
        ui_page_chat_on_msg(msg, len, (int)display_tp);
        ui_port_unlock();
        break;
    case TY_DISPLAY_TP_AI_IMAGE:
        /* An AI-saved picture (e.g. the 一键总结 summary result) landed in the
         * album. Route by the page on top: the detection record page shows a
         * "go to album" toast; everywhere else keeps the chat "查看图片" link. */
        ui_port_lock();
#if defined(UI_FEATURE_DETECTION) && UI_FEATURE_DETECTION
        if (ui_route_current() == UI_PAGE_DETECTION) {
            ui_page_detection_on_msg(msg, len, (int)display_tp);
        } else
#endif
        {
            update_chat_state(msg, len, display_tp);
            ui_page_chat_on_msg(msg, len, (int)display_tp);
        }
        ui_port_unlock();
        break;
    case TY_DISPLAY_TP_STAT_NET:
        /* data[0] != 0 → MQTT connected; mirror view module __handle_stat_net. */
        PR_INFO("ui display: STAT_NET len=%d val=%d", len, (msg && len >= 1) ? msg[0] : -1);
        if (msg && len >= 1) {
            ui_state_set_online(msg[0] != 0);
        }
        break;
    case TY_DISPLAY_TP_STAT_ONLINE:
        /* Cloud connection is up — mark online unconditionally. */
        PR_INFO("ui display: STAT_ONLINE");
        ui_state_set_online(true);
        break;
    case TY_DISPLAY_TP_STAT_NETCFG:
        /* Entering network config mode — mark offline. */
        PR_INFO("ui display: STAT_NETCFG");
        ui_state_set_online(false);
        break;
    case TY_DISPLAY_TP_VOLUME:
        /* External volume change (cloud/app DP dpid 3, or hardware net key ±).
         * Volume is global SYSTEM state (consumed by the pulldown slider), so
         * update ui_state directly like STAT_NET/ONLINE — thread-safe scalar
         * write + dirty OR, no ui_port_lock needed (no LVGL touched here). */
        if (msg && len >= 1) {
            PR_INFO("ui display: VOLUME = %u", msg[0]);
            ui_state_set_volume(msg[0]);
        }
        break;
    default:
        break;
    }
    return OPRT_OK;
}

void tuya_ai_display_ui_register(void)
{
    tuya_ai_display_msg_handler_register(ui_display_msg_handler);

    /* Proactively sync online status: the STAT_NET / STAT_ONLINE display
     * messages may have fired before this handler was registered (the
     * mqtt_thread runs in parallel with tuya_ai_toy_init).  Query the
     * authoritative source so camera capture doesn't skip the AI upload
     * due to a stale is_online == false. */
    bool online = tuya_ai_toy_is_cloud_connected();
    PR_INFO("ui display: proactive online sync -> %d", online ? 1 : 0);
    ui_state_set_online(online);
}

#endif /* UI_WUKONG_PAGES */
