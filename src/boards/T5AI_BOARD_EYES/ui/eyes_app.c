/**
 * @file eyes_app.c
 * @brief EYES 板 UI：TEF 表情动画（脏块直推 GRAM，不经 LVGL）。
 *
 * 表情素材为编译进固件的 .tef 常量数组（ui/eyes_tef/，由 gen_assets.py 生成），
 * tef_player 解码线程按素材 fps 直推双 panel。LVGL 仍随 UI 框架启动，但本板
 * 不创建任何会失效重绘的对象，启动后即静默，屏幕归 tef_player 所有。
 *
 * @copyright Copyright (c) tuya.inc 2026
 */
#include <string.h>

#include "tuya_ai_display.h"
#include "tuya_display_hw.h"
#include "tal_log.h"

#include "tef_player.h"
#include "tef_assets.h"

static int s_current = -1;

/* 未命中回落到表 0（neutral，注册表生成时固定排第 0 项） */
static int eyes_emotion_find(const char *emotion)
{
    uint32_t i;

    for (i = 0; i < g_tef_asset_cnt; i++) {
        if (0 == strcmp(g_tef_assets[i].name, emotion)) {
            return (int)i;
        }
    }
    return 0;
}

static void eyes_emotion_flush(const char *emotion)
{
    int index;

    TAL_PR_DEBUG("eyes_emotion_flush: %s", emotion);

    index = eyes_emotion_find(emotion);
    if (s_current == index) {
        return;
    }
    s_current = index;

    tef_player_play(g_tef_assets[index].data, g_tef_assets[index].len);
}

void app_ui_init(void)
{
    tef_player_init();

#if defined(TEF_BENCH) && (TEF_BENCH == 1)
    /* TEF vs GIF 基准（make app ... TEF_BENCH=1）：起播前跑一轮对比，
     * 输出 [TEF-BENCH] 报告后照常播放。样本为 neutral（两格式同源）。 */
    {
        extern void tef_bench_run(const char *name,
                                  const uint8_t *tef, uint32_t tef_len,
                                  const uint8_t *gif, uint32_t gif_len, int loops);
        extern const uint8_t g_tef_bench_gif[];
        extern const uint32_t g_tef_bench_gif_len;
        tef_bench_run("neutral", g_tef_assets[0].data, g_tef_assets[0].len,
                      g_tef_bench_gif, g_tef_bench_gif_len, 10);
    }
#endif

    eyes_emotion_flush("neutral");
}

void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)
{
    switch (msg->type) {
    case TY_DISPLAY_TP_EMOJI:
        eyes_emotion_flush((const char *)msg->data);
        break;

    case TY_DISPLAY_TP_CHAT_STAT: {
        if (0 == msg->data[0] || 1 == msg->data[0]) {
            eyes_emotion_flush("neutral");
        }
    } break;

    default:
        break;
    }
}

#if !defined(ENABLE_TUYA_UI)
/* ---- 无 UI 框架直连(ENABLE_TUYA_DISPLAY only) ----
 * 总线为同步直转:回调在发送方线程执行。app_ui_msg_handler 只做查表 +
 * 向播放器提交切换请求(存 flash 素材指针,不留存 msg->data),线程安全。 */
static OPERATE_RET eyes_display_bus_cb(UINT8_T *data, INT_T len, TY_DISPLAY_TYPE_E tp)
{
    TY_DISPLAY_MSG_T msg = {
        .type = tp,
        .len = (UINT_T)len,
        .data = data,
    };
    app_ui_msg_handler(&msg);
    return OPRT_OK;
}

OPERATE_RET eyes_ui_direct_init(void)
{
    tuya_ai_display_init();
    app_ui_init();
    tuya_ai_display_msg_handler_register(eyes_display_bus_cb);
    tuya_display_hw_backlight_open();
    return OPRT_OK;
}
#endif /* !ENABLE_TUYA_UI */
