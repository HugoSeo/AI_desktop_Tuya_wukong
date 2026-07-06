/**
 * @file tuya_ai_toy.c
 * @brief Tuya AI toy (Wukong) core: init, DP process, config load/save, key/led/network,
 *        wukong agent, idle and low-power timers, and public API (trigger mode, volume, timers).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tuya_ai_toy.h"
#include "tuya_ai_agent.h"
#include "tuya_device_cfg.h"
#include "tuya_ai_toy_led.h"
#include "tuya_ai_toy_key.h"
#include "tuya_ai_toy_camera.h"
#include "skill_cloudevent.h"
#include "skill_emotion.h"
#include "skill_music_story.h"
#include "wukong_ai_skills.h"
#include "wukong_audio_input.h"
#include "wukong_audio_player.h"
#include "wukong_audio_aec_vad.h"
#include "wukong_ai_agent.h"
#include "wukong_kws.h"
#include "tuya_ws_db.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_thread.h"
#include "tkl_audio.h"
#include "tuya_ringbuf.h"
#include "tuya_devos_utils.h"
#include "tal_semaphore.h"
#include "tal_queue.h"
#include "tal_system.h"
#include "tal_network.h"
#include "tal_mutex.h"
#include "tkl_thread.h"
#include "tal_sw_timer.h"
#include "tal_workq_service.h"
#include "base_event.h"
#include "tuya_iot_com_api.h"
#include "media_src.h"
#include "tuya_key.h"
#include "tuya_ai_client.h"
#include "tuya_ai_biz.h"
#include "tkl_wakeup.h"
#include "audio_dump.h"
#if defined(ENABLE_BATTERY) && (ENABLE_BATTERY == 1)
#include "tuya_ai_battery.h"
#endif
#include "tal_sleep.h"
#include "wukong_ai_mode.h"
#include "svc_ai_player.h"
#include "wukong_ai_mcp.h"
#include "wukong_tm.h"
#include "wukong_cron.h"
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#include "tkl_display.h"
#include "tuya_ai_display.h"
#include "tuya_app_gui_gw_core0.h"
#endif
#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
#include "tuya_iot_wifi_api.h"
#include "tuya_wifi_status.h"
#endif
#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
#include "tuya_ai_monitor.h"
#endif
#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)
#include "wukong_video_input.h"
#include "tuya_ai_toy_camera.h"
#if defined(T5AI_BOARD_DESKTOP) && (T5AI_BOARD_DESKTOP == 1)
#include "tuya_device_camera.h"
#endif
#if defined(ENABLE_MQTT_P2P) && (ENABLE_MQTT_P2P == 1)
#include "tuya_p2p_app.h"
#include "tuya_sdk_call.h"
#endif
#endif

#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
#include "wukong_picture.h"
#endif

#if defined(ENABLE_AUDIO_ANALYSIS) && (ENABLE_AUDIO_ANALYSIS == 1)
#include "audio_analysis.h"
#endif
#include "tkl_gpio.h"

// #include "hugo_ai_desktop.h"
/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define AI_TOY_PARA           "ai_toy_para"
#define LONG_KEY_TIME         400
#define SEQ_KEY_TIME          200
#define TOY_IDLE_TIMEOUT      (30 * 1000)       /* 30 sec */
#define TOY_LOWPOWER_TIMEOUT  (60 * 1000)  /* 30 min */

#define MAX_INPUT_RINGBUG_SIZE  (128 * 1024)
#define MAX_INPUT_BUF_SIZE      (16 * 1024)

#define AI_AUDIO_SLICE_TIME    80   /* Audio slice per output, 80 ms */
#define TOY_VOLUME_SETUP       20   /* Volume step */

#define AI_TOY_ALERT_PLAY_ID   "ai_toy_alert"

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC TY_AI_TOY_T *s_ai_toy = NULL;
STATIC UINT8_T      s_lang = TY_AI_DEFAULT_LANG;
STATIC INT_T        __s_wakeup_flag = 0;

/* ---------------------------------------------------------------------------
 * Config and report helpers
 * --------------------------------------------------------------------------- */

/** Report volume DP (dpid 3) to cloud. */
STATIC OPERATE_RET __ai_toy_report_volum(VOID)
{
    CHAR_T *devid = tuya_iot_get_gw_id();
    /* Build volume DP: dpid 3, type PROP_VALUE, value 0~100. */
    TY_OBJ_DP_S dp = {
        .dpid = 3,
        .type = PROP_VALUE,
        .value.dp_value = s_ai_toy->volume,
    };
    return tuya_report_dp_async(devid, &dp, 1, NULL);
}

/** Save volume and trigger_mode to KV (AI_TOY_PARA). */
STATIC OPERATE_RET __ai_toy_config_save(VOID)
{
    TAL_PR_NOTICE("ai toy -> save config");

    OPERATE_RET rt = OPRT_OK;
    CHAR_T buf[64] = {0};
    /* Serialise volume and trigger_mode as JSON and write to KV key AI_TOY_PARA. */
    snprintf(buf, sizeof(buf), "{\"volume\": %d, \"trigger_mode\":%d, \"device_mode\":%d}", 
        s_ai_toy->volume, s_ai_toy->cfg.trigger_mode, s_ai_toy->cfg.device_mode);
    TUYA_CALL_ERR_RETURN(wd_common_write(AI_TOY_PARA, (CONST BYTE_T *)buf, strlen(buf)));

    TAL_PR_DEBUG("save ai_toy config: %s", buf);
    return rt;
}

