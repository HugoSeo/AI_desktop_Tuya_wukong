/**
 * @file wukong_demo_lcd_anim.c
 * @brief SPI LCD 动图 demo 实现 —— 不跑 LVGL。
 *
 *   用户提供 .tef 素材（弱符号 g_demo_tef_data/g_demo_tef_len 非空） →
 *   调 tef_player_init() 后走 tef_player_play()，官方"解压 + 脏块直推 GRAM"通路
 *
 * @copyright Copyright (c) tuya.inc 2026
 */
#include <string.h>
#include <stdint.h>

#include "tuya_cloud_types.h"
#include "tal_log.h"
#include "tal_thread.h"
#include "tal_system.h"
#include "tkl_memory.h"
#include "tal_display_service.h"
#include "tuya_display_hw.h"
#include "tef_player.h"

#include "wukong_demo_lcd_anim.h"

/* ---------------------------------------------------------------------------
 * User-drop-in TEF asset (weak; empty by default).
 * 用户自己在别的 .c 里定义同名强符号后，本 demo 自动切到 tef_player 路径。
 * ------------------------------------------------------------------------- */

#include "intro_data.h"
/* ---------------------------------------------------------------------------
 * 主 helper task
 * ------------------------------------------------------------------------- */
static THREAD_HANDLE s_demo_task = NULL;

static void __demo_task_entry(void *arg)
{
    (void)arg;

    /* 等硬件稳定。tal_display_spi_open 里 init 序列自带延时；这里再多留一点余量。 */
    tal_system_sleep(500);

    /* 需要 tuya_display_hw_init 已成功。
     * 注意 tuya_display_hw_get_lcd_count() 永远返回 >=1（假的自检），
     * 真正的门票是 handle 非空。 */
    if (tuya_display_hw_get_handle() == NULL) {
        TAL_PR_ERR("[LCD-DEMO] tuya_display_hw_init handle=NULL — LCD driver "
                   "query/open failed. Check TUYA_LCD_IC_NAME_STR = the actual "
                   "panel IC, and that its tdd_lcd_* driver is compiled in.");
        goto _out;
    }

    if (g_demo_tef_len > 10 && g_demo_tef_data[0] == 'T') {
        uint16_t disp_w = tuya_display_hw_get_width();
        uint16_t disp_h = tuya_display_hw_get_height();
        /* tef header 头 10 字节：magic[4] + version[2] + canvas_w[2] + canvas_h[2] */
        uint16_t cw = (uint16_t)g_demo_tef_data[6] | ((uint16_t)g_demo_tef_data[7] << 8);
        uint16_t ch = (uint16_t)g_demo_tef_data[8] | ((uint16_t)g_demo_tef_data[9] << 8);
        uint16_t cver = (uint16_t)g_demo_tef_data[4] | ((uint16_t)g_demo_tef_data[5] << 8);
        TAL_PR_NOTICE("[LCD-DEMO] TEF asset: len=%u magic=%c%c%c%c ver=%u canvas=%ux%u",
                      (unsigned)g_demo_tef_len,
                      g_demo_tef_data[0], g_demo_tef_data[1],
                      g_demo_tef_data[2], g_demo_tef_data[3],
                      cver, cw, ch);
        TAL_PR_NOTICE("[LCD-DEMO] display=%ux%u  panels=%u", disp_w, disp_h, tuya_display_hw_get_lcd_count());
        if (cw != disp_w || (ch != disp_h)) {
            TAL_PR_ERR("[LCD-DEMO] SIZE MISMATCH: tef canvas %ux%u vs display %ux%u — "
                       "tef_player ctx_load 会拒绝并打印 'tef: canvas ... mismatch display'。"
                       "修法：把素材改成 %ux%u 重新用 gen_assets.py 生成，或改屏 rotation "
                       "让逻辑尺寸对齐。",
                       cw, ch, disp_w, disp_h, disp_w, disp_h);
        }

        OPERATE_RET rt = tef_player_init();
        if (rt != OPRT_OK) {
            TAL_PR_ERR("[LCD-DEMO] tef_player_init rt=%d", rt);
            goto _out;
        }
        rt = tef_player_play(g_demo_tef_data, g_demo_tef_len);
        TAL_PR_NOTICE("[LCD-DEMO] tef_player_play rt=%d (async: only queued a "
                      "switch request; actual ctx_load runs in tef_player thread)", rt);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("[LCD-DEMO] tef_player_play failed rt=%d", rt);
            goto _out;
        }
    }

_out:
    s_demo_task = NULL;
    tal_thread_delete(NULL);
}

OPERATE_RET wukong_demo_lcd_anim_start(void)
{
    if (s_demo_task != NULL) {
        return OPRT_OK;  /* 已经起过 */
    }
    THREAD_CFG_T cfg = {
        .stackDepth = 4096,
        .priority   = THREAD_PRIO_2,
        .thrdname   = "lcd_anim_demo",
    };
    return tal_thread_create_and_start(&s_demo_task, NULL, NULL, __demo_task_entry, NULL, &cfg);
}