/** Load volume and trigger_mode from KV; use defaults if read fails. */
STATIC OPERATE_RET __ai_toy_config_load(VOID)
{
    TAL_PR_NOTICE("ai toy -> load config");
    OPERATE_RET rt = OPRT_OK;
    BYTE_T *value = NULL;
    UINT_T len = 0;

    /* Default volume before reading KV. */
    s_ai_toy->volume = TY_SPK_DEFAULT_VOL;

    TUYA_CALL_ERR_RETURN(wd_common_read(AI_TOY_PARA, &value, &len));
    TAL_PR_DEBUG("read ai_toy config: %s", value);
    ty_cJSON *root = ty_cJSON_Parse((CONST CHAR_T *)value);
    wd_common_free_data(value);
    TUYA_CHECK_NULL_RETURN(root, OPRT_FILE_READ_FAILED);

    /* Parse "volume" (0~100). */
    ty_cJSON *volum = ty_cJSON_GetObjectItem(root, "volume");
    if (volum) {
        if (volum->valueint <= 100 && volum->valueint >= 0) {
            s_ai_toy->volume = volum->valueint;
        }
    }

    /* Parse "trigger_mode" (AI_CHAT_SUB_HOLD ~ AI_CHAT_SUB_FREE). */
    ty_cJSON *trigger_mode = ty_cJSON_GetObjectItem(root, "trigger_mode");
    if (trigger_mode) {
        if (trigger_mode->valueint <= AI_CHAT_SUB_FREE && trigger_mode->valueint >= AI_CHAT_SUB_HOLD) {
            s_ai_toy->cfg.trigger_mode = trigger_mode->valueint;
        }
    }

    /* Parse "device_mode"; backward compat: old KV used 4~9, new enum is 0~5. */
    ty_cJSON *device_mode = ty_cJSON_GetObjectItem(root, "device_mode");
    if (device_mode) {
        INT_T val = device_mode->valueint;
        if (val >= 4 && val <= 9) {
            s_ai_toy->cfg.device_mode = (AI_DEVICE_MODE_E)(val - 4);
        } else if (val >= 0 && val < AI_DEVICE_MODE_MAX) {
            s_ai_toy->cfg.device_mode = (AI_DEVICE_MODE_E)val;
        } else {
            s_ai_toy->cfg.device_mode = AI_DEVICE_MODE_CHAT;
        }
    } else {
        s_ai_toy->cfg.device_mode = AI_DEVICE_MODE_CHAT;
    }

    ty_cJSON_Delete(root);
    return OPRT_OK;
}

/** Forward mic data to wukong audio input. */
STATIC INT_T __on_ai_toy_mic_data(UINT8_T *data, UINT16_T datalen)
{
    return wukong_ai_mode_dispatch(AI_MODE_OP_AUDIO_INPUT, data, (INT_T)datalen);
}

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
/** WiFi network status: update language from region/country code, LED and display, report DP on first cloud connect. */
STATIC VOID __on_ai_toy_wf_nw_stat_cb(GW_WIFI_NW_STAT_E nw_stat)
{
    /* Derive language: 0 = Chinese, 1 = English from GW region or WiFi country code ("CN" -> Chinese). */
    CONST CHAR_T *region = get_gw_region();
    if (0 == strlen(region)) {
        CHAR_T ccode[COUNTRY_CODE_LEN] = {0};
        tal_wifi_get_country_code(ccode);
        s_lang = (NULL != strstr(ccode, "CN") ? 0 : TY_AI_DEFAULT_LANG);
#if defined(ENABLE_TUYA_UI)
        tuya_ai_display_msg(&s_lang, 1, TY_DISPLAY_TP_LANGUAGE);
#endif
        TAL_PR_DEBUG("network status = %d, ccode %s, language %d", nw_stat, ccode, s_lang);
    } else {
        s_lang = (NULL != strstr(region, "AY") ? 0 : TY_AI_DEFAULT_LANG);
#if defined(ENABLE_TUYA_UI)
        tuya_ai_display_msg(&s_lang, 1, TY_DISPLAY_TP_LANGUAGE);
#endif
        TAL_PR_DEBUG("network status = %d, region %s, language %d", nw_stat, region, s_lang);
    }

    UINT8_T net_stat = 0;
    STATIC BOOL_T report_flag = FALSE;
    switch (nw_stat) {
    case STAT_UNPROVISION_AP_STA_UNCFG:
        /* Unprovisioned: show netconfig UI, LED flash 200 ms. */
#if defined(ENABLE_TUYA_UI)
        tuya_ai_display_msg(NULL, 0, TY_DISPLAY_TP_STAT_NETCFG);
        tuya_ai_display_msg(&net_stat, 1, TY_DISPLAY_TP_STAT_NET);
#endif
        tuya_ai_toy_led_flash(200);
        break;

    case STAT_STA_DISC:
        /* WiFi disconnected: update display, LED off. */
#if defined(ENABLE_TUYA_UI)
        tuya_ai_display_msg(&net_stat, 1, TY_DISPLAY_TP_STAT_NET);
#endif
        tuya_ai_toy_led_off();
        break;

    case STAT_CLOUD_CONN:
        /* First time cloud connected: report volume DP once; then set net_stat=1, LED on. */
        if (!report_flag) {
            __ai_toy_report_volum();
            report_flag = TRUE;
        }
        net_stat = 1;
        #ifdef ENABLE_TUYA_UI  
        tuya_ai_display_msg(&net_stat, 1, TY_DISPLAY_TP_STAT_NET);
        #endif
        tuya_ai_toy_led_on();
        break;
    }

    s_ai_toy->nw_stat = nw_stat;
}
#endif

/* ---------------------------------------------------------------------------
 * Wukong AI event and task
 * --------------------------------------------------------------------------- */

/** Downlink event from wukong: forward to current mode. */
STATIC VOID __on_ai_toy_wukong_ai_event(WUKONG_AI_EVENT_T *event)
{
    wukong_ai_mode_dispatch(AI_MODE_OP_EVENT, event, 0);
}

/** Main AI toy task loop: handle wukong task and sleep. */
STATIC VOID __ai_toy_task(VOID *args)
{
    while (tal_thread_get_state(s_ai_toy->toy_task) == THREAD_STATE_RUNNING) {
        wukong_ai_mode_dispatch(AI_MODE_OP_TASK, NULL, 0);
        tal_system_sleep(20);
    }
}

/** KWS wakeup event: set wakeup index and forward to wukong. */
STATIC OPERATE_RET __on_ai_toy_audio_kws(VOID_T *data)
{
    if (data) {
        INT_T idx = (INT_T)data;
        __s_wakeup_flag = idx;
    }

    return wukong_ai_mode_dispatch(AI_MODE_OP_WAKEUP, NULL, 0);
}

/** VAD state change: forward to wukong. */
STATIC OPERATE_RET __on_ai_toy_vad_change(VOID *data)
{
    WUKONG_AUDIO_VAD_FLAG_E vad_flag = WUKONG_AUDIO_VAD_STOP;

    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);

    vad_flag = (WUKONG_AUDIO_VAD_FLAG_E)(uintptr_t)data;
    if (vad_flag != WUKONG_AUDIO_VAD_START && vad_flag != WUKONG_AUDIO_VAD_STOP) {
        TAL_PR_ERR("invalid vad flag from event bus: %u", (UINT_T)(uintptr_t)data);
        return OPRT_INVALID_PARM;
    }

    return wukong_ai_mode_dispatch(AI_MODE_OP_VAD, &vad_flag, sizeof(vad_flag));
}

STATIC OPERATE_RET __on_ai_toy_reset(VOID *data)
{
    wd_common_delete(AI_TOY_PARA);
    return OPRT_OK;
}

/** Work queue callback: create default AI session asynchronously. */
STATIC VOID_T __on_ai_toy_crt_default_session(VOID_T *data)
{
    OPERATE_RET rt = tuya_ai_agent_crt_session(tuya_ai_agent_get_scode(NULL), 0, 0, NULL, 0);
    if (OPRT_OK != rt) {
        TAL_PR_ERR("ai toy -> create default session failed, rt: %d", rt);
    }
}

/** AI client connected: schedule default session creation and forward to wukong. */
STATIC OPERATE_RET __on_ai_toy_ai_client_run(VOID_T *data)
{
    TAL_PR_NOTICE("ai toy -> connected to server");

#if defined(ENABLE_DEFAULT_SESSION) && (ENABLE_DEFAULT_SESSION == 1)
    /* Create default session asynchronously via work queue. */
    OPERATE_RET rt = tal_workq_schedule(WORKQ_SYSTEM, __on_ai_toy_crt_default_session, NULL);
    if (OPRT_OK != rt) {
        TAL_PR_ERR("ai toy -> schedule default session creation failed, rt: %d", rt);
    }
#endif

    wukong_ai_mode_dispatch(AI_MODE_OP_CLIENT, NULL, 0);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Wukong agent init/deinit and OTA callbacks
 * --------------------------------------------------------------------------- */

STATIC OPERATE_RET __ai_toy_wukong_ai_agent_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    /* Register downlink event callback and init player. */
    TUYA_CALL_ERR_LOG(wukong_ai_agent_init(__on_ai_toy_wukong_ai_event));
    TUYA_CALL_ERR_LOG(wukong_audio_player_init());

    /* Audio input: board (16k, mono, 80 ms slice, VAD by trigger mode) or UART. */
    WUKONG_AUDIO_INPUT_CFG_T audio_cfg = {0};
#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
    audio_cfg.type = WUKONG_AUDIO_USING_BOARD;
    audio_cfg.board.sample_rate    = TKL_AUDIO_SAMPLE_16K;
    audio_cfg.board.sample_bits    = TKL_AUDIO_DATABITS_16;
    audio_cfg.board.channel        = TKL_AUDIO_CHANNEL_MONO;
    audio_cfg.board.slice_ms       = AI_AUDIO_SLICE_TIME;
    /* HOLD trigger -> manual VAD; otherwise auto VAD (e.g. one-shot, free talk). */
    audio_cfg.board.vad_mode       = (s_ai_toy->cfg.trigger_mode == AI_CHAT_SUB_HOLD) ? WUKONG_AUDIO_VAD_MANUAL : WUKONG_AUDIO_VAD_AUTO;
    audio_cfg.board.vad_off_ms     = 1000;
    audio_cfg.board.vad_active_ms  = 500;
    audio_cfg.board.spk_io         = s_ai_toy->cfg.spk_en_pin;
    audio_cfg.board.spk_io_level   = TUYA_GPIO_LEVEL_LOW;
    audio_cfg.board.output_cb      = __on_ai_toy_mic_data;
#elif defined(USING_UART_AUDIO_INPUT) && (USING_UART_AUDIO_INPUT == 1)
    audio_cfg.type = WUKONG_AUDIO_USING_UART;
    audio_cfg.uart.mic_upload = __on_ai_toy_mic_data;
#endif
    TUYA_CALL_ERR_LOG(wukong_audio_input_init(&audio_cfg));

    wukong_audio_player_set_vol(s_ai_toy->volume);
    // TUYA_CALL_ERR_LOG(wukong_kws_default_init());       //Deleted by Hugo
    return rt;
}

STATIC OPERATE_RET __ai_toy_wukong_ai_agent_deinit(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_LOG(wukong_audio_input_deinit());
    TUYA_CALL_ERR_LOG(wukong_audio_player_deinit());
    TUYA_CALL_ERR_LOG(wukong_ai_agent_deinit());

    return rt;
}

STATIC INT_T __on_ai_toy_ota_process_cb(VOID_T *data)
{
    TAL_PR_NOTICE("ota process, stop audio...");
    __ai_toy_wukong_ai_agent_deinit();
    return 0;
}

/** OTA failed: restart system. */
STATIC INT_T __on_ai_toy_ota_fail_cb(VOID_T *data)
{
    TAL_PR_NOTICE("ota fail, restart system...");
    tal_system_reset();
    return 0;
}

/** Unsubscribe events, delete task, deinit MCP/monitor and wukong agent. */
STATIC OPERATE_RET __ai_toy_stop(VOID)
{
    TAL_PR_NOTICE("ai toy -> stop");
    OPERATE_RET rt = OPRT_OK;

    /* Unsubscribe all events registered in __ai_toy_start. */
    ty_unsubscribe_event(EVENT_OTA_PROCESS_NOTIFY, "ai_toy", __on_ai_toy_ota_process_cb);
    ty_unsubscribe_event(EVENT_OTA_FAILED_NOTIFY,  "ai_toy", __on_ai_toy_ota_fail_cb);
    ty_unsubscribe_event(EVENT_AI_CLIENT_RUN,      "ai_toy", __on_ai_toy_ai_client_run);
    ty_unsubscribe_event(EVENT_WUKONG_KWS_WAKEUP,  "ai_toy", __on_ai_toy_audio_kws);
    ty_unsubscribe_event(EVENT_AUDIO_VAD,          "ai_toy", __on_ai_toy_vad_change);
    ty_unsubscribe_event(EVENT_RESET,              "ai_toy", __on_ai_toy_reset);

    if (s_ai_toy->toy_task) {
        TUYA_CALL_ERR_LOG(tal_thread_delete(s_ai_toy->toy_task));
    }

#if defined(ENABLE_TUYA_TOOLKITS) && (ENABLE_TUYA_TOOLKITS == 1)
    TUYA_CALL_ERR_LOG(wukong_ai_mcp_deinit());
#endif

    TUYA_CALL_ERR_LOG(wukong_time_manage_deinit());
    TUYA_CALL_ERR_LOG(wukong_cron_deinit());

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    TUYA_CALL_ERR_LOG(tuya_ai_monitor_deinit());
#endif

    TUYA_CALL_ERR_LOG(__ai_toy_wukong_ai_agent_deinit());
    TAL_PR_NOTICE("ai toy -> stop success");
    return rt;
}

/** Subscribe events, init wukong agent, register WF NW cb, start monitor/MCP/mode and task. */
STATIC OPERATE_RET __ai_toy_start(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    /* Subscribe OTA, AI client, KWS wakeup, VAD. */
    ty_subscribe_event(EVENT_OTA_PROCESS_NOTIFY, "ai_toy", __on_ai_toy_ota_process_cb, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_OTA_FAILED_NOTIFY,  "ai_toy", __on_ai_toy_ota_fail_cb, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_AI_CLIENT_RUN,      "ai_toy", __on_ai_toy_ai_client_run, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_WUKONG_KWS_WAKEUP,  "ai_toy", __on_ai_toy_audio_kws, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_AUDIO_VAD,          "ai_toy", __on_ai_toy_vad_change, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_RESET,              "ai_toy", __on_ai_toy_reset, SUBSCRIBE_TYPE_NORMAL);

    TUYA_CALL_ERR_GOTO(__ai_toy_wukong_ai_agent_init(), __error);
#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
    tuya_iot_reg_get_wf_nw_stat_cb(__on_ai_toy_wf_nw_stat_cb);
#endif

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    ai_monitor_config_t monitor_cfg = AI_MONITOR_CFG_DEFAULT;
    TUYA_CALL_ERR_GOTO(tuya_ai_monitor_init(&monitor_cfg), __error);
#endif
#if defined(ENABLE_TUYA_TOOLKITS) && (ENABLE_TUYA_TOOLKITS == 1)
    TUYA_CALL_ERR_GOTO(wukong_ai_mcp_init(), __error);
#endif
    TUYA_CALL_ERR_GOTO(wukong_cron_init(), __error);
    TUYA_CALL_ERR_GOTO(wukong_time_manage_init(), __error);
    TUYA_CALL_ERR_GOTO(wukong_ai_mode_init(), __error);

    /* Create and start the AI toy state task (runs __ai_toy_task). */
    THREAD_CFG_T thrd_cfg = {
        .priority = THREAD_PRIO_5,
        .stackDepth = 3* 1024,
        .thrdname = "ai_toy_state",
        #ifdef ENABLE_EXT_RAM
        .psram_mode = 1,
        #endif            
    };
    TUYA_CALL_ERR_GOTO(tal_thread_create_and_start(&s_ai_toy->toy_task, NULL, NULL, __ai_toy_task, NULL, &thrd_cfg), __error);


    TAL_PR_NOTICE("ai toy -> start success");
    return OPRT_OK;

__error:
    TAL_PR_ERR("ai toy -> start failed, stop ai agent");
    __ai_toy_stop();
    return rt;
}

/* ---------------------------------------------------------------------------
 * Idle and low-power timers
 * --------------------------------------------------------------------------- */

/** Idle timer: if not playing, notify idle; else restart timer (TOY_IDLE_TIMEOUT ms). */
STATIC VOID __on_ai_toy_idle_timer(TIMER_ID timer_id, VOID_T *arg)
{
    TAL_PR_NOTICE("ai toy -> idle timer out, will check and enter idle status");
    if (wukong_audio_player_is_playing()) {
        TAL_PR_NOTICE("ai toy -> player is playing, idle timer reset");
        tal_sw_timer_start(s_ai_toy->idle_timer, TOY_IDLE_TIMEOUT, TAL_TIMER_ONCE);
        return;
    }
    /* No playback: notify wukong to enter idle state. */
    wukong_ai_mode_dispatch(AI_MODE_OP_NOTIFY_IDLE, NULL, 0);
}
#if defined(ENABLE_LOW_POWER) && (ENABLE_LOW_POWER == 1)
/** Configure GPIO as wakeup source (rising edge) for deep sleep. */
STATIC VOID __ai_toy_set_wakeup_source(UINT32_T pin)
{
    TAL_PR_NOTICE("ai toy -> set wakeup pin %d", pin);
    /* Config pin as input floating. */
    TUYA_GPIO_BASE_CFG_T io_cfg;
    io_cfg.direct = TUYA_GPIO_INPUT;
    io_cfg.mode = TUYA_GPIO_FLOATING;
    io_cfg.level = TUYA_GPIO_LEVEL_LOW;
    tkl_gpio_init(pin, &io_cfg);

    /* Register as wakeup source: rising edge on this GPIO wakes from deep sleep. */
    TUYA_WAKEUP_SOURCE_BASE_CFG_T cfg;
    cfg.source = TUYA_WAKEUP_SOURCE_GPIO;
    cfg.wakeup_para.gpio_param.gpio_num = pin;
    cfg.wakeup_para.gpio_param.level = TUYA_GPIO_WAKEUP_RISE;
    tkl_wakeup_source_set(&cfg);
    tal_system_sleep(200);
}
#endif

/** Exit low-power when key press, KWS not work during low-power */
STATIC VOID __on_ai_toy_key_press_exit_lowpower()
{
#if defined(ENABLE_LOW_POWER) && (ENABLE_LOW_POWER == 1)
    /* If the device was in lowpower status, exit and re-start */
    if (s_ai_toy->lp_stat) {      
        TAL_PR_NOTICE("ai toy -> key-press wakeup, exit lowpower status.");

        /* Exit Light sleep: turn on speaker and LED, init battery,  start AI agent, then disable CPU/WiFi LP. */
        tal_cpu_lp_disable();
        tal_wifi_lp_disable();

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
        /** Open LCD backlight */
        tuya_disp_lcd_backlight_open();
#endif

        /** Turn on your peripherals here. */

        __ai_toy_start();

        tkl_gpio_write(s_ai_toy->cfg.spk_en_pin, TUYA_GPIO_LEVEL_HIGH);
        tkl_gpio_write(s_ai_toy->cfg.led_pin, TUYA_GPIO_LEVEL_HIGH);
#if defined(TUYA_AI_TOY_BATTERY_ENABLE) && (TUYA_AI_TOY_BATTERY_ENABLE == 1)
        tuya_ai_toy_battery_init();
#endif
        s_ai_toy->lp_stat = FALSE;
        TAL_PR_DEBUG("tal_cpu_lp_disable");
    }
#endif
}

/** Low-power timer: if not playing and low-power enabled, enter deep sleep or light sleep. */
STATIC VOID __on_ai_toy_lowpower_timer(TIMER_ID timer_id, VOID_T *arg)
{
#if defined(ENABLE_LOW_POWER) && (ENABLE_LOW_POWER == 1)
    TAL_PR_NOTICE("ai toy -> lowpower timer out, will check and enter idle status");
    if (wukong_audio_player_is_playing()) {
        TAL_PR_NOTICE("ai toy -> player is playing, lowpower timer reset");
        tal_sw_timer_start(s_ai_toy->lowpower_timer, TOY_LOWPOWER_TIMEOUT, TAL_TIMER_ONCE);
        return;
    }

    OPERATE_RET rt = OPRT_OK;
    if (TY_AI_LOW_POWER_MODE == 1) {
        /* Deep sleep: set audio trigger pin as wakeup, then enter deep sleep. */
        __ai_toy_set_wakeup_source(s_ai_toy->cfg.audio_trigger_pin);
        tal_cpu_sleep_mode_set(1, TUYA_CPU_DEEP_SLEEP);
    } else {
        /* Light sleep: uninit battery, turn off speaker and LED, stop AI agent, then enable CPU/WiFi LP. */
#if defined(TUYA_AI_TOY_BATTERY_ENABLE) && (TUYA_AI_TOY_BATTERY_ENABLE == 1)
        tuya_ai_toy_battery_uninit();
#endif

        tkl_gpio_write(s_ai_toy->cfg.spk_en_pin, TUYA_GPIO_LEVEL_LOW);
        tkl_gpio_write(s_ai_toy->cfg.led_pin, TUYA_GPIO_LEVEL_LOW);

        __ai_toy_stop();

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
        /** Close LCD backlight */
        tuya_disp_lcd_backlight_close();
#endif

        rt = tal_cpu_lp_enable();
        rt |= tal_wifi_lp_enable();
        s_ai_toy->lp_stat = TRUE;
        TAL_PR_DEBUG("tal_cpu_lp_enable");
    }
#endif
}

/* ---------------------------------------------------------------------------
 * Key and network callbacks
 * --------------------------------------------------------------------------- */
/** Net key: normal = volume+, long = reset netconfig, seq = volume-. */
STATIC VOID __on_ai_toy_net_pin(UINT_T port, PUSH_KEY_TYPE_E type, INT_T cnt)
{
    TAL_PR_DEBUG("ai toy -> net pin pressed %d", type);

    /** Exit lowpower when key press and device was in lowpower status */
    __on_ai_toy_key_press_exit_lowpower();

    switch (type) {
    case NORMAL_KEY:
        /* Single press: volume up by TOY_VOLUME_SETUP, align to step boundary. */
        TAL_PR_DEBUG("net pin normal press trigger volume up!");
        if (s_ai_toy && s_ai_toy->volume < 100) {
            if (s_ai_toy->volume % TOY_VOLUME_SETUP) {
                s_ai_toy->volume = (s_ai_toy->volume / TOY_VOLUME_SETUP + 1) * TOY_VOLUME_SETUP;
            } else {
                s_ai_toy->volume += TOY_VOLUME_SETUP;
            }
            TAL_PR_DEBUG("volume %d", s_ai_toy->volume);
            wukong_audio_player_set_vol(s_ai_toy->volume);
            #ifdef ENABLE_TUYA_UI   
            tuya_ai_display_msg(&s_ai_toy->volume, 1, TY_DISPLAY_TP_VOLUME);
            #endif
            __ai_toy_report_volum();
        }
        break;

    case LONG_KEY:
        /* Long press: enter smart config / reset netconfig (WiFi only). */
        TAL_PR_DEBUG("net pin long press trigger Reset!");
#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
        tuya_iot_wf_gw_fast_unactive(GWCM_OLD, WF_START_SMART_AP_CONCURRENT);
#endif
        break;
    case SEQ_KEY:
        /* Double press: volume down by TOY_VOLUME_SETUP. */
        TAL_PR_DEBUG("net pin seq press trigger volume down!");
        if (s_ai_toy && s_ai_toy->volume > 0) {
            if (s_ai_toy->volume % TOY_VOLUME_SETUP) {
                s_ai_toy->volume = (s_ai_toy->volume / TOY_VOLUME_SETUP) * TOY_VOLUME_SETUP;
            } else {
                s_ai_toy->volume -= TOY_VOLUME_SETUP;
            }
            TAL_PR_DEBUG("volume %d", s_ai_toy->volume);
            wukong_audio_player_set_vol(s_ai_toy->volume);
            #ifdef ENABLE_TUYA_UI   
            tuya_ai_display_msg(&s_ai_toy->volume, 1, TY_DISPLAY_TP_VOLUME);
            #endif
            __ai_toy_report_volum();
        }
        break;
    default:
        break;
    }
}

/** Audio trigger key: seq = stop player, switch trigger mode and alert; else forward to wukong key handler. */
STATIC VOID __on_ai_toy_audio_trigger_pin(UINT_T port, PUSH_KEY_TYPE_E type, INT_T cnt)
{
    TAL_PR_DEBUG("ai toy -> audio trigger pin pressed %d", type);
    
    /** Exit lowpower status when key press and device was in lowpower status */
    __on_ai_toy_key_press_exit_lowpower();

    if (SEQ_KEY == type) {

        TAL_PR_DEBUG("[%s] trigger mode:%d, device mode:%d", __func__, s_ai_toy->cfg.trigger_mode, s_ai_toy->cfg.device_mode);
        if (s_ai_toy->cfg.device_mode != AI_DEVICE_MODE_CHAT) {
            TAL_PR_INFO("[%s] current device mode %d ====> %d", __func__, s_ai_toy->cfg.device_mode, AI_DEVICE_MODE_CHAT);
            wukong_audio_player_stop(AI_PLAYER_ALL);
            tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
            wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);
            __ai_toy_config_save();
            return;
        }

        /* Double press: stop all playback, send chat break, cycle trigger mode, save, play mode alert. */
        wukong_audio_player_stop(AI_PLAYER_ALL);
        tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
        wukong_ai_chat_sub_mode_cycle();
        __ai_toy_config_save();
        wukong_audio_player_alert(AI_TOY_ALERT_TYPE_LONG_KEY_TALK + s_ai_toy->cfg.trigger_mode, TRUE);
        return;
    }

    /* Single/long press: pass to wukong key handler (e.g. hold to talk). */
    wukong_ai_mode_dispatch(AI_MODE_OP_KEY, &type, 0);
}

/* ---------------------------------------------------------------------------
 * Public API (see tuya_ai_toy.h)
 * --------------------------------------------------------------------------- */

OPERATE_RET tuya_ai_toy_destroy(VOID)
{
    TAL_PR_NOTICE("ai toy -> destroy");
    TUYA_CHECK_NULL_RETURN(s_ai_toy, OPRT_OK);

    if (s_ai_toy->idle_timer) {
        tal_sw_timer_delete(s_ai_toy->idle_timer);
    }
    if (s_ai_toy->lowpower_timer) {
        tal_sw_timer_delete(s_ai_toy->lowpower_timer);
    }

    tal_free(s_ai_toy);
    s_ai_toy = NULL;
    TAL_PR_NOTICE("ai toy -> destroy success");
    return OPRT_OK;
}

OPERATE_RET tuya_ai_toy_init(TY_AI_TOY_CFG_T *cfg)
{
    TUYA_CHECK_NULL_RETURN(cfg, OPRT_INVALID_PARM);

    OPERATE_RET rt = OPRT_OK;
    TAL_PR_NOTICE("ai toy -> init");

    /* Alloc context: PSRAM if enabled, else heap. */
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    s_ai_toy = tal_psram_malloc(SIZEOF(TY_AI_TOY_T));
#else
    s_ai_toy = tal_malloc(SIZEOF(TY_AI_TOY_T));
#endif
    TUYA_CHECK_NULL_RETURN(s_ai_toy, OPRT_MALLOC_FAILED);
    memset(s_ai_toy, 0, SIZEOF(TY_AI_TOY_T));
    memcpy(&s_ai_toy->cfg, cfg, sizeof(TY_AI_TOY_CFG_T));
    s_ai_toy->wakeup_stat = FALSE;

    TUYA_CALL_ERR_LOG(__ai_toy_config_load());

    /* LED and two keys: audio trigger (short/long = SEQ_KEY_TIME/LONG_KEY_TIME), net (long = 10*LONG_KEY_TIME for reset). */
    TUYA_CALL_ERR_GOTO(tuya_ai_toy_led_init(s_ai_toy->cfg.led_pin), __error);
    // TUYA_CALL_ERR_GOTO(tuya_ai_toy_key_init(s_ai_toy->cfg.audio_trigger_pin, TRUE, SEQ_KEY_TIME, LONG_KEY_TIME, __on_ai_toy_audio_trigger_pin), __error);
    TUYA_CALL_ERR_GOTO(tuya_ai_toy_key_init(s_ai_toy->cfg.audio_trigger_pin, FALSE, SEQ_KEY_TIME, LONG_KEY_TIME, __on_ai_toy_audio_trigger_pin), __error);
    TUYA_CALL_ERR_GOTO(tuya_ai_toy_key_init(s_ai_toy->cfg.net_pin, TRUE, SEQ_KEY_TIME, LONG_KEY_TIME * 10, __on_ai_toy_net_pin), __error);

#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)
    TAL_PR_NOTICE("ai toy -> init camera");
    tuya_ai_toy_camera_init();
#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)             
    tuya_p2p_app_start();
    TUYA_IPC_call_init();
#endif
#endif

#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
    TUYA_CALL_ERR_LOG(wukong_picture_init());
#endif


#if defined(TUYA_AI_TOY_BATTERY_ENABLE) && (TUYA_AI_TOY_BATTERY_ENABLE == 1)
    TAL_PR_NOTICE("ai toy -> init battery");
    TUYA_CALL_ERR_LOG(tuya_ai_toy_battery_init());
#endif

    /* Idle timer: no activity for TOY_IDLE_TIMEOUT -> idle. Low-power timer: idle for TOY_LOWPOWER_TIMEOUT -> sleep. */
    TUYA_CALL_ERR_GOTO(tal_sw_timer_create(__on_ai_toy_idle_timer, s_ai_toy, &s_ai_toy->idle_timer), __error);
    TUYA_CALL_ERR_GOTO(tal_sw_timer_create(__on_ai_toy_lowpower_timer, s_ai_toy, &s_ai_toy->lowpower_timer), __error);

    TUYA_CALL_ERR_GOTO(__ai_toy_start(), __error);
    wukong_audio_player_alert(AI_TOY_ALERT_TYPE_NOT_ACTIVE, FALSE);
    TAL_PR_NOTICE("ai toy -> init success");
    return rt;

__error:
    tuya_ai_toy_destroy();
    return rt;
}

VOID tuya_ai_toy_dp_process(CONST TY_RECV_OBJ_DP_S *dp)
{
    for (UINT_T index = 0; index < dp->dps_cnt; index++) {
        /* dpid 8: robot action (T5AI_BOARD_ROBOT only). */
        if (dp->dps[index].dpid == 8) {
#if defined(T5AI_BOARD_ROBOT) && (T5AI_BOARD_ROBOT == 1)
            tuya_robot_action_set(dp->dps[index].value.dp_enum);
#endif
            TAL_PR_DEBUG("SOC Rev DP Obj Cmd dpid:%d type:%d value:%d", dp->dps[index].dpid, dp->dps[index].type, dp->dps[index].value.dp_enum);
        } else if (dp->dps[index].dpid == 3 && dp->dps[index].type == PROP_VALUE) {
            /* dpid 3: volume (0~100). Update local volume, player, display and report. */
            TAL_PR_DEBUG("SOC Rev DP Obj Cmd dpid:%d type:%d value:%d", dp->dps[index].dpid, dp->dps[index].type, dp->dps[index].value.dp_value);
            if (dp->dps[index].value.dp_value <= 100 && dp->dps[index].value.dp_value >= 0 && s_ai_toy->volume != dp->dps[index].value.dp_value) {
                s_ai_toy->volume = dp->dps[index].value.dp_value;
                wukong_audio_player_set_vol(s_ai_toy->volume);
#if defined(ENABLE_TUYA_UI)
                tuya_ai_display_msg(&s_ai_toy->volume, 1, TY_DISPLAY_TP_VOLUME);
#endif
                __ai_toy_report_volum();
            }
        }
    }
}

UINT8_T tuya_ai_toy_get_lang()
{
    return s_lang;
}

AI_CHAT_SUB_MODE_E tuya_ai_toy_trigger_mode_get(VOID)
{
    return s_ai_toy->cfg.trigger_mode;
}

VOID tuya_ai_toy_trigger_mode_set(AI_CHAT_SUB_MODE_E mode)
{
    if (mode >= AI_CHAT_SUB_HOLD && mode < AI_CHAT_SUB_MAX) {
        s_ai_toy->cfg.trigger_mode = mode;
        return;
    }
    TAL_PR_ERR("ai toy -> trigger mode set error, mode:%d", mode);
}

AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(VOID)
{
    return s_ai_toy->cfg.device_mode;
}

VOID tuya_ai_toy_device_mode_set(AI_DEVICE_MODE_E mode)
{
    if (mode >= AI_DEVICE_MODE_CHAT && mode < AI_DEVICE_MODE_MAX) {
        s_ai_toy->cfg.device_mode = mode;
        return;
    }
    TAL_PR_ERR("ai toy -> device mode set error, mode:%d", mode);
}

VOID tuya_ai_toy_lowpower_timer_ctrl(BOOL_T enable)
{
    TAL_PR_DEBUG("[====ai_toy] lowpower timer ctrl enable:%d", enable);
    if (enable) {
        tal_sw_timer_start(s_ai_toy->lowpower_timer, TOY_LOWPOWER_TIMEOUT, TAL_TIMER_ONCE);
    } else {
        tal_sw_timer_stop(s_ai_toy->lowpower_timer);
    }
}

VOID tuya_ai_toy_idle_timer_ctrl(BOOL_T enable)
{
    TAL_PR_DEBUG("[====ai_toy] idle timer ctrl enable:%d", enable);
    if (enable) {
        tal_sw_timer_start(s_ai_toy->idle_timer, TOY_IDLE_TIMEOUT, TAL_TIMER_ONCE);
    } else {
        tal_sw_timer_stop(s_ai_toy->idle_timer);
    }
}

OPERATE_RET tuya_ai_toy_volume_set(UINT8_T value)
{
    __ai_toy_config_load();

    s_ai_toy->volume = value;

    /* Persist to KV (volume + trigger_mode), then apply to player and report to cloud. */
    OPERATE_RET rt = OPRT_OK;
    CHAR_T buf[64] = {0};
    snprintf(buf, sizeof(buf), "{\"volume\": %d, \"trigger_mode\":%d, \"device_mode\":%d}", 
        s_ai_toy->volume, s_ai_toy->cfg.trigger_mode, s_ai_toy->cfg.device_mode);
    TUYA_CALL_ERR_RETURN(wd_common_write(AI_TOY_PARA, (CONST BYTE_T *)buf, strlen(buf)));

    TAL_PR_DEBUG("set volume: %s", buf);

    wukong_audio_player_set_vol(s_ai_toy->volume);
    __ai_toy_report_volum();

    return rt;
}

UINT8_T tuya_ai_toy_volume_get(VOID)
{
    __ai_toy_config_load();
    return s_ai_toy->volume;
}

// OPERATE_RET hugo_cmd_getback1()
// {
//     hugo_cmd_getback();
//     return OPRT_OK;
// }
